/* SDMMC MicroSD, LittleFS, Wi-Fi client, and terminal editor on ESP32-P4.
 *
 * Hardware configuration:
 *   - On-chip LDO channel 4 supplies power to the ESP32-P4 SDMMC IO pad domain.
 *   - GPIO 45 drives the onboard MicroSD socket power switch (active low).
 *   - Host slot 0 is the native SDMMC controller for MicroSD card (/sdcard).
 *   - Internal NOR flash houses 12 MB LittleFS storage (/data).
 *   - SDIO Host slot 1 connects to onboard ESP32-C6 for Wi-Fi and Bluetooth.
 *   - Interactive REPL console runs on UART0 with prompt "nsh> ".
 */

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "editor.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

#define PIN_TF_PWR   GPIO_NUM_45
#define PIN_TF_CLK   GPIO_NUM_43
#define PIN_TF_CMD   GPIO_NUM_44
#define PIN_TF_D0    GPIO_NUM_39
#define PIN_TF_D1    GPIO_NUM_40
#define PIN_TF_D2    GPIO_NUM_41
#define PIN_TF_D3    GPIO_NUM_42
#define PIN_C6_RESET GPIO_NUM_54

#define SDCARD_MOUNT_POINT "/sdcard"
#define LITTLEFS_MOUNT_POINT "/data"

static const char *TAG = "espidf_shell";

static char s_cwd[256] = SDCARD_MOUNT_POINT;
static esp_netif_t *s_sta_netif = NULL;
static bool s_wifi_initialized = false;
static bool s_wifi_connected = false;
static SemaphoreHandle_t s_ip_sem = NULL;

static void power_on_slot(void)
{
  gpio_config_t io_conf = {
      .pin_bit_mask = 1ULL << PIN_TF_PWR,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  gpio_set_level(PIN_TF_PWR, 0);
  vTaskDelay(pdMS_TO_TICKS(300));
}

static void resolve_path(const char *in, char *out, size_t out_len)
{
  char combined[512];
  if (!in || in[0] == '\0')
    {
      strncpy(out, s_cwd, out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }

  if (in[0] == '/')
    {
      strncpy(combined, in, sizeof(combined) - 1);
    }
  else
    {
      snprintf(combined, sizeof(combined), "%s/%s", s_cwd, in);
    }
  combined[sizeof(combined) - 1] = '\0';

  char segments[16][64];
  int count = 0;

  char copy[512];
  strncpy(copy, combined, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';

  char *saveptr = NULL;
  char *tok = strtok_r(copy, "/", &saveptr);
  while (tok != NULL)
    {
      if (strcmp(tok, ".") == 0)
        {
          /* Current directory: skip */
        }
      else if (strcmp(tok, "..") == 0)
        {
          if (count > 0)
            {
              count--;
            }
        }
      else if (strlen(tok) > 0)
        {
          if (count < 16)
            {
              strncpy(segments[count], tok, sizeof(segments[count]) - 1);
              segments[count][sizeof(segments[count]) - 1] = '\0';
              count++;
            }
        }
      tok = strtok_r(NULL, "/", &saveptr);
    }

  if (count == 0)
    {
      strncpy(out, "/", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }

  out[0] = '\0';
  for (int i = 0; i < count; i++)
    {
      strncat(out, "/", out_len - strlen(out) - 1);
      strncat(out, segments[i], out_len - strlen(out) - 1);
    }
}

static int cmd_pwd(int argc, char **argv)
{
  printf("%s\n", s_cwd);
  return 0;
}

static int cmd_cd(int argc, char **argv)
{
  char target[512];
  if (argc < 2)
    {
      strncpy(target, SDCARD_MOUNT_POINT, sizeof(target));
    }
  else
    {
      resolve_path(argv[1], target, sizeof(target));
    }

  struct stat st;
  if (stat(target, &st) == 0 && S_ISDIR(st.st_mode))
    {
      strncpy(s_cwd, target, sizeof(s_cwd) - 1);
      s_cwd[sizeof(s_cwd) - 1] = '\0';
      return 0;
    }
  printf("cd: %s: No such directory\n", target);
  return 1;
}

static int cmd_ls(int argc, char **argv)
{
  char path[512];
  if (argc > 1)
    {
      resolve_path(argv[1], path, sizeof(path));
    }
  else
    {
      strncpy(path, s_cwd, sizeof(path) - 1);
      path[sizeof(path) - 1] = '\0';
    }

  DIR *d = opendir(path);
  if (!d)
    {
      printf("ls: cannot open directory %s\n", path);
      return 1;
    }

  printf("Directory: %s\n", path);
  struct dirent *entry;
  char full_path[1024];
  struct stat st;
  while ((entry = readdir(d)) != NULL)
    {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
          continue;
        }

      snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
      if (stat(full_path, &st) == 0)
        {
          if (S_ISDIR(st.st_mode))
            {
              printf("  <DIR>         %s\n", entry->d_name);
            }
          else
            {
              printf("  %10ld B  %s\n", (long)st.st_size, entry->d_name);
            }
        }
      else
        {
          printf("                %s\n", entry->d_name);
        }
    }
  closedir(d);
  return 0;
}

static void tree_traverse(const char *base_path, int depth)
{
  if (depth > 5) return;
  DIR *d = opendir(base_path);
  if (!d) return;

  struct dirent *entry;
  char sub_path[1024];
  struct stat st;
  while ((entry = readdir(d)) != NULL)
    {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
          continue;
        }
      for (int i = 0; i < depth; i++)
        {
          printf("│   ");
        }
      snprintf(sub_path, sizeof(sub_path), "%s/%s", base_path, entry->d_name);
      if (stat(sub_path, &st) == 0 && S_ISDIR(st.st_mode))
        {
          printf("├── %s/\n", entry->d_name);
          tree_traverse(sub_path, depth + 1);
        }
      else
        {
          printf("├── %s\n", entry->d_name);
        }
    }
  closedir(d);
}

static int cmd_tree(int argc, char **argv)
{
  char path[512];
  if (argc > 1)
    {
      resolve_path(argv[1], path, sizeof(path));
    }
  else
    {
      strncpy(path, s_cwd, sizeof(path) - 1);
      path[sizeof(path) - 1] = '\0';
    }

  printf("%s\n", path);
  tree_traverse(path, 0);
  return 0;
}

static int cmd_cat(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: cat <filename>\n");
      return 1;
    }

  char full_path[512];
  resolve_path(argv[1], full_path, sizeof(full_path));

  FILE *f = fopen(full_path, "r");
  if (!f)
    {
      printf("cat: %s: No such file\n", full_path);
      return 1;
    }

  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
      fwrite(buf, 1, n, stdout);
    }
  printf("\n");
  fclose(f);
  return 0;
}

