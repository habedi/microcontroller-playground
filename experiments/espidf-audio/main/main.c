/*
 * ESP32-P4 Audio Studio, Shell, and MicroSD Recorder
 *
 * Provides an interactive console shell with real-time ASCII VU metering,
 * Voice Activity Detection (VAD), 16 kHz 16-bit mono WAV recording and playback
 * directly on MicroSD card, circular PSRAM buffering, and volume control on
 * the Waveshare ESP32-P4-WIFI6 development board.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "audio_studio";

/* Audio codec and bus pins (Waveshare ESP32-P4-WIFI6) */
#define AUDIO_I2C_PORT          I2C_NUM_0
#define AUDIO_I2C_SDA_PIN       GPIO_NUM_7
#define AUDIO_I2C_SCL_PIN       GPIO_NUM_8
#define AUDIO_I2C_FREQ_HZ       100000

#define AUDIO_I2S_PORT          I2S_NUM_0
#define AUDIO_I2S_MCLK_PIN      GPIO_NUM_13
#define AUDIO_I2S_BCLK_PIN      GPIO_NUM_12
#define AUDIO_I2S_WS_PIN        GPIO_NUM_10
#define AUDIO_I2S_DIN_PIN       GPIO_NUM_9   /* ESP32-P4 RX from codec ADC DOUT */
#define AUDIO_I2S_DOUT_PIN      GPIO_NUM_11  /* ESP32-P4 TX to codec DAC DIN */
#define AUDIO_PA_CTRL_PIN       GPIO_NUM_53  /* NS4150B Speaker Amplifier Enable */

/* MicroSD Card pins (Host slot 0 on ESP32-P4) */
#define PIN_TF_PWR              GPIO_NUM_45  /* Active low socket power switch */
#define PIN_TF_CLK              GPIO_NUM_43
#define PIN_TF_CMD              GPIO_NUM_44
#define PIN_TF_D0               GPIO_NUM_39
#define PIN_TF_D1               GPIO_NUM_40
#define PIN_TF_D2               GPIO_NUM_41
#define PIN_TF_D3               GPIO_NUM_42
#define SDCARD_MOUNT_POINT      "/sdcard"

/* Audio sample parameters */
#define SAMPLE_RATE_HZ          16000
#define MCLK_MULTIPLE           256
#define FRAME_SAMPLES           512          /* 32 ms per frame at 16 kHz */
#define FRAME_BYTES             (FRAME_SAMPLES * 2 * sizeof(int16_t)) /* Stereo 16-bit */
#define DEFAULT_MIC_GAIN_DB     30.0f        /* Microphone pre-amp gain in dB */
#define DEFAULT_OUT_VOL         70           /* Speaker volume (0-100) */

/* PSRAM audio buffer (5 seconds of 16 kHz 16-bit mono) */
#define PSRAM_BUFFER_SECONDS    5
#define PSRAM_BUFFER_SAMPLES    (SAMPLE_RATE_HZ * PSRAM_BUFFER_SECONDS)
#define PSRAM_BUFFER_BYTES      (PSRAM_BUFFER_SAMPLES * sizeof(int16_t))

/* Standard 44-byte RIFF/WAVE header */
typedef struct __attribute__((packed)) {
    char riff_id[4];          /* "RIFF" */
    uint32_t riff_size;       /* 36 + data_size */
    char wave_id[4];          /* "WAVE" */
    char fmt_id[4];           /* "fmt " */
    uint32_t fmt_size;        /* 16 */
    uint16_t audio_format;    /* 1 = PCM */
    uint16_t num_channels;    /* 1 = Mono, 2 = Stereo */
    uint32_t sample_rate;     /* e.g. 16000 */
    uint32_t byte_rate;       /* sample_rate * num_channels * bits / 8 */
    uint16_t block_align;     /* num_channels * bits / 8 */
    uint16_t bits_per_sample; /* 16 */
    char data_id[4];          /* "data" */
    uint32_t data_size;       /* Raw PCM data size in bytes */
} wav_header_t;

