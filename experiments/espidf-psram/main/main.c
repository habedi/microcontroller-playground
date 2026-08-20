/* Test whether the 32 MB of PSRAM on this ESP32-P4 board works.
 *
 * Reports on this board say any PSRAM configuration faults at 0x5008e1a0,
 * inside the PSRAM controller register space, on chip revision v1.3. This
 * program reports what the driver found and then writes and reads a pattern,
 * because a controller that initialises is not proof that the memory holds
 * data. The console is on UART0, so a panic is visible.
 */

#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TEST_BYTES (1024 * 1024)

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

void app_main(void)
{
  esp_chip_info_t info;
  uint32_t *buf;

  esp_chip_info(&info);
  printf("espidf-psram on chip revision v%u.%u\n",
         info.revision / 100, info.revision % 100);

  printf("PSRAM initialised: %s\n", esp_psram_is_initialized() ? "yes" : "no");

  if (esp_psram_is_initialized())
    {
      printf("PSRAM size: %u bytes\n", (unsigned)esp_psram_get_size());
    }

  printf("free internal: %u bytes\n",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  printf("free spiram:   %u bytes\n",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  buf = heap_caps_malloc(TEST_BYTES, MALLOC_CAP_SPIRAM);
  if (buf == NULL)
    {
      printf("could not allocate %d bytes from PSRAM\n", TEST_BYTES);
    }
  else
    {
      printf("allocated %d bytes at %p, testing\n", TEST_BYTES, buf);
      printf("pattern test: %s\n",
             pattern_test(buf, TEST_BYTES / sizeof(uint32_t)) ? "PASS"
                                                              : "FAIL");
      heap_caps_free(buf);
    }

  for (uint32_t i = 0;; i++)
    {
      printf("alive %lu\n", (unsigned long)i);
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
