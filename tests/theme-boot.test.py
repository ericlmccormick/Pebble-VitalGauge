from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
theme = (root / "src/c/theme.c").read_text().split("\n", 1)[1]
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "theme.c"
    source.write_text(r"""
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "theme_schedule.h"
#define PERSIST_KEY_DARK_MODE 102
#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))
static int preference_size, preference, solar_size;
static unsigned char solar_bytes[64];
static time_t clock_value;
static time_t fake_time(time_t *out) {
    if (out) *out = clock_value;
    return clock_value;
}
#define time fake_time
static int persist_get_size(int key) { return preference_size; }
static int persist_read_int(int key) { return preference; }
static bool persist_read_bool(int key) { return preference != 0; }
static void persist_write_int(int key, int value) {
    preference = value;
    preference_size = sizeof(int32_t);
}
static int persist_read_data(int key, void *buffer, size_t size) {
    if (solar_size != (int)size) return -1;
    memcpy(buffer, solar_bytes, size);
    return solar_size;
}
static void persist_write_data(int key, const void *buffer, size_t size) {
    memcpy(solar_bytes, buffer, size);
    solar_size = size;
}
static void ui_settings_refresh(void) {}
static void ui_health_views_mark_dirty(void) {}
""" + theme + r"""
int main(void) {
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t midnight = 1795305600 / 86400 * 86400;
    clock_value = midnight + 6 * 3600;
    theme_init();
    assert(g_theme_mode == VG_THEME_AUTO && g_dark_mode);
    VGSolarDay today = {midnight + 5 * 3600, midnight + 17 * 3600};
    VGSolarDay tomorrow = {today.sunrise + 86400, today.sunset + 86400};
    theme_cache_solar(today, tomorrow);
    assert(!g_dark_mode);
    clock_value = today.sunset;
    theme_refresh();
    assert(g_dark_mode);
    memset(s_days, 0, sizeof(s_days));
    clock_value = tomorrow.sunrise;
    theme_init();
    assert(!g_dark_mode);  /* The watch booted without a phone response. */
    theme_cache_solar((VGSolarDay){1, 0}, (VGSolarDay){0});
    theme_init();
    assert(!g_dark_mode);  /* Malformed input did not erase the cache. */
    clock_value += 86400;
    theme_init();
    assert(g_dark_mode);   /* Stale data falls back to local clock. */
    preference_size = sizeof(bool);
    preference = true;
    theme_init();
    assert(g_theme_mode == VG_THEME_NIGHT && g_dark_mode);
    preference_size = sizeof(int32_t);
    preference = VG_THEME_DAY;
    theme_init();
    assert(g_theme_mode == VG_THEME_DAY && !g_dark_mode);
    theme_set_mode(VG_THEME_AUTO);
    theme_init();
    assert(g_theme_mode == VG_THEME_AUTO && g_dark_mode);
}
""")
    binary = Path(directory) / "theme"
    subprocess.run(["cc", "-std=c99", "-I", str(root / "src/c"),
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("VitalGauge offline solar cache, rollover, and legacy preference regressions passed")
