#pragma once
#include <stdbool.h>
#include <time.h>

typedef enum {
    VG_THEME_DAY, VG_THEME_NIGHT, VG_THEME_AUTO
} VGThemeMode;

typedef struct {
    time_t sunrise;
    time_t sunset;
} VGSolarDay;

static inline bool vg_same_date(time_t first, time_t second) {
    const struct tm *local = localtime(&first);
    if (!local) return false;
    const int year = local->tm_year;
    const int day = local->tm_yday;
    local = localtime(&second);
    return local && local->tm_year == year && local->tm_yday == day;
}

static inline bool vg_solar_day_valid(VGSolarDay solar) {
    return solar.sunrise > 0 && solar.sunset > solar.sunrise &&
           solar.sunset - solar.sunrise < 24 * 60 * 60;
}

static inline bool vg_theme_is_dark(VGThemeMode mode, time_t now,
                                    const VGSolarDay *cached, unsigned count) {
    if (mode != VG_THEME_AUTO) return mode == VG_THEME_NIGHT;
    for (unsigned i = 0; i < count; ++i) {
        if (vg_solar_day_valid(cached[i]) && vg_same_date(now, cached[i].sunrise))
            return now < cached[i].sunrise || now >= cached[i].sunset;
    }
    const struct tm *local = localtime(&now);
    return local && (local->tm_hour < 7 || local->tm_hour >= 19);
}
