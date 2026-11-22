#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    VG_BG_KEY = 420,
    VG_STRESS_KEY = 421,
    VG_FOREGROUND_RESERVE_KEY = 422,
    VG_WORKER_RESERVE_KEY = 423,
    VG_REST_HR_KEY = 424,
    VG_TRENDS_KEY = 425,
    VG_HRV_DAYS_KEY = 426,
    VG_WORKER_LAUNCH_CONTEXT_KEY = 414,
    VG_SCAN_BUSY_KEY = 428,
    VG_ALERT_OPT_IN_KEY = 429,
    VG_REST_BASELINE_TIME_KEY = 434,
    VG_REST_BASELINE_KEY = 435,
    VG_STRESS_TTL = 3600,
    VG_RESERVE_FRESHNESS = 20 * 60,
    VG_RESERVE_MODEL = 2,
};

typedef struct {
    uint32_t timestamp;
    uint8_t score;
} VGStressReading;

typedef struct {
    uint32_t cursor;
    uint32_t observed;
    uint32_t sleep_credited_until;
    uint16_t units;
    uint16_t start_units;
    int16_t recovery, cardiac, activity, stress;
    uint8_t model;
    uint8_t recent_load;
} VGReserve;

typedef struct {
    uint32_t day;
    uint32_t timestamp;
    uint16_t value;
} VGDailyReading;

static inline int vg_clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

static inline bool vg_stress_valid(VGStressReading reading, uint32_t now) {
    return reading.timestamp && now >= reading.timestamp &&
           now - reading.timestamp < VG_STRESS_TTL && reading.score <= 100;
}

static inline void vg_reserve_begin_update(VGReserve *reserve) {
    reserve->start_units = reserve->units;
    reserve->recovery = reserve->cardiac = reserve->activity = reserve->stress = 0;
}

static inline int vg_reserve_adjust(VGReserve *reserve, int delta) {
    int previous = reserve->units;
    reserve->units = vg_clamp(previous + delta, 0, 10000);
    return (int)reserve->units - previous;
}

// Units are hundredths of a point. Weights are provisional wellness heuristics.
static inline void vg_reserve_minute(VGReserve *reserve, uint32_t minute,
                                     bool asleep, bool covered, int load,
                                     int hr, int baseline, VGStressReading stress) {
    reserve->cursor = minute + 60;
    if (asleep) {
        if (minute + 60 > reserve->sleep_credited_until) {
            reserve->recovery += vg_reserve_adjust(reserve, 12);
            reserve->sleep_credited_until = minute + 60;
        }
        reserve->recent_load = 0;
    } else if (covered && (hr > 0 || load > 0)) {
        if (load > 0) {
            reserve->activity += vg_reserve_adjust(reserve, -(3 + load * 2));
            reserve->recent_load = 20;
        } else if (reserve->recent_load) {
            // A small recovery credit is possible only after measured activity.
            if (baseline > 0 && hr >= 35 && hr <= baseline + 10)
                reserve->cardiac += vg_reserve_adjust(reserve, 3);
            --reserve->recent_load;
        }
        if (vg_stress_valid(stress, minute) && stress.score > 60)
            reserve->stress += vg_reserve_adjust(reserve, -(stress.score - 60) / 8);
    } else {
        if (reserve->recent_load) --reserve->recent_load;
        return;
    }
    reserve->observed = minute + 60;
}

static inline int vg_hr_band(int bpm) {
    if (bpm < 35 || bpm > 230) return -1;
    return bpm > 120 ? 3 : bpm > 100 ? 2 : bpm > 80 ? 1 : 0;
}

// Turn a local calendar date into a serial day independent of DST and UTC offset.
static inline uint32_t vg_calendar_day(int year, int yday) {
    int previous = year - 1;
    return 365 * previous + previous / 4 - previous / 100 + previous / 400 + yday + 1;
}

static inline int vg_readiness(int sleep, int hr, int hr_baseline,
                               int hrv, int hrv_baseline, unsigned *inputs) {
    int total = 0, weight = 0;
    *inputs = 0;
    if (sleep >= 0) { total += sleep * 60; weight += 60; *inputs |= 1; }
    if (hr > 0 && hr_baseline > 0) {
        total += vg_clamp(85 - (hr - hr_baseline) * 3, 0, 100) * 20;
        weight += 20;
        *inputs |= 2;
    }
    if (hrv > 0 && hrv_baseline > 0) {
        total += vg_clamp(85 + ((hrv - hrv_baseline) * 100) / hrv_baseline, 0, 100) * 20;
        weight += 20;
        *inputs |= 4;
    }
    return weight ? total / weight : -1;
}

typedef struct {
    uint32_t start, end;
    int seconds;
} VGSleepSession;

static inline unsigned vg_main_sleep(const VGSleepSession *sessions, unsigned count,
                                      uint32_t now) {
    unsigned main = 0;
    for (unsigned i = 1; i < count; ++i) {
        if (sessions[main].seconds >= 3 * 3600) break;
        if (sessions[i].end >= now - 30 * 3600 &&
            sessions[i].seconds > sessions[main].seconds)
            main = i;
    }
    return main;
}

static inline int vg_trend_slot(uint32_t day) { return day % 7; }
