#pragma once
#include "health_model.h"

static inline uint32_t vg_local_day(time_t stamp) {
    struct tm *date = localtime(&stamp);
    return date ? vg_calendar_day(date->tm_year + 1900, date->tm_yday) : 0;
}

static inline VGStressReading vg_saved_stress(void) {
    VGStressReading reading = {0};
    if (persist_read_data(VG_STRESS_KEY, &reading, sizeof(reading)) != sizeof(reading))
        return (VGStressReading){0};
    return reading;
}

static inline bool vg_metric_available(HealthMetric metric, time_t start, time_t end) {
    return (health_service_metric_accessible(metric, start, end) &
            HealthServiceAccessibilityMaskAvailable) != 0;
}

static inline VGReserve vg_read_reserve(int key, time_t now) {
    VGReserve reserve = {0};
    if (persist_read_data(key, &reserve, sizeof(reserve)) != sizeof(reserve) ||
        reserve.model != VG_RESERVE_MODEL || reserve.units > 10000 ||
        reserve.cursor > (uint32_t)now || reserve.observed > reserve.cursor)
        return (VGReserve){0};
    return reserve;
}

static inline VGReserve vg_latest_reserve(time_t now) {
    VGReserve foreground = vg_read_reserve(VG_FOREGROUND_RESERVE_KEY, now);
    VGReserve worker = vg_read_reserve(VG_WORKER_RESERVE_KEY, now);
    return worker.cursor > foreground.cursor ? worker : foreground;
}

typedef struct {
    time_t start;
    bool asleep[30];
    bool workout[30];
} VGReserveWindow;

static inline bool vg_flag_activity(HealthActivity activity, time_t start,
                                    time_t end, void *context) {
    VGReserveWindow *window = context;
    for (int i = 0; i < 30; ++i) {
        time_t minute = window->start + i * 60;
        if (minute + 30 < start || minute + 30 >= end) continue;
        if (activity == HealthActivitySleep) window->asleep[i] = true;
        else window->workout[i] = true;
    }
    return true;
}

typedef struct {
    VGReserve *reserve;
    time_t start, end;
} VGLateSleep;

static inline bool vg_credit_late_sleep(HealthActivity activity, time_t start,
                                        time_t end, void *context) {
    VGLateSleep *window = context;
    VGReserve *reserve = window->reserve;
    if (start < window->start) start = window->start;
    if (start < (time_t)reserve->sleep_credited_until) start = reserve->sleep_credited_until;
    if (end > window->end) end = window->end;
    int minutes = (end - start) / 60;
    if (minutes > 0) {
        reserve->recovery += vg_reserve_adjust(reserve, minutes * 12);
        reserve->sleep_credited_until = end;
        if (end > (time_t)reserve->observed) reserve->observed = end;
        reserve->recent_load = 0;
    }
    return true;
}

// Both tasks replay the same completed minutes. Each writes its own checkpoint.
static inline bool vg_update_reserve(VGReserve *reserve, time_t now) {
    time_t end = ((now - 120) / 60) * 60;
    if ((time_t)reserve->cursor >= end) return false;
    time_t start = reserve->cursor;
    time_t limit = end - 24 * 3600;
    if (!start) {
        start = end - 12 * 3600;
        reserve->model = VG_RESERVE_MODEL;
        reserve->units = 5000;
    } else if (start < limit) {
        start = limit;
        reserve->recent_load = 0;
    }
    vg_reserve_begin_update(reserve);
    // Firmware may publish a sleep interval after the minutes it covers have passed.
    if (reserve->cursor && start > limit) {
        VGLateSleep late = {.reserve = reserve, .start = limit, .end = start};
        health_service_activities_iterate(HealthActivitySleep, limit, start,
                                          HealthIterationDirectionFuture,
                                          vg_credit_late_sleep, &late);
    }
    VGStressReading stress = vg_saved_stress();
    time_t baseline_time = persist_exists(VG_REST_BASELINE_TIME_KEY) ?
                           persist_read_int(VG_REST_BASELINE_TIME_KEY) : 0;
    int baseline = baseline_time > 0 && now >= baseline_time &&
                   now - baseline_time < 30 * 86400 ?
                   persist_read_int(VG_REST_BASELINE_KEY) : 0;
    for (time_t chunk = start; chunk < end; chunk += 30 * 60) {
        time_t first = chunk;
        time_t last = chunk + 30 * 60;
        if (last > end) last = end;
        VGReserveWindow window = {.start = chunk};
        health_service_activities_iterate(
            HealthActivitySleep | HealthActivityWalk | HealthActivityRun |
            HealthActivityOpenWorkout, chunk, last, HealthIterationDirectionFuture,
            vg_flag_activity, &window);
        HealthMinuteData data[30];
        time_t returned_end = last;
        unsigned records = health_service_get_minute_history(
            data, 30, &first, &returned_end);
        for (int i = 0; chunk + i * 60 < last; ++i) {
            time_t minute = chunk + i * 60;
            int index = (minute - first) / 60;
            bool valid = minute >= first && index >= 0 && (unsigned)index < records && !data[index].is_invalid;
            int hr = valid ? data[index].heart_rate_bpm : 0;
            if (vg_hr_band(hr) < 0) hr = 0;
            int load = 0;
            if (valid) {
                load = vg_clamp(data[index].steps / 25, 0, 5);
                if (!load && data[index].vmc > 350) load = 1;
                if (window.workout[i] && (hr || data[index].steps || data[index].vmc > 350))
                    load = load < 2 ? 2 : load;
            }
            vg_reserve_minute(reserve, minute, window.asleep[i], valid, load, hr,
                              baseline, stress);
        }
    }
    return true;
}
