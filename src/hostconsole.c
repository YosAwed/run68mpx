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

#else

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static int console_pushback = EOF;

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

#endif

void run68_console_flush_input(void)
{
	while (run68_console_kbhit()) {
		if (run68_console_getch(FALSE) == EOF)
			break;
	}
}
