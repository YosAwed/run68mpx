#undef MAIN

#include "run68.h"

#if defined(WIN32) || defined(DOSX)
#include <conio.h>

int run68_console_getch(BOOL echo)
{
	return echo ? _getche() : _getch();
}

int run68_console_kbhit(void)
{
	return _kbhit() != 0;
}

BOOL run68_console_ungetch(int c)
{
	return _ungetch(c) == EOF ? FALSE : TRUE;
}

BOOL run68_console_begin_raw_input(void)
{
	return TRUE;
}

void run68_console_end_raw_input(void)
{
}

#elif defined(__EMSCRIPTEN__)

int run68_console_getch(BOOL echo)
{
	(void)echo;
	return getchar();
}

int run68_console_kbhit(void)
{
	return 1;
}

BOOL run68_console_ungetch(int c)
{
	return ungetc((unsigned char)c, stdin) == EOF ? FALSE : TRUE;
}

BOOL run68_console_begin_raw_input(void)
{
	return TRUE;
}

void run68_console_end_raw_input(void)
{
}

#else

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static int console_pushback = EOF;
static int console_raw_active;
static struct termios console_original;

int run68_console_getch(BOOL echo)
{
	struct termios original;
	struct termios raw;
	int result;

	if (console_pushback != EOF) {
		result = console_pushback;
		console_pushback = EOF;
		return result;
	}
	if (console_raw_active)
		return getchar();
	if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &original) != 0)
		return getchar();
	raw = original;
	raw.c_lflag &= (tcflag_t)~ICANON;
	if (echo)
		raw.c_lflag |= ECHO;
	else
		raw.c_lflag &= (tcflag_t)~ECHO;
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
		return getchar();
	result = getchar();
	(void)tcsetattr(STDIN_FILENO, TCSANOW, &original);
	return result;
}

int run68_console_kbhit(void)
{
	fd_set read_set;
	struct timeval timeout = {0, 0};

	if (console_pushback != EOF)
		return 1;
	FD_ZERO(&read_set);
	FD_SET(STDIN_FILENO, &read_set);
	return select(STDIN_FILENO + 1, &read_set, NULL, NULL, &timeout) > 0;
}

BOOL run68_console_ungetch(int c)
{
	if (console_pushback != EOF || c == EOF)
		return FALSE;
	console_pushback = (unsigned char)c;
	return TRUE;
}

BOOL run68_console_begin_raw_input(void)
{
	struct termios raw;

	if (console_raw_active || !isatty(STDIN_FILENO))
		return TRUE;
	if (tcgetattr(STDIN_FILENO, &console_original) != 0)
		return FALSE;
	raw = console_original;
	raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
		return FALSE;
	console_raw_active = 1;
	return TRUE;
}

void run68_console_end_raw_input(void)
{
	if (!console_raw_active)
		return;
	(void)tcsetattr(STDIN_FILENO, TCSANOW, &console_original);
	console_raw_active = 0;
}

#endif

void run68_console_flush_input(void)
{
	while (run68_console_kbhit()) {
		if (run68_console_getch(FALSE) == EOF)
			break;
	}
}
