

/*** includes ***/
#include "config.hpp"
#include "editor.hpp"
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>

/*** defines ***/

//#ifdef __unix__
// #include "editorPlatformLinux.hpp"
//#elif defined(_WIN32) || defined(WIN32)
#include "editorPlatform.hpp"
// #endif

#define CTRL_KEY(k) ((k) & 0x1f)


enum editorHighlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

/*** data ***/



struct editorConfig E;
/*** filetypes ***/

char* C_HL_extensions[] = {".c", ".h", ".cpp", nullptr};
char* C_HL_keywords[] = {"switch", "if",	  "while",	 "for",	   "break", "continue",	 "return",	"else",	 "struct",
						 "union",  "typedef", "static",	 "enum",   "class", "case",

						 "int|",   "long|",	  "double|", "float|", "char|", "unsigned|", "signed|", "void|", NULL};

struct editorSyntax HLDB[] = {
	{"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

/*** prototypes ***/
void editorSetStatusMessage(const char* fmt, ...);
char* editorPrompt(const char* prompt, void (*callback)(char*, int));

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

#pragma region terminal

/*** terminal ***/

/*** syntax highlighting ***/

int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow* row) {
	row->hl = static_cast<unsigned char*>(realloc(row->hl, row->rsize));
	memset(row->hl, HL_NORMAL, row->rsize);

	if (E.syntax == NULL)
		return;

	char** keywords = E.syntax->keywords;

	char* scs = E.syntax->singleline_comment_start;
	char* mcs = E.syntax->multiline_comment_start;
	char* mce = E.syntax->multiline_comment_end;

	int scs_len = scs ? strlen(scs) : 0;
	int mcs_len = mcs ? strlen(mcs) : 0;
	int mce_len = mce ? strlen(mce) : 0;

	int prev_sep = 1;
	int in_string = 0;
	int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

	int i = 0;
	while (i < row->rsize) {
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

		if (scs_len && !in_string && !in_comment) {
			if (!strncmp(&row->render[i], scs, scs_len)) {
				memset(&row->hl[i], HL_COMMENT, row->rsize - i);
				break;
			}
		}

		if (mcs_len && mce_len && !in_string) {
			if (in_comment) {
				row->hl[i] = HL_MLCOMMENT;
				if (!strncmp(&row->render[i], mce, mce_len)) {
					memset(&row->hl[i], HL_MLCOMMENT, mce_len);
					i += mce_len;
					in_comment = 0;
					prev_sep = 1;
					continue;
				} else {
					i++;
					continue;
				}
			} else if (!strncmp(&row->render[i], mcs, mcs_len)) {
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
				i += mcs_len;
				in_comment = 1;
				continue;
			}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				row->hl[i] = HL_STRING;
				if (c == '\\' && i + 1 < row->rsize) {
					row->hl[i + 1] = HL_STRING;
					i += 2;
					continue;
				}
				if (c == in_string)
					in_string = 0;
				i++;
				prev_sep = 1;
				continue;
			} else {
				if (c == '"' || c == '\'') {
					in_string = c;
					row->hl[i] = HL_STRING;
					i++;
					continue;
				}
			}
		}

		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				i++;
				prev_sep = 0;
				continue;
			}
		}

		if (prev_sep) {
			int j;
			for (j = 0; keywords[j]; j++) {
				int klen = strlen(keywords[j]);
				int kw2 = keywords[j][klen - 1] == '|';
				if (kw2)
					klen--;

				if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i + klen])) {
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					i += klen;
					break;
				}
			}
			if (keywords[j] != NULL) {
				prev_sep = 0;
				continue;
			}
		}

		prev_sep = is_separator(c);
		i++;
	}

	int changed = (row->hl_open_comment != in_comment);
	row->hl_open_comment = in_comment;
	if (changed && row->idx + 1 < E.numrows)
		editorUpdateSyntax(&E.row[row->idx + 1]);
}

int editorSyntaxToColor(int hl) {
	switch (hl) {
	case HL_COMMENT:
	case HL_MLCOMMENT:
		return 36;
	case HL_KEYWORD1:
		return 33;
	case HL_KEYWORD2:
		return 32;
	case HL_STRING:
		return 35;
	case HL_NUMBER:
		return 31;
	case HL_MATCH:
		return 34;
	default:
		return 37;
	}
}

