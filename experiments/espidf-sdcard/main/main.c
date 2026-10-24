/* SDMMC MicroSD storage test on Waveshare ESP32-P4-WIFI6.
 *
 * The MicroSD slot is connected to the ESP32-P4 SDMMC host:
 *   CLK:  GPIO 43
 *   CMD:  GPIO 44
 *   D0:   GPIO 39
 *   D1:   GPIO 40
 *   D2:   GPIO 41
 *   D3:   GPIO 42
 *   PWR:  GPIO 45 (active low P-channel MOSFET switch)
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define PIN_TF_PWR GPIO_NUM_45
#define PIN_TF_CLK GPIO_NUM_43
#define PIN_TF_CMD GPIO_NUM_44
#define PIN_TF_D0  GPIO_NUM_39
#define PIN_TF_D1  GPIO_NUM_40
#define PIN_TF_D2  GPIO_NUM_41
#define PIN_TF_D3  GPIO_NUM_42

#define MOUNT_POINT "/sdcard"

static const char *TAG = "espidf_sdcard";

static void power_on_slot(void)
{
  ESP_LOGI(TAG, "Enabling slot power on GPIO %d (active low)", PIN_TF_PWR);

  gpio_config_t io_conf = {
      .pin_bit_mask = 1ULL << PIN_TF_PWR,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  gpio_set_level(PIN_TF_PWR, 0);

  /* Allow power rail to stabilize */
  vTaskDelay(pdMS_TO_TICKS(150));
}

static esp_err_t test_file_io(const char *path)
{
  ESP_LOGI(TAG, "Testing file write: %s", path);
  FILE *f = fopen(path, "w");
  if (f == NULL)
    {
      ESP_LOGE(TAG, "Failed to open file for writing");
      return ESP_FAIL;
    }

  const char *test_str = "ESP32-P4 SDMMC test successful\n";
  fputs(test_str, f);
  fclose(f);

  ESP_LOGI(TAG, "Testing file read: %s", path);
  f = fopen(path, "r");
  if (f == NULL)
    {
      ESP_LOGE(TAG, "Failed to open file for reading");
      return ESP_FAIL;
    }

  char buf[64];
  if (fgets(buf, sizeof(buf), f) == NULL)
    {
      ESP_LOGE(TAG, "Failed to read data from file");
      fclose(f);
      return ESP_FAIL;
    }
  fclose(f);

  ESP_LOGI(TAG, "Read back content: %s", buf);
  return ESP_OK;
}

void app_main(void)
{
  ESP_LOGI(TAG, "Starting ESP32-P4 SDMMC storage test");

  power_on_slot();

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024,
  };

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.clk = PIN_TF_CLK;
  slot_config.cmd = PIN_TF_CMD;
  slot_config.d0 = PIN_TF_D0;
  slot_config.d1 = PIN_TF_D1;
  slot_config.d2 = PIN_TF_D2;
  slot_config.d3 = PIN_TF_D3;
  slot_config.cd = GPIO_NUM_NC;
  slot_config.wp = GPIO_NUM_NC;
  slot_config.width = 4;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  sdmmc_card_t *card = NULL;
  ESP_LOGI(TAG, "Mounting filesystem at %s", MOUNT_POINT);

  esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config,
                                         &mount_config, &card);

  if (ret == ESP_OK)
    {
      ESP_LOGI(TAG, "Card initialized and filesystem mounted successfully");
      sdmmc_card_print_info(stdout, card);

      const char *test_path = MOUNT_POINT "/p4_test.txt";
      if (test_file_io(test_path) == ESP_OK)
        {
          ESP_LOGI(TAG, "Card read/write test passed");
        }
    }
  else
    {
      ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
      if (ret == ESP_FAIL)
        {
          ESP_LOGW(TAG, "Mount failure indicates partition table or FAT format issue.");
          ESP_LOGW(TAG, "Cards formatted with exFAT must be formatted to FAT32.");
        }
    }

  for (uint32_t i = 0;; i++)
    {
      vTaskDelay(pdMS_TO_TICKS(5000));
      ESP_LOGI(TAG, "Alive tick %lu", (unsigned long)i);
    }
}