static i2s_chan_handle_t s_rx_handle = NULL;
static i2s_chan_handle_t s_tx_handle = NULL;
static esp_codec_dev_handle_t s_codec_handle = NULL;
static int16_t *s_psram_audio_buffer = NULL;
static size_t s_psram_write_index = 0;
static TaskHandle_t s_meter_task_handle = NULL;
static sdmmc_card_t *s_sdcard = NULL;
static bool s_sdcard_mounted = false;
static float s_current_gain_db = DEFAULT_MIC_GAIN_DB;
static int s_current_vol = DEFAULT_OUT_VOL;
static volatile bool s_meter_visual_active = false;

static void wav_header_init(wav_header_t *hdr, uint32_t sample_rate, uint16_t channels, uint32_t data_bytes)
{
    memcpy(hdr->riff_id, "RIFF", 4);
    hdr->riff_size = 36 + data_bytes;
    memcpy(hdr->wave_id, "WAVE", 4);
    memcpy(hdr->fmt_id, "fmt ", 4);
    hdr->fmt_size = 16;
    hdr->audio_format = 1; /* PCM */
    hdr->num_channels = channels;
    hdr->sample_rate = sample_rate;
    hdr->byte_rate = sample_rate * channels * sizeof(int16_t);
    hdr->block_align = channels * sizeof(int16_t);
    hdr->bits_per_sample = 16;
    memcpy(hdr->data_id, "data", 4);
    hdr->data_size = data_bytes;
}

static void path_with_prefix(const char *in, char *out, size_t out_len)
{
    if (in[0] == '/') {
        snprintf(out, out_len, "%s", in);
    } else {
        snprintf(out, out_len, "%s/%s", SDCARD_MOUNT_POINT, in);
    }
}

static bool kbhit(void)
{
    int fd = fileno(stdin);
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    if (select(fd + 1, &fds, NULL, NULL, &tv) > 0) {
        int c = getchar();
        (void)c;
        return true;
    }
    return false;
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate I2S channels: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK_PIN,
            .bclk = AUDIO_I2S_BCLK_PIN,
            .ws = AUDIO_I2S_WS_PIN,
            .dout = AUDIO_I2S_DOUT_PIN,
            .din = AUDIO_I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = MCLK_MULTIPLE;

    ret = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S RX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S TX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_handle));
    ESP_LOGI(TAG, "I2S initialized (16 kHz, 16-bit stereo, MCLK=GPIO%d)", AUDIO_I2S_MCLK_PIN);
    return ESP_OK;
}

static esp_err_t init_codec(void)
{
    /* Initialize speaker PA control pin */
    gpio_config_t pa_cfg = {
        .pin_bit_mask = 1ULL << AUDIO_PA_CTRL_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_cfg);
    gpio_set_level(AUDIO_PA_CTRL_PIN, 0);

    /* Initialize I2C master bus */
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = AUDIO_I2C_PORT,
        .sda_io_num = AUDIO_I2C_SDA_PIN,
        .scl_io_num = AUDIO_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_cfg, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    audio_codec_i2c_cfg_t codec_i2c = {
        .port = AUDIO_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
        .clock_speed_hz = AUDIO_I2C_FREQ_HZ,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&codec_i2c);
    if (!ctrl_if) {
        ESP_LOGE(TAG, "Failed to create codec control interface");
        return ESP_FAIL;
    }

    audio_codec_i2s_cfg_t codec_i2s = {
        .port = AUDIO_I2S_PORT,
        .rx_handle = s_rx_handle,
        .tx_handle = s_tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&codec_i2s);
    if (!data_if) {
        ESP_LOGE(TAG, "Failed to create codec data interface");
        return ESP_FAIL;
    }

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .pa_pin = AUDIO_PA_CTRL_PIN,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
        .mclk_div = MCLK_MULTIPLE,
    };
    const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
    if (!es8311_if) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec driver interface");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_if,
        .data_if = data_if,
    };
    s_codec_handle = esp_codec_dev_new(&dev_cfg);
    if (!s_codec_handle) {
        ESP_LOGE(TAG, "Failed to create codec device handle");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t sample_cfg = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = SAMPLE_RATE_HZ,
    };
    ret = esp_codec_dev_open(s_codec_handle, &sample_cfg);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec device: %d", ret);
        return ESP_FAIL;
    }

    /* Configure default mic gain and mute DAC until playback */
    esp_codec_dev_set_in_gain(s_codec_handle, s_current_gain_db);
    esp_codec_dev_set_out_vol(s_codec_handle, s_current_vol);
    esp_codec_dev_set_out_mute(s_codec_handle, true);
    gpio_set_level(AUDIO_PA_CTRL_PIN, 0);

    ESP_LOGI(TAG, "ES8311 audio codec initialized successfully at I2C 0x%02X", ES8311_CODEC_DEFAULT_ADDR);
    return ESP_OK;
}

