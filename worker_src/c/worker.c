// ============================================================================
// --- SECTION 1: INCLUDES & MACROS ---
// ============================================================================
// Include the standard Pebble SDK worker library
#include <pebble_worker.h>

// ============================================================================
// --- SECTION 2: HEALTH EVENT PROCESSING ---
// ============================================================================
// Define background event handler for system health updates
#define AUTO_HR_THRESHOLD 95
#define AUTO_CONFIRM_SECONDS 120
#define AUTO_COOLDOWN_SECONDS 1800
#define AUTO_LAST_TRIGGER_KEY 9001
#define HRV_CALIBRATION_DAYS_KEY 413
#define HRV_WORKER_LAUNCH_CONTEXT_KEY 414
#define HRV_REST_LAST_ATTEMPT_KEY 415
#define HRV_REST_PENDING_KEY 416
#define HRV_REST_CONFIRM_SECONDS 180
#define HRV_REST_COOLDOWN_SECONDS 21600
static time_t s_quiet_since;
static time_t s_elevated_since;

static void health_handler(HealthEventType event, void *context) {
    if (event != HealthEventHeartRateUpdate) return;
    time_t now = time(NULL);
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
    HealthActivityMask activity = health_service_peek_current_activities();
    // Opportunistic HRV calibration: the worker never samples PPI itself.
    // It only detects a quiet opportunity and asks the app to collect it.
    int calibrated_days = persist_exists(HRV_CALIBRATION_DAYS_KEY) ?
        persist_read_int(HRV_CALIBRATION_DAYS_KEY) : 0;
    if (calibrated_days < 7 && bpm >= 45 && bpm <= 100 &&
        !(activity & (HealthActivityWalk | HealthActivityRun |
                      HealthActivityOpenWorkout))) {
        HealthValue quiet_steps = health_service_sum(
            HealthMetricStepCount, now - 600, now);
        if (quiet_steps >= 0 && quiet_steps <= 8) {
            if (!s_quiet_since) s_quiet_since = now;
            time_t last_attempt = persist_exists(HRV_REST_LAST_ATTEMPT_KEY) ?
                persist_read_int(HRV_REST_LAST_ATTEMPT_KEY) : 0;
            if (now - s_quiet_since >= HRV_REST_CONFIRM_SECONDS &&
                (!last_attempt || now - last_attempt >= HRV_REST_COOLDOWN_SECONDS)) {
                persist_write_int(HRV_REST_LAST_ATTEMPT_KEY, (int)now);
                persist_write_int(HRV_WORKER_LAUNCH_CONTEXT_KEY, 2);
                persist_write_int(HRV_REST_PENDING_KEY, (int)now);
                s_quiet_since = 0;
                worker_launch_app();
                return;
            }
        } else s_quiet_since = 0;
    } else s_quiet_since = 0;

    // Model expected HR from recent exertion rather than vetoing activity.
    // These are conservative heuristic thresholds and should be personalized
    // after a baseline is established. Walking never automatically disqualifies.
    HealthValue recent_steps = health_service_sum(HealthMetricStepCount,
                                                   now - 600, now);
    int trigger_bpm = AUTO_HR_THRESHOLD;
    if (recent_steps > 100 && recent_steps <= 500) trigger_bpm += 10;
    else if (recent_steps > 500 && recent_steps <= 1000) trigger_bpm += 20;
    else if (recent_steps > 1000) trigger_bpm += 30;

    if (activity & HealthActivityWalk) {
        if (trigger_bpm < 110) trigger_bpm = 110;
    }
    if (activity & (HealthActivityRun | HealthActivityOpenWorkout)) {
        if (trigger_bpm < 140) trigger_bpm = 140;
    }

    // Suspicious means above what this coarse activity estimate explains.
    // Keep two-minute confirmation and the existing 30-minute cooldown.
    if (bpm <= trigger_bpm || bpm <= 0) {
        s_elevated_since = 0;
        return;
    }
    if (s_elevated_since == 0) {
        s_elevated_since = now;
        return;
    }
    if (now - s_elevated_since < AUTO_CONFIRM_SECONDS) return;
    time_t last = persist_exists(AUTO_LAST_TRIGGER_KEY) ?
                      persist_read_int(AUTO_LAST_TRIGGER_KEY) : 0;
    if (last > 0 && now - last < AUTO_COOLDOWN_SECONDS) return;
    persist_write_int(AUTO_LAST_TRIGGER_KEY, (int)now);
    s_elevated_since = 0;
    persist_write_int(HRV_WORKER_LAUNCH_CONTEXT_KEY, 1);
    worker_launch_app();
}

// ============================================================================
// --- SECTION 3: LIFECYCLE ---
// ============================================================================
// Define worker startup routine
static void worker_init() { 
  // Bind health handler callback to OS event system
  health_service_events_subscribe(health_handler, NULL);
}

// Define worker teardown routine
static void worker_deinit() { 
  // Release OS subscription hook to free system memory
  health_service_events_unsubscribe(); 
}

// Define standard C entry point for background process
int main(void) { 
  // Initialize worker environment
  worker_init(); 
  // Block thread execution to maintain background state
  worker_event_loop(); 
  // Execute cleanup upon thread termination
  worker_deinit(); 
}