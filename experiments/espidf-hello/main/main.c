/* Smallest ESP-IDF program that proves this board runs the vendor SDK. It
 * prints the chip revision, because that number is the source of most of this
 * board's trouble, and the free heap, because PSRAM is unusable at revision
 * v1.3 and this shows what internal RAM is left.
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

  printf("espidf-hello on chip revision v%d.%d, %d cores\n",
         info.full_revision / 100, info.full_revision % 100, info.cores);
  printf("free heap: %lu bytes\n", (unsigned long)esp_get_free_heap_size());

  for (uint32_t i = 0;; i++)
    {
      printf("tick %lu\n", (unsigned long)i);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
