/* Test the 32 MB of PSRAM on this ESP32-P4 board, and measure what it costs.
 *
 * Reports on this board say any PSRAM configuration faults at 0x5008e1a0, so
 * this first checks that the memory works, then measures how it compares with
 * internal SRAM.
 *
 * Buffer size is the whole point of the measurement. The P4 has a 128 KB L2
 * cache with 64 byte lines, so a working set of 128 KB or less measures the
 * cache rather than the memory behind it. PSRAM is therefore measured twice,
 * once inside the cache and once well past it.
 */

#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INTERNAL_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define SMALL_BYTES   (128 * 1024)
#define LARGE_BYTES   (8 * 1024 * 1024)
#define CHASE_STEPS   100000

/* Full-period LCG for a power-of-two modulus: the multiplier is 1 mod 4 and the
 * increment is odd, so it visits every slot exactly once. Successive hops land
 * far apart, which is what makes it defeat the cache.
 */

#define LCG_NEXT(i, mask) ((0x9e3779b1u * (uint32_t)(i) + 1u) & (mask))

static bool pattern_test(uint32_t *buf, size_t words)
{
  size_t i;

  for (i = 0; i < words; i++)
    {
      buf[i] = (uint32_t)i * 2654435761u;
    }

  for (i = 0; i < words; i++)
    {
      if (buf[i] != (uint32_t)i * 2654435761u)
        {
          printf("MISMATCH at word %u: got 0x%08lx\n",
                 (unsigned)i, (unsigned long)buf[i]);
          return false;
        }
    }

  return true;
}

static void bench_bulk(const char *name, uint32_t caps, size_t bytes)
{
  uint32_t *a;
  uint32_t *b;
  int64_t t;

  a = heap_caps_malloc(bytes, caps);
  b = heap_caps_malloc(bytes, caps);
  if (a == NULL || b == NULL)
    {
      printf("%s: cannot allocate 2 x %u KB\n", name, (unsigned)(bytes / 1024));
      heap_caps_free(a);
      heap_caps_free(b);
      return;
    }

  memset(a, 0, bytes);
  memset(b, 0, bytes);

  t = esp_timer_get_time();
  memset(a, 0xa5, bytes);
  t = esp_timer_get_time() - t;
  printf("  %s memset: %6lld KB/s\n", name, (int64_t)bytes * 1000 / (t ? t : 1));

  t = esp_timer_get_time();
  memcpy(b, a, bytes);
  t = esp_timer_get_time() - t;
  printf("  %s memcpy: %6lld KB/s\n", name, (int64_t)bytes * 1000 / (t ? t : 1));

  heap_caps_free(a);
  heap_caps_free(b);
}

static void bench_chase(const char *name, uint32_t caps, size_t bytes)
{
  size_t words = bytes / sizeof(uint32_t);
  uint32_t mask = (uint32_t)words - 1;
  volatile uint32_t sink = 0;
  uint32_t *buf;
  uint32_t idx = 0;
  int64_t t;
  size_t i;

  buf = heap_caps_malloc(bytes, caps);
  if (buf == NULL)
    {
      printf("  %s chase:  cannot allocate %u KB\n",
             name, (unsigned)(bytes / 1024));
      return;
    }

  for (i = 0; i < words; i++)
    {
      buf[i] = LCG_NEXT(i, mask);
    }

  t = esp_timer_get_time();
  for (i = 0; i < CHASE_STEPS; i++)
    {
      idx = buf[idx];
    }
  t = esp_timer_get_time() - t;
  sink = idx;
  (void)sink;

  printf("  %s chase:  %4lld ns per dependent load\n",
         name, t * 1000 / CHASE_STEPS);

  heap_caps_free(buf);
}

void app_main(void)
{
  esp_chip_info_t info;
  uint32_t *buf;

  esp_chip_info(&info);
  printf("espidf-psram on chip revision v%u.%u\n",
         info.revision / 100, info.revision % 100);
  printf("PSRAM initialised: %s\n",
         esp_psram_is_initialized() ? "yes" : "no");
  if (esp_psram_is_initialized())
    {
      printf("PSRAM size: %u bytes\n", (unsigned)esp_psram_get_size());
    }

  printf("free internal: %u bytes\n",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  printf("free spiram:   %u bytes\n",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  buf = heap_caps_malloc(1024 * 1024, MALLOC_CAP_SPIRAM);
  if (buf == NULL)
    {
      printf("could not allocate 1 MB from PSRAM\n");
    }
  else
    {
      printf("pattern test: %s\n",
             pattern_test(buf, (1024 * 1024) / sizeof(uint32_t)) ? "PASS"
                                                                 : "FAIL");
      heap_caps_free(buf);
    }

  printf("\n=== 128 KB working set, fits the 128 KB L2 cache ===\n");
  bench_bulk("internal", INTERNAL_CAPS, SMALL_BYTES);
  bench_chase("internal", INTERNAL_CAPS, SMALL_BYTES);
  bench_bulk("psram   ", MALLOC_CAP_SPIRAM, SMALL_BYTES);
  bench_chase("psram   ", MALLOC_CAP_SPIRAM, SMALL_BYTES);

  printf("\n=== 8 MB working set, 64x the cache ===\n");
  bench_bulk("psram   ", MALLOC_CAP_SPIRAM, LARGE_BYTES);
  bench_chase("psram   ", MALLOC_CAP_SPIRAM, LARGE_BYTES);

  for (uint32_t i = 0;; i++)
    {
      printf("alive %lu\n", (unsigned long)i);
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