static esp_err_t mount_sdcard(void)
{
    if (s_sdcard_mounted) {
        printf("MicroSD card is already mounted at %s\n", SDCARD_MOUNT_POINT);
        return ESP_OK;
    }

    /* Drive onboard MicroSD power switch (GPIO 45, active low) */
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

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LDO power control handle: %s", esp_err_to_name(ret));
    } else {
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

    ret = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_sdcard);
    if (ret == ESP_OK) {
        s_sdcard_mounted = true;
        ESP_LOGI(TAG, "MicroSD card mounted at %s", SDCARD_MOUNT_POINT);
    } else {
        s_sdcard_mounted = false;
        ESP_LOGW(TAG, "MicroSD card mount failed: %s (card may not be inserted)", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t unmount_sdcard(void)
{
    if (!s_sdcard_mounted) {
        printf("MicroSD card is not mounted.\n");
        return ESP_OK;
    }
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, s_sdcard);
    if (ret == ESP_OK) {
        s_sdcard_mounted = false;
        s_sdcard = NULL;
        gpio_set_level(PIN_TF_PWR, 1);
        printf("MicroSD card unmounted successfully.\n");
    } else {
        printf("Failed to unmount MicroSD card: %s\n", esp_err_to_name(ret));
    }
    return ret;
}

static void audio_background_task(void *pvParameters)
{
    int16_t *stereo_buf = malloc(FRAME_BYTES);
    if (!stereo_buf) {
        ESP_LOGE(TAG, "Failed to allocate audio frame buffer");
        vTaskDelete(NULL);
        return;
    }

    uint32_t frame_count = 0;
    float noise_floor_rms = 100.0f;

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_handle, stereo_buf, FRAME_BYTES, &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read != FRAME_BYTES) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int16_t peak = 0;
        int64_t sum_squares = 0;
        int32_t dc_sum = 0;

        for (size_t i = 0; i < FRAME_SAMPLES; i++) {
            int16_t sample = stereo_buf[i * 2];
            dc_sum += sample;

            int16_t abs_val = (sample < 0) ? -sample : sample;
            if (abs_val > peak) {
                peak = abs_val;
            }

            if (s_psram_audio_buffer) {
                s_psram_audio_buffer[s_psram_write_index] = sample;
                s_psram_write_index = (s_psram_write_index + 1) % PSRAM_BUFFER_SAMPLES;
            }
        }

        int16_t dc_bias = (int16_t)(dc_sum / (int32_t)FRAME_SAMPLES);
        for (size_t i = 0; i < FRAME_SAMPLES; i++) {
            int32_t centered = (int32_t)stereo_buf[i * 2] - dc_bias;
            sum_squares += (int64_t)centered * centered;
        }

        float rms = sqrtf((float)sum_squares / (float)FRAME_SAMPLES);
        if (rms < 1.0f) rms = 1.0f;

        float dbfs = 20.0f * log10f(rms / 32767.0f);
        if (dbfs < -60.0f) dbfs = -60.0f;

        if (rms < noise_floor_rms) {
            noise_floor_rms = 0.95f * noise_floor_rms + 0.05f * rms;
        } else {
            noise_floor_rms = 0.999f * noise_floor_rms + 0.001f * rms;
        }

        bool voice_active = (rms > (noise_floor_rms * 3.5f)) && (dbfs > -45.0f);

        if (s_meter_visual_active) {
            frame_count++;
            if (frame_count % 3 == 0) {
                int bar_len = (int)((dbfs + 60.0f) / 3.0f);
                if (bar_len < 0) bar_len = 0;
                if (bar_len > 20) bar_len = 20;

                char bar[21];
                for (int b = 0; b < 20; b++) {
                    bar[b] = (b < bar_len) ? '=' : ' ';
                }
                bar[20] = '\0';

                printf("\r[%s] %5.1f dBFS | RMS:%5d | Peak:%5d | %s",
                       bar,
                       dbfs,
                       (int)rms,
                       (int)peak,
                       voice_active ? "[VOICE ACTIVE]" : "              ");
                fflush(stdout);
            }
        }
    }

    free(stereo_buf);
    vTaskDelete(NULL);
}

