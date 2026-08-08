#pragma once
#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AURA UI shell
 *
 * Navigation model:
 *   watch face / lock screen -> numeric passcode -> static 2x3 app launcher
 *   BOOT short = app back / lock, BOOT long = lock immediately
 *
 *  BOOT short = back to FACE tile. BOOT long = home + reset scroll positions.
 *
 * Design rules (AMOLED + "no small clicks"):
 *  - true black (#000000) backgrounds everywhere: unlit pixels = saved battery
 *  - every tappable row/button >= AURA_MIN_TOUCH_TARGET px tall, full width
 *  - one accent color, large type, no chrome
 */

/* Theme */
#define AURA_COL_BG      lv_color_black()
#define AURA_COL_TEXT    lv_color_hex(0xF2F2F2)
#define AURA_COL_MUTED   lv_color_hex(0x8A8A8E)
#define AURA_COL_ACCENT  lv_color_hex(0x8290FF)   /* restrained indigo */
#define AURA_COL_WARN    lv_color_hex(0xFF9F0A)
#define AURA_COL_OK      lv_color_hex(0x30D158)

/* Rounded-glass safe area.  The top mask reaches farther into the panel than
 * the side corners, so screens use asymmetric insets. */
#define AURA_SCREEN_SAFE_SIDE   24
#define AURA_SCREEN_SAFE_TOP    40
#define AURA_SCREEN_SAFE_BOTTOM 30

esp_err_t aura_ui_start(void);

/* Each app owns one static screen. The shell builds each once and only changes
 * visibility, avoiding large framebuffer swipe animations. */
typedef void (*aura_tile_builder_t)(lv_obj_t *tile);

/* Shell helpers apps can use */
lv_obj_t *aura_ui_big_button(lv_obj_t *parent, const char *txt, lv_color_t col);
lv_obj_t *aura_ui_list_row(lv_obj_t *parent, const char *title, const char *sub);
void      aura_ui_apply_safe_area(lv_obj_t *obj);
void      aura_ui_go_home(void);
void      aura_ui_show_notifications(void);
void      aura_ui_close_notifications(void);
esp_err_t aura_ui_reset_passcode(void);

/* Built-in tile builders (registered by aura_ui_start) */
void tile_watchface_build(lv_obj_t *tile);
void tile_watchface_set_visible(bool visible);
void tile_clock_build(lv_obj_t *tile);
void tile_calendar_build(lv_obj_t *tile);   /* Phase 3: stub list for now */
void tile_mission_build(lv_obj_t *tile);    /* Phase 3: stub list for now */
void tile_memory_build(lv_obj_t *tile);     /* Phase 4: stub for now */
void tile_conversation_build(lv_obj_t *tile);
void tile_conversation_set_level(uint8_t level);
void tile_settings_build(lv_obj_t *tile);
void panel_quick_build(lv_obj_t *panel);
void panel_notif_build(lv_obj_t *panel);    /* Phase 3: ANCS feeds this */

#ifdef __cplusplus
}
#endif
