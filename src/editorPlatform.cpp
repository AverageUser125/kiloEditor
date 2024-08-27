#include "editorPlatform.hpp"
#include "editor.hpp"
#include <cassert>
#include <ctime>

#if defined(_WIN32) || defined(WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef DELETE


static DWORD originalConsoleMode;

int readKey() {
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD numEvents = 0;
	INPUT_RECORD ir;
	DWORD eventsRead;
	int refreshCount = 0;
	while (true) {
		updateWindowSize();
		Sleep(1);

		// Check for available input events
		if (!GetNumberOfConsoleInputEvents(hStdin, &numEvents) || numEvents == 0) {
			// check if status message time has passed meanwhile
			if ((time(NULL) - E.statusmsg_time > STATUS_MESSAGE_TIME) && refreshCount == 0) {
				editorRefreshScreen();
				refreshCount++;
			}
			continue; // No events available, continue waiting
		}

		// Read input events
		if (!ReadConsoleInput(hStdin, &ir, 1, &eventsRead)) {
			editorSetStatusMessage("");
			continue; // Error reading input, continue waiting
		}

		if (ir.EventType == KEY_EVENT) {
// Process the key event
#define TEMP_KEY_EVENT KEY_EVENT
#undef KEY_EVENT
			// Process the key event
			KEY_EVENT_RECORD keyEvent = (KEY_EVENT_RECORD)ir.Event.KeyEvent;
#define KEY_EVENT TEMP_KEY_EVENT
#undef TEMP_KEY_EVENT
			bool ctrlPressed = (keyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
			bool keyDown = keyEvent.bKeyDown;

			if (keyDown) {
				if (ctrlPressed && keyEvent.wVirtualKeyCode == 'S') {
					// Handle Ctrl+S
					editorSetStatusMessage("Ctrl+S pressed!");
					return CTRL_KEY('S');
				}

				switch (keyEvent.wVirtualKeyCode) {
				case VK_UP:
					return ARROW_UP;
				case VK_DOWN:
					return ARROW_DOWN;
				case VK_LEFT:
					return ARROW_LEFT;
				case VK_RIGHT:
					return ARROW_RIGHT;
				case VK_HOME:
					return HOME_KEY;
				case VK_END:
					return END_KEY;
				case VK_PRIOR:
					return PAGE_UP;
				case VK_NEXT:
					return PAGE_DOWN;
				case VK_DELETE:
					return DEL_KEY;
				case VK_RETURN:
					return '\n';
				case VK_ESCAPE:
					return '\x1b';
				case VK_TAB:
					return '\t';
				case VK_BACK:
					return BACKSPACE;
				default:
					if (keyEvent.uChar.AsciiChar == 0 || keyEvent.uChar.AsciiChar == -1) {
						continue; // Skip if no character code
					}
					if (ctrlPressed) {
						return CTRL_KEY(keyEvent.uChar.AsciiChar);
					}
					return keyEvent.uChar.AsciiChar;
				}
			}
		}
	}
}

void updateWindowSize() {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

	if (E.screencols != width || E.screenrows !=height) {

		E.screencols = width;
		E.screenrows = height; // Reserve space for status lines
		if (E.screenrows > 3) {
			editorRefreshScreen();
		}
	}
	E.screenrows -= 2;
}

int enableRawMode() {
	// assert(E.screencols != 0 || E.screenrows != 0);
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode;

	// Get the current console mode
	if (!GetConsoleMode(hStdin, &originalConsoleMode)) {
		return 1; // Error getting console mode
	}

	// Modify the mode to disable processed input, echo input, and line input
	mode = originalConsoleMode;
	mode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_INSERT_MODE |
			  ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT);
	mode |= (ENABLE_VIRTUAL_TERMINAL_INPUT & ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	// Set the new console mode
	if (!SetConsoleMode(hStdin, mode)) {
		SetConsoleMode(hStdin, originalConsoleMode);
		return -1; // Error setting console mode
	}
	return 0;
}

void disableRawMode() {
	std::cout << "\033[2J" << std::flush;
	// Restore the original console mode
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleMode(hConsole, originalConsoleMode);

	CONSOLE_CURSOR_INFO ci;
	GetConsoleCursorInfo(hConsole, &ci);
	ci.bVisible = true;
	SetConsoleCursorInfo(hConsole, &ci);
}

void editorRefreshScreen() {
	editorScroll();

	struct abuf ab = {nullptr, 0};

	CONSOLE_SCREEN_BUFFER_INFO csbi;
	CONSOLE_CURSOR_INFO ci;

	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	GetConsoleCursorInfo(hConsole, &ci);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);
	editorDrawLineCount(&ab);

	ci.bVisible = false;
	SetConsoleCursorInfo(hConsole, &ci);

	DWORD written = 0;
	// DWORD consoleSize = csbi.dwSize.X * csbi.dwSize.Y;
	// FillConsoleOutputCharacterA(hConsole, ' ', consoleSize, {0, 0}, &written);

	SetConsoleCursorPosition(hConsole, {0, 0});

	WriteConsoleA(hConsole, ab.b, ab.len, &written, nullptr);

	ci.bVisible = true;
	SetConsoleCursorInfo(hConsole, &ci);

	abFree(&ab);
}

#elif defined(__unix__) || defined(linux) || defined(__APPLE__)
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

static struct termios orig_termios;
void handleSigWinCh(int unused __attribute__((unused))) {
	updateWindowSize();
	if (E.cy > E.screenrows) E.cy = E.screenrows - 1;
	if (E.cx > E.screencols) E.cx = E.screencols - 1;
	editorRefreshScreen();
}
int enableRawMode() {
	signal(SIGWINCH, handleSigWinCh);
	write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen

	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
		return 1;

	struct termios raw = orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		return -1;
	
	// make cursor small
	// disabled cause I can't reset it afterwards
	// std::cout << "\033[6 q" << std::flush;
	return 0;
}

void disableRawMode() {
	write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

int readKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN){
			continue;
		}
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '1':
						return HOME_KEY;
					case '3':
						return DEL_KEY;
					case '4':
						return END_KEY;
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					case '7':
						return HOME_KEY;
					case '8':
						return END_KEY;
					}
				}
			} else {
				switch (seq[1]) {
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;
				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				case 'H':
					return HOME_KEY;
				case 'F':
					return END_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
			case 'H':
				return HOME_KEY;
			case 'F':
				return END_KEY;
			}
		}

		return '\x1b';
	} else {
		return c;
	}
}

int getCursorPosition(int* rows, int* cols) {
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

void updateWindowSize() {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			//return -1;
		getCursorPosition(&E.screenrows, &E.screencols);
	} else {
		E.screencols = ws.ws_col;
		E.screenrows = ws.ws_row;
		// return 0;
	}
	E.screenrows -= 2;
}


void editorRefreshScreen() {
	editorScroll();

	struct abuf ab = {nullptr, 0};
	
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);
	editorDrawLineCount(&ab);

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

#endif
