#include "aura_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aura_core.h"
#include "aura_sync.h"
#include "aura_hw.h"
#include "bsp/esp-bsp.h"

static lv_obj_t *s_calendar_list;
static lv_obj_t *s_calendar_status;
static lv_obj_t *s_mission_list;
static lv_obj_t *s_mission_status;
static lv_obj_t *s_memory_list;
static lv_obj_t *s_memory_status;
static lv_obj_t *s_capture_toggle_label;
static int s_expanded_task_id = -1;

static void render_mission(void);

static const char *audio_mode_text(void)
{
    switch (aura_mode_get()) {
    case AURA_MODE_STEALTH: return "Stealth · rolling 5 min";
    case AURA_MODE_CAPTURE: return "Capture · preserving audio";
    case AURA_MODE_CONVERSATION: return "Conversation · live";
    default: return "Silent · microphone off";
    }
}

static const char *audio_mode_button(void)
{
    switch (aura_mode_get()) {
    case AURA_MODE_STEALTH: return LV_SYMBOL_EYE_OPEN "  Mode: Stealth";
    case AURA_MODE_CAPTURE: return LV_SYMBOL_AUDIO "  Mode: Capture";
    default: return LV_SYMBOL_MUTE "  Mode: Silent";
    }
}

static void style_list(lv_obj_t *list)
{
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
}

