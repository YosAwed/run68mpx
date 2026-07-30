/* $Id: mem.c,v 1.2 2009-08-08 06:49:44 masamic Exp $ */

/*
 * $Log: not supported by cvs2svn $
 * Revision 1.1.1.1  2001/05/23 11:22:08  masamic
 * First imported source code and docs
 *
 * Revision 1.4  1999/12/07  12:47:22  yfujii
 * *** empty log message ***
 *
 * Revision 1.4  1999/11/29  06:18:06  yfujii
 * Calling CloseHandle instead of fclose when abort().
 *
 * Revision 1.3  1999/11/01  06:23:33  yfujii
 * Some debugging functions are introduced.
 *
 * Revision 1.2  1999/10/18  03:24:40  yfujii
 * Added RCS keywords and modified for WIN32 a little.
 *
 */

#undef	MAIN

#include <stdio.h>
#include "run68.h"
#include "x68k_bus.h"

static	int	mem_red_chk( Long, char );
static	int	mem_wrt_chk( Long, char );
static	ULong	mem_normalize_address( Long );
static	ULong	mem_access_width( char );
static	BOOL	mem_is_trap_probe( ULong, ULong );
static	void	mem_address_exception( Long, BOOL );
void	run68_abort( Long );

/*
 　機能：PCの指すメモリからインデックスレジスタ＋8ビットディスプレースメント
 　　　　の値を得る
 戻り値：その値
*/
Long idx_get(void)
{
	UShort	extension;
	UChar	idx2;
	UChar	idx_reg;
	Long	idx;
	Char	displacement;

	extension = (UShort)mem_get(pc, S_WORD);
	idx2 = (UChar)(extension >> 8);
	displacement = (Char)(extension & 0xff);
	idx_reg = ((idx2 >> 4) & 0x07);
	if ( (idx2 & 0x80) == 0 )
		idx = rd [ idx_reg ];
	else
		idx = ra [ idx_reg ];
	if ( (idx2 & 0x08) == 0 ) {	/* WORD */
		if ((idx & 0x8000) != 0)
			idx |= 0xFFFF0000;
		else
			idx &= 0x0000FFFF;
	}
	pc = run68_add32(pc, 2);

	return( (Long)((ULong)idx + (Long)displacement) );
}

/*
 　機能：PCの指すメモリから指定されたサイズのイミディエイトデータをゲットし、
 　　　　サイズに応じてPCを進める
 戻り値：データの値
*/
Long imi_get( char size )
{
	switch( size ) {
		case S_BYTE:
			pc = run68_add32(pc, 2);
			return( mem_get(run68_sub32(pc, 1), S_BYTE) );
		case S_WORD:
			pc = run68_add32(pc, 2);
			return( mem_get(run68_sub32(pc, 2), S_WORD) );
		default:	/* S_LONG */
			pc = run68_add32(pc, 4);
			return( mem_get(run68_sub32(pc, 4), S_LONG) );
	}
}

/*
 　機能：メモリから指定されたサイズのデータをゲットする
 戻り値：データの値
*/
Long mem_get( Long adr, char size )
{
	UChar   *mem;
	ULong	d;
	ULong original = (ULong)adr;
	ULong width = mem_access_width(size);
	ULong normalized = mem_normalize_address(adr);
	uint32_t io_value;

	/*
	 * The legacy run68 trap vectors use 0x20ff0000..0x28ff0000 as
	 * sentinels for absent handlers. Some resident drivers inspect a few
	 * bytes immediately before a vector target to identify another driver.
	 * Musashi applies the 68000's 24-bit address mask before calling us, so
	 * also recognize the normalized 0x00fefff0..0x00feffff range. Preserve
	 * the sentinel semantics while reporting "no signature".
	 */
	if (mem_is_trap_probe(original, width))
		return 0;

	if (x68k_bus_read(normalized, size, &io_value))
		return (Long)io_value;

	if ( normalized < ENV_TOP ||
	     normalized + mem_access_width(size) > (ULong)mem_aloc ) {
		if ( mem_red_chk( (Long)normalized, size ) == FALSE )
			return( 0 );
	}
	if (size != S_BYTE && (normalized & 1u) != 0) {
		if (cpu_instruction_active)
			mem_address_exception((Long)normalized, FALSE);
		if ( mem_red_chk( (Long)normalized, size ) == FALSE )
			return( 0 );
	}
	mem = (UChar *)prog_ptr + normalized;

	switch( size ) {
		case S_BYTE:
			return( *mem );
		case S_WORD:
			d = *(mem++);
			d = ((d << 8) | *mem);
			return( d );
		default:	/* S_LONG */
			d = *(mem++);
			d = ((d << 8) | *(mem++));
			d = ((d << 8) | *(mem++));
			d = ((d << 8) | *mem);
			return( d );
	}
}

static BOOL mem_is_trap_probe(ULong address, ULong width)
{
	ULong tag = address >> 24;
	ULong low = address & 0x00ffffffu;

	return (tag == 0 || (tag >= 0x20u && tag <= 0x28u)) &&
	       low >= 0x00fefff0u && low < 0x00ff0000u &&
	       width <= 0x00ff0000u - low;
}

