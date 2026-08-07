#pragma once
/**
 * AURA OS — global board + behavior configuration
 * Board: Waveshare ESP32-S3-Touch-AMOLED-2.06 (ESP32-S3R8, 8MB PSRAM, 32MB flash)
 *
 * Pin sources: Waveshare demo repo (examples/arduino/libraries/Mylibrary/pin_config.h)
 * and board wiki. Anything marked VERIFY should be checked once against the
 * official schematic PDF before relying on it.
 */

/* ---------- Display (handled by BSP, listed for reference) ---------- */
#define AURA_LCD_H_RES        410
#define AURA_LCD_V_RES        502
/* CO5300 over QSPI: SDIO0..3 = 4,5,6,7 | SCLK=11 | CS=12 | RST=8 */

/* ---------- Touch: FT3168 over I2C ---------- */
#define AURA_I2C_SDA          15
#define AURA_I2C_SCL          14
#define AURA_TP_INT           38   /* wake source for double-tap */
#define AURA_TP_RST           9

/* ---------- TF card (SDMMC 1-bit) ---------- */
#define AURA_SD_CLK           2
#define AURA_SD_CMD           1
#define AURA_SD_DATA0         3

/* ---------- Buttons ---------- */
#define AURA_BTN_BOOT         0    /* side button: BACK (short) / HOME (long) */
/* PWR side button is wired to the AXP2101 PWRON input, not a GPIO.
 * We receive it via the PMU IRQ (short-press / long-press events).
 * VERIFY: AXP2101 IRQ line GPIO on the schematic; polling fallback is used
 * until confirmed. */

/* ---------- Peripherals on shared I2C bus ---------- */
#define AURA_ADDR_AXP2101     0x34
#define AURA_ADDR_PCF85063    0x51
#define AURA_ADDR_QMI8658     0x6B  /* VERIFY: 0x6A if SA0 low */

/* ---------- Power behavior ---------- */
#define AURA_SLEEP_TIMEOUT_MS       30000  /* user requirement: 30 s */
#define AURA_DIM_TIMEOUT_MS         20000  /* dim before sleeping */
#define AURA_DIM_BRIGHTNESS_PCT     15
#define AURA_ACTIVE_BRIGHTNESS_PCT  85
#define AURA_DOUBLE_TAP_WINDOW_MS   400    /* max gap between the two taps */
#define AURA_DOUBLE_TAP_MAX_DIST    60     /* px between taps to count */

/* ---------- UI ---------- */
#define AURA_MIN_TOUCH_TARGET       80     /* no small clicks: min px hit area */
