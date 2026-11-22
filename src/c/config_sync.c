#include "vitalgauge.h"
#include "message_keys.auto.h"

// Phone-to-watch settings must carry an explicit save marker.
// A stale AppSync/Clay replay without the marker cannot modify watch preferences.
#define VG_SAVE_MARKER 0x5647

static VGSolarDay solar_tuple(DictionaryIterator *iter, uint32_t rise_key, uint32_t set_key) {
    const Tuple *rise = dict_find(iter, rise_key);
    const Tuple *set = dict_find(iter, set_key);
    if (!rise || !set || rise->length != 4 || set->length != 4 ||
        (rise->type != TUPLE_INT && rise->type != TUPLE_UINT) ||
        (set->type != TUPLE_INT && set->type != TUPLE_UINT))
        return (VGSolarDay){0};
    return (VGSolarDay){rise->value->int32, set->value->int32};
}

static void inbox_received(DictionaryIterator *iter, void *context) {
    if (dict_find(iter, MESSAGE_KEY_VG_THEME_REQUEST)) config_sync_send_settings();
    VGSolarDay today = solar_tuple(iter, MESSAGE_KEY_VG_SUNRISE, MESSAGE_KEY_VG_SUNSET);
    VGSolarDay tomorrow = solar_tuple(iter, MESSAGE_KEY_VG_NEXT_SUNRISE, MESSAGE_KEY_VG_NEXT_SUNSET);
    if (vg_solar_day_valid(today) || vg_solar_day_valid(tomorrow))
        theme_cache_solar(today, tomorrow);
    Tuple *save = dict_find(iter, MESSAGE_KEY_VG_SETTINGS_SAVE);
    if (!save || (int32_t)save->value->int32 != VG_SAVE_MARKER) {
        return;
    }

    Tuple *dark = dict_find(iter, MESSAGE_KEY_VG_DARK_MODE);
    Tuple *breath = dict_find(iter, MESSAGE_KEY_VG_BREATH_STYLE);
    Tuple *haptic = dict_find(iter, MESSAGE_KEY_VG_HAPTIC_LEVEL);
    Tuple *background = dict_find(iter, MESSAGE_KEY_BG_SCAN_ENABLED);
    Tuple *alerts = dict_find(iter, MESSAGE_KEY_VG_ALERTS);
    Tuple *backlight = dict_find(iter, MESSAGE_KEY_VG_BACKLIGHT);

    if (dark && dark->length == 4 && (dark->type == TUPLE_INT || dark->type == TUPLE_UINT))
        theme_set_mode(dark->value->int32);
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
    if (background && background->value->uint32 <= 1) {
        g_background_enabled = background->value->uint32 != 0;
        persist_write_bool(VG_BG_KEY, g_background_enabled);
        vitalgauge_apply_worker_setting();
    }
    if (alerts && alerts->value->uint32 <= 1) {
        g_alerts_enabled = alerts->value->uint32 != 0;
        persist_write_bool(VG_ALERT_OPT_IN_KEY, g_alerts_enabled);
    }
}

void config_sync_init(void) {
    if (persist_exists(PERSIST_KEY_VOLUME)) { 
        g_volume_level = persist_read_int(PERSIST_KEY_VOLUME);
    }
    g_breath_style = persist_exists(PERSIST_KEY_BREATH_STYLE) ? persist_read_int(PERSIST_KEY_BREATH_STYLE) : 0;
    g_haptic_level = persist_exists(PERSIST_KEY_HAPTIC_LEVEL) ? persist_read_int(PERSIST_KEY_HAPTIC_LEVEL) : 2;
    g_backlight_stay_on = persist_exists(PERSIST_KEY_BACKLIGHT) ? persist_read_bool(PERSIST_KEY_BACKLIGHT) : false;
    g_background_enabled = persist_exists(VG_BG_KEY) && persist_read_bool(VG_BG_KEY);
    g_alerts_enabled = persist_exists(VG_ALERT_OPT_IN_KEY) && persist_read_bool(VG_ALERT_OPT_IN_KEY);
    app_message_register_inbox_received(inbox_received);
}

void config_sync_deinit(void) {
    app_message_deregister_callbacks();
}

void config_sync_send_settings(void) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK || !iter) return;
    dict_write_int32(iter, MESSAGE_KEY_VG_DARK_MODE, g_theme_mode);
    dict_write_int32(iter, MESSAGE_KEY_VG_BREATH_STYLE, g_breath_style);
    dict_write_int32(iter, MESSAGE_KEY_VG_HAPTIC_LEVEL, g_haptic_level);
    dict_write_int32(iter, MESSAGE_KEY_VG_BACKLIGHT, g_backlight_stay_on ? 1 : 0);
    dict_write_int32(iter, MESSAGE_KEY_BG_SCAN_ENABLED, g_background_enabled ? 1 : 0);
    dict_write_int32(iter, MESSAGE_KEY_VG_ALERTS, g_alerts_enabled ? 1 : 0);
    app_message_outbox_send();
}