static int cmd_write(int argc, char **argv)
{
  if (argc < 3)
    {
      printf("Usage: write <filename> <text>\n");
      return 1;
    }

  char full_path[512];
  resolve_path(argv[1], full_path, sizeof(full_path));

  FILE *f = fopen(full_path, "w");
  if (!f)
    {
      printf("write: cannot open %s for writing\n", full_path);
      return 1;
    }

  for (int i = 2; i < argc; i++)
    {
      fputs(argv[i], f);
      if (i < argc - 1) fputc(' ', f);
    }
  fputc('\n', f);
  fclose(f);
  printf("Wrote to %s\n", full_path);
  return 0;
}

static int cmd_cp(int argc, char **argv)
{
  if (argc < 3)
    {
      printf("Usage: cp <source> <destination>\n");
      return 1;
    }

  char src[512];
  char dst[512];
  resolve_path(argv[1], src, sizeof(src));
  resolve_path(argv[2], dst, sizeof(dst));

  struct stat st;
  if (stat(dst, &st) == 0 && S_ISDIR(st.st_mode))
    {
      char *slash = strrchr(src, '/');
      const char *basename = slash ? slash + 1 : src;
      char dst_combined[1024];
      snprintf(dst_combined, sizeof(dst_combined), "%s/%s", dst, basename);
      strncpy(dst, dst_combined, sizeof(dst) - 1);
      dst[sizeof(dst) - 1] = '\0';
    }

  FILE *in = fopen(src, "rb");
  if (!in)
    {
      printf("cp: cannot open source file %s\n", src);
      return 1;
    }

  FILE *out = fopen(dst, "wb");
  if (!out)
    {
      printf("cp: cannot open destination file %s\n", dst);
      fclose(in);
      return 1;
    }

  char buf[512];
  size_t n;
  size_t total = 0;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
      fwrite(buf, 1, n, out);
      total += n;
    }
  fclose(in);
  fclose(out);
  printf("Copied %zu bytes from %s to %s\n", total, src, dst);
  return 0;
}

static int cmd_rm(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: rm <filename>\n");
      return 1;
    }

  char full_path[512];
  resolve_path(argv[1], full_path, sizeof(full_path));

  if (unlink(full_path) == 0)
    {
      printf("Removed %s\n", full_path);
      return 0;
    }
  printf("rm: cannot remove %s\n", full_path);
  return 1;
}

static int cmd_mkdir(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: mkdir <dirname>\n");
      return 1;
    }

  char full_path[512];
  resolve_path(argv[1], full_path, sizeof(full_path));

  if (mkdir(full_path, 0755) == 0)
    {
      printf("Created directory %s\n", full_path);
      return 0;
    }
  printf("mkdir: failed to create %s\n", full_path);
  return 1;
}

