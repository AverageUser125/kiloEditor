#pragma once

#define CTRL_KEY(k) ((k) & 0x1f)
#define INVERSE_COLOR "\033[7m"

#include <vector>
#include <string>
#include "config.hpp"
#include <string>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>

struct editorSyntax {
	char* filetype;
	char** filematch;
	char** keywords;
	char* singleline_comment_start;
	char* multiline_comment_start;
	char* multiline_comment_end;
	int flags;
};

using erow = struct erow {
	int idx;
	int size;
	int rsize;
	char* chars;
	char* render;
	unsigned char* hl;
	int hl_open_comment;
};

struct editorConfig {
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow* row;
	int dirty;
	char* filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct editorSyntax* syntax;
};

struct abuf {
	char* b;
	int len;
};

enum editorKey {
	ESC = 27,
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
};
extern editorConfig E;

void editorStart(const char* filenameIn);
void editorSetStatusMessage(const char* fmt, ...);
void editorScroll();
void editorDrawRows(struct abuf* ab);
void editorDrawStatusBar(struct abuf* ab);
void editorDrawMessageBar(struct abuf* ab);
void abAppend(struct abuf* ab, const char* s, int len);
void abFree(struct abuf* ab);
void editorDrawLineCount(struct abuf* ab);
