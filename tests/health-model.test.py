from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory() as temp:
    source = Path(temp) / "model.c"
    source.write_text(r"""
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))
enum {
    HealthActivitySleep = 1, HealthActivityWalk = 2, HealthActivityRun = 4,
    HealthActivityOpenWorkout = 8, HealthActivityRestfulSleep = 16,
    HealthIterationDirectionFuture = 0, HealthServiceAccessibilityMaskAvailable = 1,
    HealthMetricStepCount = 1
};
typedef int HealthMetric;
typedef int HealthActivity;
typedef int HealthActivityMask;
typedef struct { int heart_rate_bpm, steps, vmc; bool is_invalid; } HealthMinuteData;
static unsigned char stored[450][256];
static size_t sizes[450];
static int persist_read_data(int key, void *dest, size_t size) {
    if (sizes[key] != size) return -1;
    memcpy(dest, stored[key], size);
    return (int)size;
}
static int persist_write_data(int key, const void *bytes, size_t size) {
    assert(size <= sizeof(stored[key]));
    memcpy(stored[key], bytes, size);
    sizes[key] = size;
    return (int)size;
}
static bool persist_exists(int key) { return sizes[key] > 0; }
static int persist_read_int(int key) {
    int value = 0;
    persist_read_data(key, &value, sizeof(value));
    return value;
}
static int health_service_metric_accessible(int metric, time_t from, time_t to) {
    return HealthServiceAccessibilityMaskAvailable;
}
static int health_service_peek_current_activities(void) { return 0; }
static int health_service_sum(int metric, time_t from, time_t to) { return 0; }

static time_t sleep_start, sleep_end, activity_start, activity_end;
static bool sleep_reported, history_missing;
static void health_service_activities_iterate(int mask, time_t start, time_t end,
        int direction, bool (*callback)(HealthActivity, time_t, time_t, void *), void *context) {
    if (sleep_reported && mask & HealthActivitySleep &&
        sleep_end > start && sleep_start < end)
        callback(HealthActivitySleep, sleep_start, sleep_end, context);
}
static unsigned health_service_get_minute_history(HealthMinuteData *result,
        unsigned capacity, time_t *start, time_t *end) {
    unsigned n = 0;
    for (time_t minute = *start; minute < *end && n < capacity; minute += 60) {
        bool moving = minute >= activity_start && minute < activity_end;
        result[n++] = (HealthMinuteData){
            .steps = moving ? 80 : 0, .vmc = moving ? 600 : 0,
            .heart_rate_bpm = history_missing ? 0 : moving ? 135 : 63,
            .is_invalid = history_missing,
        };
    }
    return n;
}

#include "shared/hrv.h"

static void test_reserve(void) {
    // Midnight is inside a six-minute exercise block.
    const time_t midnight = 1763683200;
    activity_start = midnight - 180;
    activity_end = midnight + 180;
    VGReserve state = { .model = VG_RESERVE_MODEL, .cursor = activity_start,
                        .units = 7500 };
    assert(vg_update_reserve(&state, midnight + 300));
    assert(state.units == 7500 - 6 * 9);
    assert(state.activity == -54 && state.cursor == midnight + 180);
    assert(!vg_update_reserve(&state, midnight + 300));
    VGReserve checkpoint = state;
    history_missing = true;
    assert(vg_update_reserve(&state, midnight + 1800));
    assert(state.units == checkpoint.units && state.observed == checkpoint.observed);
    assert(state.cursor > state.observed);

    // Firmware publishes a native sleep interval after its minutes were read.
    sleep_start = midnight + 300;
    sleep_end = midnight + 900;
    sleep_reported = true;
    assert(vg_update_reserve(&state, midnight + 1860));
    assert(state.units == checkpoint.units + 10 * 12);
    assert(state.sleep_credited_until == (uint32_t)sleep_end);
    assert(state.observed == (uint32_t)sleep_end);
    assert(vg_update_reserve(&state, midnight + 1920));
    assert(state.units == checkpoint.units + 10 * 12);
    history_missing = false;
    sleep_reported = false;

    // A measured return toward baseline can contribute only after recent load.
    vg_reserve_begin_update(&state);
    state.recent_load = 0;
    vg_reserve_minute(&state, midnight + 1920, false, true, 2, 160, 60, (VGStressReading){0});
    vg_reserve_minute(&state, midnight + 1980, false, true, 0, 135, 60, (VGStressReading){0});
    assert(state.cardiac == 0);
    vg_reserve_minute(&state, midnight + 2040, false, true, 0, 66, 60, (VGStressReading){0});
    assert(state.cardiac == 3);
    VGStressReading reading = {.timestamp = midnight, .score = 100};
    vg_reserve_minute(&state, midnight - 60, false, true, 0, 66, 60, reading);
    assert(state.stress == 0);
    vg_reserve_minute(&state, midnight + 3540, false, true, 0, 66, 60, reading);
    assert(state.stress == -5);
    vg_reserve_minute(&state, midnight + 3600, false, true, 0, 66, 60, reading);
    assert(state.stress == -5);
}

static void test_sleep_and_calendar(void) {
    uint32_t now = 1763683200;
    VGSleepSession sessions[] = {
        {now - 3600, now - 1800, 1800},
        {now - 16 * 3600, now - 8 * 3600, 8 * 3600},
        {now - 40 * 3600, now - 31 * 3600, 9 * 3600},
    };
    assert(vg_main_sleep(sessions, 3, now) == 1);
    uint32_t before = vg_calendar_day(2024, 365);
    uint32_t after = vg_calendar_day(2025, 2);
    assert(after - before == 3);
    assert(vg_trend_slot(after) == (vg_trend_slot(before) + 3) % 7);
    assert(vg_calendar_day(2026, 66) - vg_calendar_day(2026, 65) == 1);
    assert(vg_hr_band(0) == -1 && vg_hr_band(125) == 3 && vg_hr_band(75) == 0);
}

static void test_scan_and_readiness(void) {
    uint32_t start = 1763683200;
    VGPulseScan scan = {.started = start};
    for (int i = 5; i < 60; ++i)
        vg_record_ppi(&scan, start + i, i % 2 ? 1015 : 985);
    assert(vg_rmssd(&scan) == 30);
    assert(vg_ppi_quality(&scan, start + 60) == 2);
    scan.ppi_duration = 20000;
    assert(vg_ppi_quality(&scan, start + 60) < 2);
    assert(vg_ppi_quality(&scan, start + 70) == 0);
    VGPulseScan gaps = {.started = start};
    vg_record_ppi(&gaps, start + 5, 1000);
    vg_record_ppi(&gaps, start + 15, 980);
    assert(gaps.pairs == 0);
    vg_record_ppi(&gaps, start + 16, 0);
    assert(gaps.rejected == 1 && gaps.previous == 0);

    for (int i = 1; i <= 7; ++i)
        vg_record_resting_hrv(VG_WORKER_HRV_DAYS_KEY, start - i * 86400, 30 + i);
    vg_record_resting_hrv(VG_HRV_DAYS_KEY, start - 86400 + 60, 200);
    vg_record_resting_hrv(VG_HRV_DAYS_KEY, start + 3600, 195);
    int days;
    assert(vg_resting_hrv(start, true, start, &days) == 34 && days == 7);
    assert(vg_resting_hrv(start, false, start + 7200, &days) == 195);

    unsigned used;
    assert(vg_readiness(82, 0, 0, 0, 0, &used) == 82 && used == 1);
    int good = vg_readiness(82, 60, 60, 55, 40, &used);
    assert(used == 7 && good > vg_readiness(82, 72, 60, 25, 40, &used));
    assert(vg_readiness(-1, 0, 0, 0, 0, &used) == -1);
}

int main(void) {
    setenv("TZ", "UTC0", 1);
    tzset();
    test_reserve();
    test_sleep_and_calendar();
    test_scan_and_readiness();
    return 0;
}
""")
    binary = Path(temp) / "health-model"
    subprocess.run(
        ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare", "-I", str(ROOT),
         str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
print("VitalGauge reserve continuity, gaps, late sleep, date indexing, pulse quality and readiness passed")