/* Shell command: meter [seconds] */
static int cmd_meter(int argc, char **argv)
{
    int duration_sec = 0;
    if (argc > 1) {
        duration_sec = atoi(argv[1]);
    }

    printf("Starting live VU meter (%s). Press Enter or any key to stop...\n",
           duration_sec > 0 ? "timed" : "continuous");
    s_meter_visual_active = true;

    int elapsed_ms = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        elapsed_ms += 50;

        if (duration_sec > 0 && elapsed_ms >= (duration_sec * 1000)) {
            break;
        }

        if (kbhit()) {
            break;
        }
    }

    s_meter_visual_active = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    printf("\nMeter stopped.\n");
    return 0;
}

/* Shell command: rec <seconds> [filename.wav] */
static int cmd_rec(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: rec <seconds> [filename.wav]\n");
        return 1;
    }

    if (!s_sdcard_mounted) {
        printf("Error: MicroSD card not mounted. Run 'mount' or check card.\n");
        return 1;
    }

    int seconds = atoi(argv[1]);
    if (seconds <= 0 || seconds > 3600) {
        printf("Error: duration must be between 1 and 3600 seconds.\n");
        return 1;
    }

    char filepath[256];
    if (argc > 2) {
        path_with_prefix(argv[2], filepath, sizeof(filepath));
    } else {
        static int s_rec_counter = 1;
        snprintf(filepath, sizeof(filepath), "%s/recording_%03d.wav", SDCARD_MOUNT_POINT, s_rec_counter++);
    }

    if (strlen(filepath) < 4 || strcasecmp(filepath + strlen(filepath) - 4, ".wav") != 0) {
        strncat(filepath, ".wav", sizeof(filepath) - strlen(filepath) - 1);
    }

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        printf("Error: failed to open %s for writing: %s\n", filepath, strerror(errno));
        return 1;
    }

    wav_header_t hdr;
    wav_header_init(&hdr, SAMPLE_RATE_HZ, 1, 0);
    fwrite(&hdr, 1, sizeof(hdr), fp);

    /* Suspend background meter task to gain exclusive RX channel access */
    vTaskSuspend(s_meter_task_handle);

    int16_t *stereo_buf = malloc(FRAME_BYTES);
    int16_t *mono_buf = malloc(FRAME_SAMPLES * sizeof(int16_t));
    if (!stereo_buf || !mono_buf) {
        printf("Error: out of memory for recording buffers\n");
        free(stereo_buf);
        free(mono_buf);
        fclose(fp);
        vTaskResume(s_meter_task_handle);
        return 1;
    }

    size_t total_samples = (size_t)seconds * SAMPLE_RATE_HZ;
    size_t written_samples = 0;
    uint32_t last_print_sec = 0;

    printf("Recording %d seconds to %s... (Press any key to stop early)\n", seconds, filepath);

    while (written_samples < total_samples) {
        size_t to_read_samples = FRAME_SAMPLES;
        if (written_samples + to_read_samples > total_samples) {
            to_read_samples = total_samples - written_samples;
        }

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_handle, stereo_buf, FRAME_BYTES, &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK) {
            printf("\nI2S read error: %s\n", esp_err_to_name(ret));
            break;
        }

        int16_t peak = 0;
        for (size_t i = 0; i < to_read_samples; i++) {
            int16_t s = stereo_buf[i * 2];
            mono_buf[i] = s;
            int16_t abs_s = (s < 0) ? -s : s;
            if (abs_s > peak) peak = abs_s;
        }

        size_t nw = fwrite(mono_buf, sizeof(int16_t), to_read_samples, fp);
        if (nw != to_read_samples) {
            printf("\nSD write error: wrote %u of %u samples: %s\n",
                   (unsigned)nw, (unsigned)to_read_samples, strerror(errno));
            break;
        }
        written_samples += nw;

        uint32_t cur_sec = (uint32_t)(written_samples / SAMPLE_RATE_HZ);
        if (cur_sec != last_print_sec) {
            last_print_sec = cur_sec;
            printf("\r[REC] %02lu:%02lu / %02d:%02d | Peak: %5d | %u KB written",
                   (unsigned long)(cur_sec / 60), (unsigned long)(cur_sec % 60),
                   seconds / 60, seconds % 60,
                   peak,
                   (unsigned)((written_samples * sizeof(int16_t)) / 1024));
            fflush(stdout);
        }

        if (kbhit()) {
            printf("\nRecording stopped by user.");
            break;
        }
    }

    free(stereo_buf);
    free(mono_buf);

    uint32_t data_bytes = written_samples * sizeof(int16_t);
    wav_header_init(&hdr, SAMPLE_RATE_HZ, 1, data_bytes);
    fseek(fp, 0, SEEK_SET);
    fwrite(&hdr, 1, sizeof(hdr), fp);
    fclose(fp);

    vTaskResume(s_meter_task_handle);

    printf("\nSaved %s: %.2f seconds (%u bytes, 16 kHz 16-bit mono WAV)\n",
           filepath, (double)written_samples / SAMPLE_RATE_HZ, (unsigned)(sizeof(hdr) + data_bytes));
    return 0;
}

