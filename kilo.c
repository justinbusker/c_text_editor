#include <asm-generic/errno-base.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// **** data ****
struct termios orig_termios;

// **** terminal settings ****
void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios)) {
    die("tcsetattr");
  }
}

void enableRawMode() {

  // get current og terminal settings (STDIN_FILENO
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_termios;

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

// **** init ****
int main() {
  enableRawMode();

  char c;

  // read each keystroke into char c
  while (1) {
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
      die("read");
    if (iscntrl(c)) { // control characters are not printable (tab, esc, etc)
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
    if (c == 'q') {
      break;
    }
  };
  return 0;
}