static int cmd_df(int argc, char **argv)
{
  printf("Filesystem      Size        Used       Available  Mounted on\n");

  /* FATFS on SD Card */
  uint64_t total_sd = 0, free_sd = 0;
  if (esp_vfs_fat_info(SDCARD_MOUNT_POINT, &total_sd, &free_sd) == ESP_OK)
    {
      uint64_t used_sd = total_sd - free_sd;
      printf("fatfs (SD)      %-10lu MB %-10lu MB %-10lu MB %s\n",
             (unsigned long)(total_sd / (1024 * 1024)),
             (unsigned long)(used_sd / (1024 * 1024)),
             (unsigned long)(free_sd / (1024 * 1024)),
             SDCARD_MOUNT_POINT);
    }

  /* LittleFS on Internal Flash */
  size_t total_lfs = 0, used_lfs = 0;
  if (esp_littlefs_info("storage", &total_lfs, &used_lfs) == ESP_OK)
    {
      size_t free_lfs = total_lfs - used_lfs;
      printf("littlefs        %-10u KB %-10u KB %-10u KB %s\n",
             (unsigned)(total_lfs / 1024),
             (unsigned)(used_lfs / 1024),
             (unsigned)(free_lfs / 1024),
             LITTLEFS_MOUNT_POINT);
    }

  return 0;
}

static const char *task_state_name(eTaskState s)
{
  switch (s)
    {
      case eRunning:   return "RUNNING";
      case eReady:     return "READY";
      case eBlocked:   return "BLOCKED";
      case eSuspended: return "SUSPEND";
      case eDeleted:   return "DELETED";
      default:         return "UNKNOWN";
    }
}

static int cmd_ps(int argc, char **argv)
{
  UBaseType_t task_count = uxTaskGetNumberOfTasks();
  TaskStatus_t *status_array = malloc(task_count * sizeof(TaskStatus_t));
  if (!status_array)
    {
      printf("ps: out of memory\n");
      return 1;
    }

  task_count = uxTaskGetSystemState(status_array, task_count, NULL);

  printf("%-4s  %-16s  %-8s  %-4s  %-8s  %-5s\n",
         "PID", "NAME", "STATE", "PRIO", "STACK-HW", "CORE");
  printf("----  ----------------  --------  ----  --------  -----\n");

  for (UBaseType_t i = 0; i < task_count; i++)
    {
      char core_str[8] = "Any";
      BaseType_t core = xTaskGetCoreID(status_array[i].xHandle);
      if (core == 0)
        {
          snprintf(core_str, sizeof(core_str), "0");
        }
      else if (core == 1)
        {
          snprintf(core_str, sizeof(core_str), "1");
        }
      printf("%-4u  %-16s  %-8s  %-4u  %-8u  %-5s\n",
             (unsigned)status_array[i].xTaskNumber,
             status_array[i].pcTaskName,
             task_state_name(status_array[i].eCurrentState),
             (unsigned)status_array[i].uxCurrentPriority,
             (unsigned)status_array[i].usStackHighWaterMark,
             core_str);
    }

  free(status_array);
  return 0;
}