void editorSelectSyntaxHighlight() {
	E.syntax = NULL;
	if (E.filename == NULL)
		return;

	char* ext = strrchr(E.filename, '.');

	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
		struct editorSyntax* s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i]))) {
				E.syntax = s;

				int filerow;
				for (filerow = 0; filerow < E.numrows; filerow++) {
					editorUpdateSyntax(&E.row[filerow]);
				}

				return;
			}
			i++;
		}
	}
}

/*** row operations ***/

int editorRowCxToRx(erow* row, int cx) {
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
		rx++;
	}
	return rx;
}

int editorRowRxToCx(erow* row, int rx) {
	int cur_rx = 0;
	int cx;
	for (cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t')
			cur_rx += (TAB_SIZE - 1) - (cur_rx % TAB_SIZE);
		cur_rx++;

		if (cur_rx > rx)
			return cx;
	}
	return cx;
}

void editorUpdateRow(erow* row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t')
			tabs++;

	free(row->render);
	row->render = static_cast<char*>(malloc(row->size + tabs * (TAB_SIZE - 1) + 1));

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % TAB_SIZE != 0)
				row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, const char* s, size_t len) {
	if (at < 0 || at > E.numrows)
		return;

	E.row = static_cast<erow*>(realloc(E.row, sizeof(erow) * (E.numrows + 1)));
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
	for (int j = at + 1; j <= E.numrows; j++)
		E.row[j].idx++;

	E.row[at].idx = at;

	E.row[at].size = len;
	E.row[at].chars = static_cast<char*>(malloc(len + 1));
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	E.row[at].hl = NULL;
	E.row[at].hl_open_comment = 0;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;
}

void editorFreeRow(erow* row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at) {
	if (at < 0 || at >= E.numrows)
		return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
	for (int j = at; j < E.numrows - 1; j++)
		E.row[j].idx--;
	E.numrows--;
	E.dirty++;
}

void editorRowInsertChar(erow* row, int at, int c) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = static_cast<char*>(realloc(row->chars, row->size + 2));
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(erow* row, char* s, size_t len) {
	row->chars = static_cast<char*>(realloc(row->chars, row->size + len + 1));
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow* row, int at) {
	if (at < 0 || at >= row->size)
		return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
	if (E.cy == E.numrows) {
		editorInsertRow(E.numrows, "", 0);
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}

void editorInsertNewline() {
	if (E.cx == 0) {
		editorInsertRow(E.cy, "", 0);
	} else {
		erow* row = &E.row[E.cy];
		editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
		row = &E.row[E.cy];
		row->size = E.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.cy++;
	E.cx = 0;
}

void editorDelChar() {
	if (E.cx == 0 && E.cy == 0) {
		if (E.numrows == 1 && E.row[0].size == 0) {
			editorFreeRow(&E.row[0]);
			E.numrows = 0;
		}
		return;
	}
	if (E.cy == E.numrows) {
		E.cx = E.row[E.cy - 1].size;
		E.cy--;
		return;
	}
	erow* row = &E.row[E.cy];
	if (E.cx > 0) {
		editorRowDelChar(row, E.cx - 1);
		E.cx--;
	} else {
		E.cx = E.row[E.cy - 1].size;
		editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
		editorDelRow(E.cy);
		E.cy--;
	}
}

/*** file i/o ***/
char* editorRowsToString(int* buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < E.numrows; j++)
		totlen += E.row[j].size + 1;
	*buflen = totlen;

	char* buf = static_cast<char*>(malloc(totlen));
	char* p = buf;
	for (j = 0; j < E.numrows; j++) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

bool editorOpen(const char* filename) {
	if (E.filename != nullptr) {
		free(E.filename);
	}
	E.filename = strdup(filename);

	editorSelectSyntaxHighlight();
	
	// Create the file if it doesn't exist
	std::ofstream createFile(filename, std::ios::app);
	if (!createFile) {
		return false;
	}
	createFile.close();


	std::ifstream file(filename, std::ios::binary);
	if (!file.is_open()) {
		return false;
		// die("fopen");
	}

	std::string line;
	while (std::getline(file, line)) {
		// Remove trailing newline and carriage return characters
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
			line.pop_back();
		}
		editorInsertRow(E.numrows, line.c_str(), line.size());
	}

	E.dirty = false;
	return true;
}

void editorSave() {
	if (E.filename == NULL) {
		E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if (E.filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
		editorSelectSyntaxHighlight();
	}

	int buflen = 0;
	char* buf = editorRowsToString(&buflen);

	std::ofstream file(E.filename, std::ios::binary | std::ios::trunc);

    if (file.is_open()) {
		file.write(buf, buflen);
        if (file.good()) {
            file.close();
			free(buf);
            E.dirty = false;
            editorSetStatusMessage("%d bytes written to disk", buflen);
            return;
        }
    }

	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(char* query, int key) {
	static int last_match = -1;
	static int direction = 1;

	static int saved_hl_line;
	static char* saved_hl = NULL;

	if (saved_hl) {
		memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	} else {
		last_match = -1;
		direction = 1;
	}

	if (last_match == -1)
		direction = 1;
	int current = last_match;
	int i;
	for (i = 0; i < E.numrows; i++) {
		current += direction;
		if (current == -1)
			current = E.numrows - 1;
		else if (current == E.numrows)
			current = 0;

		erow* row = &E.row[current];
		char* match = strstr(row->render, query);
		if (match) {
			last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, match - row->render);
			E.rowoff = E.numrows;

			saved_hl_line = current;
			saved_hl = static_cast<char*>(malloc(row->rsize));
			memcpy(saved_hl, row->hl, row->rsize);
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
			break;
		}
	}
}

void editorFind() {
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char* query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

	if (query) {
		free(query);
	} else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
}

/*** append buffer ***/


void abAppend(struct abuf* ab, const char* s, int len) {
	char* newLine = static_cast<char*>(realloc(ab->b, ab->len + len));

	if (newLine == NULL)
		return;
	memcpy(&newLine[ab->len], s, len);
	ab->b = newLine;
	ab->len += len;
}

void abFree(struct abuf* ab) {
	free(ab->b);
}

/*** output ***/

void editorScroll() {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}

	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff + E.screencols) {
		E.coloff = E.rx - E.screencols + 1;
	}
}

