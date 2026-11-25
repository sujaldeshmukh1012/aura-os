# Hardware record: LCDWiki ES3C40P

Status: **MCU, memory, flashing, and native USB hardware-validated; PCB revision, display, and touch unverified**.

Sources:

- Manufacturer product page: <https://www.lcdwiki.com/4.0inch_ESP32-S3_Display>
- Manufacturer ES3C40P Specification V1.0: <https://www.lcdwiki.com/res/ES3C40P/ES3C40P_Specification_V1.0.pdf>
- Manufacturer schematic: <https://www.lcdwiki.com/res/ES3C40P/4.0inch_ESP32-S3_Display_Schematic.pdf>
- Espressif USB Serial/JTAG guide: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html>
- Espressif ESP32-S3 BLE overview: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/ble/overview.html>

## Verified from current manufacturer documentation

| Capability | Documented value |
| --- | --- |
| SKU/family | ES3C40P |
| MCU module | ESP32-S3 N16R8 |
| Flash / PSRAM | 16 MB SPI flash / 8 MB OPI PSRAM |
| Display | 4.0-inch IPS, ST7796S, 320×480 native, 4-wire SPI |
| Touch | FT6336U capacitive, I²C address `0x38` |
| USB | ESP32-S3 native USB on GPIO19 D- / GPIO20 D+ |
| Audio | I²S codec path, onboard MEMS microphone, external speaker connector |
| Storage | microSD, 4-bit SDIO |
| Expansion | GPIO2, GPIO3, GPIO14, GPIO21 plus shared I²C |
| Other | GPIO42 addressable RGB, GPIO9 battery ADC, GPIO0 boot key |

Landscape logical resolution is 480×320. Rotation and touch transforms must be verified on hardware.

## Verified on the owned board

On 2026-11-24, `esptool` and the running NEXUS firmware confirmed:

| Capability | Observed value |
| --- | --- |
| MCU | ESP32-S3 QFN56 revision 0.2, 240 MHz capable |
| Flash | 16 MB quad, 3.3 V |
| PSRAM | 8 MB embedded PSRAM |
| USB | Native USB Serial/JTAG; flash hashes verified and bidirectional NEXUS frames exchanged |
| Runtime | Stable status frames at five-second intervals with no reset marker during validation |

The board's printed PCB revision has not yet been recorded, so display, touch, audio, storage, and auxiliary GPIO claims remain documentation-only.

## Pin allocation

| Function | Pins |
| --- | --- |
| LCD | CS 10, DC 46, SCLK 12, MOSI 11, MISO 13, backlight 45, reset shared with CHIP_PU |
| Touch | SDA 16, SCL 15, reset 18, interrupt 17 |
| USB | D- 19, D+ 20 |
| Audio | amplifier enable 1; MCLK 4; BCLK 5; DOUT 6; LRCLK 7; DIN 8; shared I²C 15/16 |
| microSD | CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47 |
| UART0 header | GPIO43 / GPIO44 (manufacturer documents labels inconsistently) |
| Expansion | 2, 3, 14, 21 |

## Corrections and limitations

- The manufacturer table claims Bluetooth BR/EDR. Espressif documents ESP32-S3 as **Bluetooth LE only**; NEXUS does not plan Classic Bluetooth.
- The USB Serial/JTAG controller is fixed-function CDC/JTAG. It supports flashing, debugging, and bidirectional serial communication, not USB hub behavior.
- Native USB disappears during deep sleep and some light-sleep configurations. Host-connected operation must prevent incompatible sleep states.
- Only four uncommitted GPIOs are exposed; the planned encoders, buttons, NFC, RGB, privacy signal, and sensors likely require an I²C GPIO expander.
- The shared I²C bus includes touch and audio. Expansion devices require address and bus-load checks.
- A true microphone privacy switch must interrupt microphone power/data electrically. Firmware-only mute does not meet this requirement.
- The onboard RGB LED is already usable, but enclosure RGB may be separate.

## Physical verification checklist

- Photograph front/back labels and record PCB revision.
- [x] Confirm ESP32-S3 identity and flash/PSRAM through `esptool` and boot logs.
- Compare board continuity/layout with the published schematic.
- Run manufacturer display initialization and verify color order, rotation, backlight polarity, and maximum SPI clock.
- Verify FT6336U coordinates and interrupt/reset polarity in all rotations.
- Confirm audio codec identity and I²C address from the physical board.
- [x] Verify the Type-C connector exposes native USB data and stable reconnect behavior.
- Inventory connector pitch, voltage levels, and current budget before expansion wiring.
