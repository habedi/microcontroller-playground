const microzig = @import("microzig");
const hal = microzig.hal;

comptime {
    _ = microzig.export_startup();
}

// GP25 belongs to the wireless interface on the Pico 2 WH.
const led = hal.gpio.num(0);

pub fn main() void {
    led.set_function(.sio);
    led.set_direction(.out);
    led.put(0);

    while (true) {
        led.toggle();
        hal.time.sleep_ms(500);
    }
}
