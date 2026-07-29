#include "cpu_backend.h"

#include "m68k.h"

static RUN68_CPU_BACKEND selected_backend = RUN68_CPU_LEGACY;
static BOOL musashi_initialized = FALSE;
static BOOL musashi_finished = FALSE;
static BOOL opcode_fetch_expected = FALSE;
static ULong opcode_fetch_address = 0;
static BOOL musashi_step_active = FALSE;
static BOOL musashi_instruction_seen = FALSE;
static jmp_buf musashi_step_boundary;

static void musashi_sync_to_core(void)
{
	int i;

	for (i = 0; i < 8; ++i)
		m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), (ULong)rd[i]);
	for (i = 0; i < 7; ++i)
		m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), (ULong)ra[i]);

	/* Set the mode first so USP/ISP are written to the intended bank. */
	m68k_set_reg(M68K_REG_SR, (UShort)sr);
	m68k_set_reg(M68K_REG_USP, (ULong)usp);
	m68k_set_reg(M68K_REG_ISP, (ULong)ssp);
	m68k_set_reg(M68K_REG_A7, (ULong)ra[7]);
	m68k_set_reg(M68K_REG_PC, (ULong)pc);
}

static void musashi_sync_from_core(void)
{
	int i;

	for (i = 0; i < 8; ++i)
		rd[i] = (Long)m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
	for (i = 0; i < 8; ++i)
		ra[i] = (Long)m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
	usp = (Long)m68k_get_reg(NULL, M68K_REG_USP);
	ssp = (Long)m68k_get_reg(NULL, M68K_REG_ISP);
	pc = (Long)m68k_get_reg(NULL, M68K_REG_PC);
	sr = (short)m68k_get_reg(NULL, M68K_REG_SR);
}

static void musashi_refresh_live_sr(void)
{
	if (musashi_initialized && selected_backend == RUN68_CPU_MUSASHI)
		sr = (short)m68k_get_reg(NULL, M68K_REG_SR);
}

static ULong raw_read_long(ULong address)
{
	UChar *memory = (UChar *)prog_ptr;

	address &= 0x00ffffffu;
	if (memory == NULL || address + 4u > (ULong)mem_aloc)
		return 0;
	return ((ULong)memory[address] << 24) |
	       ((ULong)memory[address + 1] << 16) |
	       ((ULong)memory[address + 2] << 8) |
	       (ULong)memory[address + 3];
}

static void musashi_instruction_hook(unsigned int address)
{
	/* Address-error recovery can otherwise start the handler in the same call. */
	if (musashi_step_active && musashi_instruction_seen)
		longjmp(musashi_step_boundary, 1);
	musashi_instruction_seen = TRUE;
	opcode_fetch_expected = TRUE;
	opcode_fetch_address = address & 0x00ffffffu;
}

static int musashi_trap_callback(int trap_number)
{
	int finished;

	if (trap_number != 15)
		return 0;

	musashi_sync_from_core();
	finished = iocs_call();
	if (finished)
		musashi_finished = TRUE;
	musashi_sync_to_core();
	return 1;
}

static unsigned int musashi_read_16(unsigned int address, BOOL immediate)
{
	UShort opcode;
	ULong normalized = address & 0x00ffffffu;

	musashi_refresh_live_sr();
	opcode = (UShort)mem_get((Long)normalized, S_WORD);
	if (!immediate || !opcode_fetch_expected ||
	    normalized != opcode_fetch_address)
		return opcode;

	opcode_fetch_expected = FALSE;
	if ((opcode & 0xff00u) == 0xff00u ||
	    ((opcode & 0xff00u) == 0xfe00u &&
	     raw_read_long(0x2cu) == HUMAN_WORK)) {
		int finished;

		/* Musashi has already advanced PC past the fetched opcode. */
		musashi_sync_from_core();
		pc = (Long)normalized;
		finished = linef(prog_ptr + normalized);
		if (finished)
			musashi_finished = TRUE;
		musashi_sync_to_core();
		return 0x4e71u; /* Execute a harmless NOP in place of the HLE opcode. */
	}

	return opcode;
}

BOOL cpu_backend_select(const char *name)
{
	if (strcmp(name, "legacy") == 0) {
		selected_backend = RUN68_CPU_LEGACY;
		return TRUE;
	}
	if (strcmp(name, "musashi") == 0) {
		selected_backend = RUN68_CPU_MUSASHI;
		return TRUE;
	}
	return FALSE;
}

const char *cpu_backend_name(void)
{
	return selected_backend == RUN68_CPU_MUSASHI ? "musashi" : "legacy";
}

BOOL cpu_backend_is_musashi(void)
{
	return selected_backend == RUN68_CPU_MUSASHI;
}

void cpu_backend_prepare(void)
{
	short initial_sr;

	if (selected_backend != RUN68_CPU_MUSASHI)
		return;

	if (!musashi_initialized) {
		m68k_init();
		musashi_initialized = TRUE;
	}
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_set_instr_hook_callback(musashi_instruction_hook);
	m68k_set_trap_instr_callback(musashi_trap_callback);
	initial_sr = sr;
	m68k_pulse_reset();
	/* Drain the reset delay without executing a guest instruction. */
	(void)m68k_execute(132);
	/* Reset vector reads expose Musashi's temporary supervisor SR. */
	sr = initial_sr;
	musashi_sync_to_core();
}

BOOL cpu_backend_execute_one(void)
{
	if (selected_backend == RUN68_CPU_LEGACY)
		return prog_exec();

	musashi_finished = FALSE;
	opcode_fetch_expected = FALSE;
	musashi_sync_to_core();
	musashi_instruction_seen = FALSE;
	musashi_step_active = TRUE;
	if (setjmp(musashi_step_boundary) == 0)
		(void)m68k_execute(1);
	musashi_step_active = FALSE;
	musashi_sync_from_core();
	return musashi_finished;
}

unsigned int m68k_read_memory_8(unsigned int address)
{
	musashi_refresh_live_sr();
	return (unsigned int)(UChar)mem_get((Long)address, S_BYTE);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
	return musashi_read_16(address, FALSE);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
	musashi_refresh_live_sr();
	return (unsigned int)(ULong)mem_get((Long)address, S_LONG);
}

unsigned int m68k_read_immediate_16(unsigned int address)
{
	return musashi_read_16(address, TRUE);
}

unsigned int m68k_read_immediate_32(unsigned int address)
{
	return m68k_read_memory_32(address);
}

unsigned int m68k_read_pcrelative_8(unsigned int address)
{
	return m68k_read_memory_8(address);
}

unsigned int m68k_read_pcrelative_16(unsigned int address)
{
	return m68k_read_memory_16(address);
}

unsigned int m68k_read_pcrelative_32(unsigned int address)
{
	return m68k_read_memory_32(address);
}

unsigned int m68k_read_disassembler_8(unsigned int address)
{
	return m68k_read_memory_8(address);
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
	return m68k_read_memory_16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
	return m68k_read_memory_32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	musashi_refresh_live_sr();
	mem_set((Long)address, (Long)value, S_BYTE);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	musashi_refresh_live_sr();
	mem_set((Long)address, (Long)value, S_WORD);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	musashi_refresh_live_sr();
	mem_set((Long)address, (Long)value, S_LONG);
}

void m68k_write_memory_32_pd(unsigned int address, unsigned int value)
{
	/* MC68000 predecrement long writes the high-address word first. */
	m68k_write_memory_16(address + 2u, value & 0xffffu);
	m68k_write_memory_16(address, value >> 16);
}
