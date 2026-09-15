/****************************************************************************
 * experiments/pico-face/src/face_graph.c
 *
 * The graph preset: the state machine drawn as a diagram.  The six states
 * sit on a ring, every pair is joined by a faint edge because the hook can
 * move between any two, and the current state is a red node with its edges
 * lit while the other five are orange.  The current node pulses with the
 * pose's glow, so the diagram breathes the same way the faces do.
 *
 * Drawn in panel pixels rather than on a grid, since a diagram wants
 * straight lines and round nodes rather than blocks.
 *
 ****************************************************************************/

#include <stddef.h>

#include "face_font.h"
#include "face_overlay.h"
#include "face_preset.h"
#include "face_sprite.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Ring radius and node radius, as a percentage of the shorter panel side. */

#define RING_PCT 33
#define NODE_PCT  5

/* How much the current node grows at full glow, in pixels. */

#define PULSE_PX 4

#define C_BG      face_rgb565(10, 8, 8)
#define C_EDGE    face_rgb565(70, 36, 10)
#define C_LIT     face_rgb565(200, 40, 24)
#define C_NODE    face_rgb565(236, 128, 20)
#define C_CURRENT face_rgb565(228, 36, 28)
#define C_LABEL   face_rgb565(240, 170, 90)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Unit positions of the six nodes, clockwise from the top, in thousandths.
 * The order is the order of enum face_state, so idle is at the top.
 */

static const int16_t g_ring[FACE_NSTATES][2] =
{
  { 0, -1000 }, { 866, -500 }, { 866, 500 },
  { 0, 1000 }, { -866, 500 }, { -866, -500 }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void put(const struct face_surface *s, int x, int y, uint16_t colour)
{
  if (x >= 0 && x < s->width && y >= 0 && y < s->height)
    {
      s->pixels[(size_t)y * (size_t)s->stride_px + (size_t)x] = colour;
    }
}

/* A line two pixels wide, stepped along its longer axis. */

static void line(const struct face_surface *s, int x0, int y0, int x1,
                 int y1, uint16_t colour)
{
  int dx = x1 - x0;
  int dy = y1 - y0;
  int adx = dx < 0 ? -dx : dx;
  int ady = dy < 0 ? -dy : dy;
  int steps = adx > ady ? adx : ady;
  int i;

  for (i = 0; i <= steps; i++)
    {
      int x = x0 + (steps ? (dx * i) / steps : 0);
      int y = y0 + (steps ? (dy * i) / steps : 0);

      put(s, x, y, colour);
      put(s, x + (adx > ady ? 0 : 1), y + (adx > ady ? 1 : 0), colour);
    }
}

static void disc(const struct face_surface *s, int cx, int cy, int r,
                 uint16_t colour)
{
  int dy;

  for (dy = -r; dy <= r; dy++)
    {
      int dx;

      for (dx = -r; dx <= r; dx++)
        {
          if (dx * dx + dy * dy <= r * r)
            {
              put(s, cx + dx, cy + dy, colour);
            }
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void face_graph_node(int index, int width, int height, int *x, int *y)
{
  int side = width < height ? width : height;
  int ring = (side * RING_PCT) / 100;

  if (index < 0 || index >= FACE_NSTATES)
    {
      index = 0;
    }

  *x = width / 2 + (ring * g_ring[index][0]) / 1000;
  *y = height / 2 + (ring * g_ring[index][1]) / 1000;
}

void face_render_graph(const struct face_surface *s,
                       const struct face_pose *pose,
                       enum face_state state,
                       uint32_t now_ms,
                       int palette,
                       struct face_dirty *dirty)
{
  int side = s->width < s->height ? s->width : s->height;
  int node = (side * NODE_PCT) / 100;
  int pulse = (PULSE_PX * pose->glow) / FACE_UNIT;
  int i;
  int j;

  (void)now_ms;
  (void)palette;

  if (state < 0 || state >= FACE_NSTATES)
    {
      state = FACE_IDLE;
    }

  face_sprite_clear(s, C_BG);

  /* Central telemetry reticle */

  {
    int cx = s->width / 2;
    int cy = s->height / 2;
    int r_hub = (side * 10) / 100;

    /* Faint inner telemetry ring */

    for (i = 0; i < 360; i += 15)
      {
        int rx = cx + (r_hub * g_ring[i % FACE_NSTATES][0]) / 1000;
        int ry = cy + (r_hub * g_ring[i % FACE_NSTATES][1]) / 1000;
        put(s, rx, ry, C_EDGE);
      }

    /* Crosshair ticks */

    line(s, cx - r_hub - 4, cy, cx - r_hub + 2, cy, C_EDGE);
    line(s, cx + r_hub - 2, cy, cx + r_hub + 4, cy, C_EDGE);
    line(s, cx, cy - r_hub - 4, cx, cy - r_hub + 2, C_EDGE);
    line(s, cx, cy + r_hub - 2, cx, cy + r_hub + 4, C_EDGE);
  }

  /* Every edge faintly, then the current node's edges over them. */

  for (i = 0; i < FACE_NSTATES; i++)
    {
      for (j = i + 1; j < FACE_NSTATES; j++)
        {
          int x0;
          int y0;
          int x1;
          int y1;

          face_graph_node(i, s->width, s->height, &x0, &y0);
          face_graph_node(j, s->width, s->height, &x1, &y1);
          line(s, x0, y0, x1, y1,
               (i == (int)state || j == (int)state) ? C_LIT : C_EDGE);
        }
    }

  /* Animated telemetry data packets flowing along active edges */

  for (j = 0; j < FACE_NSTATES; j++)
    {
      if (j != (int)state)
        {
          int x0, y0, x1, y1;
          int t, px, py;

          face_graph_node((int)state, s->width, s->height, &x0, &y0);
          face_graph_node(j, s->width, s->height, &x1, &y1);

          /* Flow from connected nodes toward active node */

          t = 20 + (int)((now_ms / 12 + (uint32_t)j * 18) % 60);
          px = x1 + ((x0 - x1) * t) / 100;
          py = y1 + ((y0 - y1) * t) / 100;

          disc(s, px, py, 2, C_CURRENT);
        }
    }

  /* Nodes over the edges, labels beside them: above the top node, below
   * the bottom one, and outward from the others.
   */

  for (i = 0; i < FACE_NSTATES; i++)
    {
      const char *name = face_state_name((enum face_state)i);
      int len = 0;
      int x;
      int y;
      int lx;
      int ly;

      while (name[len] != '\0')
        {
          len++;
        }

      face_graph_node(i, s->width, s->height, &x, &y);

      if (i == (int)state)
        {
          disc(s, x, y, node + pulse, C_CURRENT);
        }
      else
        {
          disc(s, x, y, node, C_NODE);
        }

      lx = x - (len * (FACE_FONT_W + 1)) / 2;
      ly = y + node + PULSE_PX + 3;

      if (g_ring[i][1] < 0)
        {
          ly = y - node - PULSE_PX - 3 - FACE_FONT_H;
        }

      face_text(s, lx, ly, 1, i == (int)state ? C_CURRENT : C_LABEL, name);
    }

  if (dirty != NULL)
    {
      dirty->x = 0;
      dirty->y = 0;
      dirty->w = s->width;
      dirty->h = s->height;
    }
}
