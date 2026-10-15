# Hourglass

A physics-simulated sand timer for the **ESP32-S3** and a **2.9" 168×384 ST7305 monochrome LCD**.

About 5000 individually simulated grains fall through the neck and settle into a realistic heap.
When the top bulb is empty, the glass flips over and starts again. You can set the physics
parameters and Wi-Fi from a built-in web UI, and update the firmware over the air.

All firmware is built in GitHub Actions. The only thing you do locally is flash the result.

> **Status:** early development. The hourglass runs: 5000 simulated grains, about 75 seconds
> per run, then the glass flips and starts again. Wi-Fi, the web UI and OTA are in progress.

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

## How the simulation works

Every grain is a particle with a position and a velocity, and occupies one pixel. An occupancy
grid of the screen handles collisions with the glass and with other grains.

- **Falling:** gravity accelerates the grains up to a top speed. Each frame is split into
  substeps, so no grain jumps more than one pixel at a time.
- **Landing:** a grain that lands bounces a little (elasticity) and loses its sideways speed
  (friction). If there is room, it rolls diagonally down, so the heaps form natural slopes. On
  the smooth glass it also slides down walls that are flatter than that.
- **Resting:** grains with nowhere to go fall asleep and cost almost nothing until room opens
  up below them.
- **Flipping:** when the top bulb is empty and nothing has moved for 1.5 seconds, the glass
  turns over in an animated rotation and the sand runs again.

There is no target duration: the run time comes out of the physics. With the defaults
(5000 grains, a 4 pixel neck) a run takes about 75 seconds. The physics uses about 7 ms per
frame on the ESP32-S3, and the display runs at 30 fps.

The simulation (`components/sandsim`) is plain C without ESP-IDF dependencies, so it also
builds and runs on a PC.

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
- [x] Particle sand simulation (5000 grains, 30 fps)
- [x] Glass outline and flip animation
- [ ] SoftAP Wi-Fi provisioning
- [ ] Web UI for physics parameters and OTA upload
- [ ] Release workflow with flash scripts

## Credits

The ST7305 init sequence and RAM layout are based on
[FT-tele/ST7305_MonoTFT_Arduino_Library](https://github.com/FT-tele/ST7305_MonoTFT_Arduino_Library)
(MIT). The gate voltages and auto power down setting are adjusted to the YDP290H004 datasheet.

## License

[MIT](LICENSE)
