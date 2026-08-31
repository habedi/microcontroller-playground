/****************************************************************************
 * experiments/pico-face/src/face_main.c
 *
 * NuttX side of the face: opens the framebuffer, runs the render loop, and
 * reads the current state out of a small file so anything on the board or on
 * the network can drive the expression with one echo.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <nuttx/video/fb.h>

#include "face_render.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_PICO_FACE_STATE_FILE
#  define CONFIG_PICO_FACE_STATE_FILE "/tmp/face"
#endif

/* Target frame interval.  The panel can go faster than this, but there is
 * nothing to be gained: a blink is 150 ms long.
 */

#define FRAME_MS 33

/* How often the state file is re-read.  Reading a few bytes out of tmpfs is
 * cheap, so this is generous rather than clever.
 */

#define POLL_MS 100

#define BENCH_FRAMES 120

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Reads the state word out of the file.  A missing file is not an error: it
 * just means nothing has set a state yet.
 */

static bool read_state(enum face_state *state, bool *quit)
{
  char buf[16];
  char *end;
  int fd;
  ssize_t n;

  fd = open(CONFIG_PICO_FACE_STATE_FILE, O_RDONLY);
  if (fd < 0)
    {
      return false;
    }

  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (n <= 0)
    {
      return false;
    }

  buf[n] = '\0';

  /* Trim trailing whitespace, since the word almost always arrives from an
   * echo and carries a newline.
   */

  end = buf + strlen(buf);
  while (end > buf && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
    {
      *--end = '\0';
    }

  if (strcmp(buf, "quit") == 0)
    {
      *quit = true;
      return false;
    }

  return face_state_from_name(buf, state) == 0;
}

static int write_state(const char *word)
{
  enum face_state state;
  int fd;
  int len;

  /* Validate before writing, so a typo is reported here rather than being
   * silently ignored by the render loop.
   */

  if (strcmp(word, "quit") != 0 && face_state_from_name(word, &state) != 0)
    {
      fprintf(stderr, "face: unknown state \"%s\"\n", word);
      fprintf(stderr, "face: try idle, working, editing, waiting, failed, "
                      "done, or quit\n");
      return 1;
    }

  fd = open(CONFIG_PICO_FACE_STATE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    {
      fprintf(stderr, "face: cannot write %s: %d\n",
              CONFIG_PICO_FACE_STATE_FILE, errno);
      return 1;
    }

  len = (int)strlen(word);
  if (write(fd, word, (size_t)len) != len)
    {
      close(fd);
      return 1;
    }

  close(fd);
  return 0;
}

static int open_surface(int *fd, struct face_surface *surface)
{
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;

  *fd = open("/dev/fb0", O_RDWR);
  if (*fd < 0)
    {
      fprintf(stderr, "face: cannot open /dev/fb0: %d\n", errno);
      return 1;
    }

  if (ioctl(*fd, FBIOGET_VIDEOINFO, (unsigned long)&vinfo) < 0 ||
      ioctl(*fd, FBIOGET_PLANEINFO, (unsigned long)&pinfo) < 0)
    {
      fprintf(stderr, "face: cannot query the framebuffer: %d\n", errno);
      close(*fd);
      return 1;
    }

  if (pinfo.bpp != 16)
    {
      fprintf(stderr, "face: needs a 16 bit panel, this one is %d\n",
              pinfo.bpp);
      close(*fd);
      return 1;
    }

  surface->pixels    = (uint16_t *)pinfo.fbmem;
  surface->width     = vinfo.xres;
  surface->height    = vinfo.yres;
  surface->stride_px = (int)(pinfo.stride / sizeof(uint16_t));

  return 0;
}

/* Pushes the changed area to the panel.  Without CONFIG_FB_UPDATE the driver
 * writes through on its own and this is not needed.
 */

static void push(int fd, const struct face_dirty *dirty)
{
#ifdef CONFIG_FB_UPDATE
  struct fb_area_s area;

  area.x = (fb_coord_t)dirty->x;
  area.y = (fb_coord_t)dirty->y;
  area.w = (fb_coord_t)dirty->w;
  area.h = (fb_coord_t)dirty->h;

  ioctl(fd, FBIO_UPDATE, (unsigned long)&area);
#else
  (void)fd;
  (void)dirty;
#endif
}

/* Draws as fast as the panel allows and reports the rate.  This is the number
 * that decides whether any animated idea on top of this is possible.
 */

static int benchmark(void)
{
  struct face_surface surface;
  struct face_dirty dirty;
  struct face f;
  uint32_t start;
  uint32_t elapsed;
  int fd;
  int i;

  if (open_surface(&fd, &surface) != 0)
    {
      return 1;
    }

  face_init(&f, now_ms());
  start = now_ms();

  for (i = 0; i < BENCH_FRAMES; i++)
    {
      face_tick(&f, now_ms());
      face_render(&surface, &f.pose, &dirty);
      push(fd, &dirty);
    }

  elapsed = now_ms() - start;
  close(fd);

  if (elapsed == 0)
    {
      elapsed = 1;
    }

  printf("face: %d frames of %dx%d in %lu ms, %lu fps\n",
         BENCH_FRAMES, surface.width, surface.height,
         (unsigned long)elapsed,
         (unsigned long)((BENCH_FRAMES * 1000u) / elapsed));

  return 0;
}

static int run(void)
{
  struct face_surface surface;
  struct face_dirty dirty;
  struct face f;
  enum face_state wanted;
  uint32_t last_poll;
  bool quit = false;
  int fd;

  if (open_surface(&fd, &surface) != 0)
    {
      return 1;
    }

  printf("face: %dx%d, state from %s\n", surface.width, surface.height,
         CONFIG_PICO_FACE_STATE_FILE);

  face_init(&f, now_ms());
  last_poll = now_ms();

  while (!quit)
    {
      uint32_t t = now_ms();

      if (t - last_poll >= POLL_MS)
        {
          last_poll = t;

          if (read_state(&wanted, &quit) && wanted != f.state)
            {
              face_set_state(&f, wanted, t);
            }
        }

      face_tick(&f, t);
      face_render(&surface, &f.pose, &dirty);
      push(fd, &dirty);

      usleep(FRAME_MS * 1000);
    }

  close(fd);
  printf("face: stopped\n");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  if (argc > 1)
    {
      if (strcmp(argv[1], "-b") == 0)
        {
          return benchmark();
        }

      return write_state(argv[1]);
    }

  return run();
}