static void style_connected_tile(lv_obj_t *tile, const char *title_text,
                                 lv_obj_t **status, lv_obj_t **list)
{
    lv_obj_set_style_bg_color(tile, AURA_COL_BG, 0);
    aura_ui_apply_safe_area(tile);
    lv_obj_set_style_pad_row(tile, 8, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

    *status = lv_label_create(tile);
    lv_obj_set_style_text_font(*status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(*status, AURA_COL_MUTED, 0);

    *list = lv_obj_create(tile);
    style_list(*list);
}

static void render_calendar(void)
{
    if (!s_calendar_list) return;
    aura_sync_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return;
    aura_sync_snapshot(snapshot);
    lv_label_set_text(s_calendar_status, aura_sync_status_text());
    lv_obj_clean(s_calendar_list);
    if (!snapshot->has_cache) {
        lv_obj_t *label = lv_label_create(s_calendar_list);
        lv_label_set_text(label, "No server digest yet\nConfigure Aura Server from the watch IP.");
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_color(label, AURA_COL_MUTED, 0);
        free(snapshot);
        return;
    }
    if (snapshot->event_count == 0) {
        lv_obj_t *label = lv_label_create(s_calendar_list);
        lv_label_set_text(label, "No events in the next 48 hours");
        lv_obj_set_style_text_color(label, AURA_COL_MUTED, 0);
        free(snapshot);
        return;
    }
    for (uint8_t i = 0; i < snapshot->event_count; ++i) {
        aura_sync_event_t *event = &snapshot->events[i];
        struct tm local;
        localtime_r(&event->start_epoch, &local);
        char subtitle[96];
        if (event->all_day)
            strftime(subtitle, sizeof(subtitle), "%a · all day", &local);
        else
            strftime(subtitle, sizeof(subtitle), "%a %b %e · %I:%M %p", &local);
        if (event->calendar[0]) {
            size_t used = strlen(subtitle);
            snprintf(subtitle + used, sizeof(subtitle) - used, " · %s", event->calendar);
        }
        aura_ui_list_row(s_calendar_list, event->title, subtitle);
    }
    free(snapshot);
}

static void task_expand_cb(lv_event_t *event)
{
    int task_id = (int)(intptr_t)lv_event_get_user_data(event);
    s_expanded_task_id = s_expanded_task_id == task_id ? -1 : task_id;
    render_mission();
}

static void task_complete_cb(lv_event_t *event)
{
    int task_id = (int)(intptr_t)lv_event_get_user_data(event);
    lv_obj_add_state(lv_event_get_target(event), LV_STATE_DISABLED);
    (void)aura_sync_complete_task(task_id);
}

static void task_delete_cb(lv_event_t *event)
{
    int task_id = (int)(intptr_t)lv_event_get_user_data(event);
    lv_obj_add_state(lv_event_get_target(event), LV_STATE_DISABLED);
    (void)aura_sync_delete_task(task_id);
}

static void render_mission(void)
{
    if (!s_mission_list) return;
    aura_sync_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return;
    aura_sync_snapshot(snapshot);
    lv_label_set_text(s_mission_status, aura_sync_status_text());
    lv_obj_clean(s_mission_list);
    if (!snapshot->has_cache) {
        lv_obj_t *label = lv_label_create(s_mission_list);
        lv_label_set_text(label, "No Mission Control data yet");
        lv_obj_set_style_text_color(label, AURA_COL_MUTED, 0);
        free(snapshot);
        return;
    }
    if (snapshot->task_count == 0) {
        lv_obj_t *label = lv_label_create(s_mission_list);
        lv_label_set_text(label, LV_SYMBOL_OK "  You are caught up");
        lv_obj_set_style_text_color(label, AURA_COL_OK, 0);
        free(snapshot);
        return;
    }
    for (uint8_t i = 0; i < snapshot->task_count; ++i) {
        aura_sync_task_t *task = &snapshot->tasks[i];
        char subtitle[96];
        if (task->due_at[0] && strlen(task->due_at) >= 16)
            snprintf(subtitle, sizeof(subtitle), "%s · due %.10s %.5s%s",
                     task->project, task->due_at, task->due_at + 11,
                     task->priority >= 2 ? " · high" : "");
        else
            snprintf(subtitle, sizeof(subtitle), "%s%s", task->project,
                     task->priority >= 2 ? " · high priority" : "");
        lv_obj_t *row = aura_ui_list_row(s_mission_list, task->title, subtitle);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, task_expand_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)task->id);
        if (task->id == s_expanded_task_id) {
            lv_obj_set_height(row, 178);
            lv_obj_t *done = lv_button_create(row);
            lv_obj_set_size(done, 138, 62);
            lv_obj_set_style_bg_color(done, lv_color_hex(0x164B2C), 0);
            lv_obj_align(done, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            lv_obj_t *label = lv_label_create(done);
            lv_label_set_text(label, LV_SYMBOL_OK "  Done");
            lv_obj_center(label);
            lv_obj_add_event_cb(done, task_complete_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)task->id);

            lv_obj_t *remove = lv_button_create(row);
            lv_obj_set_size(remove, 138, 62);
            lv_obj_set_style_bg_color(remove, lv_color_hex(0x4A1D22), 0);
            lv_obj_align(remove, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
            label = lv_label_create(remove);
            lv_label_set_text(label, LV_SYMBOL_TRASH "  Delete");
            lv_obj_center(label);
            lv_obj_add_event_cb(remove, task_delete_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)task->id);
        }
    }
    free(snapshot);
}

static void proposal_cb(lv_event_t *event)
{
    intptr_t action = (intptr_t)lv_event_get_user_data(event);
    bool approved = action > 0;
    int proposal_id = (int)(approved ? action : -action);
    lv_obj_add_state(lv_event_get_target(event), LV_STATE_DISABLED);
    (void)aura_sync_resolve_proposal(proposal_id, approved);
}

