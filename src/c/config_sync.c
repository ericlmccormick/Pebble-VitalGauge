#include "vitalgauge.h"
#include "message_keys.auto.h" // Generated from package.json pebble.messageKeys

// ============================================================================
// --- SECTION 1: APPSYNC KEYS AND STATE ---
// ============================================================================
static AppSync s_sync;
static uint8_t s_sync_buffer[256];
static bool s_initializing_sync;

enum AppSyncKeys {
    SYNC_VOLUME_KEY = 0,
    SYNC_TARGET_HR_KEY = 1
};

// ============================================================================
// --- SECTION 2: SYNC CALLBACKS ---
// ============================================================================
static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
    if (key == SYNC_VOLUME_KEY) {
            g_volume_level = new_tuple->value->int32;
            persist_write_int(PERSIST_KEY_VOLUME, g_volume_level);
            ui_stress_mark_dirty();
    } else if (key == SYNC_TARGET_HR_KEY) {
            g_target_hr = new_tuple->value->int32;
    } else if (key == MESSAGE_KEY_VG_DARK_MODE) {
            if (s_initializing_sync) return;
            g_dark_mode = new_tuple->value->int32 != 0;
            persist_write_bool(PERSIST_KEY_DARK_MODE, g_dark_mode);
            ui_hub_mark_dirty();
            ui_stress_mark_dirty();
            ui_sleep_mark_dirty();
    } else if (key == MESSAGE_KEY_VG_BREATH_STYLE) {
            if (s_initializing_sync) return;
            if (new_tuple->value->int32 >= 0 && new_tuple->value->int32 <= 2) {
                g_breath_style = new_tuple->value->int32;
                persist_write_int(PERSIST_KEY_BREATH_STYLE, g_breath_style);
            }
    } else if (key == MESSAGE_KEY_VG_HAPTIC_LEVEL) {
            if (s_initializing_sync) return;
            if (new_tuple->value->int32 >= 0 && new_tuple->value->int32 <= 2) {
                g_haptic_level = new_tuple->value->int32;
                persist_write_int(PERSIST_KEY_HAPTIC_LEVEL, g_haptic_level);
            }
    } else if (key == MESSAGE_KEY_VG_BACKLIGHT) {
            if (s_initializing_sync) return;
            g_backlight_stay_on = new_tuple->value->int32 != 0;
            persist_write_bool(PERSIST_KEY_BACKLIGHT, g_backlight_stay_on);
    }
}

static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "AppSync Error: %d", app_message_error);
}

// ============================================================================
// --- SECTION 3: SYNC INIT/DEINIT ---
// ============================================================================
void config_sync_init() {
    if (persist_exists(PERSIST_KEY_VOLUME)) { 
        g_volume_level = persist_read_int(PERSIST_KEY_VOLUME);
    }
    g_dark_mode = persist_exists(PERSIST_KEY_DARK_MODE) ? persist_read_bool(PERSIST_KEY_DARK_MODE) : false;
    g_breath_style = persist_exists(PERSIST_KEY_BREATH_STYLE) ? persist_read_int(PERSIST_KEY_BREATH_STYLE) : 0;
    g_haptic_level = persist_exists(PERSIST_KEY_HAPTIC_LEVEL) ? persist_read_int(PERSIST_KEY_HAPTIC_LEVEL) : 2;
    g_backlight_stay_on = persist_exists(PERSIST_KEY_BACKLIGHT) ? persist_read_bool(PERSIST_KEY_BACKLIGHT) : false;
    Tuplet initial_values[] = {
        TupletInteger(SYNC_VOLUME_KEY, g_volume_level),
        TupletInteger(SYNC_TARGET_HR_KEY, 60),
        TupletInteger(MESSAGE_KEY_VG_DARK_MODE, g_dark_mode),
        TupletInteger(MESSAGE_KEY_VG_BREATH_STYLE, g_breath_style),
        TupletInteger(MESSAGE_KEY_VG_HAPTIC_LEVEL, g_haptic_level),
        TupletInteger(MESSAGE_KEY_VG_BACKLIGHT, g_backlight_stay_on)
    };
    s_initializing_sync = true;
    app_sync_init(&s_sync, s_sync_buffer, sizeof(s_sync_buffer), initial_values, ARRAY_LENGTH(initial_values), sync_tuple_changed_callback, sync_error_callback, NULL);
    s_initializing_sync = false;
}

void config_sync_deinit() {
    app_sync_deinit(&s_sync);
}
void config_sync_send_settings(void) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK || !iter) return;
    dict_write_int32(iter, MESSAGE_KEY_VG_DARK_MODE, g_dark_mode ? 1 : 0);
    dict_write_int32(iter, MESSAGE_KEY_VG_BREATH_STYLE, g_breath_style);
    dict_write_int32(iter, MESSAGE_KEY_VG_HAPTIC_LEVEL, g_haptic_level);
    dict_write_int32(iter, MESSAGE_KEY_VG_BACKLIGHT, g_backlight_stay_on ? 1 : 0);
    app_message_outbox_send();
}
