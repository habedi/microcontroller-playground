/* Scan for Wi-Fi networks from the ESP32-P4, which has no radio of its own.
 *
 * The scan runs on the onboard ESP32-C6 and reaches it over SDIO through
 * ESP-Hosted, so a successful scan proves the whole path: the SDIO link, the
 * slave firmware on the C6, and the proxied esp_wifi API on the P4.
 */

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define MAX_APS 20

static const char *TAG = "espidf-wifi";

static const char *auth_name(wifi_auth_mode_t mode)
{
  switch (mode)
    {
      case WIFI_AUTH_OPEN:            return "open";
      case WIFI_AUTH_WEP:             return "WEP";
      case WIFI_AUTH_WPA_PSK:         return "WPA";
      case WIFI_AUTH_WPA2_PSK:        return "WPA2";
      case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
      case WIFI_AUTH_WPA3_PSK:        return "WPA3";
      case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
      default:                        return "other";
    }
}

static void scan_once(void)
{
  wifi_ap_record_t records[MAX_APS];
  uint16_t count = MAX_APS;
  uint16_t found = 0;
  esp_err_t err;

  err = esp_wifi_scan_start(NULL, true);
  if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(err));
      return;
    }

  esp_wifi_scan_get_ap_num(&found);
  memset(records, 0, sizeof(records));
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, records));

  printf("found %u networks (showing %u)\n", (unsigned)found, (unsigned)count);
  for (uint16_t i = 0; i < count; i++)
    {
      printf("  %-32s ch %2d  %4d dBm  %s\n",
             (const char *)records[i].ssid, records[i].primary,
             records[i].rssi, auth_name(records[i].authmode));
    }
}

void app_main(void)
{
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err;

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
    }
  ESP_ERROR_CHECK(err);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  /* Do not abort when this fails. The point of the experiment is to find out
   * whether the link to the C6 works, and aborting turns a failed probe into a
   * boot loop that hides the diagnosis.
   */

  err = esp_wifi_init(&cfg);
  if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
      ESP_LOGE(TAG, "the SDIO link to the ESP32-C6 did not come up");
      for (;;)
        {
          vTaskDelay(pdMS_TO_TICKS(10000));
          ESP_LOGW(TAG, "idle, no radio");
        }
    }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "radio is up, the C6 is answering");

  for (;;)
    {
      scan_once();
      vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
