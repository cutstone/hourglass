# Hourglass

A physics-simulated sand timer for the **ESP32-S3** and a **2.9" 168×384 ST7305 monochrome LCD**.

About 5000 individually simulated grains fall through the neck and settle into a realistic heap.
When the top bulb is empty, the glass flips over and starts again. You can set the physics
parameters and Wi-Fi from a built-in web UI, and update the firmware over the air.

All firmware is built in GitHub Actions. The only thing you do locally is flash the result.

> **Status:** early development. Implemented so far: project scaffold, CI build and the
> display driver with a test pattern. The sand simulation, web UI and OTA are in progress.

## Hardware

| Part    | Details                                                     |
| ------- | ----------------------------------------------------------- |
| MCU     | ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM)      |
| Display | 2.9" ST7305 reflective mono LCD, 168×384, SPI, portrait (YDP290H004-V3) |

### Display wiring (defaults)

| Signal       | GPIO |
| ------------ | ---- |
| CS           | 10   |
| SDA / MOSI   | 11   |
| SCL / SCK    | 12   |
| D/C          | 13   |
| RESET        | 14   |

The SPI clock is 20 MHz on `SPI2_HOST`. To use different pins, change them under
`idf.py menuconfig` → *Hourglass hardware*. In the same menu you can:

- **Rotate display 180 degrees:** use this if the picture is upside down.
- **Use high power mode:** on by default. It gives a 32 Hz refresh for smooth animation;
  switch it off for a 1 Hz refresh.

### Test pattern

The current firmware shows a test pattern: a frame, an hourglass outline, a solid square in
the **top-left** corner and a small block sliding downwards. The serial log reports the frame
rate every 5 seconds. If the square is not in the top-left corner, turn on
*Rotate display 180 degrees*.

## Getting the firmware

- **Releases:** every `v*` tag publishes a release with a ready-to-flash zip. *(coming soon)*
- **CI builds:** every push uploads a `hourglass-firmware` artifact. Download it from the
  Actions run.

## Flashing

You need [esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`).
Connect the board over USB and run:

```bash
esptool --chip esp32s3 write_flash 0x0 hourglass-merged.bin
```

`hourglass-merged.bin` contains the bootloader, partition table and application, so a single
write at offset `0x0` is enough. esptool detects the serial port automatically. If it picks
the wrong one, add `--port COMx` (or `/dev/ttyACM0` on Linux).

## Building locally (optional)

You don't need to build locally, because CI does it for you. If you want to anyway, use
ESP-IDF v5.5:

```bash
idf.py set-target esp32s3
idf.py build
```

## Roadmap

- [x] ESP-IDF project scaffold, 16 MB partition table with two OTA slots
- [x] CI: build and clang-format check
- [x] ST7305 driver and 1 bpp framebuffer
- [ ] Particle sand simulation (~5000 grains, ~30 fps)
- [ ] Glass outline and flip animation
- [ ] SoftAP Wi-Fi provisioning
- [ ] Web UI for physics parameters and OTA upload
- [ ] Release workflow with flash scripts

## Credits

The ST7305 init sequence and RAM layout are based on
[FT-tele/ST7305_MonoTFT_Arduino_Library](https://github.com/FT-tele/ST7305_MonoTFT_Arduino_Library)
(MIT). The gate voltages and auto power down setting are adjusted to the YDP290H004 datasheet.

## License

[MIT](LICENSE)
