/* $Id: iocscall.c,v 1.2 2009-08-08 06:49:44 masamic Exp $ */

/*
 * $Log: not supported by cvs2svn $
 * Revision 1.1.1.1  2001/05/23 11:22:07  masamic
 * First imported source code and docs
 *
 * Revision 1.3  1999/12/07  12:42:59  yfujii
 * *** empty log message ***
 *
 * Revision 1.3  1999/10/25  03:24:58  yfujii
 * Trace output is now controlled with command option.
 *
 * Revision 1.2  1999/10/18  03:24:40  yfujii
 * Added RCS keywords and modified for WIN32 a little.
 *
 */

#undef	MAIN

#include <stdio.h>
#include <string.h>
#include "run68.h"

#if defined(WIN32)
  #include <windows.h>
#elif defined(DOSX)
  #include <dos.h>
#elif defined(__APPLE__)
#else
  #include <sys/sysinfo.h>
#endif

#if defined (USE_ICONV)
  #include<iconv.h>
#endif

#include <time.h>
#include <sys/time.h>

static Long	Putc( UShort );
static Long	Color( short );
static void	Putmes( void );
static Long	Keydata( int );
static Long	Datebcd( Long );
static Long	Dateset( Long );
static Long	Timebcd( Long );
static Long	Timeset( Long );
static Long	Dateget( void );
static Long	Timeget( void );
static Long	Datebin( Long );
static Long	Timebin( Long );
static Long	Datecnv( Long );
static Long	Timecnv( Long );
static Long	Dateasc( Long, Long );
static Long	Timeasc( Long, Long );
static void	Dayasc( Long, Long );
static Long	Intvcs( Long, Long );
static void	Memstr( BOOL );
static void	Poke( char );
static void	Dmamove( Long, Long, Long, Long );
static Long opm_interrupt_handler;
static void *adpcm_context;
static IOCS_ADPCM_START_CALLBACK adpcm_start;
static IOCS_ADPCM_CONTROL_CALLBACK adpcm_control;
static IOCS_ADPCM_STATUS_CALLBACK adpcm_status;

void iocs_set_adpcm_backend(void *context,
	IOCS_ADPCM_START_CALLBACK start,
	IOCS_ADPCM_CONTROL_CALLBACK control,
	IOCS_ADPCM_STATUS_CALLBACK status)
{
	adpcm_context = context;
	adpcm_start = start;
	adpcm_control = control;
	adpcm_status = status;
}

void iocs_audio_reset(void)
{
	opm_interrupt_handler = 0;
}

Long iocs_opm_interrupt_handler(void)
{
	return opm_interrupt_handler;
}

