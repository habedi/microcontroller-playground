#include "wget.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

struct download_output {
  FILE *stream;
  bool failed;
};

static esp_err_t download_event(esp_http_client_event_t *event)
{
  struct download_output *output = event->user_data;
  int status = esp_http_client_get_status_code(event->client);
  if (event->event_id == HTTP_EVENT_ON_DATA && status >= 200 && status < 300 &&
      !output->failed && event->data_len > 0)
    {
      output->failed = fwrite(event->data, 1, event->data_len, output->stream) !=
                       (size_t)event->data_len;
    }
  return ESP_OK;
}

static int publish_download(const char *temporary, const char *backup, const char *path)
{
  if (rename(temporary, path) == 0) return 0;
  if (errno != EEXIST) return -1;

  /* FAT cannot replace a file with rename; keep the old file until publication succeeds. */
  /* shortcut: replacement is not atomic; add boot recovery if power loss must be handled without manual recovery. */
  if (rename(path, backup) != 0) return -1;

  /* Inspect the object actually moved, inside the staging directory we own. */
  int error = 0;
  int fd = open(backup, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
  if (fd < 0) {
    error = errno;
  } else {
    struct stat st;
    if (fstat(fd, &st) != 0) error = errno;
    else if (!S_ISREG(st.st_mode)) error = EISDIR;
    if (close(fd) != 0 && error == 0) error = errno;
  }
  if (error == 0 && rename(temporary, path) == 0) {
    if (unlink(backup) != 0)
      fprintf(stderr, "wget: could not remove previous file at %s\n", backup);
    return 0;
  }
  if (error == 0) error = errno;
  if (rename(backup, path) != 0)
    fprintf(stderr, "wget: previous output preserved at %s\n", backup);
  errno = error;
  return -1;
}

int wget_download(const char *url, const char *path)
{
  if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
    {
      fprintf(stderr, "wget: use an http:// or https:// URL\n");
      return 1;
    }

  char staging[528] = "";
  char temporary[sizeof(staging) + 4];
  char backup[sizeof(staging) + 4];
  FILE *stream = stdout;
  bool to_stdout = strcmp(path, "-") == 0;
  if (!to_stdout)
    {
      int len = snprintf(staging, sizeof(staging), "%s.partXXXXXX", path);
      if (len < 0 || (size_t)len >= sizeof(staging))
        {
          fprintf(stderr, "wget: output path too long\n");
          return 1;
        }
      if (mkdtemp(staging) == NULL)
        {
          perror("wget: cannot create staging directory");
          return 1;
        }
      snprintf(temporary, sizeof(temporary), "%s/new", staging);
      snprintf(backup, sizeof(backup), "%s/old", staging);
      int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
      if (fd < 0)
        {
          perror("wget: cannot create output");
          rmdir(staging);
          return 1;
        }
      stream = fdopen(fd, "wb");
      if (stream == NULL)
        {
          close(fd);
          unlink(temporary);
          rmdir(staging);
          perror("wget: cannot open output");
          return 1;
        }
    }

  struct download_output output = { .stream = stream };
  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 15000,
      .event_handler = download_event,
      .user_data = &output,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = ESP_FAIL;
  int status = 0;
  if (client != NULL)
    {
      err = esp_http_client_perform(client);
      status = esp_http_client_get_status_code(client);
      if (err == ESP_OK && !esp_http_client_is_complete_data_received(client))
        err = ESP_ERR_HTTP_INCOMPLETE_DATA;
      esp_http_client_cleanup(client);
    }

  bool failed = err != ESP_OK || status < 200 || status >= 300;
  if (failed)
    fprintf(stderr, "wget: request failed (%s, HTTP %d)\n", esp_err_to_name(err), status);

  bool write_failed = output.failed || ferror(stream);
  if (to_stdout)
    {
      if (fflush(stream) != 0) write_failed = true;
    }
  else if (fclose(stream) != 0) write_failed = true;

  if (write_failed)
    {
      fprintf(stderr, "wget: output write failed\n");
      failed = true;
    }
  if (!to_stdout)
    {
      /* Publish only a complete response, preserving any existing destination on failure. */
      if (!failed && publish_download(temporary, backup, path) != 0)
        {
          perror("wget: cannot replace output");
          failed = true;
        }
      if (failed) unlink(temporary);
      if (rmdir(staging) != 0)
        fprintf(stderr, "wget: recovery directory retained at %s\n", staging);
    }
  return failed ? 1 : 0;
}
