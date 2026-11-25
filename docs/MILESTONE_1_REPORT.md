# Milestone 1 report: physical display and touch bring-up

Date: 2026-11-24

## Implemented

- Native ST7796S 4-wire SPI driver at 40 MHz on the documented ES3C40P pins.
- LCDWiki's current ST7796S initialization table, RGB565 format, inversion mode, and landscape MADCTL `0x68`.
- Active-high GPIO45 backlight sequencing after a black framebuffer clear.
- LVGL 8.4.0 partial-render pipeline using a PSRAM draw buffer and internal DMA staging buffer.
- 480×320 NEXUS diagnostic interface with display, touch, and USB status cards.
- FT6336U reset, I²C probe at `0x38`, identity reads, 60 Hz polling, and landscape coordinate transform.
- LVGL pointer input, visible cursor, and interactive touch verification card.
- Extended `device.status` payload with display, touch, UI, heap, and coordinate health.

## Physical evidence

| Check | Result |
| --- | --- |
| Firmware 0.2.0 build | Passed; `0x6c000` bytes, 58% of 1 MiB app partition free |
| Flash write | Passed; bootloader, partition table, and application hashes verified |
| Display driver | `display_ready: true` across consecutive hardware status frames |
| LVGL UI | `ui_ready: true` across consecutive hardware status frames |
| Touch controller | `touch_ready: true`, I²C address `0x38`, chip ID `0x64` |
| Touch data | Live pressed/released states and in-range landscape coordinates observed |
| USB regression | Welcome/ping/pong correlation and status framing passed after UI startup |
| Stability | Zero reboot markers; stable free heap of 8,612,048 bytes during validation |

## Source basis

- LCDWiki ES3C40P product and pin record: <https://www.lcdwiki.com/4.0inch_ESP32-S3_Display>
- LCDWiki ST7796S initialization code: <https://www.lcdwiki.com/res/ES3C40P/ST7796_Init.txt>
- LCDWiki ESP-IDF demo description (ESP-IDF 5.4.1 / LVGL 8.4): <https://www.lcdwiki.com/res/ES3C40P/4.0inch_ES3C40P_ESP-IDF_Demo_Instructions.pdf>
- FT6336U datasheet: <https://www.lcdwiki.com/res/ES3C40P/DFT6336UDataSheetV1.1.pdf>
- LVGL 8.4 component: <https://components.espressif.com/components/lvgl/lvgl/versions/8.4.0/readme>

## Remaining physical signoff

- Visually confirm RGB color order and that the NEXUS header reads left-to-right in 480×320 landscape.
- Tap all four corners and the center to verify the selected clockwise touch transform.
- Photograph the board labels and record the PCB revision.
