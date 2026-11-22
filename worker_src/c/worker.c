#include <pebble_worker.h>
#include "../../shared/hrv.h"

enum {
    AUTO_LAST_TRIGGER_KEY = 9001,
    HRV_REST_LAST_ATTEMPT_KEY = 415,
};

static VGPulseScan s_scan;
static uint8_t s_kind;
static time_t s_elevated_since, s_last_heart_event;
static time_t s_last_checkpoint;
static bool s_scan_moved;
static void tick_handler(struct tm *local, TimeUnits units);

static bool has_recent_sleep(HealthActivity activity, time_t start, time_t end,
                             void *context) {
    bool *found = context;
    if (end - start >= 90 * 60) *found = true;
    return !*found;
}

static bool morning_window(time_t now) {
    bool found = false;
    health_service_activities_iterate(HealthActivitySleep, now - 2 * 3600, now,
                                      HealthIterationDirectionPast, has_recent_sleep, &found);
    return found;
}

static void finish_scan(time_t now) {
    int kind = s_kind;
    s_kind = 0;
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    health_service_set_hrv_sample_period(0);
    if (s_scan_moved || vg_ppi_quality(&s_scan, now) < 2 || !vg_was_quiet(now, 60, 5))
        return;
    unsigned value = vg_rmssd(&s_scan);
    if (!value) return;
    if (kind == 2) {
        vg_record_resting_hrv(VG_WORKER_HRV_DAYS_KEY, now, value);
        VGDailyReading latest = {vg_local_day(now), now, value};
        persist_write_data(VG_WORKER_HRV_LATEST_KEY, &latest, sizeof(latest));
    } else if (persist_read_bool(VG_ALERT_OPT_IN_KEY)) {
        int days;
        int baseline = vg_resting_hrv(now, true, now, &days);
        if (baseline > 0 && value * 10 < (unsigned)baseline * 7) {
            persist_write_int(VG_WORKER_LAUNCH_CONTEXT_KEY, 1);
            worker_launch_app();
        }
    }
}

static bool start_scan(time_t now, int kind) {
    time_t foreground_expiry = persist_read_int(VG_SCAN_BUSY_KEY);
    if (foreground_expiry > now || !health_service_set_hrv_sample_period(1)) return false;
    s_scan = (VGPulseScan){.started = now};
    s_scan_moved = false;
    s_kind = kind;
    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    return true;
}

static void update_checkpoint(time_t now) {
    VGReserve reserve = vg_latest_reserve(now);
    if (vg_update_reserve(&reserve, now))
        persist_write_data(VG_WORKER_RESERVE_KEY, &reserve, sizeof(reserve));
    s_last_checkpoint = now;
}

static void tick_handler(struct tm *local, TimeUnits units) {
    time_t now = time(NULL);
    if (s_kind && now - (time_t)s_scan.started >= 60) finish_scan(now);
    if (!s_kind && now - s_last_checkpoint >= 10 * 60) update_checkpoint(now);
}

static void health_handler(HealthEventType event, void *context) {
    if (s_kind && event == HealthEventHRVUpdate) {
        if (persist_read_int(VG_SCAN_BUSY_KEY) > (int)time(NULL)) {
            s_kind = 0;
            tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
            health_service_set_hrv_sample_period(0);
        } else {
            vg_record_ppi(&s_scan, time(NULL), health_service_peek_hrv_ppi_ms());
        }
        return;
    }
    if (event == HealthEventMovementUpdate && s_kind) {
        HealthActivityMask activity = health_service_peek_current_activities();
        if (activity & (HealthActivityWalk | HealthActivityRun | HealthActivityOpenWorkout))
            s_scan_moved = true;
    }
    if (event != HealthEventHeartRateUpdate || s_kind) return;
    time_t now = time(NULL);
    // Count sustained elevation only across closely spaced real events.
    if (s_last_heart_event && now - s_last_heart_event > 90)
        s_elevated_since = 0;
    s_last_heart_event = now;
    int bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    bool quiet = vg_was_quiet(now, 180, 5);
    if (vg_hr_band(bpm) < 0 || !quiet) {
        s_elevated_since = 0;
        return;
    }
    time_t last_rest = persist_read_int(HRV_REST_LAST_ATTEMPT_KEY);
    if (bpm <= 100 &&
        (!last_rest || now - last_rest >= 3600) && morning_window(now)) {
        VGDailyReading latest = {0};
        persist_read_data(VG_WORKER_HRV_LATEST_KEY, &latest, sizeof(latest));
        if (latest.day != vg_local_day(now) && start_scan(now, 2)) {
            persist_write_int(HRV_REST_LAST_ATTEMPT_KEY, now);
            return;
        }
    }

    int resting = persist_read_int(VG_REST_BASELINE_KEY);
    time_t baseline_time = persist_read_int(VG_REST_BASELINE_TIME_KEY);
    int threshold = baseline_time > 0 && now >= baseline_time &&
                    now - baseline_time < 30 * 86400 ? vg_clamp(resting + 30, 90, 125) : 95;
    if (!persist_read_bool(VG_ALERT_OPT_IN_KEY) || bpm <= threshold) {
        s_elevated_since = 0;
        return;
    }
    if (!s_elevated_since) s_elevated_since = now;
    time_t last_alert = persist_read_int(AUTO_LAST_TRIGGER_KEY);
    if (now - s_elevated_since >= 120 && (!last_alert || now - last_alert >= 1800) &&
        start_scan(now, 1)) {
        persist_write_int(AUTO_LAST_TRIGGER_KEY, now);
        s_elevated_since = 0;
    }
}

int main(void) {
    health_service_events_subscribe(health_handler, NULL);
    update_checkpoint(time(NULL));
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    worker_event_loop();
    if (s_kind) health_service_set_hrv_sample_period(0);
    tick_timer_service_unsubscribe();
    health_service_events_unsubscribe();
    return 0;
}
