/* Ask the ESP32-C6 to answer as a flash target over SDIO.
 *
 * The C6 does not enumerate as an SDIO card under ESP-Hosted, and three host
 * generations fail identically, so the question is whether the chip responds at
 * all. This library talks to the ROM loader rather than to slave firmware, and
 * it drives both the reset and the bootstrap, so it can force download mode
 * without the C6 running anything useful.
 *
 * This only connects and identifies. Flashing needs slave firmware built for
 * the C6, which is a separate step and pointless until the link answers.
 */

#include <stdio.h>

#include "esp32_sdio_port.h"
#include "esp_loader.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Pins for the ESP32-P4-Function-EV-Board, as ESP-Hosted reports them and as
 * Espressif documents them. The library's own example uses GPIO 47 to 52 for a
 * different board, so these must be set explicitly.
 */

#define C6_SDIO_CLK GPIO_NUM_18
#define C6_SDIO_CMD GPIO_NUM_19
#define C6_SDIO_D0  GPIO_NUM_14
#define C6_SDIO_D1  GPIO_NUM_15
#define C6_SDIO_D2  GPIO_NUM_16
#define C6_SDIO_D3  GPIO_NUM_17
#define C6_RESET    GPIO_NUM_54

/* The board documentation names only the reset line. The library's example
 * pairs reset 54 with boot 53 on a P4 host, so 53 is the likely strap, but
 * whether it reaches the C6 on this board is exactly what is being tested.
 */

#define C6_BOOT     GPIO_NUM_53

static const char *TAG = "c6flash";

static const char *chip_name(target_chip_t chip)
{
  switch (chip)
    {
      case ESP32_CHIP:   return "ESP32";
      case ESP32S2_CHIP: return "ESP32-S2";
      case ESP32C3_CHIP: return "ESP32-C3";
      case ESP32S3_CHIP: return "ESP32-S3";
      case ESP32C2_CHIP: return "ESP32-C2";
      case ESP32H2_CHIP: return "ESP32-H2";
      case ESP32C6_CHIP: return "ESP32-C6";
      case ESP32P4_CHIP: return "ESP32-P4";
      default:           return "unknown";
    }
}

void app_main(void)
{
  esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();
  esp_loader_error_t err;
  esp_loader_t loader;

  esp32_sdio_port_t port =
  {
    .port.ops     = &esp32_sdio_ops,
    .slot         = SDMMC_HOST_SLOT_1,
    .max_freq_khz = SDMMC_FREQ_DEFAULT,
    .reset_pin    = C6_RESET,
    .boot_pin     = C6_BOOT,
    .bus_width    = SDIO_4BIT,
    .sdio_clk_pin = C6_SDIO_CLK,
    .sdio_cmd_pin = C6_SDIO_CMD,
    .sdio_d0_pin  = C6_SDIO_D0,
    .sdio_d1_pin  = C6_SDIO_D1,
    .sdio_d2_pin  = C6_SDIO_D2,
    .sdio_d3_pin  = C6_SDIO_D3,
  };

  ESP_LOGI(TAG, "SDIO: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d RESET=%d BOOT=%d",
           C6_SDIO_CLK, C6_SDIO_CMD, C6_SDIO_D0, C6_SDIO_D1, C6_SDIO_D2,
           C6_SDIO_D3, C6_RESET, C6_BOOT);

  err = esp_loader_init_sdio(&loader, &port.port);
  if (err != ESP_LOADER_SUCCESS)
    {
      ESP_LOGE(TAG, "esp_loader_init_sdio failed: %d", (int)err);
    }
  else
    {
      ESP_LOGI(TAG, "SDIO port initialised, attempting connect");

      err = esp_loader_connect(&loader, &args);
      if (err == ESP_LOADER_SUCCESS)
        {
          target_chip_t chip = esp_loader_get_target(&loader);

          ESP_LOGI(TAG, "CONNECTED. Target reports as %s (%d)",
                   chip_name(chip), (int)chip);
          ESP_LOGI(TAG, "The C6 answers its ROM loader over SDIO, so it can be"
                        " reflashed from the P4 with no cable.");
        }
      else
        {
          ESP_LOGE(TAG, "connect failed: %d", (int)err);
          if (err == ESP_LOADER_ERROR_TIMEOUT)
            {
              ESP_LOGE(TAG, "timeout: the C6 did not answer at all");
            }
          else if (err == ESP_LOADER_ERROR_INVALID_TARGET)
            {
              ESP_LOGE(TAG, "answered, but the chip or revision is unsupported");
            }
        }
    }

  for (uint32_t i = 0;; i++)
    {
      printf("idle %lu\n", (unsigned long)i);
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