/* Shell command: save_buffer [filename.wav] */
static int cmd_save_buffer(int argc, char **argv)
{
    if (!s_sdcard_mounted) {
        printf("Error: MicroSD card not mounted. Run 'mount' or check card.\n");
        return 1;
    }

    if (!s_psram_audio_buffer) {
        printf("Error: PSRAM circular audio buffer is not allocated.\n");
        return 1;
    }

    char filepath[256];
    if (argc > 1) {
        path_with_prefix(argv[1], filepath, sizeof(filepath));
    } else {
        snprintf(filepath, sizeof(filepath), "%s/psram_buffer_%lu.wav",
                 SDCARD_MOUNT_POINT, (unsigned long)time(NULL));
    }

    if (strlen(filepath) < 4 || strcasecmp(filepath + strlen(filepath) - 4, ".wav") != 0) {
        strncat(filepath, ".wav", sizeof(filepath) - strlen(filepath) - 1);
    }

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        printf("Error: cannot open %s for writing: %s\n", filepath, strerror(errno));
        return 1;
    }

    uint32_t data_bytes = PSRAM_BUFFER_BYTES;
    wav_header_t hdr;
    wav_header_init(&hdr, SAMPLE_RATE_HZ, 1, data_bytes);

    if (fwrite(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        printf("Error: failed to write WAV header\n");
        fclose(fp);
        return 1;
    }

    /* Unwind circular buffer starting from oldest sample at s_psram_write_index */
    size_t head = s_psram_write_index;
    size_t part1_samples = PSRAM_BUFFER_SAMPLES - head;
    size_t part2_samples = head;

    fwrite(&s_psram_audio_buffer[head], sizeof(int16_t), part1_samples, fp);
    if (part2_samples > 0) {
        fwrite(&s_psram_audio_buffer[0], sizeof(int16_t), part2_samples, fp);
    }

    fclose(fp);
    printf("Saved %u KB (5.0s audio) to %s\n", (unsigned)(data_bytes / 1024), filepath);
    return 0;
}

