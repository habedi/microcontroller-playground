/* ESP-Hosted coprocessor firmware for the ESP32-C6 on the ESP32-P4 function EV
 * board.
 *
 * The component provides the whole coprocessor implementation and starts it
 * itself when CONFIG_ESP_HOSTED_CP is set, so this only reports that it is
 * alive. The console runs at the C6's own UART0, which is reachable on the
 * PROG_C6 header, so this output is the way to tell the chip is running at all.
 */

#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "c6-slave";

void app_main(void)
{
  esp_chip_info_t info;

  esp_chip_info(&info);
  ESP_LOGI(TAG, "ESP-Hosted coprocessor firmware, %d cores, revision v%u.%u",
           info.cores, info.revision / 100, info.revision % 100);

  for (uint32_t i = 0;; i++)
    {
      ESP_LOGI(TAG, "coprocessor alive %lu", (unsigned long)i);
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