static int cmd_top(int argc, char **argv)
{
  UBaseType_t task_count1 = uxTaskGetNumberOfTasks();
  TaskStatus_t *status1 = malloc(task_count1 * sizeof(TaskStatus_t));
  uint32_t total_time1 = 0;
  if (!status1)
    {
      printf("top: out of memory\n");
      return 1;
    }
  task_count1 = uxTaskGetSystemState(status1, task_count1, &total_time1);

  /* Sample over 500 ms */
  vTaskDelay(pdMS_TO_TICKS(500));

  UBaseType_t task_count2 = uxTaskGetNumberOfTasks();
  TaskStatus_t *status2 = malloc(task_count2 * sizeof(TaskStatus_t));
  uint32_t total_time2 = 0;
  if (!status2)
    {
      free(status1);
      printf("top: out of memory\n");
      return 1;
    }
  task_count2 = uxTaskGetSystemState(status2, task_count2, &total_time2);

  uint32_t delta_time = total_time2 - total_time1;
  if (delta_time == 0) delta_time = 1;
#if (configNUMBER_OF_CORES > 1)
  uint32_t total_capacity = delta_time * configNUMBER_OF_CORES;
#else
  uint32_t total_capacity = delta_time;
#endif
  if (total_capacity == 0) total_capacity = 1;

  typedef struct {
    UBaseType_t pid;
    char name[configMAX_TASK_NAME_LEN];
    eTaskState state;
    UBaseType_t prio;
    UBaseType_t stack_hw;
    char core[8];
    float cpu_pct;
  } top_item_t;

  top_item_t *items = malloc(task_count2 * sizeof(top_item_t));
  if (!items)
    {
      free(status1);
      free(status2);
      printf("top: out of memory\n");
      return 1;
    }

  float idle_pct = 0.0f;
  for (UBaseType_t i = 0; i < task_count2; i++)
    {
      items[i].pid = status2[i].xTaskNumber;
      strncpy(items[i].name, status2[i].pcTaskName, sizeof(items[i].name) - 1);
      items[i].name[sizeof(items[i].name) - 1] = '\0';
      items[i].state = status2[i].eCurrentState;
      items[i].prio = status2[i].uxCurrentPriority;
      items[i].stack_hw = status2[i].usStackHighWaterMark;

      strncpy(items[i].core, "Any", sizeof(items[i].core));
      BaseType_t core = xTaskGetCoreID(status2[i].xHandle);
      if (core == 0)
        {
          snprintf(items[i].core, sizeof(items[i].core), "0");
        }
      else if (core == 1)
        {
          snprintf(items[i].core, sizeof(items[i].core), "1");
        }

      uint32_t task_time1 = 0;
      for (UBaseType_t j = 0; j < task_count1; j++)
        {
          if (status1[j].xHandle == status2[i].xHandle)
            {
              task_time1 = status1[j].ulRunTimeCounter;
              break;
            }
        }
      uint32_t task_delta = status2[i].ulRunTimeCounter - task_time1;
      items[i].cpu_pct = ((float)task_delta * 100.0f) / (float)total_capacity;
      if (strncmp(items[i].name, "IDLE", 4) == 0)
        {
          idle_pct += items[i].cpu_pct;
        }
    }

  /* Sort items descending by cpu_pct */
  for (UBaseType_t i = 0; i < task_count2; i++)
    {
      for (UBaseType_t j = i + 1; j < task_count2; j++)
        {
          if (items[j].cpu_pct > items[i].cpu_pct)
            {
              top_item_t tmp = items[i];
              items[i] = items[j];
              items[j] = tmp;
            }
        }
    }

  float active_cpu = 100.0f - idle_pct;
  if (active_cpu < 0.0f) active_cpu = 0.0f;
  if (active_cpu > 100.0f) active_cpu = 100.0f;

  printf("Tasks: %u total | Free Heap: %lu B | Min Free: %lu B\n",
         (unsigned)task_count2, (unsigned long)esp_get_free_heap_size(),
         (unsigned long)esp_get_minimum_free_heap_size());
  printf("CPU Total Active Load: %.1f%%\n\n", active_cpu);
  printf("%-4s  %-16s  %-8s  %-4s  %-6s  %-8s  %-5s\n",
         "PID", "NAME", "STATE", "PRIO", "CPU%", "STACK-HW", "CORE");
  printf("----  ----------------  --------  ----  ------  --------  -----\n");

  for (UBaseType_t i = 0; i < task_count2; i++)
    {
      printf("%-4u  %-16s  %-8s  %-4u  %5.1f%%  %-8u  %-5s\n",
             (unsigned)items[i].pid,
             items[i].name,
             task_state_name(items[i].state),
             (unsigned)items[i].prio,
             items[i].cpu_pct,
             (unsigned)items[i].stack_hw,
             items[i].core);
    }
  printf("\n");

  free(items);
  free(status1);
  free(status2);
  return 0;
}

