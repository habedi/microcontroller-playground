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
#include <stdio.h>
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

static int cmd_ls(int argc, char **argv)
{
  const char *path = (argc > 1) ? argv[1] : MOUNT_POINT;
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
      char full_path[300];
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

static int cmd_cat(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: cat <filename>\n");
      return 1;
    }

  const char *path = argv[1];
  char full_path[300];
  if (path[0] != '/')
    {
      snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, path);
      path = full_path;
    }

  FILE *f = fopen(path, "r");
  if (!f)
    {
      printf("cat: failed to open '%s'\n", path);
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

  const char *path = argv[1];
  char full_path[300];
  if (path[0] != '/')
    {
      snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, path);
      path = full_path;
    }

  FILE *f = fopen(path, "w");
  if (!f)
    {
      printf("write: failed to open '%s'\n", path);
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
  printf("Wrote to %s\n", path);
  return 0;
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

static int cmd_rm(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: rm <filename>\n");
      return 1;
    }

  const char *path = argv[1];
  char full_path[300];
  if (path[0] != '/')
    {
      snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, path);
      path = full_path;
    }

  if (unlink(path) == 0)
    {
      printf("Removed %s\n", path);
      return 0;
    }
  printf("rm: failed to remove %s\n", path);
  return 1;
}

static int cmd_mkdir(int argc, char **argv)
{
  if (argc < 2)
    {
      printf("Usage: mkdir <dirname>\n");
      return 1;
    }

  const char *path = argv[1];
  char full_path[300];
  if (path[0] != '/')
    {
      snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, path);
      path = full_path;
    }

  if (mkdir(path, 0777) == 0)
    {
      printf("Created directory %s\n", path);
      return 0;
    }
  printf("mkdir: failed to create %s\n", path);
  return 1;
}

static int cmd_reboot(int argc, char **argv)
{
  printf("Restarting system...\n");
  esp_restart();
  return 0;
}

static void register_commands(void)
{
  esp_console_register_help_command();

  const esp_console_cmd_t cmds[] = {
      {
          .command = "ls",
          .help = "List files in directory (default: /sdcard)",
          .hint = "[<path>]",
          .func = &cmd_ls,
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
          .command = "df",
          .help = "Show filesystem usage",
          .hint = NULL,
          .func = &cmd_df,
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
