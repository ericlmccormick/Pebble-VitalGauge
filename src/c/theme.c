#include "vitalgauge.h"

#define VG_SOLAR_CACHE_KEY 436

bool g_dark_mode;
VGThemeMode g_theme_mode;
static VGSolarDay s_days[2];

void theme_init(void) {
    const int preference_size = persist_get_size(PERSIST_KEY_DARK_MODE);
    int mode = VG_THEME_AUTO;
    if (preference_size == sizeof(int32_t))
        mode = persist_read_int(PERSIST_KEY_DARK_MODE);
    else if (preference_size == sizeof(bool))
        mode = persist_read_bool(PERSIST_KEY_DARK_MODE) ? VG_THEME_NIGHT : VG_THEME_DAY;
    g_theme_mode = mode >= VG_THEME_DAY && mode <= VG_THEME_AUTO ? mode : VG_THEME_AUTO;
    if (persist_read_data(VG_SOLAR_CACHE_KEY, s_days, sizeof(s_days)) != sizeof(s_days))
        memset(s_days, 0, sizeof(s_days));
    g_dark_mode = vg_theme_is_dark(g_theme_mode, time(NULL), s_days, ARRAY_LENGTH(s_days));
}

void theme_refresh(void) {
    bool dark = vg_theme_is_dark(g_theme_mode, time(NULL), s_days, ARRAY_LENGTH(s_days));
    if (dark == g_dark_mode) return;
    g_dark_mode = dark;
    ui_health_views_mark_dirty();
    ui_settings_refresh();
}

void theme_set_mode(int mode) {
    if (mode < VG_THEME_DAY || mode > VG_THEME_AUTO || mode == g_theme_mode) return;
    g_theme_mode = mode;
    persist_write_int(PERSIST_KEY_DARK_MODE, mode);
    theme_refresh();
    ui_settings_refresh();
}

void theme_cache_solar(VGSolarDay today, VGSolarDay tomorrow) {
    time_t now = time(NULL);
    time_t next_date = now + 24 * 60 * 60;
    // Use a noon anchor for the next date so DST transitions near midnight
    // don't choose a date two days out or repeat today's date.
    struct tm *local = localtime(&now);
    if (local) next_date = now + (36 - local->tm_hour) * 60 * 60;
    const VGSolarDay incoming[] = {today, tomorrow};
    const time_t dates[] = {now, next_date};
    VGSolarDay updated[2] = {{0}};
    for (unsigned day = 0; day < ARRAY_LENGTH(dates); ++day) {
        for (unsigned i = 0; i < ARRAY_LENGTH(s_days); ++i) {
            if (vg_solar_day_valid(s_days[i]) && vg_same_date(s_days[i].sunrise, dates[day]))
                updated[day] = s_days[i];
        }
        for (unsigned i = 0; i < ARRAY_LENGTH(incoming); ++i) {
            if (vg_solar_day_valid(incoming[i]) && vg_same_date(incoming[i].sunrise, dates[day]))
                updated[day] = incoming[i];
        }
    }
    if (memcmp(s_days, updated, sizeof(s_days))) {
        memcpy(s_days, updated, sizeof(s_days));
        persist_write_data(VG_SOLAR_CACHE_KEY, s_days, sizeof(s_days));
    }
    theme_refresh();
}
