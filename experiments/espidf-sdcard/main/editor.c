/*
 * Compact in-terminal text editor for FreeRTOS / ESP-IDF console.
 * Implements VT100/ANSI screen editing inspired by Kilo.
 */

#include "editor.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char filename[512];
  char statusmsg[80];
};

static struct editorConfig E;

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

static void abAppend(struct abuf *ab, const char *s, int len)
{
  char *new_buf = realloc(ab->b, ab->len + len);
  if (new_buf == NULL) return;
  memcpy(&new_buf[ab->len], s, len);
  ab->b = new_buf;
  ab->len += len;
}

static void abFree(struct abuf *ab)
{
  free(ab->b);
}

static void editorSetStatusMessage(const char *msg)
{
  snprintf(E.statusmsg, sizeof(E.statusmsg), "%s", msg);
}

/* Bytes of an escape sequence arrive together; a lone Esc key has nothing
 * after it, so wait briefly instead of blocking and eating the next key.
 */
static int editorReadSeqByte(void)
{
  int fd = fileno(stdin);
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  struct timeval tv = { .tv_sec = 0, .tv_usec = 50 * 1000 };
  if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0) return EOF;
  return getchar();
}

static int editorReadKey(void)
{
  int c = getchar();
  if (c == EOF) return 0;

  if (c == '\x1b') {
    int seq0 = editorReadSeqByte();
    if (seq0 == EOF) return '\x1b';
    if (seq0 == '[') {
      int seq1 = editorReadSeqByte();
      if (seq1 == EOF) return '\x1b';
      if (seq1 >= '0' && seq1 <= '9') {
        int seq2 = editorReadSeqByte();
        if (seq2 == '~') {
          switch (seq1) {
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
        switch (seq1) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq0 == 'O') {
      int seq1 = editorReadSeqByte();
      switch (seq1) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  }
  return c;
}

static void editorInsertRow(int at, const char *s, size_t len)
{
  if (at < 0 || at > E.numrows) return;
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
  E.dirty++;
}

static void editorFreeRow(erow *row)
{
  free(row->chars);
}

static void editorDelRow(int at)
{
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

static void editorRowInsertChar(erow *row, int at, int c)
{
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  E.dirty++;
}

static void editorRowAppendString(erow *row, char *s, size_t len)
{
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  E.dirty++;
}

static void editorRowDelChar(erow *row, int at)
{
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  E.dirty++;
}

static void editorInsertChar(int c)
{
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

static void editorInsertNewline(void)
{
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
  }
  E.cy++;
  E.cx = 0;
}

static void editorDelChar(void)
{
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
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

static int editorOpenRows(const char *filename)
{
  FILE *fp = fopen(filename, "r");
  if (!fp) return -1;

  /* fgets returns long lines in chunks; join them back into one row. */
  char line[512];
  bool continuing = false;
  while (fgets(line, sizeof(line), fp)) {
    int linelen = strlen(line);
    bool complete = linelen > 0 && line[linelen - 1] == '\n';
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    if (continuing) {
      editorRowAppendString(&E.row[E.numrows - 1], line, linelen);
    } else {
      editorInsertRow(E.numrows, line, linelen);
    }
    continuing = !complete;
  }
  fclose(fp);
  E.dirty = 0;
  return 0;
}

static int editorSave(void)
{
  if (E.filename[0] == '\0') return -1;

  FILE *fp = fopen(E.filename, "w");
  if (!fp) {
    editorSetStatusMessage("Error: Could not open file for writing!");
    return -1;
  }

  int total_bytes = 0;
  bool failed = false;
  for (int i = 0; i < E.numrows && !failed; i++) {
    if (fprintf(fp, "%s\n", E.row[i].chars) < 0) {
      failed = true;
    }
    total_bytes += E.row[i].size + 1;
  }
  if (fclose(fp) != 0) failed = true;
  if (failed) {
    editorSetStatusMessage("Error: Write failed, file is incomplete!");
    return -1;
  }
  E.dirty = 0;
  char msg[sizeof(E.filename) + 64];
  snprintf(msg, sizeof(msg), "Wrote %d bytes to %s", total_bytes, E.filename);
  editorSetStatusMessage(msg);
  return 0;
}

static void editorScroll(void)
{
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.cx < E.coloff) {
    E.coloff = E.cx;
  }
  if (E.cx >= E.coloff + E.screencols) {
    E.coloff = E.cx - E.screencols + 1;
  }
}

static void editorDrawRows(struct abuf *ab)
{
  for (int y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "ESP32-P4 Nano Editor -- Version 1.0");
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~\x1b[K", 4);
      }
    } else {
      int len = E.row[filerow].size - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      if (len > 0) {
        abAppend(ab, &E.row[filerow].chars[E.coloff], len);
      }
      abAppend(ab, "\x1b[K", 3);
    }
    abAppend(ab, "\r\n", 2);
  }
}

static void editorDrawStatusBar(struct abuf *ab)
{
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), " %s %s",
                     E.filename[0] ? E.filename : "[No Name]",
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d ", E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m\r\n", 5);
}

static void editorDrawMessageBar(struct abuf *ab)
{
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen) abAppend(ab, E.statusmsg, msglen);
}

static void editorRefreshScreen(void)
{
  editorScroll();

  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.cx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

static void editorMoveCursor(int key)
{
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

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
      if (E.cy != 0) E.cy--;
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) E.cy++;
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) E.cx = rowlen;
}

static int editorProcessKeypress(void)
{
  int c = editorReadKey();

  switch (c) {
    case '\r':
    case '\n':
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):
    case CTRL_KEY('x'):
      if (E.dirty) {
        editorSetStatusMessage("File has unsaved changes! Press Ctrl+Q again to force quit.");
        editorRefreshScreen();
        int next_c = editorReadKey();
        if (next_c != CTRL_KEY('q') && next_c != CTRL_KEY('x')) {
          editorSetStatusMessage("");
          return 0;
        }
      }
      return 1; /* Exit */

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
      break;

    case '\x08':
    case 127:
      editorDelChar();
      break;

    case DEL_KEY:
      editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case '\t':
      editorInsertChar(' ');
      editorInsertChar(' ');
      break;

    case '\x1b':
      break;

    default:
      if (c >= 32 && c <= 126) {
        editorInsertChar(c);
      }
      break;
  }
  return 0;
}

int editor_open(const char *filepath)
{
  if (strlen(filepath) >= sizeof(E.filename)) {
    printf("nano: path too long: %s\n", filepath);
    return 1;
  }

  memset(&E, 0, sizeof(E));
  E.screenrows = 22;
  E.screencols = 80;
  strcpy(E.filename, filepath);

  if (editorOpenRows(filepath) != 0) {
    editorInsertRow(0, "", 0);
    editorSetStatusMessage("New File -- Ctrl+S to save, Ctrl+Q to exit");
    E.dirty = 0;
  } else {
    editorSetStatusMessage("Ctrl+S to save, Ctrl+Q to exit");
  }

  /* Clear screen and place cursor */
  write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);

  while (1) {
    editorRefreshScreen();
    if (editorProcessKeypress()) break;
  }

  /* Clear screen on exit */
  write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);

  for (int i = 0; i < E.numrows; i++) {
    editorFreeRow(&E.row[i]);
  }
  free(E.row);
  E.row = NULL;
  E.numrows = 0;

  return 0;
}
