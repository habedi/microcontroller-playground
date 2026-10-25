#ifndef TEST_ESP_HTTP_CLIENT_H
#define TEST_ESP_HTTP_CLIENT_H

#include <stdbool.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTP_INCOMPLETE_DATA -2
#define HTTP_EVENT_ON_DATA 1

typedef struct test_http_client *esp_http_client_handle_t;
typedef struct {
  int event_id;
  esp_http_client_handle_t client;
  void *user_data;
  void *data;
  int data_len;
} esp_http_client_event_t;

typedef struct {
  const char *url;
  int (*crt_bundle_attach)(void *config);
  int timeout_ms;
  esp_err_t (*event_handler)(esp_http_client_event_t *event);
  void *user_data;
  bool skip_cert_common_name_check;
  bool disable_auto_redirect;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
const char *esp_err_to_name(esp_err_t error);

#endif