static int cmd_free(int argc, char **argv)
{
  printf("%-17s %10s %10s %10s %10s\n", "Memory Tier", "Total", "Free", "Min Free", "Max Block");
  printf("%-17s %10s %10s %10s %10s\n", "-----------", "-----", "----", "--------", "---------");

  struct {
    const char *name;
    uint32_t caps;
  } tiers[] = {
    {"Internal SRAM",   MALLOC_CAP_INTERNAL},
    {"DMA-capable",     MALLOC_CAP_DMA},
    {"SPI RAM (PSRAM)", MALLOC_CAP_SPIRAM},
  };

  for (size_t i = 0; i < sizeof(tiers) / sizeof(tiers[0]); i++)
    {
      size_t total = heap_caps_get_total_size(tiers[i].caps);
      size_t free_sz = heap_caps_get_free_size(tiers[i].caps);
      size_t min_free = heap_caps_get_minimum_free_size(tiers[i].caps);
      size_t max_block = heap_caps_get_largest_free_block(tiers[i].caps);

      if (total > 0 || free_sz > 0 || tiers[i].caps == MALLOC_CAP_SPIRAM)
        {
          printf("%-17s %10zu %10zu %10zu %10zu\n",
                 tiers[i].name, total, free_sz, min_free, max_block);
        }
    }

  printf("\nOverall System Allocator:\n");
  printf("  Total free:       %10lu bytes\n", (unsigned long)esp_get_free_heap_size());
  printf("  Minimum free:     %10lu bytes\n", (unsigned long)esp_get_minimum_free_heap_size());
  printf("  Largest block:    %10zu bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

  if (esp_psram_is_initialized())
    {
      printf("  PSRAM Physical:   %10zu bytes (32 MB)\n", esp_psram_get_size());
    }
  else
    {
      printf("  PSRAM Physical:   Disabled / Not initialized\n");
    }

  return 0;
}

static int cmd_uname(int argc, char **argv)
{
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  printf("ESP-IDF v%s (FreeRTOS) chip rev v%d.%d, %d cores\n",
         esp_get_idf_version(),
         chip_info.revision / 100,
         chip_info.revision % 100,
         chip_info.cores);
  return 0;
}

static int cmd_reboot(int argc, char **argv)
{
  printf("Restarting system...\n");
  esp_restart();
  return 0;
}

static int cmd_reset_c6(int argc, char **argv)
{
  gpio_config_t io_conf = {
      .pin_bit_mask = 1ULL << PIN_C6_RESET,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);

  printf("Asserting C6 reset (GPIO %d LOW)...\n", PIN_C6_RESET);
  gpio_set_level(PIN_C6_RESET, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  printf("Releasing C6 reset (GPIO %d HIGH)...\n", PIN_C6_RESET);
  gpio_set_level(PIN_C6_RESET, 1);
  return 0;
}

static int cmd_nano(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: nano <filename> (or edit <filename>)\n");
      return 1;
    }

  char full_path[512];
  resolve_path(argv[1], full_path, sizeof(full_path));
  return editor_open(full_path);
}

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

static int cmd_wifi_scan(int argc, char **argv)
{
  if (!s_wifi_initialized)
    {
      printf("wifi_scan: Wi-Fi stack not initialized\n");
      return 1;
    }

  printf("Scanning Wi-Fi networks...\n");
  esp_err_t err = esp_wifi_scan_start(NULL, true);
  if (err != ESP_OK)
    {
      printf("wifi_scan: scan failed (%s)\n", esp_err_to_name(err));
      return 1;
    }

  uint16_t found = 0;
  esp_wifi_scan_get_ap_num(&found);
  uint16_t count = found > 20 ? 20 : found;
  wifi_ap_record_t *records = malloc(count * sizeof(wifi_ap_record_t));
  if (!records)
    {
      printf("wifi_scan: out of memory\n");
      return 1;
    }

  esp_wifi_scan_get_ap_records(&count, records);
  printf("Found %u networks (showing %u):\n", (unsigned)found, (unsigned)count);
  printf("  %-32s  %-4s  %-8s  %-10s\n", "SSID", "CHAN", "RSSI", "AUTH");
  printf("  --------------------------------  ----  --------  ----------\n");
  for (uint16_t i = 0; i < count; i++)
    {
      printf("  %-32s  %4d  %4d dBm  %s\n",
             (const char *)records[i].ssid,
             records[i].primary,
             records[i].rssi,
             auth_name(records[i].authmode));
    }
  free(records);
  return 0;
}

static int cmd_wifi_connect(int argc, char **argv)
{
  if (!s_wifi_initialized)
    {
      printf("wifi_connect: Wi-Fi stack not initialized\n");
      return 1;
    }

  if (argc < 2)
    {
      printf("Usage: wifi_connect <ssid> [password]\n");
      return 1;
    }

  wifi_config_t wifi_cfg = {0};
  strncpy((char *)wifi_cfg.sta.ssid, argv[1], sizeof(wifi_cfg.sta.ssid) - 1);
  if (argc >= 3)
    {
      strncpy((char *)wifi_cfg.sta.password, argv[2], sizeof(wifi_cfg.sta.password) - 1);
    }

  esp_wifi_disconnect();
  s_wifi_connected = false;
  esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

  printf("Connecting to %s...\n", argv[1]);
  esp_wifi_connect();

  if (xSemaphoreTake(s_ip_sem, pdMS_TO_TICKS(15000)) == pdTRUE)
    {
      esp_netif_ip_info_t ip_info;
      esp_netif_get_ip_info(s_sta_netif, &ip_info);
      printf("Connected successfully! IP: " IPSTR ", Gateway: " IPSTR "\n",
             IP2STR(&ip_info.ip), IP2STR(&ip_info.gw));
      return 0;
    }
  else
    {
      printf("Connection timed out.\n");
      return 1;
    }
}

static int cmd_wifi_disconnect(int argc, char **argv)
{
  if (!s_wifi_initialized)
    {
      printf("wifi_disconnect: Wi-Fi stack not initialized\n");
      return 1;
    }
  esp_wifi_disconnect();
  s_wifi_connected = false;
  printf("Disconnected from Wi-Fi\n");
  return 0;
}

static int cmd_ifconfig(int argc, char **argv)
{
  if (!s_wifi_initialized || !s_sta_netif)
    {
      printf("ifconfig: interface not initialized\n");
      return 1;
    }

  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(s_sta_netif, &ip_info);

  uint8_t mac[6] = {0};
  esp_wifi_get_mac(WIFI_IF_STA, mac);

  printf("wlan0: flags=%s\n", s_wifi_connected ? "UP,CONNECTED" : "DOWN");
  printf("       inet " IPSTR " netmask " IPSTR " broadcast " IPSTR "\n",
         IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));
  return 0;
}

