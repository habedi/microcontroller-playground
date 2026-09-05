/****************************************************************************
 * experiments/pico-face/src/face_main.c
 *
 * NuttX side of the face: opens the framebuffer, runs the render loop, reads
 * the current state out of a small file so anything on the board or on the
 * network can drive the expression with one echo, and reads the panel's
 * joystick and buttons so the look can be changed at the board.
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

#include "face_dirty.h"
#include "face_input.h"
#include "face_overlay.h"
#include "face_preset.h"
#include "face_sprite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_PICO_FACE_STATE_FILE
#  define CONFIG_PICO_FACE_STATE_FILE "/tmp/face"
#endif

#ifndef CONFIG_PICO_FACE_BUTTONS_DEV
#  define CONFIG_PICO_FACE_BUTTONS_DEV "/dev/buttons"
#endif

/* Target frame interval.  The panel can go faster than this, but there is
 * nothing to be gained: a blink is 150 ms long.
 */

#define FRAME_MS 33

/* How often the state file and the buttons are read.  Reading a few bytes
 * out of tmpfs is cheap, so this is generous rather than clever, and at
 * 100 ms a button sample is already past any contact bounce.
 */

#define POLL_MS 100

#define BENCH_FRAMES 120

/* Animation speeds the X button steps through, as a percentage of real
 * time.  The middle one is the default.
 */

static const int g_speeds[] = { 50, 100, 200 };

#define NSPEEDS ((int)(sizeof(g_speeds) / sizeof(g_speeds[0])))

/* Software brightness steps the joystick moves between, as a percentage.
 * This dims the pixels rather than the backlight, because the backlight pin
 * is a plain GPIO in the board glue and only knows on and off.
 */