static void render_memory(void)
{
    if (!s_memory_list) return;
    aura_sync_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return;
    aura_sync_snapshot(snapshot);
    aura_capture_status_t capture;
    aura_hw_capture_status(&capture);
    char status[96];
    snprintf(status, sizeof(status), "%s · %u queued · %s",
             audio_mode_text(),
             capture.queued_files, aura_sync_status_text());
    lv_label_set_text(s_memory_status, status);
    if (s_capture_toggle_label)
        lv_label_set_text(s_capture_toggle_label, audio_mode_button());
    lv_obj_clean(s_memory_list);

    lv_obj_t *capture_row = aura_ui_list_row(
        s_memory_list, capture.recording ? LV_SYMBOL_AUDIO "  Recording now" : capture.status,
        capture.storage_ready ? "Five-minute pre-roll · microSD outbox"
                              : "Use one MBR partition formatted as FAT32");
    (void)capture_row;
    if (snapshot->proposal_count == 0) {
        lv_obj_t *label = lv_label_create(s_memory_list);
        lv_label_set_text(label, "Nothing waiting for review");
        lv_obj_set_style_text_color(label, AURA_COL_MUTED, 0);
        free(snapshot);
        return;
    }
    for (uint8_t i = 0; i < snapshot->proposal_count; ++i) {
        aura_sync_proposal_t *proposal = &snapshot->proposals[i];
        lv_obj_t *card = lv_obj_create(s_memory_list);
        lv_obj_set_size(card, lv_pct(100), 184);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x111114), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *title = lv_label_create(card);
        lv_label_set_text(title, proposal->title);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *kind = lv_label_create(card);
        lv_label_set_text(kind, proposal->kind);
        lv_obj_set_style_text_color(kind, AURA_COL_MUTED, 0);
        lv_obj_align(kind, LV_ALIGN_TOP_LEFT, 0, 34);
        lv_obj_t *approve = lv_button_create(card);
        lv_obj_set_size(approve, 154, 70);
        lv_obj_set_style_bg_color(approve, lv_color_hex(0x164B2C), 0);
        lv_obj_align(approve, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_t *label = lv_label_create(approve);
        lv_label_set_text(label, LV_SYMBOL_OK " Approve"); lv_obj_center(label);
        lv_obj_add_event_cb(approve, proposal_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)proposal->id);
        lv_obj_t *reject = lv_button_create(card);
        lv_obj_set_size(reject, 154, 70);
        lv_obj_set_style_bg_color(reject, lv_color_hex(0x4A1D22), 0);
        lv_obj_align(reject, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        label = lv_label_create(reject);
        lv_label_set_text(label, LV_SYMBOL_CLOSE " Reject"); lv_obj_center(label);
        lv_obj_add_event_cb(reject, proposal_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)-proposal->id);
    }
    free(snapshot);
}

static void capture_toggle_cb(lv_event_t *event)
{
    (void)event;
    aura_mode_t current = aura_mode_get();
    aura_mode_set(current == AURA_MODE_SILENT ? AURA_MODE_STEALTH
                  : current == AURA_MODE_STEALTH ? AURA_MODE_CAPTURE
                                                 : AURA_MODE_SILENT);
    render_memory();
}

static void digest_changed(void *handler, esp_event_base_t base, int32_t id, void *data)
{
    (void)handler; (void)base; (void)id; (void)data;
    bsp_display_lock(0);
    render_calendar();
    render_mission();
    render_memory();
    bsp_display_unlock();
}

void tile_calendar_build(lv_obj_t *tile)
{
    style_connected_tile(tile, "Calendar", &s_calendar_status, &s_calendar_list);
    render_calendar();
}

void tile_mission_build(lv_obj_t *tile)
{
    style_connected_tile(tile, "Mission Control", &s_mission_status, &s_mission_list);
    render_mission();
    (void)aura_subscribe(AURA_EVT_DIGEST_UPDATED, digest_changed, NULL);
}

static void capture_changed(void *handler, esp_event_base_t base, int32_t id, void *data)
{
    (void)handler; (void)base; (void)id; (void)data;
    bsp_display_lock(0);
    render_memory();
    bsp_display_unlock();
}

void tile_memory_build(lv_obj_t *tile)
{
    style_connected_tile(tile, "Capture Options", &s_memory_status, &s_memory_list);
    lv_obj_t *button = aura_ui_big_button(
        tile, audio_mode_button(),
        lv_color_hex(0x1C1C1F));
    /* Place the primary control above the scrollable status/proposal list. */
    lv_obj_move_to_index(button, 2);
    s_capture_toggle_label = lv_obj_get_child(button, 0);
    lv_obj_add_event_cb(button, capture_toggle_cb, LV_EVENT_CLICKED, NULL);
    render_memory();
    (void)aura_subscribe(AURA_EVT_CAPTURE_CHANGED, capture_changed, NULL);
    (void)aura_subscribe(AURA_EVT_MODE_CHANGED, capture_changed, NULL);
}
