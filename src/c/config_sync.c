#include "vitalgauge.h"
#include "message_keys.auto.h"

// Phone-to-watch settings must carry an explicit save marker.
// A stale AppSync/Clay replay without the marker cannot modify watch preferences.
#define VG_SAVE_MARKER 0x5647

static void inbox_received(DictionaryIterator *iter, void *context) {
    Tuple *save = dict_find(iter, MESSAGE_KEY_VG_SETTINGS_SAVE);
    if (!save || (int32_t)save->value->int32 != VG_SAVE_MARKER) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Ignoring unsolicited settings message");
        return;
    }

    Tuple *dark = dict_find(iter, MESSAGE_KEY_VG_DARK_MODE);
    Tuple *breath = dict_find(iter, MESSAGE_KEY_VG_BREATH_STYLE);
    Tuple *haptic = dict_find(iter, MESSAGE_KEY_VG_HAPTIC_LEVEL);
    Tuple *backlight = dict_find(iter, MESSAGE_KEY_VG_BACKLIGHT);

    if (dark && dark->value->int32 <= 1) {
        g_dark_mode = dark->value->int32 != 0;
        int result = persist_write_int(PERSIST_KEY_DARK_MODE, g_dark_mode ? 1 : 0);
        APP_LOG(APP_LOG_LEVEL_INFO, "Explicit phone dark mode save=%d persisted=%d",
                g_dark_mode, result);
        ui_hub_mark_dirty();
        ui_stress_mark_dirty();
        ui_sleep_mark_dirty();
    }
    if (breath && breath->value->int32 <= 2) {
        g_breath_style = breath->value->int32;
        persist_write_int(PERSIST_KEY_BREATH_STYLE, g_breath_style);
    }
    if (haptic && haptic->value->int32 <= 2) {
        g_haptic_level = haptic->value->int32;
        persist_write_int(PERSIST_KEY_HAPTIC_LEVEL, g_haptic_level);
    }
    if (backlight && backlight->value->int32 <= 1) {
        g_backlight_stay_on = backlight->value->int32 != 0;
        persist_write_bool(PERSIST_KEY_BACKLIGHT, g_backlight_stay_on);
    }
}

void config_sync_init(void) {
    if (persist_exists(PERSIST_KEY_VOLUME)) { 
        g_volume_level = persist_read_int(PERSIST_KEY_VOLUME);
    }
    // Read the legacy bool format (1 byte) or the new integer format (4 bytes).
    int dark_size = persist_get_size(PERSIST_KEY_DARK_MODE);
    g_dark_mode = dark_size == (int)sizeof(int32_t)
        ? persist_read_int(PERSIST_KEY_DARK_MODE) != 0
        : dark_size == (int)sizeof(bool)
            ? persist_read_bool(PERSIST_KEY_DARK_MODE) : false;
    APP_LOG(APP_LOG_LEVEL_INFO, "Dark mode startup: value=%d persisted_size=%d", g_dark_mode, dark_size);
    g_breath_style = persist_exists(PERSIST_KEY_BREATH_STYLE) ? persist_read_int(PERSIST_KEY_BREATH_STYLE) : 0;
    g_haptic_level = persist_exists(PERSIST_KEY_HAPTIC_LEVEL) ? persist_read_int(PERSIST_KEY_HAPTIC_LEVEL) : 2;
    g_backlight_stay_on = persist_exists(PERSIST_KEY_BACKLIGHT) ? persist_read_bool(PERSIST_KEY_BACKLIGHT) : false;
    app_message_register_inbox_received(inbox_received);
}

void config_sync_deinit(void) {
    app_message_deregister_callbacks();
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
