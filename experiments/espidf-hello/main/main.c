/* Smallest ESP-IDF program for the ESP32-P4, used to find out whether the
 * vendor SDK reaches a running system on a board whose NuttX build never
 * reaches a shell. It prints the chip revision, because this sample is
 * pre-production silicon, and the free heap, because PSRAM is unusable here
 * and only internal RAM is left.
 */

#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
  esp_chip_info_t info;

  esp_chip_info(&info);

  /* The revision field holds the wafer major and minor versions as MXX. */

  printf("espidf-hello on chip revision v%u.%u, %u cores\n",
         info.revision / 100, info.revision % 100, info.cores);
  printf("free heap: %lu bytes\n", (unsigned long)esp_get_free_heap_size());

  for (uint32_t i = 0;; i++)
    {
      printf("tick %lu\n", (unsigned long)i);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
