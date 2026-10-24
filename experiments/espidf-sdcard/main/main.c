/* SDMMC MicroSD storage with interactive NSH-compatible console on ESP32-P4.
 *
 * Hardware configuration:
 *   - On-chip LDO channel 4 supplies power to the ESP32-P4 SDMMC IO pad domain.
 *   - GPIO 45 drives the onboard MicroSD socket power switch (active low).
 *   - Host slot 0 is the native SDMMC controller for:
 *       CLK:  GPIO 43
 *       CMD:  GPIO 44
 *       D0:   GPIO 39
 *       D1:   GPIO 40
 *       D2:   GPIO 41
 *       D3:   GPIO 42
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
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

#define MOUNT_POINT "/sdcard"

static const char *TAG = "espidf_sdcard";

static char s_cwd[256] = MOUNT_POINT;

static void power_on_slot(void)
{
  ESP_LOGI(TAG, "Enabling slot power switch on GPIO %d (active low)", PIN_TF_PWR);

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
          /* Current dir, ignore */
        }
      else if (strcmp(tok, "..") == 0)
        {
          if (count > 0)
            {
              count--;
            }
        }
      else if (tok[0] != '\0')
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
  char target[256];
  if (argc < 2)
    {
      resolve_path(MOUNT_POINT, target, sizeof(target));
    }
  else
    {
      resolve_path(argv[1], target, sizeof(target));
    }

  if (strcmp(target, "/") == 0)
    {
      strncpy(s_cwd, "/", sizeof(s_cwd) - 1);
      s_cwd[sizeof(s_cwd) - 1] = '\0';
      return 0;
    }

  DIR *d = opendir(target);
  if (!d)
    {
      printf("cd: %s: No such directory\n", target);
      return 1;
    }
  closedir(d);

  strncpy(s_cwd, target, sizeof(s_cwd) - 1);
  s_cwd[sizeof(s_cwd) - 1] = '\0';
  return 0;
}

static int cmd_ls(int argc, char **argv)
{
  char path[256];
  resolve_path(argc > 1 ? argv[1] : s_cwd, path, sizeof(path));

  if (strcmp(path, "/") == 0)
    {
      printf("Directory: /\n");
      printf("  <DIR>         sdcard\n");
      return 0;
    }

  DIR *dir = opendir(path);
  if (!dir)
    {
      printf("ls: cannot open directory '%s'\n", path);
      return 1;
    }

  printf("Directory: %s\n", path);
  struct dirent *de;
  while ((de = readdir(dir)) != NULL)
    {
      char full_path[512];
      snprintf(full_path, sizeof(full_path), "%s/%s", path, de->d_name);
      struct stat st;
      if (stat(full_path, &st) == 0)
        {
          if (S_ISDIR(st.st_mode))
            {
              printf("  <DIR>         %s\n", de->d_name);
            }
          else
            {
              printf("  %10lu B  %s\n", (unsigned long)st.st_size, de->d_name);
            }
        }
      else
        {
          printf("  %s\n", de->d_name);
        }
    }
  closedir(dir);
  return 0;
}

static void tree_traverse(const char *dir_path, int depth, int *dir_count, int *file_count)
{
  if (depth > 6)
    {
      return;
    }

  DIR *dir = opendir(dir_path);
  if (!dir)
    {
      return;
    }

  struct dirent *de;
  while ((de = readdir(dir)) != NULL)
    {
      if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
        {
          continue;
        }

      char sub_path[512];
      snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, de->d_name);

      struct stat st;
      bool is_dir = false;
      if (stat(sub_path, &st) == 0 && S_ISDIR(st.st_mode))
        {
          is_dir = true;
        }

      for (int i = 0; i < depth; i++)
        {
          printf("  ");
        }

      if (is_dir)
        {
          printf("+-- %s/\n", de->d_name);
          (*dir_count)++;
          tree_traverse(sub_path, depth + 1, dir_count, file_count);
        }
      else
        {
          printf("|-- %s (%lu B)\n", de->d_name, (unsigned long)st.st_size);
          (*file_count)++;
        }
    }
  closedir(dir);
}