static void ping_on_success(esp_ping_handle_t hdl, void *args)
{
  uint8_t ttl;
  uint16_t seqno;
  uint32_t elapsed_time, recv_len;
  ip_addr_t target_addr;
  esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
  esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
  esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
  printf("%lu bytes from %s: icmp_seq=%u ttl=%u time=%lu ms\n",
         (unsigned long)recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, (unsigned long)elapsed_time);
}

static void ping_on_timeout(esp_ping_handle_t hdl, void *args)
{
  uint16_t seqno;
  ip_addr_t target_addr;
  esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
  printf("From %s icmp_seq=%u Request timeout\n",
         inet_ntoa(target_addr.u_addr.ip4), seqno);
}

static void ping_on_end(esp_ping_handle_t hdl, void *args)
{
  SemaphoreHandle_t sem = (SemaphoreHandle_t)args;
  uint32_t transmitted = 0, received = 0, total_time_ms = 0;
  esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
  esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
  esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));

  printf("--- ping statistics ---\n");
  printf("%lu packets transmitted, %lu received, %d%% packet loss, time %lu ms\n",
         (unsigned long)transmitted, (unsigned long)received,
         transmitted > 0 ? (int)((transmitted - received) * 100 / transmitted) : 0,
         (unsigned long)total_time_ms);
  esp_ping_delete_session(hdl);
  if (sem) xSemaphoreGive(sem);
}

static int cmd_ping(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: ping <host_or_ip>\n");
      return 1;
    }

  ip_addr_t target_addr;
  struct addrinfo hint = { .ai_family = AF_INET };
  struct addrinfo *res = NULL;
  if (getaddrinfo(argv[1], NULL, &hint, &res) != 0 || res == NULL)
    {
      printf("ping: cannot resolve host %s\n", argv[1]);
      return 1;
    }

  struct sockaddr_in *saddr = (struct sockaddr_in *)res->ai_addr;
  inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &saddr->sin_addr);
  target_addr.type = IPADDR_TYPE_V4;
  freeaddrinfo(res);

  SemaphoreHandle_t sem = xSemaphoreCreateBinary();
  esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
  ping_config.target_addr = target_addr;
  ping_config.count = 4;

  esp_ping_callbacks_t cbs = {
      .cb_args = sem,
      .on_ping_success = ping_on_success,
      .on_ping_timeout = ping_on_timeout,
      .on_ping_end = ping_on_end,
  };

  esp_ping_handle_t ping_hdl;
  if (esp_ping_new_session(&ping_config, &cbs, &ping_hdl) != ESP_OK)
    {
      printf("ping: failed to create ping session\n");
      vSemaphoreDelete(sem);
      return 1;
    }

  printf("PING %s (%s): %lu data bytes\n",
         argv[1], inet_ntoa(target_addr.u_addr.ip4), (unsigned long)ping_config.data_size);
  esp_ping_start(ping_hdl);
  xSemaphoreTake(sem, portMAX_DELAY);
  vSemaphoreDelete(sem);
  return 0;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {
      printf("\n[Wi-Fi] Connected to AP, negotiating DHCP lease...\n");
    }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
      s_wifi_connected = false;
      printf("\n[Wi-Fi] Disconnected\n");
    }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      printf("\n[Wi-Fi] Acquired IP address: " IPSTR "\n", IP2STR(&event->ip_info.ip));
      s_wifi_connected = true;
      if (s_ip_sem)
        {
          xSemaphoreGive(s_ip_sem);
        }
    }
}

static void init_wifi(void)
{
  s_ip_sem = xSemaphoreCreateBinary();
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
      nvs_flash_erase();
      err = nvs_flash_init();
    }
  if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "NVS flash init failed (%s)", esp_err_to_name(err));
      return;
    }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  s_sta_netif = esp_netif_create_default_wifi_sta();

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &wifi_event_handler, NULL, NULL);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &wifi_event_handler, NULL, NULL);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "esp_wifi_init failed: %s (SDIO link to C6 not ready)", esp_err_to_name(err));
      return;
    }

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  s_wifi_initialized = true;
  ESP_LOGI(TAG, "Wi-Fi radio is up and ready");
}

/* Intercept write calls from linenoise to drop cursor position query (\x1b[6n).
 * On serial UART links, querying terminal dimensions with DSR escape sequences
 * causes terminals to send cursor reports that pollute the input buffer or throw
 * the cursor to margin 999. Returning -1 here forces linenoise to immediately
 * default to 80 columns without sending escape sequences or blocking UART.
 */
