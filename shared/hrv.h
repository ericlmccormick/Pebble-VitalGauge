#pragma once
#include "watch_health.h"

enum { VG_WORKER_HRV_DAYS_KEY = 430, VG_WORKER_HRV_LATEST_KEY = 431 };

typedef struct {
    uint32_t started, last_event, ppi_duration, squares;
    uint16_t previous, received, rejected, pairs;
} VGPulseScan;

static inline void vg_record_ppi(VGPulseScan *scan, uint32_t now, unsigned ppi) {
    if (now < scan->started + 5) return; // Allow the optical sensor to settle.
    ++scan->received;
    if (scan->last_event && now - scan->last_event > 3) scan->previous = 0;
    scan->last_event = now;
    if (ppi < 400 || ppi > 1500 ||
        (scan->previous && abs((int)ppi - scan->previous) > 250)) {
        ++scan->rejected;
        scan->previous = 0;
        return;
    }
    if (scan->previous) {
        int delta = (int)ppi - scan->previous;
        scan->squares += delta * delta;
        scan->ppi_duration += ppi;
        ++scan->pairs;
    }
    scan->previous = ppi;
}

static inline unsigned vg_rmssd(const VGPulseScan *scan) {
    if (scan->pairs < 20) return 0;
    unsigned mean_square = scan->squares / scan->pairs;
    unsigned root = 0;
    while ((root + 1) * (root + 1) <= mean_square) ++root;
    return root;
}

static inline int vg_ppi_quality(const VGPulseScan *scan, uint32_t now) {
    if (!scan->received || scan->pairs < 20 || now < scan->started + 55 ||
        now < scan->last_event || now - scan->last_event > 8) return 0;
    if (scan->pairs >= 30 && scan->ppi_duration >= 35000 &&
        scan->ppi_duration <= (now - scan->started) * 1000 &&
        scan->rejected * 10 <= scan->received) return 2;
    return scan->rejected * 4 <= scan->received ? 1 : 0;
}

#define VG_HRV_DAYS 20
static inline void vg_record_resting_hrv(int key, time_t now, unsigned value) {
    VGDailyReading days[VG_HRV_DAYS] = {{0}};
    if (persist_read_data(key, days, sizeof(days)) != sizeof(days))
        memset(days, 0, sizeof(days));
    uint32_t day = vg_local_day(now);
    if (!day) return;
    unsigned slot = 0;
    for (unsigned i = 0; i < VG_HRV_DAYS; ++i) {
        if (days[i].day == day) return;
        if (days[i].day < days[slot].day) slot = i;
    }
    days[slot] = (VGDailyReading){day, now, value};
    persist_write_data(key, days, sizeof(days));
}

static inline int vg_resting_hrv(time_t cutoff, bool baseline, time_t end, int *count) {
    VGDailyReading merged[VG_HRV_DAYS * 2] = {{0}};
    VGDailyReading readings[VG_HRV_DAYS];
    const int keys[] = {VG_HRV_DAYS_KEY, VG_WORKER_HRV_DAYS_KEY};
    uint32_t selected_day = vg_local_day(cutoff);
    *count = 0;
    VGDailyReading latest = {0};
    for (unsigned source = 0; source < ARRAY_LENGTH(keys); ++source) {
        if (persist_read_data(keys[source], readings, sizeof(readings)) != sizeof(readings)) continue;
        for (unsigned i = 0; i < VG_HRV_DAYS; ++i) {
            VGDailyReading sample = readings[i];
            if (!sample.day || !sample.value || sample.value > 250) continue;
            if (!baseline) {
                if ((time_t)sample.timestamp >= cutoff && (time_t)sample.timestamp <= end &&
                    sample.timestamp > latest.timestamp) latest = sample;
                continue;
            }
            if (sample.day >= selected_day || selected_day - sample.day > 30 ||
                (time_t)sample.timestamp >= cutoff) continue;
            int slot = 0;
            while (slot < *count && merged[slot].day != sample.day) ++slot;
            if (slot == *count) ++*count;
            if (!merged[slot].timestamp || sample.timestamp < merged[slot].timestamp)
                merged[slot] = sample;
        }
    }
    if (!baseline) return latest.value;
    if (*count < 7) return 0;
    for (int i = 1; i < *count; ++i) {
        VGDailyReading value = merged[i];
        int j = i;
        while (j && merged[j - 1].value > value.value) {
            merged[j] = merged[j - 1];
            --j;
        }
        merged[j] = value;
    }
    return merged[*count / 2].value;
}

static inline bool vg_was_quiet(time_t now, int seconds, int limit) {
    HealthActivityMask activity = health_service_peek_current_activities();
    if (activity & (HealthActivitySleep | HealthActivityRestfulSleep |
                    HealthActivityWalk | HealthActivityRun | HealthActivityOpenWorkout))
        return false;
    return vg_metric_available(HealthMetricStepCount, now - seconds, now) &&
           health_service_sum(HealthMetricStepCount, now - seconds, now) <= limit;
}