static int cmd_tree(int argc, char **argv)
{
  char target[256];
  resolve_path(argc > 1 ? argv[1] : s_cwd, target, sizeof(target));

  if (strcmp(target, "/") == 0)
    {
      printf("/\n");
      printf("+-- sdcard/\n");
      int dir_count = 1;
      int file_count = 0;
      tree_traverse(MOUNT_POINT, 1, &dir_count, &file_count);
      printf("\n%d directories, %d files\n", dir_count, file_count);
      return 0;
    }

  printf("%s\n", target);
  int dir_count = 0;
  int file_count = 0;
  tree_traverse(target, 0, &dir_count, &file_count);
  printf("\n%d directories, %d files\n", dir_count, file_count);
  return 0;
}

static int cmd_cat(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: cat <filename>\n");
      return 1;
    }

  char full_path[256];
  resolve_path(argv[1], full_path, sizeof(full_path));

  FILE *f = fopen(full_path, "r");
  if (!f)
    {
      printf("cat: failed to open '%s'\n", full_path);
      return 1;
    }

  char buf[128];
  size_t bytes;
  while ((bytes = fread(buf, 1, sizeof(buf) - 1, f)) > 0)
    {
      buf[bytes] = '\0';
      fputs(buf, stdout);
    }
  fclose(f);
  printf("\n");
  return 0;
}

static int cmd_write(int argc, char **argv)
{
  if (argc < 3)
    {
      printf("Usage: write <filename> <text>\n");
      return 1;
    }

  char full_path[256];
  resolve_path(argv[1], full_path, sizeof(full_path));

  FILE *f = fopen(full_path, "w");
  if (!f)
    {
      printf("write: failed to open '%s'\n", full_path);
      return 1;
    }

  for (int i = 2; i < argc; i++)
    {
      fputs(argv[i], f);
      if (i + 1 < argc)
        {
          fputc(' ', f);
        }
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

  char src[256];
  char dst[256];
  resolve_path(argv[1], src, sizeof(src));
  resolve_path(argv[2], dst, sizeof(dst));

  /* If dst is an existing directory, append src filename */
  DIR *d = opendir(dst);
  if (d)
    {
      closedir(d);
      const char *slash = strrchr(src, '/');
      const char *basename = slash ? slash + 1 : src;
      char new_dst[512];
      snprintf(new_dst, sizeof(new_dst), "%s/%s", dst, basename);
      strncpy(dst, new_dst, sizeof(dst) - 1);
      dst[sizeof(dst) - 1] = '\0';
    }

  FILE *fsrc = fopen(src, "rb");
  if (!fsrc)
    {
      printf("cp: cannot open source '%s'\n", src);
      return 1;
    }

  FILE *fdst = fopen(dst, "wb");
  if (!fdst)
    {
      printf("cp: cannot create destination '%s'\n", dst);
      fclose(fsrc);
      return 1;
    }

  char buf[512];
  size_t n;
  size_t total = 0;
  while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0)
    {
      fwrite(buf, 1, n, fdst);
      total += n;
    }
  fclose(fsrc);
  fclose(fdst);
  printf("Copied %lu bytes to %s\n", (unsigned long)total, dst);
  return 0;
}

static int cmd_rm(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: rm <filename>\n");
      return 1;
    }

  char full_path[256];
  resolve_path(argv[1], full_path, sizeof(full_path));

  if (unlink(full_path) == 0)
    {
      printf("Removed %s\n", full_path);
      return 0;
    }
  printf("rm: failed to remove %s\n", full_path);
  return 1;
}

static int cmd_mkdir(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: mkdir <dirname>\n");
      return 1;
    }

  char full_path[256];
  resolve_path(argv[1], full_path, sizeof(full_path));

  if (mkdir(full_path, 0777) == 0)
    {
      printf("Created directory %s\n", full_path);
      return 0;
    }
  printf("mkdir: failed to create %s\n", full_path);
  return 1;
}

