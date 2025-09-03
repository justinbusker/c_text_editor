#include <asm-generic/errno-base.h>
#include <asm-generic/ioctls.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// ctrl q to quit
#define CTRL_KEY(k) ((k) & 0x1f)

// **** data ****
struct editorConfig {
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

// **** input ****
void editorProcessKeypress() {
  char c = editorReadKey();
  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;
  }
}

// **** output ****
void editorDrawRows() {
  int y;
  for (y = 0; y < E.screencols; y++) {
    write(STDIN_FILENO, "~", 1);

    if (y < E.screencols - 1) {
      write(STDIN_FILENO, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  // escape sequences for drawing editor
  // clear entire screen
  write(STDOUT_FILENO, "\x1b[2J", 4);

  // position cursor in the top left
  write(STDOUT_FILENO, "\x1b[H", 3);

  editorDrawRows();

  write(STDOUT_FILENO, "\x1b[H", 3);
}

// **** init ****
void initEditor() {
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
