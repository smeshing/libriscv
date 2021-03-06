#include "machine.hpp"
#include "common.hpp"
#include "decoder_cache.hpp"
#include "riscvbase.hpp"
#include "rv32i_instr.hpp"
#include "rv64i.hpp"
#ifdef __GNUG__
#include "rv32i.cpp"
#endif

namespace riscv
{
	template <int W>
	void CPU<W>::reset()
	{
		this->m_regs = {};
		this->reset_stack_pointer();
		// jumping causes some extra calculations
		this->jump(machine().memory.start_address());
	}

	template <int W>
	typename CPU<W>::format_t CPU<W>::read_next_instruction()
	{
#ifndef BOUNDS_CHECK_JUMPS_ONLY
		// We have to check the bounds just to be thorough, as this will
		// instantly crash if something is wrong. In addition,
		// page management is only done for jumps outside of execute segment.
		// Secondly, any jump traps will **HAVE** to return to the execute
		// segment before returning.
		if (UNLIKELY(this->pc() < m_exec_begin || this->pc() >= m_exec_end)) {
			trigger_exception(EXECUTION_SPACE_PROTECTION_FAULT, this->pc());
			__builtin_unreachable();
		}
#endif
		return format_t { *(uint32_t*) &m_exec_data[this->pc()] };
	}

	template<int W>
	void CPU<W>::simulate()
	{
#ifdef RISCV_DEBUG
		this->break_checks();
#endif
		const auto instruction = this->read_next_instruction();

#ifdef RISCV_DEBUG
		const auto& handler = this->decode(instruction);
		// instruction logging
		if (machine().verbose_instructions)
		{
			const auto string = isa_type<W>::to_string(*this, instruction, handler);
			printf("%s\n", string.c_str());
		}

		// execute instruction
		handler.handler(*this, instruction);
#else
# ifdef RISCV_INSTR_CACHE
		// retrieve instructions directly from the constant cache
		auto& cache_entry =
			machine().memory.get_decoder_cache()[this->pc() / DecoderCache<W>::DIVISOR];
#ifndef RISCV_INSTR_CACHE_PREGEN
		if (UNLIKELY(!cache_entry)) {
			cache_entry = this->decode(instruction).handler;
		}
#endif
		// execute instruction
		cache_entry(*this, instruction);
# else
		// decode & execute instruction directly
		this->execute(instruction);
# endif
#endif
		// increment instruction counter
		this->m_counter++;

#ifdef RISCV_DEBUG
		if (UNLIKELY(machine().verbose_registers))
		{
			auto regs = this->registers().to_string();
			printf("\n%s\n\n", regs.c_str());
			if (UNLIKELY(machine().verbose_fp_registers)) {
				printf("%s\n", registers().flp_to_string().c_str());
			}
		}
#endif

		// increment PC
		if constexpr (compressed_enabled)
			registers().pc += instruction.length();
		else
			registers().pc += 4;
	}

	template<int W> __attribute__((cold))
	void CPU<W>::trigger_exception(interrupt_t intr, address_t data)
	{
		switch (intr)
		{
		case ILLEGAL_OPCODE:
			throw MachineException(ILLEGAL_OPCODE,
					"Illegal opcode executed", data);
		case ILLEGAL_OPERATION:
			throw MachineException(ILLEGAL_OPERATION,
					"Illegal operation during instruction decoding", data);
		case PROTECTION_FAULT:
			throw MachineException(PROTECTION_FAULT,
					"Protection fault", data);
		case EXECUTION_SPACE_PROTECTION_FAULT:
			throw MachineException(EXECUTION_SPACE_PROTECTION_FAULT,
					"Execution space protection fault", data);
		case MISALIGNED_INSTRUCTION:
			// NOTE: only check for this when jumping or branching
			throw MachineException(MISALIGNED_INSTRUCTION,
					"Misaligned instruction executed", data);
		case UNIMPLEMENTED_INSTRUCTION:
			throw MachineException(UNIMPLEMENTED_INSTRUCTION,
					"Unimplemented instruction executed", data);
		default:
			throw MachineException(UNKNOWN_EXCEPTION,
					"Unknown exception", intr);
		}
	}

	template <int W> __attribute__((cold))
	std::string Registers<W>::to_string() const
	{
		char buffer[600];
		int  len = 0;
		for (int i = 1; i < 32; i++) {
			len += snprintf(buffer+len, sizeof(buffer) - len,
					"[%s\t%08lX] ", RISCV::regname(i), (long) this->get(i));
			if (i % 5 == 4) {
				len += snprintf(buffer+len, sizeof(buffer)-len, "\n");
			}
		}
		return std::string(buffer, len);
	}

	template <int W> __attribute__((cold))
	std::string Registers<W>::flp_to_string() const
	{
		char buffer[800];
		int  len = 0;
		for (int i = 0; i < 32; i++) {
			auto& src = this->getfl(i);
			const char T = (src.i32[1] == -1) ? 'S' : 'D';
			double val = (src.i32[1] == -1) ? src.f32[0] : src.f64;
			len += snprintf(buffer+len, sizeof(buffer) - len,
					"[%s\t%c%+.2f] ", RISCV::flpname(i), T, val);
			if (i % 5 == 4) {
				len += snprintf(buffer+len, sizeof(buffer)-len, "\n");
			}
		}
		return std::string(buffer, len);
	}

	template struct CPU<4>;
	template struct Registers<4>;
	template struct CPU<8>;
	template struct Registers<8>;
}
