struct abuf {
	char *b;
	int len;
};

void abAppend(struct abuf *ab, const char *s, int len) {
	char *update = realloc(ab->b, ab->len+len);
	
	if(update == NULL) return;
	memcpy(update+ab->len, s, len);
	ab->b = update;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

int getCursorPosition(int *srows, int *scols) {
	char buf[32];
  unsigned int i = 0;
  
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  
	while (i < sizeof(buf)-1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }

	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", srows, scols) != 2) return -1;

	return 0;
}

int getWindowSize(int *srows, int *scols) {
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		getCursorPosition(srows, scols);
	}

	*scols = ws.ws_col;
	*srows = ws.ws_row;
	return 0;
}

int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	int j;
	for(j = 0; j < cx; j++) {
		if(row->chars[j] == '\t') 
			rx += (MEDIT_TABSTOP - 1) - (rx % MEDIT_TABSTOP); 
		rx++;
	}
	return rx;
}

int editorRowRxToCx(erow *row, int rx) {
	int cur_rx = 0;
	int cx;
	for (cx = 0; cx < row->size; cx++) {
		if(row->chars[cx] == '\t') 
			cur_rx += (MEDIT_TABSTOP - 1) - (cur_rx % MEDIT_TABSTOP);
		cur_rx++;

		if (cur_rx > rx) return cx;
	}
	return cx;
}