/* Shell command: play <filename.wav> */
static int cmd_play(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: play <filename.wav>\n");
        return 1;
    }

    if (!s_sdcard_mounted) {
        printf("Error: MicroSD card not mounted. Run 'mount' or check card.\n");
        return 1;
    }

    char filepath[256];
    path_with_prefix(argv[1], filepath, sizeof(filepath));

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        printf("Error: cannot open %s: %s\n", filepath, strerror(errno));
        return 1;
    }

    wav_header_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        printf("Error: cannot read WAV header from %s\n", filepath);
        fclose(fp);
        return 1;
    }

    if (memcmp(hdr.riff_id, "RIFF", 4) != 0 || memcmp(hdr.wave_id, "WAVE", 4) != 0) {
        printf("Error: not a valid RIFF/WAVE file\n");
        fclose(fp);
        return 1;
    }

    if (hdr.audio_format != 1 || hdr.bits_per_sample != 16) {
        printf("Error: only 16-bit PCM WAV is supported (format=%u, bits=%u)\n",
               hdr.audio_format, hdr.bits_per_sample);
        fclose(fp);
        return 1;
    }

    printf("Playing %s (%lu Hz, %u channel(s), %lu bytes PCM)...\n",
           filepath, (unsigned long)hdr.sample_rate, hdr.num_channels, (unsigned long)hdr.data_size);
    printf("Press any key to abort playback.\n");

    /* Enable speaker amplifier and unmute codec output */
    gpio_set_level(AUDIO_PA_CTRL_PIN, 1);
    esp_codec_dev_set_out_mute(s_codec_handle, false);

    int16_t *read_buf = malloc(FRAME_SAMPLES * sizeof(int16_t) * hdr.num_channels);
    int16_t *stereo_buf = malloc(FRAME_BYTES);
    if (!read_buf || !stereo_buf) {
        printf("Error: out of memory for playback buffers\n");
        free(read_buf);
        free(stereo_buf);
        fclose(fp);
        esp_codec_dev_set_out_mute(s_codec_handle, true);
        gpio_set_level(AUDIO_PA_CTRL_PIN, 0);
        return 1;
    }

    uint32_t total_played = 0;
    while (total_played < hdr.data_size) {
        size_t samples_to_read = FRAME_SAMPLES;
        size_t bytes_to_read = samples_to_read * sizeof(int16_t) * hdr.num_channels;
        if (total_played + bytes_to_read > hdr.data_size) {
            bytes_to_read = hdr.data_size - total_played;
            samples_to_read = bytes_to_read / (sizeof(int16_t) * hdr.num_channels);
        }

        size_t nr = fread(read_buf, 1, bytes_to_read, fp);
        if (nr == 0) break;

        size_t samples = nr / (sizeof(int16_t) * hdr.num_channels);

        if (hdr.num_channels == 1) {
            for (size_t i = 0; i < samples; i++) {
                stereo_buf[i * 2] = read_buf[i];
                stereo_buf[i * 2 + 1] = read_buf[i];
            }
        } else {
            memcpy(stereo_buf, read_buf, samples * 2 * sizeof(int16_t));
        }

        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_handle, stereo_buf, samples * 2 * sizeof(int16_t),
                                          &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            printf("\nI2S write error: %s\n", esp_err_to_name(ret));
            break;
        }

        total_played += nr;

        if (kbhit()) {
            printf("\nPlayback aborted by user.\n");
            break;
        }
    }

    /* Allow DMA FIFO to drain before disabling amplifier */
    vTaskDelay(pdMS_TO_TICKS(150));
    esp_codec_dev_set_out_mute(s_codec_handle, true);
    gpio_set_level(AUDIO_PA_CTRL_PIN, 0);

    free(read_buf);
    free(stereo_buf);
    fclose(fp);

    printf("Playback finished.\n");
    return 0;
}

/* Shell command: gain [0-30] */
static int cmd_gain(int argc, char **argv)
{
    if (argc > 1) {
        float gain = atof(argv[1]);
        if (gain < 0.0f || gain > 30.0f) {
            printf("Error: gain must be between 0.0 and 30.0 dB\n");
            return 1;
        }
        int ret = esp_codec_dev_set_in_gain(s_codec_handle, gain);
        if (ret == ESP_CODEC_DEV_OK) {
            s_current_gain_db = gain;
            printf("Microphone pre-amp gain set to +%.1f dB\n", s_current_gain_db);
        } else {
            printf("Failed to set mic gain: %d\n", ret);
            return 1;
        }
    } else {
        printf("Current microphone gain: +%.1f dB (range: 0.0 to 30.0 dB)\n", s_current_gain_db);
    }
    return 0;
}

