#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// ctrl q to quit
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

// **** data ****
struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

// **** terminal settings ****
void die(const char *s) {
  // clear screen on exit
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios)) {
    die("tcsetattr");
  }
}

void enableRawMode() {

  // get current og terminal settings (STDIN_FILENO
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;

  // input flags
  // IXON software flow control, gets rid of stopping data from being
  // transmitted from ctrl s and resuming from ctrl q
  // ICRNL fixes ctrl m (carriage return & new line)
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

  // 8 bytes per character
  raw.c_cflag |= (CS8);

  // output flags
  // stop translation of \n to \r\n
  raw.c_oflag &= ~(OPOST);

  // local flags
  // not echo => don't display what user types to terminal
  // turn off canonical mode => read input byte by byte instead of line by line
  // turn off ISIG => ctrl c / ctrl z
  // IEXEN gets rid of weird ctrl v cases where it sends value of next character
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  // timing out read()
  raw.c_cc[VMIN] = 0;  // min # of bytes before read can return
  raw.c_cc[VTIME] = 1; // max time to wait before read returns

  // apply attributes (echo off) back into STDIN_FILENO
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
}

int getCursorPosition(int *rows, int *cols) {
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

int getWindowsSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

// **** append buffer ****
struct abuf {
  char *b; // dynamic, may change depending on what we are appending
  int len;
};

#define ABUF_INIT {NULL, 0};

void abAppend(struct abuf *ab, const char *s,
              int len) {                     // dynamic resize of buffer
  char *new = realloc(ab->b, ab->len + len); // realloc new block if too big

  if (new == NULL) // allocation failure
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

// **** input ****
//
void editorMoveCursor(char key) {
  switch (key) {
  case 'w':
    E.cy--;
    break;
  case 's':
    E.cy++;
    break;
  case 'a':
    E.cx--;
    break;
  case 'd':
    E.cx++;
    break;
  }
}

void editorProcessKeypress() {
  char c = editorReadKey();
  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  case 'w':
  case 'a':
  case 's':
  case 'd':
    editorMoveCursor(c);
    break;
  }
}

// **** output ****
//
// draws tilde rows based off of screen size
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
                                "Kilo editor -- version %s", KILO_VERSION);
      if (welcomelen > E.screencols)
        welcomelen = E.screencols;
      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while (padding--)
        abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }
    abAppend(ab, "\x1b[K", 3);

    if (y < E.screencols - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  // escape sequences for drawing editor
  // clear entire screen
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6); // hide cursor before drawing

  // position cursor in the top left
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); // re enable cursor
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

// **** init ****
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  if (getWindowsSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowsSize");
}

int main() {
  enableRawMode();
  initEditor();

  // read each keystroke into char c
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  };
  return 0;
}