/*
 　機能：IOCSCALLを実行する
 戻り値： TRUE = 実行終了
         FALSE = 実行継続
*/
int iocs_call()
{
	UChar	*data_ptr;
	UChar	no;
	int	x, y;
	short	save_s;

	no = rd [ 0 ] & 0xff;

    if (func_trace_f)
    {
        printf( "IOCS(%02X): PC=%06X\n", no, pc );
    }
	switch( no ) {
		case 0x00:	/* B_KEYINP */
			rd[0] = Keydata(run68_console_getch(FALSE));
			break;
		case 0x01:	/* B_KEYSNS */
			if (!run68_console_kbhit()) {
				rd[0] = 0;
			} else {
				int key = run68_console_getch(FALSE);
				(void)run68_console_ungetch(key);
				rd[0] = 0x10000 | Keydata(key);
			}
			break;
		case 0x02:	/* B_SFTSNS: modifiers are not observable via a TTY */
			rd[0] = 0;
			break;
		case 0x03:	/* KEY_INIT */
			run68_console_flush_input();
			rd[0] = 0;
			break;
		case 0x1E:	/* B_CURON */
			printf("\033[?25h");
			rd[0] = 0;
			break;
		case 0x1F:	/* B_CUROFF */
			printf("\033[?25l");
			rd[0] = 0;
			break;
		case 0x20:	/* B_PUTC */
			rd [ 0 ] = Putc( (rd [ 1 ] & 0xFFFF) );
			break;
		case 0x21:	/* B_PRINT */
			data_ptr = (UChar *)prog_ptr + ra [ 1 ];
#if defined (USE_ICONV)
			{
				// SJIS to UTF-8
				char utf8_buf[8192];
				iconv_t icd = iconv_open("UTF-8", "Shift_JIS");
				size_t inbytes  = strlen(data_ptr);
				size_t outbytes = sizeof(utf8_buf) - 1;
				char *ptr_in = data_ptr;
				char *ptr_out = utf8_buf;
				memset(utf8_buf, 0x00, sizeof(utf8_buf));
				iconv(icd, &ptr_in, &inbytes, &ptr_out, &outbytes);
				iconv_close(icd);

				printf("%s", utf8_buf);
			}
#else
			printf( "%s", data_ptr );
#endif

			ra [ 1 ] = run68_add32(ra [ 1 ], (Long)strlen((char *)data_ptr));
			rd [ 0 ] = get_locate();
			break;
		case 0x22:	/* B_COLOR */
			rd [ 0 ] = Color( (rd [ 1 ] & 0xFFFF) );
			break;
		case 0x23:	/* B_LOCATE */
			if ( rd [ 1 ] != -1 ) {
				x = (rd [ 1 ] & 0xFFFF) + 1;
				y = (rd [ 2 ] & 0xFFFF) + 1;
				printf( "%c[%d;%dH", 0x1B, y, x );
			}
			rd [ 0 ] = get_locate();
			break;
		case 0x24:	/* B_DOWN_S */
			printf( "%c[s\n%c[u%c[1B", 0x1B, 0x1B, 0x1B );
			rd[0] = 0;
			break;
		case 0x25:	/* B_UP_S *//* (スクロール未サポート) */
			printf( "%c[1A", 0x1B );
			rd[0] = 0;
			break;
		case 0x26:	/* B_UP */
		case 0x27:	/* B_DOWN */
		case 0x28:	/* B_RIGHT */
		case 0x29:	/* B_LEFT */
			{
				static const char command[] = {'A', 'B', 'C', 'D'};
				unsigned count = (unsigned)rd[1] & 0xffu;
				if (count == 0)
					count = 1;
				printf("\033[%u%c", count, command[no - 0x26]);
				rd[0] = 0;
			}
			break;
		case 0x2A:	/* B_CLR_ST */
			if ((rd[1] & 0xff) > 2) {
				rd[0] = -1;
			} else {
				unsigned mode = (unsigned)rd[1] & 0xffu;
				printf("\033[%uJ", mode);
				if (mode == 2)
					printf("\033[H");
				rd[0] = 0;
			}
			break;
		case 0x2B:	/* B_ERA_ST */
			if ((rd[1] & 0xff) > 2) {
				rd[0] = -1;
			} else {
				printf("\033[%uK", (unsigned)rd[1] & 0xffu);
				rd[0] = 0;
			}
			break;
		case 0x2C:	/* B_INS */
		case 0x2D:	/* B_DEL */
			{
				unsigned count = (unsigned)rd[1] & 0xffu;
				if (count == 0)
					count = 1;
				printf("\r\033[%u%c", count, no == 0x2C ? 'L' : 'M');
				rd[0] = 0;
			}
			break;
		case 0x2F:	/* B_PUTMES */
			Putmes();
			break;
		case 0x50:	/* DATEBCD */
			rd[0] = Datebcd(rd[1]);
			break;
		case 0x51:	/* DATESET */
			rd[0] = Dateset(rd[1]);
			break;
		case 0x52:	/* TIMEBCD */
			rd[0] = Timebcd(rd[1]);
			break;
		case 0x53:	/* TIMESET */
			rd[0] = Timeset(rd[1]);
			break;
		case 0x54:	/* DATEGET */
			rd [ 0 ] = Dateget();
			break;
		case 0x55:	/* DATEBIN */
			rd [ 0 ] = Datebin( rd [ 1 ] );
			break;
		case 0x56:	/* TIMEGET */
			rd [ 0 ] = Timeget();
			break;
		case 0x57:	/* TIMEBIN */
			rd [ 0 ] = Timebin( rd [ 1 ] );
			break;
		case 0x58:	/* DATECNV */
			rd[0] = Datecnv(ra[1]);
			break;
		case 0x59:	/* TIMECNV */
			rd[0] = Timecnv(ra[1]);
			break;
		case 0x5A:	/* DATEASC */
			rd [ 0 ] = Dateasc( rd [ 1 ], ra [ 1 ] );
			break;
		case 0x5B:	/* TIMEASC */
			rd [ 0 ] = Timeasc( rd [ 1 ], ra [ 1 ] );
			break;
		case 0x5C:	/* DAYASC */
			Dayasc( rd [ 1 ], ra [ 1 ] );
			break;
		case 0x60:	/* ADPCMOUT */
		{
			ULong address = (ULong)ra[1] & 0x00ffffffu;
			ULong length = (ULong)rd[2];
			UShort mode = (UShort)rd[1];

			if (rd[2] < 0 || address > (ULong)mem_aloc ||
			    length > (ULong)mem_aloc - address) {
				rd[0] = -1;
			} else if (adpcm_start != NULL &&
			           adpcm_start(adpcm_context,
			                       (const UChar *)prog_ptr + address,
			                       (size_t)length, mode) != 0) {
				rd[0] = -1;
			} else {
				rd[0] = 0;
			}
			break;
		}
		case 0x66:	/* ADPCMSNS */
			rd[0] = adpcm_status == NULL ? 0 :
			        adpcm_status(adpcm_context);
			break;
		case 0x67:	/* ADPCMMOD */
			if (rd[1] < 0 || rd[1] > 2)
				rd[0] = -1;
			else if (adpcm_control != NULL)
				rd[0] = adpcm_control(adpcm_context, (int)rd[1]);
			else
				rd[0] = 0;
			break;
		case 0x68:	/* OPMSET */
			mem_set(0x00e90001, rd[1], S_BYTE);
			mem_set(0x00e90003, rd[2], S_BYTE);
			break;
		case 0x69:	/* OPMSNS */
			rd[0] = (rd[0] & (Long)0xffffff00u) |
			        (mem_get(0x00e90003, S_BYTE) & 0xff);
			break;
		case 0x6A:	/* OPMINTST */
			save_s = SR_S_REF();
			SR_S_ON();
			if (ra[1] == 0) {
				opm_interrupt_handler = 0;
				mem_set(OPM_CALLBACK_WORK, 0, S_LONG);
				rd[0] = 0;
			} else if (opm_interrupt_handler == 0) {
				opm_interrupt_handler = ra[1];
				mem_set(OPM_CALLBACK_WORK, ra[1], S_LONG);
				rd[0] = 0;
			} else {
				rd[0] = opm_interrupt_handler;
			}
			if (save_s == 0)
				SR_S_OFF();
			break;
		case 0x6C:	/* VDISPST */
			save_s = SR_S_REF();
			SR_S_ON();
			if ( ra [ 1 ] == 0 ) {
				mem_set( 0x118, 0, S_LONG );
			} else {
				rd [ 0 ] = mem_get( 0x118, S_LONG );
				if ( rd [ 0 ] == 0 )
					mem_set( 0x118, ra [ 1 ], S_LONG );
			}
			if ( save_s == 0 )
				SR_S_OFF();
			break;
		case 0x6D:	/* CRTCRAS */
			save_s = SR_S_REF();
			SR_S_ON();
			if ( ra [ 1 ] == 0 ) {
				mem_set( 0x138, 0, S_LONG );
			} else {
				rd [ 0 ] = mem_get( 0x138, S_LONG );
				if ( rd [ 0 ] == 0 )
					mem_set( 0x138, ra [ 1 ], S_LONG );
			}
			if ( save_s == 0 )
				SR_S_OFF();
			break;
		case 0x6E:	/* HSYNCST */
			err68( "水平同期割り込みを設定しようとしました" );
			return( TRUE );
		case 0x7F:	/* ONTIME */
			{
				uint64_t elapsed = run68_elapsed_centiseconds();
				rd[0] = (Long)(elapsed % 8640000u);
				rd[1] = (Long)((elapsed / 8640000u) & 0xffffu);
			}
			break;
		case 0x80:	/* B_INTVCS */
			rd [ 0 ] = Intvcs( rd [ 1 ], ra [ 1 ] );
			break;
		case 0x81:	/* B_SUPER */
			if ( ra [ 1 ] == 0 ) {
				/* user -> super */
				if ( SR_S_REF() != 0 ) {
					rd [ 0 ] = -1;	/* エラー */
				} else {
					rd [ 0 ] = ra [ 7 ];
					cpu_set_sr((UShort)sr | 0x2000u);
				}
			} else {
				/* super -> user */
				usp = ra [ 1 ];
				rd [ 0 ] = 0;
				cpu_set_sr((UShort)sr & 0xdfffu);
			}
			break;
		case 0x82:	/* B_BPEEK */
			save_s = SR_S_REF();
			SR_S_ON();
			rd [ 0 ] = ( ( rd [ 0 ] & 0xFFFFFF00 ) |
				     ( mem_get( ra [ 1 ], S_BYTE ) & 0xFF ) );
			if ( save_s == 0 )
				SR_S_OFF();
			ra [ 1 ] = run68_add32(ra [ 1 ], 1);
			break;
		case 0x83:	/* B_WPEEK */
			save_s = SR_S_REF();
			SR_S_ON();
			rd [ 0 ] = ( ( rd [ 0 ] & 0xFFFF0000 ) |
				     ( mem_get( ra [ 1 ], S_WORD ) & 0xFFFF ) );
			if ( save_s == 0 )
				SR_S_OFF();
			ra [ 1 ] = run68_add32(ra [ 1 ], 2);
			break;
		case 0x84:	/* B_LPEEK */
			save_s = SR_S_REF();
			SR_S_ON();
			rd [ 0 ] = mem_get( ra [ 1 ], S_LONG );
			if ( save_s == 0 )
				SR_S_OFF();
			ra [ 1 ] = run68_add32(ra [ 1 ], 4);
			break;
		case 0x85:	/* B_MEMSTR */
			Memstr(TRUE);
			break;
		case 0x86:	/* B_BPOKE */
			Poke(S_BYTE);
			break;
		case 0x87:	/* B_WPOKE */
			Poke(S_WORD);
			break;
		case 0x88:	/* B_LPOKE */
			Poke(S_LONG);
			break;
		case 0x89:	/* B_MEMSET */
			Memstr(FALSE);
			break;
		case 0x8A:	/* DMAMOVE */
			Dmamove( rd [ 1 ], rd [ 2 ], ra [ 1 ], ra [ 2 ] );
			break;
		case 0xAE:	/* OS_CURON */
			printf("\033[?25h");
			break;
		case 0xAF:	/* OS_CUROF */
			printf("\033[?25l");
			break;
		default:
    if (func_trace_f)
    {
			printf( "IOCS(%02X): Unknown IOCS call. Ignored.\n", no );
    }
			break;
	}

	return( FALSE );
}