/* Shell command: vol [0-100] */
static int cmd_vol(int argc, char **argv)
{
    if (argc > 1) {
        int vol = atoi(argv[1]);
        if (vol < 0 || vol > 100) {
            printf("Error: volume must be between 0 and 100\n");
            return 1;
        }
        int ret = esp_codec_dev_set_out_vol(s_codec_handle, vol);
        if (ret == ESP_CODEC_DEV_OK) {
            s_current_vol = vol;
            printf("Speaker volume set to %d%%\n", s_current_vol);
        } else {
            printf("Failed to set volume: %d\n", ret);
            return 1;
        }
    } else {
        printf("Current speaker volume: %d%% (range: 0 to 100)\n", s_current_vol);
    }
    return 0;
}

/* Shell command: ls [path] */
static int cmd_ls(int argc, char **argv)
{
    if (!s_sdcard_mounted) {
        printf("Error: MicroSD card not mounted. Run 'mount' or check card.\n");
        return 1;
    }

    char path[256];
    if (argc > 1) {
        path_with_prefix(argv[1], path, sizeof(path));
    } else {
        snprintf(path, sizeof(path), "%s", SDCARD_MOUNT_POINT);
    }

    DIR *d = opendir(path);
    if (!d) {
        printf("ls: cannot open directory '%s': %s\n", path, strerror(errno));
        return 1;
    }

    printf("Directory: %s\n", path);
    printf("%-28s %12s  %s\n", "Name", "Size", "Type");
    printf("------------------------------------------------------\n");

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, ent->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0) {
            bool is_dir = S_ISDIR(st.st_mode);
            printf("%-28s %10lu B  %s\n",
                   ent->d_name,
                   (unsigned long)st.st_size,
                   is_dir ? "<DIR>" : "FILE");
        } else {
            printf("%-28s            ?  FILE\n", ent->d_name);
        }
        count++;
    }
    closedir(d);
    printf("------------------------------------------------------\n");
    printf("Total: %d entries\n", count);
    return 0;
}

/* Shell command: rm <file> */
static int cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: rm <file>\n");
        return 1;
    }

    if (!s_sdcard_mounted) {
        printf("Error: MicroSD card not mounted. Run 'mount' or check card.\n");
        return 1;
    }

    char path[256];
    path_with_prefix(argv[1], path, sizeof(path));

    if (unlink(path) == 0) {
        printf("Removed: %s\n", path);
        return 0;
    } else {
        printf("rm: failed to remove '%s': %s\n", path, strerror(errno));
        return 1;
    }
}

/* Shell command: df */
static int cmd_df(int argc, char **argv)
{
    printf("\n--- System & Storage Status ---\n");

    if (s_sdcard_mounted && s_sdcard) {
        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        esp_err_t ret = esp_vfs_fat_info(SDCARD_MOUNT_POINT, &total_bytes, &free_bytes);
        if (ret == ESP_OK) {
            printf("MicroSD Card: %s (slot 0, 4-bit)\n", s_sdcard->cid.name);
            printf("  Capacity:   %llu MB (%llu KB)\n",
                   total_bytes / (1024 * 1024), total_bytes / 1024);
            printf("  Free Space: %llu MB (%llu KB)\n",
                   free_bytes / (1024 * 1024), free_bytes / 1024);
            printf("  Used Space: %llu MB (%.1f%%)\n",
                   (total_bytes - free_bytes) / (1024 * 1024),
                   total_bytes > 0 ? ((double)(total_bytes - free_bytes) * 100.0 / total_bytes) : 0.0);
        } else {
            printf("MicroSD Card: Mounted, but failed to read FATFS info (%s)\n", esp_err_to_name(ret));
        }
    } else {
        printf("MicroSD Card: Not mounted\n");
    }

    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    printf("PSRAM Memory:\n");
    printf("  Total: %u KB, Free: %u KB (Circular Buffer: %d s at 16 kHz)\n",
           (unsigned)(psram_total / 1024), (unsigned)(psram_free / 1024), PSRAM_BUFFER_SECONDS);

    size_t sram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    printf("Internal SRAM Free: %u KB\n", (unsigned)(sram_free / 1024));

    printf("Audio Settings:\n");
    printf("  Microphone Gain: +%.1f dB\n", s_current_gain_db);
    printf("  Speaker Volume:  %d%%\n", s_current_vol);
    printf("  Format:          16 kHz, 16-bit PCM\n\n");
    return 0;
}

