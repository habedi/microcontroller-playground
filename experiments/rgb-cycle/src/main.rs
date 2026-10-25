use std::thread::sleep;
use std::time::Duration;

use esp_idf_hal::peripherals::Peripherals;
use log::info;
use ws2812_esp32_rmt_driver::Ws2812Esp32RmtDriver;

// WS2812 pixel format is GRB (Green, Red, Blue).
struct Color {
    name: &'static str,
    r: u8,
    g: u8,
    b: u8,
}

impl Color {
    const fn new(name: &'static str, r: u8, g: u8, b: u8) -> Self {
        Self { name, r, g, b }
    }

    fn to_grb(&self) -> [u8; 3] {
        [self.g, self.r, self.b]
    }
}

const COLORS: [Color; 7] = [
    Color::new("Red", 32, 0, 0),
    Color::new("Yellow", 24, 24, 0),
    Color::new("Green", 0, 32, 0),
    Color::new("Cyan", 0, 24, 24),
    Color::new("Blue", 0, 0, 32),
    Color::new("Magenta", 24, 0, 24),
    Color::new("White", 16, 16, 16),
];

fn main() {
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    info!("========================================");
    info!("Waveshare ESP32-S3-Zero (idf-rs)");
    info!("WS2812 RGB LED on GPIO 21");
    info!("========================================");

    let free_heap = unsafe { esp_idf_svc::sys::esp_get_free_heap_size() };
    let free_psram = unsafe {
        esp_idf_svc::sys::heap_caps_get_free_size(esp_idf_svc::sys::MALLOC_CAP_SPIRAM)
    };
    info!(
        "Startup memory: free SRAM = {} B ({} KB), free PSRAM = {} B ({} KB)",
        free_heap,
        free_heap / 1024,
        free_psram,
        free_psram / 1024
    );

    let peripherals = Peripherals::take().expect("failed to acquire peripherals");
    let led_pin = peripherals.pins.gpio21;

    let mut ws2812 = Ws2812Esp32RmtDriver::new(led_pin)
        .expect("failed to initialize WS2812 RMT driver on GPIO 21");

    info!("RMT driver initialized; beginning color cycle loop");

    let mut cycle_count: u32 = 0;
    loop {
        for color in &COLORS {
            ws2812
                .write_blocking_slice(&color.to_grb())
                .expect("failed to write color to WS2812 LED");

            let current_heap = unsafe { esp_idf_svc::sys::esp_get_free_heap_size() };
            let current_psram = unsafe {
                esp_idf_svc::sys::heap_caps_get_free_size(esp_idf_svc::sys::MALLOC_CAP_SPIRAM)
            };

            info!(
                "[Cycle {:03}] Color: {:<8} (R:{:02}, G:{:02}, B:{:02}) | SRAM: {} KB free | PSRAM: {} KB free",
                cycle_count,
                color.name,
                color.r,
                color.g,
                color.b,
                current_heap / 1024,
                current_psram / 1024
            );

            sleep(Duration::from_secs(1));
        }
        cycle_count = cycle_count.wrapping_add(1);
    }
}