static Long Keydata(int key)
{
	static const char letters[] = "qwertyuiopasdfghjklzxcvbnm";
	static const UChar letter_scans[] = {
		0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
		0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
		0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30
	};
	UChar ascii;
	UChar scan = 0;
	const char *letter;

	if (key == EOF)
		return 0;
	ascii = (UChar)key;
	if (ascii == '\n')
		ascii = '\r';
	if (ascii >= 'A' && ascii <= 'Z')
		letter = strchr(letters, ascii - 'A' + 'a');
	else if (ascii >= 'a' && ascii <= 'z')
		letter = strchr(letters, ascii);
	else
		letter = NULL;
	if (letter != NULL)
		scan = letter_scans[letter - letters];
	else if (ascii >= '1' && ascii <= '9')
		scan = (UChar)(ascii - '1' + 0x02);
	else {
		switch (ascii) {
		case 0x1b: scan = 0x01; break;
		case '0': scan = 0x0b; break;
		case '-': case '=': scan = 0x0c; break;
		case '^': case '~': scan = 0x0d; break;
		case '\\': case '|': scan = 0x0e; break;
		case '\b': scan = 0x0f; break;
		case '\t': scan = 0x10; break;
		case '@': case '`': scan = 0x1b; break;
		case '[': case '{': scan = 0x1c; break;
		case '\r': scan = 0x1d; break;
		case ';': case '+': scan = 0x28; break;
		case ']': case '}': scan = 0x29; break;
		case ':': case '*': scan = 0x2b; break;
		case ',': case '<': scan = 0x31; break;
		case '.': case '>': scan = 0x32; break;
		case '/': case '?': scan = 0x33; break;
		case '_': scan = 0x34; break;
		case ' ': scan = 0x35; break;
		case 0x7f: scan = 0x37; break;
		default: break;
		}
	}
	return ((Long)scan << 8) | ascii;
}

