#pragma once

// NOTE(Felix): Necessary includes
// "language_layer.h"
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>

// NOTE(Felix): Usefule stuff that one can copy over on need
#if 0
// NOTE(Felix): Setup on startup
{
	// NOTE(Felix): Disable printf stdout buffering
	setvbuf(stdout, 0, _IONBF, 0);

	// NOTE(Felix): Set signal so a CTRL-C restores console settings
	// Calls (custom) function internal void SignalSIGINTHandler(int Signal)
	// needs <signal.h>
	{
		struct sigaction SignalAction = { 0 };
		SignalAction.sa_handler = &SignalSIGINTHandler;
		sigaction(SIGINT, &SignalAction, 0);
	}
	
	// NOTE(Felix): Handle resize signal so we can reformat the window
	// Calls (custom) function internal void SignalSIGWINCHHandler(int Signal)
	// needs <signal.h>
	{
		struct sigaction SignalAction = { 0 };
		SignalAction.sa_handler = &SignalSIGWINCHHandler;
		sigaction(SIGWINCH, &SignalAction, 0);
	}
	
	// NOTE(Felix): Disable buffering of input, we want to process it immediately
	{
		struct termios TerminalSettings = { 0 };
		tcgetattr(STDIN_FILENO, &TerminalSettings);
		TerminalSettings.c_lflag &= (tcflag_t)~ICANON;
		tcsetattr(STDIN_FILENO, TCSANOW, &TerminalSettings);
	}
}
	
// NOTE(Felix): Polling of input
// needs <poll.h>
while (1)
{
	struct pollfd PollRequest = { 0 };
	PollRequest.fd = STDIN_FILENO;
	PollRequest.events = POLLIN;

	// NOTE(Felix): Wait for either
	//  - Input
	//  - Interrupt of any kind (including resizing of console)
	poll(&PollRequest, 1, -1);

	if (SomeFlagThatWasSetInAHandler)
	{
		// Handle stuff
		...

			continue;
	}

	read(STDIN_FILENO, &InputCharacter, sizeof(InputCharacter));

	...
}

// NOTE(Felix): Functions that may come in handy
internal void
ConsoleSetup(void)
{
	EchoDisable();
	CursorHide();
}

internal void
ConsoleCleanup(void)
{
	//ColorResetToDefault();
	ScreenClear();
	EchoEnable();
	CursorShow();
	CursorMove(0, 0);
}

internal void
ColorSet(i32 ForegroundColor, i32 BackgroundColor)
{
	printf("\033[%d;%dm", BackgroundColor, ForegroundColor);
}
#endif


internal void
ScreenClear(void)
{
	printf("\033[2J");
}

internal void
ClearCurrentLine(void)
{
	printf("\033[2K");
}

internal void
EchoDisable(void)
{
	struct termios TerminalSettings = { 0 };
	tcgetattr(STDOUT_FILENO, &TerminalSettings);
	TerminalSettings.c_lflag &= (tcflag_t) ~ECHO;
	tcsetattr(STDOUT_FILENO, TCSANOW, &TerminalSettings);
}

internal void
EchoEnable(void)
{
	struct termios TerminalSettings = { 0 };
	tcgetattr(STDOUT_FILENO, &TerminalSettings);
	TerminalSettings.c_lflag |= (tcflag_t) ECHO;
	tcsetattr(STDOUT_FILENO, TCSANOW, &TerminalSettings);
}

internal void
CursorHide(void)
{
	printf("\033[?25l");
}

internal void
CursorShow(void)
{
	printf("\033[?25h");
}

internal void
CursorMoveTo(i32 Y, i32 X)
{
	// NOTE(Felix): This function is zero indexed, ANSI escape sequences apparently aren't though
	printf("\033[%d;%dH", Y+1, X+1);
}

internal void
ConsoleUpdateDimensions(i32 *ConsoleRows, i32 *ConsoleColumns)
{
	struct winsize ConsoleDimensions = { 0 };
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ConsoleDimensions);
	*ConsoleRows = ConsoleDimensions.ws_row;
	*ConsoleColumns = ConsoleDimensions.ws_col;
}

internal void
ConsoleMoveCursorUp(i32 LinesToMove)
{
	printf("\033[%dA", LinesToMove);
}