extern ssize_t __real_write(int fd, const void *buf, size_t count);

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
  if (count >= 4 && ((const char *)buf)[0] == '\x1b' && ((const char *)buf)[1] == '[' &&
      ((const char *)buf)[2] == '6' && ((const char *)buf)[3] == 'n')
    {
      return -1;
    }
  return __real_write(fd, buf, count);
}

static void custom_completion(const char *buf, linenoiseCompletions *lc)
{
  if (!buf) return;

  /* If command has no space yet, complete command names */
  if (strchr(buf, ' ') == NULL)
    {
      esp_console_get_completion(buf, lc);
      return;
    }

  /* Command has a space: complete file path argument */
  const char *last_space = strrchr(buf, ' ');
  const char *arg = last_space + 1;

  char prefix_cmd[512];
  size_t pfx_len = arg - buf;
  if (pfx_len >= sizeof(prefix_cmd)) return;
  strncpy(prefix_cmd, buf, pfx_len);
  prefix_cmd[pfx_len] = '\0';

  char dir_to_open[512];
  char match_prefix[128] = "";

  const char *last_slash = strrchr(arg, '/');
  if (last_slash)
    {
      char dir_part[512];
      size_t dlen = last_slash - arg;
      if (dlen == 0)
        {
          strncpy(dir_part, "/", sizeof(dir_part));
        }
      else
        {
          strncpy(dir_part, arg, dlen);
          dir_part[dlen] = '\0';
        }
      resolve_path(dir_part, dir_to_open, sizeof(dir_to_open));
      strncpy(match_prefix, last_slash + 1, sizeof(match_prefix) - 1);
    }
  else
    {
      strncpy(dir_to_open, s_cwd, sizeof(dir_to_open) - 1);
      strncpy(match_prefix, arg, sizeof(match_prefix) - 1);
    }

  /* Root '/' is not a physical directory in ESP-IDF VFS; supply mounted filesystems */
  if (strcmp(dir_to_open, "/") == 0)
    {
      const char *mounts[] = { "sdcard", "data" };
      size_t match_len = strlen(match_prefix);
      for (size_t m = 0; m < sizeof(mounts) / sizeof(mounts[0]); m++)
        {
          if (strncmp(mounts[m], match_prefix, match_len) == 0)
            {
              char completion[1536];
              snprintf(completion, sizeof(completion), "%s/%s/", prefix_cmd, mounts[m]);
              linenoiseAddCompletion(lc, completion);
            }
        }
      return;
    }

  DIR *d = opendir(dir_to_open);
  if (!d) return;

  struct dirent *de;
  size_t match_len = strlen(match_prefix);
  while ((de = readdir(d)) != NULL)
    {
      if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
      if (strncmp(de->d_name, match_prefix, match_len) == 0)
        {
          char completion[1536];
          char path_prefix[512] = "";
          if (last_slash)
            {
              size_t dlen = last_slash - arg + 1;
              strncpy(path_prefix, arg, dlen);
              path_prefix[dlen] = '\0';
            }
          snprintf(completion, sizeof(completion), "%s%s%s", prefix_cmd, path_prefix, de->d_name);

          /* Check if directory to append '/' */
          char full_test[1024];
          snprintf(full_test, sizeof(full_test), "%s/%s", dir_to_open, de->d_name);
          struct stat st;
          if (stat(full_test, &st) == 0 && S_ISDIR(st.st_mode))
            {
              strncat(completion, "/", sizeof(completion) - strlen(completion) - 1);
            }

          linenoiseAddCompletion(lc, completion);
        }
    }
  closedir(d);
}