#define BRIGHT_MIN  25
#define BRIGHT_MAX  100
#define BRIGHT_STEP 25

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ui
{
  int preset;
  int palette;
  int speed;      /* index into g_speeds */
  int bright;     /* percent */
  bool hold;      /* ignore the state file */
  bool overlay;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Hashes of the last pushed frame.  Static rather than on the stack, since
 * the task has 4 KB of stack and this is nearly 4 KB on its own.
 */

static struct face_tracker g_tracker;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
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

  if (dirty->w <= 0 || dirty->h <= 0)
    {
      return;
    }

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

/* Scales every channel of every pixel.  Skipped entirely at full brightness,
 * which is the usual case, so the cost is only paid when it is asked for.
 */

static void dim_surface(const struct face_surface *s, int percent)
{
  int x;
  int y;

  if (percent >= 100)
    {
      return;
    }

  for (y = 0; y < s->height; y++)
    {
      uint16_t *row = s->pixels + (size_t)y * (size_t)s->stride_px;

      for (x = 0; x < s->width; x++)
        {
          uint16_t c = row[x];
          int r = ((c >> 11) & 0x1f) * percent / 100;
          int g = ((c >> 5) & 0x3f) * percent / 100;
          int b = (c & 0x1f) * percent / 100;

          row[x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

/* Applies one button action to the settings. */

static void apply(struct ui *ui, enum face_action action)
{
  switch (action)
    {
      case FACE_ACT_PRESET_NEXT:
        ui->preset = face_preset_wrap(ui->preset + 1);
        break;

      case FACE_ACT_PRESET_PREV:
        ui->preset = face_preset_wrap(ui->preset - 1);
        break;

      case FACE_ACT_BRIGHT_UP:
        ui->bright += BRIGHT_STEP;
        if (ui->bright > BRIGHT_MAX)
          {
            ui->bright = BRIGHT_MAX;
          }
        break;

      case FACE_ACT_BRIGHT_DOWN:
        ui->bright -= BRIGHT_STEP;
        if (ui->bright < BRIGHT_MIN)
          {
            ui->bright = BRIGHT_MIN;
          }
        break;

      case FACE_ACT_HOLD:
        ui->hold = !ui->hold;
        break;

      case FACE_ACT_PALETTE:
        ui->palette = (ui->palette + 1) % FACE_NPALETTES;
        break;

      case FACE_ACT_SPEED:
        ui->speed = (ui->speed + 1) % NSPEEDS;
        break;

      case FACE_ACT_OVERLAY:
        ui->overlay = !ui->overlay;
        break;

      default:
        break;
    }
}

/* Times the three parts of a frame apart from each other: drawing the whole
 * surface, scanning it for changes, and pushing all of it over the bus.
 * Per frame, in tenths of a millisecond, so the sum says what a full redraw
 * costs and the first two say what a frame costs once the bus is out of it.
 */

static uint32_t tenths(uint32_t elapsed_ms)
{
  return (elapsed_ms * 10u + BENCH_FRAMES / 2) / BENCH_FRAMES;
}

static int benchmark(void)
{
  struct face_surface surface;
  struct face_dirty dirty;
  struct face f;
  uint32_t start;
  uint32_t draw;
  uint32_t scan;
  uint32_t full;
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
      face_preset(0)->render(&surface, &f.pose, f.state, now_ms(), 0, &dirty);
    }

  draw = now_ms() - start;

  face_tracker_reset(&g_tracker);
  start = now_ms();
  for (i = 0; i < BENCH_FRAMES; i++)
    {
      face_tracker_scan(&g_tracker, &surface, &dirty);
    }

  scan = now_ms() - start;

  dirty.x = 0;
  dirty.y = 0;
  dirty.w = surface.width;
  dirty.h = surface.height;
  start = now_ms();
  for (i = 0; i < BENCH_FRAMES; i++)
    {
      push(fd, &dirty);
    }

  full = now_ms() - start;
  close(fd);

  printf("face: %dx%d, %d frames each, per frame in 0.1 ms:\n",
         surface.width, surface.height, BENCH_FRAMES);
  printf("face:   draw %lu   scan %lu   full push %lu\n",
         (unsigned long)tenths(draw), (unsigned long)tenths(scan),
         (unsigned long)tenths(full));
  printf("face: full redraw %lu fps, partial redraw ceiling %lu fps\n",
         (unsigned long)((BENCH_FRAMES * 1000u) / (draw + full + 1)),
         (unsigned long)((BENCH_FRAMES * 1000u) / (draw + scan + 1)));

  return 0;
}

static int run(void)
{
  struct face_surface surface;
  struct face_dirty dirty;
  struct face f;
  struct ui ui;
  enum face_state wanted;
  uint32_t last_poll;
  uint32_t last_real;
  uint32_t anim_ms;
  uint32_t fps_mark;
  unsigned int frames = 0;
  unsigned int fps = 0;
  uint32_t pushed = 0;     /* pixels sent to the panel since fps_mark */
  unsigned int bus = 100;  /* percent of a full frame per frame, last second */
  uint32_t buttons = 0;
  bool quit = false;
  int btnfd;
  int fd;

  if (open_surface(&fd, &surface) != 0)
    {
      return 1;
    }

  ui.preset  = 0;
  ui.palette = 0;
  ui.speed   = 1;
  ui.bright  = BRIGHT_MAX;
  ui.hold    = false;
  ui.overlay = false;

  /* The buttons are optional.  A build without them, or a board where the
   * driver did not register, still runs the face from the state file.
   */

  btnfd = open(CONFIG_PICO_FACE_BUTTONS_DEV, O_RDONLY | O_NONBLOCK);
  if (btnfd < 0)
    {
      printf("face: no %s, running without the panel controls\n",
             CONFIG_PICO_FACE_BUTTONS_DEV);
    }

  printf("face: %dx%d, state from %s, %d presets\n", surface.width,
         surface.height, CONFIG_PICO_FACE_STATE_FILE, face_preset_count());

  /* A quit word left over from the last run would stop this one on its
   * first poll.  It is consumed here rather than removed by the loop that
   * quits, so that every loop reading the file sees it and stops.
   */

  if (read_state(&wanted, &quit) == false && quit)
    {
      unlink(CONFIG_PICO_FACE_STATE_FILE);
      quit = false;
    }

  /* The face is ticked on the animation clock below, so it has to start on
   * that clock too.  Starting it on the real clock leaves it waiting for the
   * animation clock to catch up, which is a frozen face for as long as the
   * board had been up.
   */

  anim_ms   = 0;
  face_init(&f, anim_ms);
  face_tracker_reset(&g_tracker);
  last_poll = now_ms();
  last_real = last_poll;
  fps_mark  = last_poll;

  while (!quit)
    {
      const struct face_preset *preset = face_preset(ui.preset);
      uint32_t t = now_ms();

      /* The animation runs on its own clock, so changing the speed bends the
       * rate from here on rather than jumping the pose.
       */

      anim_ms += ((t - last_real) * (uint32_t)g_speeds[ui.speed]) / 100u;
      last_real = t;

      if (t - last_poll >= POLL_MS)
        {
          last_poll = t;

          if (btnfd >= 0)
            {
              uint32_t sample = 0;

              if (read(btnfd, &sample, sizeof(sample)) == (ssize_t)sizeof(sample))
                {
                  apply(&ui, face_input_action(sample, buttons));
                  buttons = sample;
                }
            }

          /* Hold ignores the expression in the file, not the quit word.
           * Nothing else can stop the loop, since kill does not work in
           * this configuration.
           */

          if (read_state(&wanted, &quit) && !ui.hold && wanted != f.state)
            {
              face_set_state(&f, wanted, anim_ms);
            }
        }

      face_tick(&f, anim_ms);

      preset->render(&surface, &f.pose, f.state, anim_ms, ui.palette, &dirty);

      if (ui.overlay)
        {
          face_overlay(&surface, face_palette(ui.palette), preset->name,
                       face_palette(ui.palette)->name,
                       face_state_name(f.state), fps, bus, ui.hold,
                       g_speeds[ui.speed]);
        }

      dim_surface(&surface, ui.bright);

      /* The preset reports the whole panel as dirty.  What actually changed
       * is usually far less, and the bus is the bottleneck, so the frame is
       * compared with the last one and only the difference is sent.
       */

      face_tracker_scan(&g_tracker, &surface, &dirty);
      push(fd, &dirty);
      pushed += (uint32_t)dirty.w * (uint32_t)dirty.h;

      frames++;
      if (t - fps_mark >= 1000)
        {
          uint32_t full = (uint32_t)surface.width * (uint32_t)surface.height;

          fps = frames;
          bus = (unsigned int)((pushed * 100u) / (full * frames));
          frames = 0;
          pushed = 0;
          fps_mark = t;
        }

      usleep(FRAME_MS * 1000);
    }

  if (btnfd >= 0)
    {
      close(btnfd);
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