/*
 　機能：メモリに指定されたサイズのデータをセットする
 戻り値：なし
*/
void mem_set( Long adr, Long d, char size )
{
	UChar   *mem;
	ULong normalized = mem_normalize_address(adr);

	if (x68k_bus_write(normalized, size, (ULong)d))
		return;

	if ( normalized < ENV_TOP ||
	     normalized + mem_access_width(size) > (ULong)mem_aloc ) {
		if ( mem_wrt_chk( (Long)normalized, size ) == FALSE )
			return;
	}
	if (size != S_BYTE && (normalized & 1u) != 0) {
		if (cpu_instruction_active)
			mem_address_exception((Long)normalized, TRUE);
		if ( mem_wrt_chk( (Long)normalized, size ) == FALSE )
			return;
	}
	mem = (UChar *)prog_ptr + normalized;

	switch( size ) {
		case S_BYTE:
			*mem = (d & 0xFF);
			return;
		case S_WORD:
			*(mem++) = ((d >> 8) & 0xFF);
			*mem = (d & 0xFF);
			return;
		default:	/* S_LONG */
			*(mem++) = ((d >> 24) & 0xFF);
			*(mem++) = ((d >> 16) & 0xFF);
			*(mem++) = ((d >> 8) & 0xFF);
			*mem = (d & 0xFF);
			return;
	}
}

/*
 　機能：読み込みアドレスのチェック
 戻り値： TRUE = OK
         FALSE = NGだが、0を読み込んだとみなす
*/
static int mem_red_chk( Long adr, char size )
{
	char message[256];
	ULong width = mem_access_width(size);

	if (size != S_BYTE && ((ULong)adr & 1u) != 0) {
		snprintf(message, sizeof(message), "アドレスエラー($%06X)からの読み込みです。", adr);
		err68(message);
		run68_abort( adr );
	}
	if ( adr >= 0xC00000 ) {
		if ( ini_info.io_through == TRUE )
			return( FALSE );
		snprintf(message, sizeof(message), "I/OポートorROM($%06X)から読み込もうとしました。", adr);
		err68(message);
		run68_abort( adr );
	}
	if ( SR_S_REF() == 0 || (ULong)adr + width > (ULong)mem_aloc ) {
		snprintf(message, sizeof(message), "不正アドレス($%06X)からの読み込みです。", adr);
		err68(message);
		run68_abort( adr );
	}
	return( TRUE );
}

/*
 　機能：書き込みアドレスのチェック
 戻り値： TRUE = OK
         FALSE = NGだが、何も書き込まずにOKとみなす
*/
static int mem_wrt_chk( Long adr, char size )
{
	char message[256];
	ULong width = mem_access_width(size);

	if (size != S_BYTE && ((ULong)adr & 1u) != 0) {
		snprintf(message, sizeof(message), "アドレスエラー($%06X)への書き込みです。", adr);
		err68(message);
		run68_abort( adr );
	}
	if ( adr >= 0xC00000 ) {
		if ( ini_info.io_through == TRUE )
			return( FALSE );
/*
		if ( adr == 0xE8A01F )	/# RESET CONTROLLER #/
			return( FALSE );
*/
		snprintf(message, sizeof(message), "I/OポートorROM($%06X)に書き込もうとしました。", adr);
		err68(message);
		run68_abort(adr);
	}
	if ( SR_S_REF() == 0 || (ULong)adr + width > (ULong)mem_aloc ) {
		snprintf(message, sizeof(message), "不正アドレスへの書き込みです($%06X)", adr);
		err68(message);
		run68_abort( adr );
	}
	return( TRUE );
}

static ULong mem_normalize_address(Long adr)
{
	return (ULong)adr & 0x00ffffffu;
}

static ULong mem_access_width(char size)
{
	switch (size) {
		case S_BYTE:
			return 1;
		case S_WORD:
			return 2;
		default:
			return 4;
	}
}

static void mem_address_exception(Long address, BOOL is_write)
{
	ULong instruction_pc = (ULong)OP_info.pc & 0x00ffffffu;
	UShort instruction = 0;

	if (instruction_pc + 1u < (ULong)mem_aloc) {
		UChar *bytes = (UChar *)prog_ptr + instruction_pc;
		instruction = (UShort)(((UShort)bytes[0] << 8) | bytes[1]);
	}

	/* Prevent a recursive frame if the supervisor stack itself is invalid. */
	cpu_instruction_active = FALSE;
	cpu_enter_address_error(address, OP_info.pc, instruction, is_write,
	                        FALSE);
	longjmp(jmp_when_abort, RUN68_ABORT_CPU_EXCEPTION);
}

/*
 機能：異常終了する
*/
void run68_abort( Long adr )
{
	int	i;

	fprintf( stderr, "アドレス：%08X\n", adr );

	for ( i = 5; i < FILE_MAX; i ++ ) {
		if ( finfo [ i ].fh != NULL )
#if defined(WIN32)
			CloseHandle(finfo [ i ].fh);
#else
			fclose(finfo [ i ].fh);
#endif
	}

#ifdef	TRACE
	printf( "d0-7=%08lx" , rd [ 0 ] );
	for ( i = 1; i < 8; i++ ) {
		printf( ",%08lx" , rd [ i ] );
	}
	printf("\n");
	printf( "a0-7=%08lx" , ra [ 0 ] );
	for ( i = 1; i < 8; i++ ) {
		printf( ",%08lx" , ra [ i ] );
	}
	printf("\n");
	printf( "  pc=%08lx    sr=%04x\n" , pc, sr );
#endif
	longjmp(jmp_when_abort, 2);
}
