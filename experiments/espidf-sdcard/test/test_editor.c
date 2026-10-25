/* Include the editor to exercise its internal logic without a serial port. */
#define _POSIX_C_SOURCE 200809L
#include "../main/editor.c"

#include <assert.h>

static void reset_editor(void)
{
  for (int i = 0; i < E.numrows; i++) editorFreeRow(&E.row[i]);
  free(E.row);
  memset(&E, 0, sizeof(E));
}

static void test_long_filename(void)
{
  reset_editor();
  memset(E.filename, 'a', sizeof(E.filename) - 1);
  E.screencols = 80;
  struct abuf ab = ABUF_INIT;
  editorDrawStatusBar(&ab);
  assert(ab.len == 4 + E.screencols + 5);
  assert(ab.b[4] == ' ');
  for (int i = 5; i < 84; i++) assert(ab.b[i] == 'a');
  abFree(&ab);
}

static void test_long_line(const char *filename, int cr_position, bool crlf)
{
  reset_editor();
  char line[1024];
  memset(line, 'a', sizeof(line));
  if (cr_position >= 0) line[cr_position] = '\r';
  FILE *fp = fopen(filename, "w");
  assert(fp);
  assert(fwrite(line, 1, sizeof(line), fp) == sizeof(line));
  assert(fputs(crlf ? "\r\n" : "\n", fp) >= 0);
  assert(fputs("next\n", fp) >= 0);
  assert(fclose(fp) == 0);

  assert(editorOpenRows(filename) == 0);
  assert(E.numrows == 2);
  assert(E.row[0].size == sizeof(line));
  assert(memcmp(E.row[0].chars, line, sizeof(line)) == 0);
  assert(strcmp(E.row[1].chars, "next") == 0);
  assert(E.dirty == 0);
}

static void test_split_crlf(const char *filename)
{
  reset_editor();
  FILE *fp = fopen(filename, "w");
  assert(fp);
  for (int i = 0; i < 510; i++) assert(fputc('a', fp) == 'a');
  assert(fputs("\r\nend\r", fp) >= 0);
  assert(fclose(fp) == 0);

  assert(editorOpenRows(filename) == 0);
  assert(E.numrows == 2);
  assert(E.row[0].size == 510);
  assert(E.row[0].chars[509] == 'a');
  assert(strcmp(E.row[1].chars, "end\r") == 0);
}

int main(void)
{
  char filename[] = "/tmp/editor-test-XXXXXX";
  int fd = mkstemp(filename);
  assert(fd >= 0);
  close(fd);
  test_long_filename();
  test_long_line(filename, -1, false);
  test_long_line(filename, 510, false);
  test_long_line(filename, 1021, true);
  test_split_crlf(filename);
  reset_editor();
  assert(unlink(filename) == 0);
  puts("Editor tests passed");
  return 0;
}
