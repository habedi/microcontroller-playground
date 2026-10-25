#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

static const char *mode;
static size_t test_fwrite(const void *data, size_t size, size_t count, FILE *stream)
{
  if (strcmp(mode, "write-error") == 0) return 0;
  return fwrite(data, size, count, stream);
}

static int test_fclose(FILE *stream)
{
  int result = fclose(stream);
  return strcmp(mode, "close-error") == 0 ? EOF : result;
}

static int test_rename(const char *source, const char *destination)
{
  if (strcmp(mode, "rename-error") == 0) { errno = EACCES; return -1; }
  return rename(source, destination);
}

#define fwrite test_fwrite
#define fclose test_fclose
#define rename test_rename
#include "../main/wget.c"
#undef fwrite
#undef fclose
#undef rename

struct test_http_client {
  esp_http_client_config_t config;
  int status;
  bool cleaned;
};
static struct test_http_client client;

int esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
const char *esp_err_to_name(esp_err_t error) { return error ? "ERROR" : "OK"; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
  assert(config->crt_bundle_attach == esp_crt_bundle_attach);
  assert(!config->skip_cert_common_name_check);
  assert(!config->disable_auto_redirect);
  assert(config->timeout_ms > 0);
  if (strcmp(mode, "init-error") == 0) return NULL;
  client.config = *config;
  return &client;
}

static void body(esp_http_client_handle_t handle, const char *data, int length)
{
  esp_http_client_event_t event = {
      .event_id = HTTP_EVENT_ON_DATA, .client = handle,
      .user_data = handle->config.user_data, .data = (void *)data,
      .data_len = length,
  };
  handle->config.event_handler(&event);
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t handle)
{
  if (strcmp(mode, "redirect") == 0)
    {
      handle->status = 302;
      body(handle, "redirect body", 13);
    }
  handle->status = strcmp(mode, "http-error") == 0 ? 404 : 200;
  if (strcmp(mode, "empty") != 0)
    {
      body(handle, "abc\0", 4);
      body(handle, "def", 3);
    }
  return strcmp(mode, "network-error") == 0 ? ESP_FAIL : ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t handle)
{ return handle->status; }

bool esp_http_client_is_complete_data_received(esp_http_client_handle_t handle)
{ (void)handle; return strcmp(mode, "incomplete") != 0; }

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle)
{ handle->cleaned = true; return ESP_OK; }

int main(int argc, char **argv)
{
  assert(argc == 4);
  mode = argv[1];
  int result = wget_download(argv[2], argv[3]);
  if (client.config.url != NULL) assert(client.cleaned);
  return result;
}