static int cmd_df(int argc, char **argv)
{
  uint64_t total_bytes = 0;
  uint64_t free_bytes = 0;
  esp_err_t err = esp_vfs_fat_info(MOUNT_POINT, &total_bytes, &free_bytes);
  if (err != ESP_OK)
    {
      printf("df: failed to get filesystem statistics for %s (%s)\n",
             MOUNT_POINT, esp_err_to_name(err));
      return 1;
    }

  uint64_t used_bytes = total_bytes - free_bytes;

  printf("Filesystem      Size        Used       Available  Mounted on\n");
  printf("fatfs (SD)      %-10lu MB %-10lu MB %-10lu MB %s\n",
         (unsigned long)(total_bytes / (1024 * 1024)),
         (unsigned long)(used_bytes / (1024 * 1024)),
         (unsigned long)(free_bytes / (1024 * 1024)),
         MOUNT_POINT);
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
#if ( ( configUSE_CORE_AFFINITY == 1 ) && ( configNUMBER_OF_CORES > 1 ) )
      if (status_array[i].uxCoreAffinityMask == 1)
        {
          snprintf(core_str, sizeof(core_str), "0");
        }
      else if (status_array[i].uxCoreAffinityMask == 2)
        {
          snprintf(core_str, sizeof(core_str), "1");
        }
      else if (status_array[i].uxCoreAffinityMask == 3)
        {
          snprintf(core_str, sizeof(core_str), "0/1");
        }
#endif
      printf("%-4lu  %-16s  %-8s  %-4lu  %-8lu  %-5s\n",
             (unsigned long)status_array[i].xTaskNumber,
             status_array[i].pcTaskName,
             task_state_name(status_array[i].eCurrentState),
             (unsigned long)status_array[i].uxCurrentPriority,
             (unsigned long)status_array[i].usStackHighWaterMark,
             core_str);
    }

  free(status_array);
  return 0;
}

static int cmd_free(int argc, char **argv)
{
  printf("Free heap: %lu bytes\n", (unsigned long)esp_get_free_heap_size());
  printf("Minimum free heap: %lu bytes\n", (unsigned long)esp_get_minimum_free_heap_size());
  return 0;
}

static int cmd_uname(int argc, char **argv)
{
  esp_chip_info_t info;
  esp_chip_info(&info);
  printf("ESP-IDF %s (FreeRTOS) chip rev v%u.%u, %u cores\n",
         esp_get_idf_version(), info.revision / 100, info.revision % 100, info.cores);
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

  printf("Holding ESP32-C6 in reset (GPIO 54 = 0)...\n");
  gpio_set_level(PIN_C6_RESET, 0);
  vTaskDelay(pdMS_TO_TICKS(200));

  printf("Releasing ESP32-C6 from reset (GPIO 54 = 1)...\n");
  gpio_set_level(PIN_C6_RESET, 1);
  return 0;
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
  power_on_slot();

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024,
  };

  /* Initialize the on-chip LDO (channel 4) for ESP32-P4 SDMMC IO pad domain */
  sd_pwr_ctrl_ldo_config_t ldo_config = {
      .ldo_chan_id = 4,
  };
  sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
  esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
  if (ret != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to create on-chip LDO driver: %s", esp_err_to_name(ret));
    }

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  host.pwr_ctrl_handle = pwr_ctrl_handle;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.clk = PIN_TF_CLK;
  slot_config.cmd = PIN_TF_CMD;
  slot_config.d0 = PIN_TF_D0;
  slot_config.d1 = PIN_TF_D1;
  slot_config.d2 = PIN_TF_D2;
  slot_config.d3 = PIN_TF_D3;
  slot_config.d4 = GPIO_NUM_NC;
  slot_config.d5 = GPIO_NUM_NC;
  slot_config.d6 = GPIO_NUM_NC;
  slot_config.d7 = GPIO_NUM_NC;
  slot_config.cd = GPIO_NUM_NC;
  slot_config.wp = GPIO_NUM_NC;
  slot_config.width = 4;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  sdmmc_card_t *card = NULL;
  ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config,
                                &mount_config, &card);
  if (ret == ESP_OK)
    {
      ESP_LOGI(TAG, "SD card mounted at %s", MOUNT_POINT);
      sdmmc_card_print_info(stdout, card);
    }
  else
    {
      ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
    }

  /* Initialize the interactive REPL console with nsh> prompt */
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = "nsh> ";
  repl_config.max_cmdline_length = 256;
  repl_config.task_stack_size = 8192;

  esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

  register_commands();

  printf("\n");
  printf("====================================================\n");
  printf("  ESP32-P4 FreeRTOS Shell (ESP-IDF v%s)\n", esp_get_idf_version());
  printf("  SD card mounted at /sdcard (128 GB)\n");
  printf("  Type 'help' to list available commands\n");
  printf("====================================================\n\n");

  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