void welcomeMessage(abuf* ab, int y) {
    if (E.numrows != 0 || y != E.screenrows / 3) {
        abAppend(ab, "~", 1);
        return;
    }

    int welcomelen = strlen(WELCOME_MESSAGE);
    if (welcomelen > E.screencols)
        welcomelen = E.screencols;

    int padding = (E.screencols - welcomelen) / 2;
    if (padding) {
        abAppend(ab, "~", 1);
        padding--;
    }
    while (padding--)
        abAppend(ab, " ", 1);

    abAppend(ab, WELCOME_MESSAGE, welcomelen);
}

void editorDrawLineCount(struct abuf* ab) {
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	abAppend(ab, buf, strlen(buf));
}

void editorDrawRows(struct abuf* ab) {
	int y;
	for (y = 0; y < E.screenrows - 2; y++) {
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {
			welcomeMessage(ab, y);
			abAppend(ab, "\x1b[K", 3);
			abAppend(ab, "\r\n", 2);
			continue;
		}
		int len = E.row[filerow].rsize - E.coloff;
		if (len < 0)
			len = 0;
		if (len > E.screencols)
			len = E.screencols;
		char* c = &E.row[filerow].render[E.coloff];
		unsigned char* hl = &E.row[filerow].hl[E.coloff];
		int current_color = -1;
		int j;
		for (j = 0; j < len; j++) {
			if (c != nullptr && iscntrl(c[j])) {
				char sym = (c[j] <= 26) ? '@' + c[j] : '?';
				abAppend(ab, "\x1b[7m", 4);
				abAppend(ab, &sym, 1);
				abAppend(ab, "\x1b[m", 3);
				if (current_color != -1) {
					char buf[16];
					int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
					abAppend(ab, buf, clen);
				}
			} else if (hl[j] == HL_NORMAL) {
				if (current_color != -1) {
					abAppend(ab, "\x1b[39m", 5);
					current_color = -1;
				}
				abAppend(ab, &c[j], 1);
			} else {
				int color = editorSyntaxToColor(hl[j]);
				if (color != current_color) {
					current_color = color;
					char buf[16];
					int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
					abAppend(ab, buf, clen);
				}
				abAppend(ab, &c[j], 1);
			}
		}
		abAppend(ab, "\x1b[39m", 5);
		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct abuf* ab) {
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows,
					   E.dirty ? "(modified)" : "");
	int rlen =
		snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
	if (len > E.screencols)
		len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		}  
		abAppend(ab, " ", 1);
		len++;
	
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf* ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols)
		msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < STATUS_MESSAGE_TIME)
		abAppend(ab, E.statusmsg, msglen);
}


