## ESP-IDF Audio Studio and Shell

Captures microphone audio from the onboard Everest ES8311 codec, streams to a circular PSRAM buffer, records uncompressed WAV files directly to a MicroSD card, and provides an interactive console shell on the Waveshare ESP32-P4-WIFI6 development board.

- Board: Waveshare ESP32-P4-WIFI6 (ESP32-P4NRW32, silicon revision v1.3)
- OS: FreeRTOS, through ESP-IDF v6.1
- Hardware: onboard SMD analog microphone, Everest ES8311 I2S/I2C audio codec, NS4150B speaker amplifier, MicroSD card socket, and 32 MB PSRAM

### Pin Mapping

The Waveshare ESP32-P4-WIFI6 routes audio and MicroSD signals through the following pins:

- I2C control bus (address `0x18`):
  - SDA on GPIO 7
  - SCL on GPIO 8
- I2S digital audio bus:
  - MCLK (master clock) on GPIO 13
  - BCLK (bit clock) on GPIO 12
  - WS (word select and frame clock) on GPIO 10
  - DIN (ESP32-P4 I2S RX from codec ADC DOUT) on GPIO 9
  - DOUT (ESP32-P4 I2S TX to codec DAC DIN) on GPIO 11
- Speaker amplifier control:
  - PA enable (NS4150B class-D amplifier driving MX1.25 header) on GPIO 53
- MicroSD card socket (SDMMC Host Slot 0, 4-bit):
  - Power switch on GPIO 45 (active low)
  - CLK on GPIO 43
  - CMD on GPIO 44
  - D0 on GPIO 39
  - D1 on GPIO 40
  - D2 on GPIO 41
  - D3 on GPIO 42
  - IO pad domain powered by on-chip LDO channel 4

### Interactive Shell Commands

The console prompt (`nsh> `) runs over UART0 at 115200 baud with line editing and history:

- `meter [seconds]`: displays the live ASCII VU meter with adaptive background noise tracking and voice activity detection; press any key or Enter to return to the shell.
- `rec <seconds> [filename.wav]`: records 16 kHz 16-bit mono PCM directly to `/sdcard/<filename>.wav` with real-time peak, RMS, and progress displays.
- `save_buffer [filename.wav]`: writes the last 5 seconds from the circular PSRAM audio buffer to a WAV file on the MicroSD card.
- `play <filename.wav>`: plays a WAV file through the ES8311 DAC and NS4150B speaker amplifier; press any key to stop early.
- `gain [gain_db]`: inspects or updates microphone pre-amp gain (range is 0.0 to 30.0 dB).
- `vol [volume]`: inspects or updates speaker playback volume (range is 0 to 100).
- `ls [path]`: lists files and directories on the MicroSD card with sizes in bytes.
- `rm <filename>`: removes a file from the MicroSD card.
- `df`: displays MicroSD card free and total capacity, PSRAM memory usage, and internal SRAM.
- `mount`: mounts the MicroSD card FATFS filesystem on `/sdcard`.
- `unmount`: safely unmounts the MicroSD card.
- `help`: lists all registered commands.

### Build and Flash

Build inside the Nix dev shell (`nix develop`):

```shell
make espidf-build ESPIDF_SRC=experiments/espidf-audio
```

Flash over USB serial:

```shell
make espidf-flash ESPIDF_SRC=experiments/espidf-audio ESPIDF_PORT=/dev/ttyACM0
```

Open serial console:

```shell
make espidf-monitor ESPIDF_SRC=experiments/espidf-audio ESPIDF_PORT=/dev/ttyACM0
```
