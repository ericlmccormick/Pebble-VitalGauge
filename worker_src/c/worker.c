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

#define HRV_HISTORY_KEY 410
#define HRV_HISTORY_CAP 16
#define HRV_QUALITY_MIN 40
typedef struct {
  uint32_t timestamp;
  uint16_t rmssd;
  uint16_t bpm;
  uint8_t accepted;
  uint8_t rejected;
  uint8_t quality;
  uint8_t automatic; // 2: background resting baseline
} HRVRecord;
static bool s_hrv_recording;
static time_t s_hrv_started;
static time_t s_last_ppi_time;
static uint16_t s_previous_ppi, s_pairs, s_accepted, s_rejected, s_received;
static uint32_t s_square_sum;
static uint32_t int_sqrt(uint32_t n) {
  uint32_t r = 0, bit = (uint32_t)1 << 30;
  while (bit > n) bit >>= 2;
  while (bit) {
    if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return r;
}
static void finish_rest_scan(void) {
  if (!s_hrv_recording) return;
  s_hrv_recording = false;
  health_service_set_hrv_sample_period(0);
  if (s_pairs < 20) return;
  uint16_t rmssd = (uint16_t)int_sqrt(s_square_sum / s_pairs);
  int rejected_pct = s_received ? (int)s_rejected * 100 / s_received : 100;
  uint8_t quality = s_pairs >= HRV_QUALITY_MIN && rejected_pct <= 10 ? 2 :
                    rejected_pct <= 25 ? 1 : 0;
  HRVRecord records[HRV_HISTORY_CAP];
  int bytes = persist_exists(HRV_HISTORY_KEY) ?
              persist_get_size(HRV_HISTORY_KEY) : 0;
  int count = 0;
  if (bytes > 0 && bytes <= (int)sizeof(records) &&
      bytes % (int)sizeof(HRVRecord) == 0 &&
      persist_read_data(HRV_HISTORY_KEY, records, bytes) == bytes)
    count = bytes / sizeof(HRVRecord);
  if (count == HRV_HISTORY_CAP) {
    memmove(records, records + 1, (HRV_HISTORY_CAP - 1) * sizeof(HRVRecord));
    --count;
  }
  HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
  records[count++] = (HRVRecord){
    .timestamp = (uint32_t)time(NULL), .rmssd = rmssd,
    .bpm = (uint16_t)(bpm > 0 ? bpm : 0),
    .accepted = (uint8_t)(s_accepted > 255 ? 255 : s_accepted),
    .rejected = (uint8_t)(s_rejected > 255 ? 255 : s_rejected),
    .quality = quality, .automatic = 2
  };
  persist_write_data(HRV_HISTORY_KEY, records, count * sizeof(HRVRecord));
  // Rebuild the number of distinct good-quality calibration days.
  int days = 0;
  time_t limit = time(NULL) - 30 * 86400;
  for (int i = 0; i < count; ++i) {
    if (records[i].quality != 2 || records[i].automatic == 1 ||
        records[i].timestamp < (uint32_t)limit) continue;
    bool seen = false;
    for (int j = i + 1; j < count; ++j)
      if (records[j].quality == 2 && records[j].automatic != 1 &&
          records[j].timestamp / 86400 == records[i].timestamp / 86400)
        seen = true;
    if (!seen) ++days;
  }
  persist_write_int(HRV_CALIBRATION_DAYS_KEY, days);
}
static void begin_rest_scan(time_t now) {
  if (s_hrv_recording) return;
  if (!health_service_set_hrv_sample_period(1)) return;
  s_hrv_recording = true;
  s_hrv_started = now;
  s_last_ppi_time = 0;
  s_previous_ppi = s_pairs = s_accepted = s_rejected = s_received = 0;
  s_square_sum = 0;
  persist_write_int(HRV_REST_LAST_ATTEMPT_KEY, (int)now);
}
static void tick_handler(struct tm *t, TimeUnits units) {
  if (s_hrv_recording && time(NULL) - s_hrv_started >= 60)
    finish_rest_scan();
}
static void record_ppi(void) {
  if (!s_hrv_recording) return;
  uint16_t ppi = health_service_peek_hrv_ppi_ms();
  if (s_received < 65535) ++s_received;
  time_t now = time(NULL);
  if (ppi < 400 || ppi > 1500) {
    ++s_rejected; s_previous_ppi = 0; return;
  }
  if (s_last_ppi_time && now - s_last_ppi_time > 3)
    s_previous_ppi = 0;
  s_last_ppi_time = now;
  if (s_previous_ppi) {
    int delta = (int)ppi - (int)s_previous_ppi;
    if (delta > 250 || delta < -250) {
      ++s_rejected; s_previous_ppi = 0; return;
    }
    s_square_sum += (uint32_t)(delta * delta);
    ++s_pairs;
  }
  s_previous_ppi = ppi;
  ++s_accepted;
}

static time_t s_elevated_since;

static void health_handler(HealthEventType event, void *context) {
    if (event == HealthEventHRVUpdate) { record_ppi(); return; }
    if (event != HealthEventHeartRateUpdate) return;
    time_t now = time(NULL);
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
    HealthActivityMask activity = health_service_peek_current_activities();
    // Opportunistic HRV calibration: the worker never samples PPI itself.
    // It only detects a quiet opportunity and asks the app to collect it.
    int calibrated_days = persist_exists(HRV_CALIBRATION_DAYS_KEY) ?
        persist_read_int(HRV_CALIBRATION_DAYS_KEY) : 0;
    if (!s_hrv_recording && calibrated_days < 7 && bpm >= 45 && bpm <= 100 &&
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
                s_quiet_since = 0;
                begin_rest_scan(now);
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
    if (s_hrv_recording) return;
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
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

// Define worker teardown routine
static void worker_deinit() { 
  // Release OS subscription hook to free system memory
  finish_rest_scan();
  tick_timer_service_unsubscribe();
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