void editorSetStatusMessage(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/*** input ***/

char* editorPrompt(const char* prompt, void (*callback)(char*, int)) {
	size_t bufsize = 128;
	char* buf = static_cast<char*>(malloc(bufsize));

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = readKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0)
				buf[--buflen] = '\0';
		} else if (c == '\x1b' || c == CTRL_KEY('q')) {
			editorSetStatusMessage("");
			if (callback)
				callback(buf, c);
			free(buf);
			return NULL;
		} else if (c == '\r' || c == '\n') {
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback)
					callback(buf, c);
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				buf = static_cast<char*>(realloc(buf, bufsize));
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}

		if (callback)
			callback(buf, c);
	}
}

void editorMoveCursor(int key) {
	erow* row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	switch (key) {
	case ARROW_LEFT:
		if (E.cx != 0) {
			E.cx--;
		} else if (E.cy > 0) {
			E.cy--;
			E.cx = E.row[E.cy].size;
		}
		break;
	case ARROW_RIGHT:
		if (row && E.cx < row->size) {
			E.cx++;
		} else if (row && E.cx == row->size) {
			E.cy++;
			E.cx = 0;
		}
		break;
	case ARROW_UP:
		if (E.cy != 0) {
			E.cy--;
		}
		break;
	case ARROW_DOWN:
		if (E.cy < E.numrows) {
			E.cy++;
		}
		break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

bool editorProcessKeypress(int c) {
	static int quit_times = KILO_QUIT_TIMES;

	switch (c) {
	case '\n':
	case '\r':
		editorInsertNewline();
		break;
	
	case CTRL_KEY('q'):
		if (E.dirty && quit_times > 0) {
			editorSetStatusMessage("WARNING!!! File has unsaved changes. "
								   "Press Ctrl-Q %d more times to quit.",
								   quit_times);
			quit_times--;
			return false;
		}
		//write(STDOUT_FILENO, "\x1b[2J", 4);
		//write(STDOUT_FILENO, "\x1b[H", 3);
		return true;
		break;

	case CTRL_KEY('s'):
		editorSave();
		break;

	case HOME_KEY:
		E.cx = 0;
		break;

	case END_KEY:
		if (E.cy < E.numrows)
			E.cx = E.row[E.cy].size;
		break;

	case CTRL_KEY('f'):
		editorFind();
		break;

	case BACKSPACE:
	case CTRL_KEY('h'):
	case DEL_KEY:
		if (c == DEL_KEY)
			editorMoveCursor(ARROW_RIGHT);
		editorDelChar();
		break;

	case PAGE_UP:
	case PAGE_DOWN: {
		if (c == PAGE_UP) {
			E.cy = E.rowoff;
		} else if (c == PAGE_DOWN) {
			E.cy = E.rowoff + E.screenrows - 1;
			if (E.cy > E.numrows)
				E.cy = E.numrows;
		}

		int times = E.screenrows;
		while (times--)
			editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
	} break;

	case ARROW_UP:
	case ARROW_DOWN:
	case ARROW_LEFT:
	case ARROW_RIGHT:
		editorMoveCursor(c);
		break;

	case CTRL_KEY('l'):
	case '\x1b':
		break;

	default:
		editorInsertChar(c);
		break;
	}

	quit_times = KILO_QUIT_TIMES;
	return false;
}

/*** init ***/

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.syntax = NULL;

	updateWindowSize();
	// E.screenrows -= 2;
}


void editorStart(const char* filenameIn) {
	initEditor();
	if (enableRawMode() != 0) {
		return;
	}

	if (filenameIn != nullptr) {
		// to convert from const to non const, a.k.a making a copy
		if (!editorOpen(filenameIn)) {
			editorSetStatusMessage("%s", "File cannot exist, Press any key to exit");
			editorRefreshScreen();
			readKey();
			disableRawMode();
			return;
		}
	}

	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
	editorRefreshScreen();
	while (true) {
		int key = readKey();
		if (editorProcessKeypress(key)) {
			break;
		}
		editorRefreshScreen();
	}


	for (int i = 0; i < E.numrows; i++) {
		editorFreeRow(&E.row[i]);
	}
	if (E.filename)
		free(E.filename);


	disableRawMode();
}