#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_random.h"

static const char *mode;
static bool moved_backup;
static int backup_fd = -1;
static int mkdir_calls;

uint32_t esp_random(void)
{
  static uint32_t next = 0x12345678;
  return next++;
}
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
  if (strncmp(mode, "fat", 3) == 0) {
    if (strstr(source, "/new") && moved_backup &&
        (strcmp(mode, "fat-publish-error") == 0 || strcmp(mode, "fat-rollback-error") == 0)) {
      errno = EIO;
      return -1;
    }
    if ((strstr(destination, "/old") && strcmp(mode, "fat-backup-error") == 0) ||
        (strstr(source, "/old") && strcmp(mode, "fat-rollback-error") == 0)) {
      errno = EACCES;
      return -1;
    }
    struct stat st;
    if (stat(destination, &st) == 0) { errno = EEXIST; return -1; }
    if (strstr(destination, "/old") && strncmp(mode, "fat-swap-", 9) == 0) {
      char displaced[1024];
      snprintf(displaced, sizeof(displaced), "%s.displaced", source);
      assert(rename(source, displaced) == 0);
      if (strcmp(mode, "fat-swap-directory") == 0) {
        assert(mkdir(source, 0700) == 0);
      } else if (strcmp(mode, "fat-swap-symlink") == 0) {
        assert(symlink(displaced, source) == 0);
      } else {
        assert(strcmp(mode, "fat-swap-fifo") == 0);
        assert(mkfifo(source, 0600) == 0);
      }
    }
  }
  int result = rename(source, destination);
  if (result == 0 && strstr(destination, "/old")) {
    moved_backup = true;
    if (strcmp(mode, "fat-competitor") == 0) {
      FILE *file = fopen(source, "w");
      assert(file);
      assert(fputs("competing output", file) >= 0);
      assert(fclose(file) == 0);
    }
  }
  return result;
}

static int test_mkdir(const char *path, mode_t permissions)
{
  mkdir_calls++;
  if (strcmp(mode, "fat-staging-create-error") == 0) {
    errno = ENOSPC;
    return -1;
  }
  if (strcmp(mode, "staging-collisions-exhausted") == 0) {
    errno = EEXIST;
    return -1;
  }
  if (strcmp(mode, "staging-collision") == 0 && mkdir_calls == 1) {
    assert(mkdir(path, 0700) == 0);
    char marker[1024];
    snprintf(marker, sizeof(marker), "%s/keep", path);
    FILE *file = fopen(marker, "w");
    assert(file);
    assert(fputs("another download", file) >= 0);
    assert(fclose(file) == 0);
    errno = EEXIST;
    return -1;
  }
  return mkdir(path, permissions);
}

static int test_open(const char *path, int flags, ...)
{
  if (strstr(path, "/old")) {
    assert(flags & O_NOFOLLOW);
    assert(flags & O_NONBLOCK);
    if (strcmp(mode, "fat-open-error") == 0) { errno = EACCES; return -1; }
    backup_fd = open(path, flags);
    return backup_fd;
  }
  assert(flags & O_EXCL);
  char directory[1024];
  snprintf(directory, sizeof(directory), "%s", path);
  *strrchr(directory, '/') = '\0';
  struct stat st;
  assert(stat(directory, &st) == 0 && S_ISDIR(st.st_mode));
  assert((st.st_mode & 0777) == 0700);
  va_list args;
  va_start(args, flags);
  int permissions = va_arg(args, int);
  va_end(args);
  return open(path, flags, permissions);
}

static int test_fstat(int fd, struct stat *st)
{
  if (strcmp(mode, "fat-stat-error") == 0) { errno = EIO; return -1; }
  return fstat(fd, st);
}

static int test_close(int fd)
{
  int result = close(fd);
  if (fd == backup_fd && strcmp(mode, "fat-close-error") == 0) {
    errno = EIO;
    return -1;
  }
  return result;
}

#define fwrite test_fwrite
#define fclose test_fclose
#define rename test_rename
#define mkdir test_mkdir
#define open test_open
#define fstat test_fstat
#define close test_close
#include "../main/wget.c"
#undef fwrite
#undef fclose
#undef rename
#undef mkdir
#undef open
#undef fstat
#undef close

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
  if (strcmp(mode, "staging-collisions-exhausted") == 0) assert(mkdir_calls == 8);
  return result;
}
