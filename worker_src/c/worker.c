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
static time_t s_elevated_since;

static void health_handler(HealthEventType event, void *context) {
    if (event != HealthEventHeartRateUpdate) return;
    time_t now = time(NULL);
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
    HealthActivityMask activity = health_service_peek_current_activities();
    bool exercising = (activity & (HealthActivityRun | HealthActivityWalk |
                                    HealthActivityOpenWorkout)) != 0;
    // Recent steps are a coarse activity filter; activity flags take priority.
    HealthValue recent_steps = health_service_sum(HealthMetricStepCount,
                                                   now - 600, now);
    if (bpm <= AUTO_HR_THRESHOLD || exercising || recent_steps > 100) {
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