/*
 　機能：文字を表示する
 戻り値：カーソル位置
*/
static Long Putc( UShort code )
{
	if ( code == 0x1A ) {
		printf( "%c[0J", 0x1B ); /* 最終行左端まで消去 */
	} else {
		if ( code >= 0x0100 )
			putchar( code >> 8 );
		putchar( code );
	}
	return( get_locate() );
}

/*
 　機能：文字のカラー属性を指定する
 戻り値：変更前のカラーまたは現在のカラー
*/
static Long Color( short arg )
{
	if ( arg == -1 )	/* 現在のカラーを調べる(未サポート) */
		return( 3 );

	text_color( arg );

	return( 3 );
}

/*
 　機能：文字列を表示する
 戻り値：なし
*/
static void Putmes()
{
	char	temp [ 97 ];
	char	*p;
	int	x, y;
	int	keta;
	int	len;

	x = (rd [ 2 ] & 0xFFFF) + 1;
	y = (rd [ 3 ] & 0xFFFF) + 1;
	keta = (rd [ 4 ] & 0xFFFF) + 1;

	p = prog_ptr + ra [ 1 ];
	len = strlen( p );
	if ( keta > 96 )
		keta = 96;
	memcpy( temp, p, keta );
	temp [ keta ] = '\0';

	printf( "%c[%d;%dH", 0x1B, y, x );
	text_color( (rd [ 1 ] & 0xFF) );
	printf("%s", temp);

	ra [ 1 ] = run68_add32(ra [ 1 ], len);
}

