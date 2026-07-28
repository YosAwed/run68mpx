#include "run68.h"

/*
 * Switch the active A7 bank when the supervisor bit changes.  The host uses
 * SR_S_ON/OFF temporarily for protected memory access, so guest-visible SR
 * writes must come through this function instead of changing those macros.
 */
void cpu_set_sr(UShort new_sr)
{
	BOOL was_supervisor = SR_S_REF() != 0;
	BOOL will_be_supervisor = (new_sr & 0x2000u) != 0;

	if (!was_supervisor && will_be_supervisor) {
		usp = ra[7];
		ra[7] = ssp;
	} else if (was_supervisor && !will_be_supervisor) {
		ssp = ra[7];
		ra[7] = usp;
	}
	sr = (short)new_sr;
}

/* Enter an MC68000 group 1/2 exception using the six-byte stack frame. */
BOOL cpu_enter_exception(int vector_number, Long stacked_pc)
{
	UShort old_sr = (UShort)sr;

	if ((old_sr & 0x2000u) == 0) {
		usp = ra[7];
		ra[7] = ssp;
	}

	/* Exception processing enters supervisor mode and clears trace. */
	sr = (short)((old_sr | 0x2000u) & 0x7fffu);
	ra[7] = run68_sub32(ra[7], 4);
	mem_set(ra[7], stacked_pc, S_LONG);
	ra[7] = run68_sub32(ra[7], 2);
	mem_set(ra[7], old_sr, S_WORD);
	ssp = ra[7];
	pc = mem_get((Long)((ULong)vector_number * 4u), S_LONG);
	return FALSE;
}

/* Enter the MC68000's 14-byte bus/address-error stack frame. */
BOOL cpu_enter_address_error(Long fault_address, Long stacked_pc,
                             UShort instruction, BOOL is_write,
                             BOOL instruction_access)
{
	UShort old_sr = (UShort)sr;
	UShort function_code;
	UShort special_status;

	if ((old_sr & 0x2000u) == 0) {
		usp = ra[7];
		ra[7] = ssp;
	}

	function_code = instruction_access
	    ? ((old_sr & 0x2000u) != 0 ? 6u : 2u)
	    : ((old_sr & 0x2000u) != 0 ? 5u : 1u);
	special_status = (UShort)((is_write ? 0u : 0x10u) |
	                          (instruction_access ? 0u : 0x08u) |
	                          function_code);

	sr = (short)((old_sr | 0x2000u) & 0x7fffu);
	ra[7] = run68_sub32(ra[7], 2);
	mem_set(ra[7], special_status, S_WORD);
	ra[7] = run68_sub32(ra[7], 4);
	mem_set(ra[7], fault_address, S_LONG);
	ra[7] = run68_sub32(ra[7], 2);
	mem_set(ra[7], instruction, S_WORD);
	ra[7] = run68_sub32(ra[7], 4);
	mem_set(ra[7], stacked_pc, S_LONG);
	ra[7] = run68_sub32(ra[7], 2);
	mem_set(ra[7], old_sr, S_WORD);
	ssp = ra[7];
	pc = mem_get(3 * 4, S_LONG);
	return FALSE;
}

/* Return from the six-byte MC68000 exception frame at the active SSP. */
BOOL cpu_return_from_exception(void)
{
	UShort restored_sr = (UShort)mem_get(ra[7], S_WORD);
	Long restored_pc = mem_get(run68_add32(ra[7], 2), S_LONG);
	Long restored_ssp = run68_add32(ra[7], 6);

	ssp = restored_ssp;
	if ((restored_sr & 0x2000u) != 0)
		ra[7] = restored_ssp;
	else
		ra[7] = usp;
	sr = (short)restored_sr;
	pc = restored_pc;
	trap_count = RAS_INTERVAL;
	return FALSE;
}