/* Shell command: mount */
static int cmd_mount(int argc, char **argv)
{
    esp_err_t ret = mount_sdcard();
    if (ret == ESP_OK) {
        printf("MicroSD card mounted at %s\n", SDCARD_MOUNT_POINT);
        return 0;
    }
    printf("Failed to mount MicroSD card (%s). Make sure card is inserted.\n", esp_err_to_name(ret));
    return 1;
}

/* Shell command: unmount */
static int cmd_unmount(int argc, char **argv)
{
    unmount_sdcard();
    return 0;
}

static void register_shell_commands(void)
{
    esp_console_register_help_command();

    const esp_console_cmd_t cmds[] = {
        {
            .command = "meter",
            .help = "Start real-time ASCII VU meter & voice activity detection",
            .hint = "[seconds]",
            .func = &cmd_meter,
        },
        {
            .command = "rec",
            .help = "Record 16 kHz 16-bit mono WAV to MicroSD card",
            .hint = "<seconds> [filename.wav]",
            .func = &cmd_rec,
        },
        {
            .command = "save_buffer",
            .help = "Save last 5 seconds from circular PSRAM buffer to WAV file",
            .hint = "[filename.wav]",
            .func = &cmd_save_buffer,
        },
        {
            .command = "play",
            .help = "Play a WAV file through ES8311 DAC and onboard speaker",
            .hint = "<filename.wav>",
            .func = &cmd_play,
        },
        {
            .command = "gain",
            .help = "Get or set microphone pre-amp gain in dB (0.0 to 30.0)",
            .hint = "[gain_db]",
            .func = &cmd_gain,
        },
        {
            .command = "vol",
            .help = "Get or set speaker volume (0 to 100)",
            .hint = "[volume]",
            .func = &cmd_vol,
        },
        {
            .command = "ls",
            .help = "List files on MicroSD card",
            .hint = "[path]",
            .func = &cmd_ls,
        },
        {
            .command = "rm",
            .help = "Delete a file on MicroSD card",
            .hint = "<file>",
            .func = &cmd_rm,
        },
        {
            .command = "df",
            .help = "Display storage, PSRAM, and audio status",
            .hint = NULL,
            .func = &cmd_df,
        },
        {
            .command = "mount",
            .help = "Mount MicroSD card",
            .hint = NULL,
            .func = &cmd_mount,
        },
        {
            .command = "unmount",
            .help = "Unmount MicroSD card",
            .hint = NULL,
            .func = &cmd_unmount,
        },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_console_cmd_register(&cmds[i]);
    }
}

void app_main(void)
{
    printf("\n======================================================\n");
    printf(" Waveshare ESP32-P4 Audio Studio & Shell\n");
    printf("======================================================\n");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("Chip: ESP32-P4 (revision v%d.%d, %d cores)\n",
           chip_info.revision / 100, chip_info.revision % 100, chip_info.cores);

    s_psram_audio_buffer = heap_caps_malloc(PSRAM_BUFFER_BYTES, MALLOC_CAP_SPIRAM);
    if (s_psram_audio_buffer) {
        printf("PSRAM Audio Buffer: %u KB (5.0 seconds at 16 kHz mono)\n",
               (unsigned)(PSRAM_BUFFER_BYTES / 1024));
    } else {
        printf("Warning: could not allocate PSRAM buffer\n");
    }

    /* Mount MicroSD card */
    mount_sdcard();

    /* Initialize I2S peripheral */
    ESP_ERROR_CHECK(init_i2s());

    /* Initialize ES8311 Codec */
    ESP_ERROR_CHECK(init_codec());

    /* Start audio background processing task */
    xTaskCreatePinnedToCore(audio_background_task, "audio_bg", 4096, NULL, 5, &s_meter_task_handle, 1);

    /* Initialize Interactive REPL Console */
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "audio> ";
    repl_config.max_cmdline_length = 256;
    repl_config.task_stack_size = 16384;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    linenoiseSetDumbMode(0);
    register_shell_commands();

    printf("\nAudio Shell ready. Storage: %s. Type 'help' for commands.\n\n",
           s_sdcard_mounted ? "/sdcard mounted" : "no SD card (run 'mount')");

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