static BOOL Leapyear(int year)
{
	return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
	           ? TRUE : FALSE;
}

static BOOL Validdate(int year, int month, int day)
{
	static const UChar month_days[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	int limit;

	if (year < 1980 || year > 2079 || month < 1 || month > 12 || day < 1)
		return FALSE;
	limit = month_days[month - 1];
	if (month == 2 && Leapyear(year))
		limit++;
	return day <= limit ? TRUE : FALSE;
}

static int Weekday(int year, int month, int day)
{
	static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

	if (month < 3)
		year--;
	return (year + year / 4 - year / 100 + year / 400 +
	        offsets[month - 1] + day) % 7;
}

static BOOL Isbcd(UChar value)
{
	return (value & 0x0f) <= 9 && ((value >> 4) & 0x0f) <= 9;
}

static UChar Tobcd(int value)
{
	return (UChar)(((value / 10) << 4) | (value % 10));
}

static int Frombcd(UChar value)
{
	return ((value >> 4) & 0x0f) * 10 + (value & 0x0f);
}

static Long Datebcd(Long binary)
{
	int year = ((ULong)binary >> 16) & 0x0fff;
	int month = ((ULong)binary >> 8) & 0xff;
	int day = (ULong)binary & 0xff;
	int leap_counter;

	if (!Validdate(year, month, day))
		return -1;
	leap_counter = year & 3;
	return ((Long)leap_counter << 28) | (Weekday(year, month, day) << 24) |
	       ((Long)Tobcd(year - 1980) << 16) | ((Long)Tobcd(month) << 8) |
	       Tobcd(day);
}

static Long Dateset(Long bcd)
{
	UChar year_bcd = (UChar)((ULong)bcd >> 16);
	UChar month_bcd = (UChar)((ULong)bcd >> 8);
	UChar day_bcd = (UChar)bcd;

	if (!Isbcd(year_bcd) || !Isbcd(month_bcd) || !Isbcd(day_bcd))
		return -1;
	return run68_set_virtual_date(1980 + Frombcd(year_bcd),
	                              Frombcd(month_bcd), Frombcd(day_bcd))
	           ? 0 : -1;
}

static Long Timebcd(Long binary)
{
	int hour = ((ULong)binary >> 16) & 0xff;
	int minute = ((ULong)binary >> 8) & 0xff;
	int second = (ULong)binary & 0xff;

	if (hour > 23 || minute > 59 || second > 59)
		return -1;
	return 0x10000000 | ((Long)Tobcd(hour) << 16) |
	       ((Long)Tobcd(minute) << 8) | Tobcd(second);
}

static Long Timeset(Long bcd)
{
	UChar hour_bcd = (UChar)((ULong)bcd >> 16);
	UChar minute_bcd = (UChar)((ULong)bcd >> 8);
	UChar second_bcd = (UChar)bcd;

	if (!Isbcd(hour_bcd) || !Isbcd(minute_bcd) || !Isbcd(second_bcd))
		return -1;
	return run68_set_virtual_time(Frombcd(hour_bcd), Frombcd(minute_bcd),
	                              Frombcd(second_bcd)) ? 0 : -1;
}

/*
 　機能：日付を得る
 戻り値：BCDの日付データ
*/
static Long Dateget()
{
	struct tm value;
	int year;

	if (run68_get_virtual_localtime(&value) == FALSE)
		return -1;
	year = value.tm_year - 80;
	if (year < 0 || year > 99)
		return -1;
	return ((Long)value.tm_wday << 24) | ((Long)Tobcd(year) << 16) |
	       ((Long)Tobcd(value.tm_mon + 1) << 8) | Tobcd(value.tm_mday);
}

/*
 　機能：時刻を得る
 戻り値：BCDの時刻データ
*/
static Long Timeget()
{
	struct tm value;

	if (run68_get_virtual_localtime(&value) == FALSE)
		return -1;
	return 0x10000000 | ((Long)Tobcd(value.tm_hour) << 16) |
	       ((Long)Tobcd(value.tm_min) << 8) | Tobcd(value.tm_sec);
}

/*
 　機能：BCD表現の日付データをバイナリ表現に直す
 戻り値：バイナリの日付データ
*/
static Long Datebin( Long bcd )
{
	UShort	youbi;
	UShort	year;
	UShort	month;
	UShort	day;

	youbi = ((ULong)bcd >> 24) & 0x0f;
	year  = (( bcd >> 20 ) & 0xF) * 10 + (( bcd >> 16 ) & 0xF) + 1980;
	month = (( bcd >> 12 ) & 0xF) * 10 + (( bcd >> 8 ) & 0xF);
	day   = (( bcd >> 4 ) & 0xF) * 10 + (bcd & 0xF);
	if (!Isbcd((UChar)((ULong)bcd >> 16)) ||
	    !Isbcd((UChar)((ULong)bcd >> 8)) || !Isbcd((UChar)bcd) ||
	    youbi > 6 || !Validdate(year, month, day))
		return -1;

	return( (youbi << 28) | (year << 16) | (month << 8) | day );
}

/*
 　機能：BCD表現の時刻データをバイナリ表現に直す
 戻り値：バイナリの時刻データ
*/
static Long Timebin( Long bcd )
{
	UShort	hh;
	UShort	mm;
	UShort	ss;

	hh = (( bcd >> 20 ) & 0xF) * 10 + (( bcd >> 16 ) & 0xF);
	mm = (( bcd >> 12 ) & 0xF) * 10 + (( bcd >> 8 ) & 0xF);
	ss = (( bcd >> 4 ) & 0xF) * 10 + (bcd & 0xF);
	if (!Isbcd((UChar)((ULong)bcd >> 16)) ||
	    !Isbcd((UChar)((ULong)bcd >> 8)) || !Isbcd((UChar)bcd) ||
	    hh > 23 || mm > 59 || ss > 59)
		return -1;

	return( (hh << 16) | (mm << 8) | ss );
}

static char *Gueststring(Long address, ULong *guest_address)
{
	ULong normalized = (ULong)address & 0x00ffffffu;
	char *text;

	if (normalized >= (ULong)mem_aloc)
		return NULL;
	text = prog_ptr + normalized;
	if (memchr(text, '\0', (size_t)((ULong)mem_aloc - normalized)) == NULL)
		return NULL;
	*guest_address = normalized;
	return text;
}

static Long Datecnv(Long address)
{
	ULong guest_address;
	char *text = Gueststring(address, &guest_address);
	char *cursor;
	char *end;
	long year;
	long month;
	long day;

	if (text == NULL)
		return -1;
	year = strtol(text, &end, 10);
	if (end == text || *end == '\0')
		return -1;
	cursor = end + 1;
	month = strtol(cursor, &end, 10);
	if (end == cursor || *end == '\0')
		return -1;
	cursor = end + 1;
	day = strtol(cursor, &end, 10);
	if (end == cursor || *end != '\0')
		return -1;
	if (year >= 0 && year <= 79)
		year += 2000;
	else if (year >= 80 && year <= 99)
		year += 1900;
	if (!Validdate((int)year, (int)month, (int)day))
		return -1;
	ra[1] = (Long)(guest_address + (ULong)(end - text));
	return (Weekday((int)year, (int)month, (int)day) << 28) |
	       ((Long)year << 16) | ((Long)month << 8) | (Long)day;
}

static Long Timecnv(Long address)
{
	ULong guest_address;
	char *text = Gueststring(address, &guest_address);
	char *cursor;
	char *end;
	long hour;
	long minute;
	long second;

	if (text == NULL)
		return -1;
	hour = strtol(text, &end, 10);
	if (end == text || *end != ':')
		return -1;
	cursor = end + 1;
	minute = strtol(cursor, &end, 10);
	if (end == cursor || *end != ':')
		return -1;
	cursor = end + 1;
	second = strtol(cursor, &end, 10);
	if (end == cursor || *end != '\0' || hour < 0 || hour > 23 ||
	    minute < 0 || minute > 59 || second < 0 || second > 59)
		return -1;
	ra[1] = (Long)(guest_address + (ULong)(end - text));
	return ((Long)hour << 16) | ((Long)minute << 8) | (Long)second;
}

/*
 　機能：バイナリ表現の日付データを文字列に直す
 戻り値：-1のときエラー
*/
static Long Dateasc( Long data, Long adr )
{
	char	*data_ptr;
	ULong	guest_address;
	UShort	year;
	UShort	month;
	UShort	day;
	int	form;

	guest_address = (ULong)adr & 0x00ffffffu;
	if (guest_address >= (ULong)mem_aloc ||
	    (ULong)mem_aloc - guest_address < 11u)
		return -1;
	data_ptr = prog_ptr + guest_address;

	form = data >> 28;
	year = ((data >> 16) & 0xFFF);
	if ( year < 1980 || year > 2079 )
		return( -1 );
	month = ((data >> 8) & 0xFF);
	if ( month < 1 || month > 12 )
		return( -1 );
	day = (data & 0xFF);
	if (!Validdate(year, month, day))
		return( -1 );

	switch( form ) {
		case 0:
			snprintf(data_ptr, 11, "%04d/%02d/%02d", year, month, day);
			ra [ 1 ] = run68_add32(ra [ 1 ], 10);
			break;
		case 1:
			snprintf(data_ptr, 11, "%04d-%02d-%02d", year, month, day);
			ra [ 1 ] = run68_add32(ra [ 1 ], 10);
			break;
		case 2:
			snprintf(data_ptr, 9, "%02d/%02d/%02d", year % 100, month, day);
			ra [ 1 ] = run68_add32(ra [ 1 ], 8);
			break;
		case 3:
			snprintf(data_ptr, 9, "%02d-%02d-%02d", year % 100, month, day);
			ra [ 1 ] = run68_add32(ra [ 1 ], 8);
			break;
		default:
			return( -1 );
	}

	return( 0 );
}

/*
 　機能：バイナリ表現の時刻データを文字列に直す
 戻り値：-1のときエラー
*/
static Long Timeasc( Long data, Long adr )
{
	char	*data_ptr;
	ULong	guest_address;
	UShort	hh;
	UShort	mm;
	UShort	ss;

	guest_address = (ULong)adr & 0x00ffffffu;
	if (guest_address >= (ULong)mem_aloc ||
	    (ULong)mem_aloc - guest_address < 9u)
		return -1;
	data_ptr = prog_ptr + guest_address;

	hh = ((data >> 16) & 0xFF);
	if ( hh < 0 || hh > 23 )
		return( -1 );
	mm = ((data >> 8) & 0xFF);
	if ( mm < 0 || mm > 59 )
		return( -1 );
	ss = (data & 0xFF);
	if ( ss < 0 || ss > 59 )
		return( -1 );

	snprintf(data_ptr, 9, "%02d:%02d:%02d", hh, mm, ss);
	ra [ 1 ] = run68_add32(ra [ 1 ], 8);

	return( 0 );
}

/*
 　機能：曜日番号から文字列を得る
 戻り値：なし
*/
static void Dayasc( Long data, Long adr )
{
	static const UChar sjis[][2] = {
		{0x93, 0xfa}, {0x8c, 0x8e}, {0x89, 0xce}, {0x90, 0x85},
		{0x96, 0xd8}, {0x8b, 0xe0}, {0x93, 0x79}
	};
	ULong guest_address = (ULong)adr & 0x00ffffffu;
	UChar *data_ptr;

	if ((ULong)data > 6u || guest_address >= (ULong)mem_aloc ||
	    (ULong)mem_aloc - guest_address < 3u)
		return;
	data_ptr = (UChar *)prog_ptr + guest_address;
	data_ptr[0] = sjis[data][0];
	data_ptr[1] = sjis[data][1];
	data_ptr[2] = '\0';
	ra[1] = run68_add32(ra[1], 2);
}

/*
 　機能：ベクタ・テーブルを書き換える
 戻り値：設定前の処理アドレス
*/
static Long Intvcs( Long no, Long adr )
{
	Long	adr2;
	Long	mae = 0;
	short	save_s;

	no &= 0xFFFF;
	adr2 = no * 4;
	save_s = SR_S_REF();
	SR_S_ON();
	mae = mem_get( adr2, S_LONG );
	mem_set( adr2, adr, S_LONG );
	if ( save_s == 0 )
		SR_S_OFF();

	return( mae );
}

static void Memstr(BOOL read_direction)
{
	ULong count = (ULong)rd[1] + 1u;
	Long source = read_direction ? ra[1] : ra[2];
	Long destination = read_direction ? ra[2] : ra[1];
	short save_s = SR_S_REF();

	SR_S_ON();
	while (count-- != 0) {
		Long value = mem_get(source, S_BYTE);
		mem_set(destination, value, S_BYTE);
		source = run68_add32(source, 1);
		destination = run68_add32(destination, 1);
	}
	if (save_s == 0)
		SR_S_OFF();
	if (read_direction) {
		ra[1] = source;
		ra[2] = destination;
	} else {
		ra[1] = destination;
		ra[2] = source;
	}
	rd[1] = -1;
}

static void Poke(char size)
{
	static const Long widths[] = {1, 2, 4};
	short save_s = SR_S_REF();

	SR_S_ON();
	mem_set(ra[1], rd[1], size);
	if (save_s == 0)
		SR_S_OFF();
	ra[1] = run68_add32(ra[1], widths[(int)size]);
}

/*
 　機能：DMA転送をする
 戻り値：設定前の処理アドレス
*/
static void Dmamove( Long md, Long size, Long adr1, Long adr2 )
{
	unsigned a1_mode = ((ULong)md >> 2) & 3u;
	unsigned a2_mode = (ULong)md & 3u;
	ULong remaining = (ULong)size;
	short save_s = SR_S_REF();

	if (a1_mode == 3 || a2_mode == 3) {
		rd[0] = -1;
		return;
	}
	SR_S_ON();
	while (remaining-- != 0) {
		if ((md & 0x80) == 0)
			mem_set(adr2, mem_get(adr1, S_BYTE), S_BYTE);
		else
			mem_set(adr1, mem_get(adr2, S_BYTE), S_BYTE);
		if (a1_mode == 1)
			adr1 = run68_add32(adr1, 1);
		else if (a1_mode == 2)
			adr1 = run68_sub32(adr1, 1);
		if (a2_mode == 1)
			adr2 = run68_add32(adr2, 1);
		else if (a2_mode == 2)
			adr2 = run68_sub32(adr2, 1);
	}
	if (save_s == 0)
		SR_S_OFF();
	ra[1] = adr1;
	ra[2] = adr2;
	rd[2] = 0;
}