void editorUpdateRow(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++)
		if(row->chars[j] == '\t')  tabs++;

	free(row->render);
	row->render = malloc(row->size + tabs*(MEDIT_TABSTOP-1) + 1);

	int idx = 0;
	for(j = 0; j < row->size; j++) {
		if(row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while(idx % MEDIT_TABSTOP != 0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
	if(at < 0 || at > E.numrows) return;

	E.row = realloc(E.row, sizeof(erow) * (E.numrows+1));
	memmove(&E.row[at+1], &E.row[at], sizeof(erow) * (E.numrows - at));
	
	E.row[at].size = len;
	E.row[at].chars = malloc(len+1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	//E.row[at].hl = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
	//free(row->hl);
}

void editorDelRow(int at) {
	if(at < 0 || at > E.numrows) return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at+1], sizeof(erow) * (E.numrows - at));
	E.numrows--;
	E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
	if(at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size+2);
	memmove(&row->chars[at+1], &row->chars[at], row->size-at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
	if(at < 0 || at > row->size) return;
	memmove(&row->chars[at], &row->chars[at+1], row->size-at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}

void editorInsertChar(int c) {
	if(E.cy == E.numrows) {
		editorInsertRow(E.numrows, "", 0);
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}

void editorInsertNewline() {
	if(E.cx == 0) {
		editorInsertRow(E.cy, "", 0);
	} else {
		erow *row = &E.row[E.cy];
		editorInsertRow(E.cy+1, &row->chars[E.cx], row->size-E.cx);
		row = &E.row[E.cy];
		row->size = E.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.cy++;
	E.cx = 0;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size+len+1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorDelChar() {
	if(E.cy == E.numrows) return;
	if(E.cx == 0 && E.cy == 0) return;

	erow *row = &E.row[E.cy];
	if(E.cx > 0) {
		editorRowDelChar(row, E.cx-1);
		E.cx--;
	} else {
		E.cx = E.row[E.cy-1].size;
		editorRowAppendString(&E.row[E.cy-1], row->chars, row->size);
		editorDelRow(E.cy);
		E.cy--;
	}
}

char *editorRowsToString(int *buflen) {
	int total = 0;
	int j;
	for(j = 0; j < E.numrows; j++)
		total += E.row[j].size + 1;
	*buflen = total;

	char *buf = malloc(total);
	char *p = buf;
	for(j = 0; j < E.numrows; j++) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpen(char *filename) {
	free(E.filename);
	E.filename = strdup(filename);

	FILE *fp = fopen(filename, "r");
	if(!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while((linelen = getline(&line, &linecap, fp)) != -1) {
		while(linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r')) 
			linelen--;
		editorInsertRow(E.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave() { 
	if(E.filename == NULL) {
		E.filename = editorPrompt("Save as (ESC to cancel): %s", NULL);
		if(E.filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
	}

	int len; 
	char *buf = editorRowsToString(&len);

	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
				E.dirty = 0;
				editorSetStatusMessage("%d bytes written", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

void editorFindCallback(char *query, int key) {
	static int last_match = -1;
	static int direction = 1;

	if(key == '\r' || key == '\x1b') {
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

	if(last_match == -1) direction = 1;
	int current = last_match;
	int i;
	for(i = 0; i < E.numrows; i++) {
		current += direction;
		if (current == -1) current = E.numrows - 1;
		else if (current == E.numrows) current = 0;

		erow *row = &E.row[current];
		char *match = strstr(row->render, query);
		if(match) {
			last_match = current;
			E.cy = current;
			E.cx = editorRowRxToCx(row, match - row->render);
			E.rowoff = E.numrows;
			break;
		}
	}
}

void editorFind() {
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char *query = editorPrompt("Search (Use ESC/Arrows/Enter): %s", editorFindCallback);

	if(query) {
		free(query);
	} else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
}

int editorReadKey() {
	int nread;
	char c; 
	while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if(nread == -1 && errno != EAGAIN) die("read");
	}

	if(c == '\x1b') {
		char seq[3];

		if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if(seq[0] == '[') {
			if(seq[1] >= '0' && seq[1] <= '9') {
				if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
						case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch(seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		return '\x1b';
	}

	return c;
}

void editorScroll() {
	E.rx = E.cx;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}

	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	} 
	if(E.cy >= E.rowoff + E.srows) {
		E.rowoff = E.cy - E.srows + 1;
	}

	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	} 
	if(E.rx >= E.coloff + E.scols) {
		E.coloff = E.rx - E.scols + 1;
	}
}

void editorWelcomeMessage(struct abuf *ab, const char *fmt, const char *msg) {
	int wmax = 200;

	char welcome[wmax];
	int wlen = snprintf(
		welcome, 
		sizeof(welcome), 
		fmt,
		msg
	);

	if(wlen > E.scols) wlen = E.scols;
	int padding = (E.scols - wlen) / 2;
	if (padding) abAppend(ab, "~", 1);
	while(padding--) abAppend(ab, " ", 1);
	abAppend(ab, welcome, wlen);
}

void editorDrawRows(struct abuf *ab) { 
	const char *fstring[] = {
		"medit - (more)edit -- version %s",
		"Ctrl-S = save | Ctrl-Q/C = quit | Ctrl-F = find",
	};

	const char *fmsgs[] = {
		MEDIT_VERSION,
		NULL,
	};

	int y;
	int m = 0;
	int mrows = 2;
	for(y = 0; y < E.srows; y++) {
		int frow = y + E.rowoff;
		if(frow >= E.numrows) {
			if(E.numrows == 0 && (y == E.srows/2 || y == E.srows/2 + (mrows-1))) {	
				editorWelcomeMessage(ab, fstring[m], fmsgs[m]);
				m++;
			} else {
				abAppend(ab, "~", 1);
			} 
		} else {
			int len = E.row[frow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.scols) len = E.scols;
			abAppend(ab, &E.row[frow].render[E.coloff], len);
		}

		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct abuf *ab) {
	char status[80], rstatus[80];
	int len = snprintf(
		status, 
		sizeof(status),
		"%.20s %.60s", E.filename ? E.filename : "[No Name]", E.statusmsg 
	);
	int rlen = snprintf(
		rstatus, 
		sizeof(rstatus),
		"%d/%d", E.cy+1, E.numrows
	);
	
	if (len > E.scols) len = E.scols;
	abAppend(ab, status, len);
	while(len < E.scols) {
		if(E.scols-len == rlen) {
			abAppend(ab, rstatus, rlen);	
			break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		} 
	}
}

void editorRefreshScreen() {
	editorScroll();

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy-E.rowoff) + 1, (E.rx-E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));
	
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
	size_t bufsize = 128;
	char *buf = malloc(bufsize);

	size_t buflen = 0;
	buf[0] = '\0';

	while(1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if(c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if(buflen != 0) buf[--buflen] = '\0';
		} else if(c == '\x1b') {	
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
		} else if(c == '\r') {
			if(buflen != 0) {
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if(buflen == bufsize - 1) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}

		if (callback) callback(buf, c);
	}
}

void editorMoveCursor(int key) {
	erow *row = (E.cy > E.numrows-1) ? NULL : &E.row[E.cy];

	switch(key) {
		case ARROW_LEFT:
			if(E.cx != 0) {
				E.cx--;
			} else if(E.cy > 0) {
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size) {
				E.cx++;
			} else if(row && E.cx == row->size) {
				E.cy++;	
				if(E.cy == E.numrows) E.cy--; 
				else E.cx = 0;
			}
			break;
		case ARROW_UP:
			if(E.cy != 0) E.cy--;
			break;
		case ARROW_DOWN:
			if(E.cy < E.numrows-1) E.cy++;
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rlen = row ? row->size : 0;
	if (E.cx > rlen) E.cx = rlen;
}

void editorProcessKeypress() {
	int c = editorReadKey();

	switch (c) {
		case '\r':
			editorInsertNewline();
			break;

		case CTRL_KEY('c'):
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

		case CTRL_KEY('s'):
			editorSave();
			break;

		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			if(E.cy < E.numrows) E.cx = E.row[E.cy].size;
			break;

		case CTRL_KEY('f'):
			editorFind();
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				if(c == PAGE_UP) {
					E.cy = E.rowoff;
				} else if (c == PAGE_DOWN) {
					E.cy = E.rowoff + E.srows - 1;
					if(E.cy > E.numrows) E.cy = E.numrows - 1;
				}

				int times = E.srows;
				while(times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

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
}

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

	if(getWindowSize(&E.srows, &E.scols) == -1) die("getWindowSize");
	E.srows -= 1;
}

