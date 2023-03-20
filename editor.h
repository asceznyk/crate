#include <string.h>
#include <sys/ioctl.h>

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
	char *update = realloc(ab->b, ab->len + len);

	if(update == NULL) return;
	memcpy(update+ab->len, s, len);
	ab->b = update;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

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

void editorDrawRows(struct abuf *ab) {
	int y;
	for(y = 0; y < E.srows; y++) {
		if(y == E.srows/2) {
			char welcome[80];
			int wlen = snprintf(welcome, sizeof(welcome), 
					"medit - (more)edit -- version %s", MEDIT_VERSION);
			if(wlen > E.scols) wlen = E.scols;
			int padding = (E.scols - wlen) / 2;
			if (padding) abAppend(ab, "~", 1);
			while(padding--) abAppend(ab, " ", 1);
			abAppend(ab, welcome, wlen);
		} else {
			abAppend(ab, "~", 1);
		}

		abAppend(ab, "\x1b[K", 3);
		if(y < E.srows-1) abAppend(ab, "\r\n", 2);
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, E.cx+1);
	abAppend(&ab, buf, strlen(buf));
	
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorMoveCursor(int key) {
	switch(key) {
		case ARROW_LEFT:
			if(E.cx != 0) E.cx--;
			break;
		case ARROW_RIGHT:
			if(E.cx != E.scols-1) E.cx++;
			break;
		case ARROW_UP:
			if(E.cy != 0) E.cy--;
			break;
		case ARROW_DOWN:
			if(E.cy != E.srows-1) E.cy++;
			break;
	}
}

void editorProcessKeypress() {
	int c = editorReadKey();

	switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			E.cx = E.scols-1;
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
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
  }
}

void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.numrows = 0;

	if(getWindowSize(&E.srows, &E.scols) == -1) die("getWindowSize");
}