static void register_commands(void)
{
  esp_console_register_help_command();

  const esp_console_cmd_t cmds[] = {
      {
          .command = "reset_c6",
          .help = "Pulse hardware reset line (GPIO 54) to ESP32-C6",
          .hint = NULL,
          .func = &cmd_reset_c6,
      },
      {
          .command = "top",
          .help = "Show live FreeRTOS tasks and CPU usage",
          .hint = NULL,
          .func = &cmd_top,
      },
      {
          .command = "pwd",
          .help = "Print current working directory",
          .hint = NULL,
          .func = &cmd_pwd,
      },
      {
          .command = "cd",
          .help = "Change working directory",
          .hint = "[<path>]",
          .func = &cmd_cd,
      },
      {
          .command = "ls",
          .help = "List files in directory",
          .hint = "[<path>]",
          .func = &cmd_ls,
      },
      {
          .command = "tree",
          .help = "List directory contents in a tree structure",
          .hint = "[<path>]",
          .func = &cmd_tree,
      },
      {
          .command = "cat",
          .help = "Print file contents",
          .hint = "<filename>",
          .func = &cmd_cat,
      },
      {
          .command = "write",
          .help = "Write text to file",
          .hint = "<filename> <text>",
          .func = &cmd_write,
      },
      {
          .command = "nano",
          .help = "Interactive terminal text editor",
          .hint = "<filename>",
          .func = &cmd_nano,
      },
      {
          .command = "edit",
          .help = "Alias for nano",
          .hint = "<filename>",
          .func = &cmd_nano,
      },
      {
          .command = "cp",
          .help = "Copy file",
          .hint = "<src> <dst>",
          .func = &cmd_cp,
      },
      {
          .command = "rm",
          .help = "Delete a file",
          .hint = "<filename>",
          .func = &cmd_rm,
      },
      {
          .command = "mkdir",
          .help = "Create a directory",
          .hint = "<dirname>",
          .func = &cmd_mkdir,
      },
      {
          .command = "df",
          .help = "Show filesystem usage",
          .hint = NULL,
          .func = &cmd_df,
      },
      {
          .command = "wifi_scan",
          .help = "Scan for Wi-Fi access points",
          .hint = NULL,
          .func = &cmd_wifi_scan,
      },
      {
          .command = "wifi_connect",
          .help = "Connect to Wi-Fi access point",
          .hint = "<ssid> [password]",
          .func = &cmd_wifi_connect,
      },
      {
          .command = "wifi_disconnect",
          .help = "Disconnect from Wi-Fi",
          .hint = NULL,
          .func = &cmd_wifi_disconnect,
      },
      {
          .command = "ifconfig",
          .help = "Show network interface configuration",
          .hint = NULL,
          .func = &cmd_ifconfig,
      },
      {
          .command = "ping",
          .help = "Send ICMP echo requests to network host",
          .hint = "<host_or_ip>",
          .func = &cmd_ping,
      },
      {
          .command = "ps",
          .help = "Show FreeRTOS tasks",
          .hint = NULL,
          .func = &cmd_ps,
      },
      {
          .command = "free",
          .help = "Show available heap memory",
          .hint = NULL,
          .func = &cmd_free,
      },
      {
          .command = "uname",
          .help = "Show OS and chip information",
          .hint = NULL,
          .func = &cmd_uname,
      },
      {
          .command = "reboot",
          .help = "Reboot the chip",
          .hint = NULL,
          .func = &cmd_reboot,
      },
  };

  for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
    {
      esp_console_cmd_register(&cmds[i]);
    }
}

void app_main(void)
{
  ESP_LOGI(TAG, "Starting ESP32-P4 workstation firmware");

  /* 1. Initialize MicroSD Card */
  power_on_slot();

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sd_pwr_ctrl_ldo_config_t ldo_config = {
      .ldo_chan_id = 4,
  };
  sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
  esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
  if (ret != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to create LDO power control handle (%s)", esp_err_to_name(ret));
    }
  else
    {
      host.pwr_ctrl_handle = pwr_ctrl_handle;
    }

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 4;
  slot_config.clk = PIN_TF_CLK;
  slot_config.cmd = PIN_TF_CMD;
  slot_config.d0 = PIN_TF_D0;
  slot_config.d1 = PIN_TF_D1;
  slot_config.d2 = PIN_TF_D2;
  slot_config.d3 = PIN_TF_D3;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024,
  };

  sdmmc_card_t *card = NULL;
  ret = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
  if (ret == ESP_OK)
    {
      ESP_LOGI(TAG, "MicroSD card mounted at %s", SDCARD_MOUNT_POINT);
    }
  else
    {
      ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
    }

  /* 2. Initialize LittleFS on Internal Flash */
  esp_vfs_littlefs_conf_t lfs_conf = {
      .base_path = LITTLEFS_MOUNT_POINT,
      .partition_label = "storage",
      .format_if_mount_failed = true,
      .dont_mount = false,
  };
  ret = esp_vfs_littlefs_register(&lfs_conf);
  if (ret == ESP_OK)
    {
      ESP_LOGI(TAG, "LittleFS mounted at %s", LITTLEFS_MOUNT_POINT);
    }
  else
    {
      ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
    }

  /* 3. Initialize Wi-Fi via onboard ESP32-C6 */
  init_wifi();

  /* 4. Initialize Interactive REPL Console */
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = "nsh> ";
  repl_config.max_cmdline_length = 256;
  repl_config.task_stack_size = 8192;

  esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

  /* Force enable raw mode for readline, arrow keys, history, and tab completion */
  linenoiseSetDumbMode(0);
  linenoiseSetCompletionCallback(&custom_completion);

  register_commands();

  printf("\n");
  printf("====================================================\n");
  printf("  ESP32-P4 Workstation Shell (ESP-IDF v%s)\n", esp_get_idf_version());
  printf("  Storage: /sdcard (FATFS), /data (LittleFS)\n");
  printf("  Network: Wi-Fi STA via ESP32-C6 coprocessor\n");
  printf("  Editor:  nano <file> / edit <file>\n");
  printf("  Type 'help' to list available commands\n");
  printf("====================================================\n\n");

  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
