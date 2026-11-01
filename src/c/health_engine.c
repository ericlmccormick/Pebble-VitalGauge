#include "vitalgauge.h"

#define CHUNK_SIZE 60
#define HISTORY_CHUNKS (1440 / CHUNK_SIZE)
#define VG_SNAPSHOT_KEY 520
#define VG_SNAPSHOT_VERSION 1
#define VG_SNAPSHOT_CHUNK 192
typedef struct {
    uint16_t version;
    uint16_t size;
    uint32_t timestamp;
    uint16_t activity[144];
    uint8_t stress[144];
    uint8_t sleep[1440];
    int32_t sleep_start, sleep_end;
    int32_t deep, light, longest;
    int16_t rhr, cardiac, exertion, stress_drain;
    uint8_t sleep_available;
} VGHealthSnapshot;
static VGHealthSnapshot s_snapshot;
static void restore_snapshot(void) {
    uint8_t *dest = (uint8_t *)&s_snapshot;
    for (size_t pos = 0; pos < sizeof(s_snapshot); pos += VG_SNAPSHOT_CHUNK) {
        size_t len = sizeof(s_snapshot) - pos;
        if (len > VG_SNAPSHOT_CHUNK) len = VG_SNAPSHOT_CHUNK;
        int key = VG_SNAPSHOT_KEY + (int)(pos / VG_SNAPSHOT_CHUNK);
        if (persist_get_size(key) != (int)len ||
            persist_read_data(key, dest + pos, len) != (int)len) return;
    }
    if (s_snapshot.version != VG_SNAPSHOT_VERSION ||
        s_snapshot.size != sizeof(s_snapshot)) return;
    memcpy(g_act_map, s_snapshot.activity, sizeof(g_act_map));
    memcpy(g_stress_map, s_snapshot.stress, sizeof(g_stress_map));
    memcpy(g_sleep_map, s_snapshot.sleep, sizeof(g_sleep_map));
    // Maps are 24-hour rolling windows. Restore the original map origin,
    // not the current time, until a fresh reconstruction is available.
    g_24h_start = (time_t)s_snapshot.timestamp;
    g_sleep_start = (time_t)s_snapshot.sleep_start;
    g_sleep_end = (time_t)s_snapshot.sleep_end;
    g_sleep_deep_sec = s_snapshot.deep;
    g_sleep_light_sec = s_snapshot.light;
    g_longest_sleep_duration = s_snapshot.longest;
    g_morning_hr_comp = s_snapshot.rhr;
    g_card_eff_comp = s_snapshot.cardiac;
    g_exertion_comp = s_snapshot.exertion;
    g_stress_comp = s_snapshot.stress_drain;
    g_sleep_data_available = s_snapshot.sleep_available != 0;
}
static void save_snapshot(void) {
    s_snapshot.version = VG_SNAPSHOT_VERSION;
    s_snapshot.size = sizeof(s_snapshot);
    s_snapshot.timestamp = (uint32_t)g_24h_start;
    memcpy(s_snapshot.activity, g_act_map, sizeof(g_act_map));
    memcpy(s_snapshot.stress, g_stress_map, sizeof(g_stress_map));
    memcpy(s_snapshot.sleep, g_sleep_map, sizeof(g_sleep_map));
    s_snapshot.sleep_start = (int32_t)g_sleep_start;
    s_snapshot.sleep_end = (int32_t)g_sleep_end;
    s_snapshot.deep = g_sleep_deep_sec;
    s_snapshot.light = g_sleep_light_sec;
    s_snapshot.longest = g_longest_sleep_duration;
    s_snapshot.rhr = g_morning_hr_comp;
    s_snapshot.cardiac = g_card_eff_comp;
    s_snapshot.exertion = g_exertion_comp;
    s_snapshot.stress_drain = g_stress_comp;
    s_snapshot.sleep_available = g_sleep_data_available;
    const uint8_t *data = (const uint8_t *)&s_snapshot;
    for (size_t pos = 0; pos < sizeof(s_snapshot); pos += VG_SNAPSHOT_CHUNK) {
        size_t len = sizeof(s_snapshot) - pos;
        if (len > VG_SNAPSHOT_CHUNK) len = VG_SNAPSHOT_CHUNK;
        persist_write_data(VG_SNAPSHOT_KEY + (int)(pos / VG_SNAPSHOT_CHUNK),
                           data + pos, len);
    }
}

// ============================================================================
// --- SECTION 1: GLOBAL STATE INSTANTIATION ---
// ============================================================================
int g_live_energy_score = 0;
int g_sleep_comp = 0;
bool g_sleep_data_available = false;
int g_readiness_comp = 0;
int g_card_eff_comp = 0;
int g_exertion_comp = 0;
int g_stress_comp = 0;
int g_morning_hr_comp = 0;
int g_volume_level = 100;
int g_target_hr = 60;

uint16_t g_act_map[144] = {0};
uint8_t g_stress_map[144] = {0};
uint8_t g_sleep_map[1440] = {0};

time_t g_24h_start = 0;
time_t g_sleep_start = 0;
time_t g_sleep_end = 0;
int g_sleep_deep_sec = 0;
int g_sleep_light_sec = 0;
int g_longest_sleep_duration = 0;

int g_hist_energy[7] = {0};
int g_hist_sleep[7] = {0};
int g_hist_stress[7] = {0};
int g_hist_activity[7] = {0};

static bool s_native_restful_mask[1440] = {0};
static DataLoggingSessionRef s_log_session;

// ============================================================================
// --- SECTION 2: CALCULATION ENGINE ---
// ============================================================================
void health_engine_calculate_energy() {
    g_sleep_comp = persist_exists(PERSIST_KEY_SLEEP_SCORE) ? persist_read_int(PERSIST_KEY_SLEEP_SCORE) : 0;
    g_readiness_comp = persist_exists(PERSIST_KEY_READINESS) ? persist_read_int(PERSIST_KEY_READINESS) : 0;
    int stress_score = persist_exists(PERSIST_KEY_SCORE) ? persist_read_int(PERSIST_KEY_SCORE) : 0;

    time_t now = time(NULL);
    time_t start_today = time_start_of_today();
    
    int min_hr = (int)health_service_aggregate_averaged(HealthMetricHeartRateBPM, start_today, now, HealthAggregationMin, HealthServiceTimeScopeOnce);
    int max_hr = (int)health_service_aggregate_averaged(HealthMetricHeartRateBPM, start_today, now, HealthAggregationMax, HealthServiceTimeScopeOnce);
    
    if (min_hr <= 0) min_hr = 60;
    g_card_eff_comp = (min_hr > 0) ? (153 * (max_hr > 0 ? max_hr : 120)) / (min_hr * 10) : 0;
    
    int active_kcal = (int)health_service_sum_today(HealthMetricActiveKCalories);
    g_exertion_comp = active_kcal / 15;
    g_stress_comp = stress_score / 5;

    int base_charge = (g_sleep_comp + g_readiness_comp) / 2;
    int final_score = base_charge + (g_card_eff_comp / 10) - g_exertion_comp - g_stress_comp;

    if (final_score > 100) final_score = 100;
    if (final_score < 0) final_score = 0;

    g_live_energy_score = final_score;
    persist_write_int(PERSIST_KEY_ENERGY_RESERVE, final_score);
}

static void update_history_arrays() {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int current_day = t->tm_yday;
    int last_day = persist_exists(PERSIST_KEY_HIST_DAY) ? persist_read_int(PERSIST_KEY_HIST_DAY) : -1;

    if (persist_exists(PERSIST_KEY_HIST_ENERGY)) persist_read_data(PERSIST_KEY_HIST_ENERGY, g_hist_energy, sizeof(g_hist_energy));
    if (persist_exists(PERSIST_KEY_HIST_SLEEP)) persist_read_data(PERSIST_KEY_HIST_SLEEP, g_hist_sleep, sizeof(g_hist_sleep));
    if (persist_exists(PERSIST_KEY_HIST_STRESS)) persist_read_data(PERSIST_KEY_HIST_STRESS, g_hist_stress, sizeof(g_hist_stress));
    if (persist_exists(PERSIST_KEY_HIST_ACTIVITY)) persist_read_data(PERSIST_KEY_HIST_ACTIVITY, g_hist_activity, sizeof(g_hist_activity));

    if (last_day != current_day && last_day != -1) {
        for(int i = 0; i < 6; i++) {
            g_hist_energy[i] = g_hist_energy[i+1];
            g_hist_sleep[i] = g_hist_sleep[i+1];
            g_hist_stress[i] = g_hist_stress[i+1];
            g_hist_activity[i] = g_hist_activity[i+1];
        }
        g_hist_energy[6] = 0; g_hist_sleep[6] = 0; g_hist_stress[6] = 0; g_hist_activity[6] = 0;
        persist_write_int(PERSIST_KEY_HIST_DAY, current_day);
    } else if (last_day == -1) {
        persist_write_int(PERSIST_KEY_HIST_DAY, current_day);
    }

    g_hist_energy[6] = g_live_energy_score;
    g_hist_sleep[6] = g_sleep_comp;
    g_hist_stress[6] = persist_exists(PERSIST_KEY_SCORE) ? persist_read_int(PERSIST_KEY_SCORE) : 0;
    
    int steps = (int)health_service_sum_today(HealthMetricStepCount);
    int act_score = (steps * 100) / 10000;
    g_hist_activity[6] = act_score > 100 ? 100 : act_score;

    persist_write_data(PERSIST_KEY_HIST_ENERGY, g_hist_energy, sizeof(g_hist_energy));
    persist_write_data(PERSIST_KEY_HIST_SLEEP, g_hist_sleep, sizeof(g_hist_sleep));
    persist_write_data(PERSIST_KEY_HIST_STRESS, g_hist_stress, sizeof(g_hist_stress));
    persist_write_data(PERSIST_KEY_HIST_ACTIVITY, g_hist_activity, sizeof(g_hist_activity));
}

// ============================================================================
// --- SECTION: DATA FETCHING AND PARSING (health_engine.c) ---
// ============================================================================
// Native activity intervals anchor the overnight window; minute-level motion
// drives the final classification when available.
static uint8_t s_native_sleep_mask[1440];
// Avoid putting several kilobytes of sensor buffers on the Pebble app stack.
static uint8_t s_valid_motion[1440];
static uint8_t s_quiet_motion[1440];
static uint8_t s_minute_hr[1440];
static uint8_t s_active_motion[1440];
// Keep API buffers off the constrained Pebble app stack.
static HealthMinuteData s_history_chunk[CHUNK_SIZE];
static HealthMinuteData s_history_probe[60];
static bool s_found_sleep;

static bool find_latest_sleep_cb(HealthActivity activity, time_t start,
                                 time_t end, void *context) {
    if (activity != HealthActivitySleep || end <= start) return true;
    if (!s_found_sleep) {
        s_found_sleep = true;
        g_sleep_start = start;
        g_sleep_end = end;
    } else if (end >= g_sleep_start - 90 * 60 && start <= g_sleep_end + 90 * 60) {
        if (start < g_sleep_start) g_sleep_start = start;
        if (end > g_sleep_end) g_sleep_end = end;
    } else if (end < g_sleep_start - 90 * 60) {
        return false; // Older separate sleep session; don't merge a prior night.
    }
    return true;
}

static bool fill_sleep_mask_cb(HealthActivity activity, time_t start,
                                time_t end, void *context) {
    if (end <= g_sleep_start || start >= g_sleep_end || end <= start) return true;
    if (start < g_sleep_start) start = g_sleep_start;
    if (end > g_sleep_end) end = g_sleep_end;
    // Round outward for display; use exactly one state per minute to avoid
    // double counting overlapping activity records.
    int first = (int)((start - g_24h_start) / 60);
    int last = (int)((end - g_24h_start + 59) / 60);
    if (first < 0) first = 0;
    if (last > 1440) last = 1440;
    for (int i = first; i < last; ++i) {
        if (activity == HealthActivitySleep) s_native_sleep_mask[i] = 1;
        else if (activity == HealthActivityRestfulSleep) s_native_restful_mask[i] = true;
    }
    return true;
}

void health_engine_update_buffers() { 
    time_t now = time(NULL); g_24h_start = (now / 60) * 60 - (24 * 3600);
    g_sleep_start = 0; g_sleep_end = 0; g_sleep_deep_sec = 0; g_sleep_light_sec = 0; g_longest_sleep_duration = 0;
    memset(g_act_map, 0, sizeof(g_act_map)); memset(g_stress_map, 0, sizeof(g_stress_map));
    memset(g_sleep_map, 0, sizeof(g_sleep_map)); memset(s_native_restful_mask, 0, sizeof(s_native_restful_mask));
    
    s_found_sleep = false;
    memset(s_native_sleep_mask, 0, sizeof(s_native_sleep_mask));
    time_t query_start = now - 36 * 3600;
    HealthServiceAccessibilityMask access = health_service_any_activity_accessible(
        HealthActivitySleep | HealthActivityRestfulSleep, query_start, now);
    APP_LOG(APP_LOG_LEVEL_INFO, "Sleep activity accessibility mask: %lu",
            (unsigned long)access);
    if (access & HealthServiceAccessibilityMaskAvailable) {
        health_service_activities_iterate(HealthActivitySleep, query_start, now,
                                          HealthIterationDirectionPast,
                                          find_latest_sleep_cb, NULL);
        if (s_found_sleep) {
            health_service_activities_iterate(
                HealthActivitySleep | HealthActivityRestfulSleep,
                g_sleep_start, g_sleep_end, HealthIterationDirectionPast,
                fill_sleep_mask_cb, NULL);
        }
    }
    // First collect motion/HR evidence; native intervals alone are insufficient
    // when the firmware stops recognizing sleep after a brief awakening.
    // Each minute keeps its own validity flag; missing records are not rest.
    memset(s_valid_motion, 0, sizeof(s_valid_motion));
    memset(s_quiet_motion, 0, sizeof(s_quiet_motion));
    memset(s_minute_hr, 0, sizeof(s_minute_hr));
    memset(s_active_motion, 0, sizeof(s_active_motion));
    time_t rhr_window_end = (g_sleep_end != 0) ? g_sleep_end : time_start_of_today() + (6 * 3600);
    int m_hr = 999;

    int valid_minute_records = 0;
    int motion_records = 0;
    for (int c = 0; c < HISTORY_CHUNKS; c++) {
        time_t chunk_start = g_24h_start + (c * CHUNK_SIZE * 60), chunk_end = chunk_start + (CHUNK_SIZE * 60);
        time_t requested_start = chunk_start;
        time_t requested_end = chunk_end;
        uint32_t fetched = health_service_get_minute_history(s_history_chunk, CHUNK_SIZE,
                                                               &chunk_start, &chunk_end);
        int chunk_valid = 0;
        if (fetched > CHUNK_SIZE) fetched = CHUNK_SIZE;
        for (uint32_t i = 0; i < fetched; i++) {
            if (s_history_chunk[i].is_invalid) continue;
            ++valid_minute_records;
            ++chunk_valid;
            int slot = (int)((chunk_start + (time_t)i * 60 - g_24h_start) / 60);
            if (slot >= 0 && slot < 1440) {
                s_valid_motion[slot] = 1;
                s_minute_hr[slot] = s_history_chunk[i].heart_rate_bpm;
                // Original application used 15 steps and 350 VMC to exclude
                // movement-heavy periods. Reuse those limits as a veto.
                s_quiet_motion[slot] = s_history_chunk[i].steps <= 15 && s_history_chunk[i].vmc <= 350;
                s_active_motion[slot] = (s_history_chunk[i].steps > 15 || s_history_chunk[i].vmc > 350);
                ++motion_records;
            }
            int min_of_day = (chunk_start + (i * 60) - g_24h_start) / 60, b = min_of_day / 10;
            if (b >= 0 && b < 144) { g_act_map[b] += s_history_chunk[i].steps; if (s_history_chunk[i].heart_rate_bpm > g_stress_map[b]) g_stress_map[b] = s_history_chunk[i].heart_rate_bpm; }
            if (chunk_start + ((time_t)i * 60) >= rhr_window_end - (90 * 60) && chunk_start + ((time_t)i * 60) <= rhr_window_end) if (s_history_chunk[i].heart_rate_bpm > 0 && s_history_chunk[i].vmc == 0 && s_history_chunk[i].heart_rate_bpm < m_hr) m_hr = s_history_chunk[i].heart_rate_bpm;
        }
    }
    // Independent short-range probes: distinguish an entirely empty minute
    // history API from missing older overnight records. A zero return makes
    // output timestamps undefined; log requested times and count only.
    {

        time_t probe_end = now - 15 * 60;
        time_t probe_start = probe_end - 60 * 60;
        uint32_t found = health_service_get_minute_history(
            s_history_probe, 60, &probe_start, &probe_end);
        int valid = 0;
        for (uint32_t i = 0; i < found; ++i) {
            if (!s_history_probe[i].is_invalid) ++valid;
        }
        APP_LOG(APP_LOG_LEVEL_INFO,
                "MINUTE_PROBE recent_15_to_75_min_ago returned=%lu valid=%d",
                (unsigned long)found, valid);
        if (g_sleep_start > 0) {
            time_t night_start = g_sleep_start + 30 * 60;
            time_t night_end = night_start + 60 * 60;
            uint32_t night_found = health_service_get_minute_history(
                s_history_probe, 60, &night_start, &night_end);
            int night_valid = 0;
            for (uint32_t i = 0; i < night_found; ++i) {
                if (!s_history_probe[i].is_invalid) ++night_valid;
            }
            APP_LOG(APP_LOG_LEVEL_INFO,
                    "MINUTE_PROBE first_sleep_hour returned=%lu valid=%d",
                    (unsigned long)night_found, night_valid);
        }
    }
    // Rebuild the entire night from minute data. Native Pebble sessions are
    // anchors, not the final authority. Orientation changes are deliberately
    // ignored: an arm hanging off the bed is not evidence of getting up.
    int first_native = -1, last_native = -1;
    for (int i = 0; i < 1440; ++i) {
        if (s_native_sleep_mask[i]) {
            if (first_native < 0) first_native = i;
            last_native = i;
        }
    }

    int native_minutes = 0, estimated_minutes = 0, awake_minutes = 0;
    int restful_minutes = 0, missing_minutes = 0;
    int first_sleep = -1, last_sleep = -1;
    if (first_native >= 0) {
        // Look 90 min before Pebble's sleep start and 4 hours after its end.
        // Full gaps INSIDE its sleep span are always evaluated as well.
        int begin = first_native - 90;
        if (begin < 0) begin = 0;
        int finish = last_native + 240;
        if (finish > 1439) finish = 1439;
        // Establish nighttime HR baseline when possible, but never interpret
        // missing HR as proof of waking or sleeping.
        int hr_sum = 0, hr_count = 0;
        for (int i = first_native; i <= last_native; ++i) {
            if (s_valid_motion[i] && s_native_sleep_mask[i] &&
                s_quiet_motion[i] && s_minute_hr[i] >= 35 &&
                s_minute_hr[i] <= 110) {
                hr_sum += s_minute_hr[i];
                ++hr_count;
            }
        }
        int baseline = hr_count >= 20 ? hr_sum / hr_count : 0;
        // Identify an actual waking transition only after sustained activity.
        // Isolated arm movement and transient sensor errors do not end sleep.
        int last_confirmed = last_native;
        int sustained_awake = 0;
        bool extended_started = false;
        for (int i = begin; i <= finish; ++i) {
            time_t sample_time = g_24h_start + (time_t)i * 60;
            struct tm *tm_ptr = localtime(&sample_time);
            if (tm_ptr == NULL) break;
            int hour = tm_ptr->tm_hour;
            // Avoid detecting ordinary daytime inactivity as overnight sleep.
            if (i > last_native && hour >= 11 && hour < 20) break;

            if (!s_valid_motion[i]) {
                if (s_native_sleep_mask[i]) {
                    g_sleep_map[i] = s_native_restful_mask[i] ? 2 : 1;
                } else {
                    ++missing_minutes;
                }
                continue;
            }

            // 11-minute neighborhood: movement must persist before classifying
            // awake. This also bridges Pebble's false awake gaps during the night.
            int observed = 0, active = 0, quiet = 0;
            int from = i > 5 ? i - 5 : 0;
            int to = i + 5 < 1440 ? i + 5 : 1439;
            for (int j = from; j <= to; ++j) {
                if (!s_valid_motion[j]) continue;
                ++observed;
                if (s_active_motion[j]) ++active;
                else ++quiet;
            }
            bool awake_evidence = observed >= 6 && active >= 6;
            bool quiet_evidence = observed >= 6 && quiet >= 8;
            bool hr_supports_sleep = baseline > 0 &&
                s_minute_hr[i] >= 35 &&
                s_minute_hr[i] <= baseline + 18;
            bool anchored = i >= first_native && i <= last_native;
            bool possible_extension = i > last_native && i <= last_native + 240;
            bool possible_early = i < first_native && first_native - i <= 90;

            if (awake_evidence) {
                ++sustained_awake;
                g_sleep_map[i] = 0;
                ++awake_minutes;
                if (possible_extension && sustained_awake >= 15) break;
                continue;
            }
            if (sustained_awake > 0) --sustained_awake;
            // Native sleeping minutes remain sleep unless sustained activity
            // disagrees. Reclassify quiet minutes in native "awake" gaps.
            if (anchored && (quiet_evidence || s_native_sleep_mask[i])) {
                g_sleep_map[i] = s_native_restful_mask[i] ? 2 :
                                 (s_native_sleep_mask[i] ? 1 : 3);
            } else if ((possible_extension || possible_early) &&
                       quiet_evidence && hr_supports_sleep) {
                // Outside the anchor require both motion and HR agreement.
                // A quiet awake period may still be ambiguous, so mark inferred.
                g_sleep_map[i] = 3;
                extended_started = true;
                last_confirmed = i;
            }
            // Don't extend forever without new positive evidence.
            if (possible_extension && extended_started &&
                i - last_confirmed >= 30) break;
        }

        for (int i = begin; i <= finish; ++i) {
            uint8_t state = g_sleep_map[i];
            if (!state) continue;
            if (first_sleep < 0) first_sleep = i;
            last_sleep = i;
            if (state == 2) ++restful_minutes;
            if (state == 3) ++estimated_minutes;
            else ++native_minutes;
        }
    }
    if (first_sleep >= 0) {
        g_sleep_start = g_24h_start + (time_t)first_sleep * 60;
        g_sleep_end = g_24h_start + (time_t)(last_sleep + 1) * 60;
        g_longest_sleep_duration = (last_sleep - first_sleep + 1) * 60;
    }
    g_sleep_deep_sec = restful_minutes * 60;
    g_sleep_light_sec = (native_minutes + estimated_minutes -
                         restful_minutes) * 60;
    APP_LOG(APP_LOG_LEVEL_INFO,
            "Sleep native=%d inferred=%d restful=%d awakeEvidence=%d missing=%d valid=%d",
            native_minutes, estimated_minutes, restful_minutes,
            awake_minutes, missing_minutes, valid_minute_records);
    APP_LOG(APP_LOG_LEVEL_INFO, "Minute history valid records=%d", valid_minute_records);
    g_morning_hr_comp = (m_hr == 999) ? 60 : m_hr;
    persist_write_int(PERSIST_KEY_RHR, g_morning_hr_comp);
    int net = g_sleep_deep_sec + g_sleep_light_sec;
    g_sleep_data_available = (net > 0);
    if (g_sleep_data_available) {
        int duration_pct = (net * 100) / (8 * 3600);
        if (duration_pct > 100) duration_pct = 100;
        int restful_pct = (g_sleep_deep_sec * 100) / net;
        int restful_target = (restful_pct * 100) / 25;
        if (restful_target > 100) restful_target = 100;
        g_sleep_comp = (duration_pct * 70 + restful_target * 30) / 100;
        persist_write_int(PERSIST_KEY_SLEEP_SCORE, g_sleep_comp);
        int hr_penalty = g_morning_hr_comp > 55 ? g_morning_hr_comp - 55 : 0;
        g_readiness_comp = g_sleep_comp - hr_penalty;
        if (g_readiness_comp < 10) g_readiness_comp = 10;
        if (g_readiness_comp > 100) g_readiness_comp = 100;
        persist_write_int(PERSIST_KEY_READINESS, g_readiness_comp);
    } else {
        APP_LOG(APP_LOG_LEVEL_WARNING, "No valid native sleep; preserving last score");
        // Keep last valid score, but mark it unavailable rather than showing
        // stale data as though it was collected this morning.
    }
    health_engine_calculate_energy(); update_history_arrays();
    if (valid_minute_records > 0) save_snapshot();
}

// ============================================================================
// --- SECTION 4: DATALOGGING & LIFECYCLE ---
// ============================================================================
void health_engine_init() { 
    s_log_session = data_logging_create(0xDEADBEEF, DATA_LOGGING_INT, 4, false); 
    if (persist_exists(PERSIST_KEY_ENERGY_RESERVE)) g_live_energy_score = persist_read_int(PERSIST_KEY_ENERGY_RESERVE);
    if (persist_exists(PERSIST_KEY_SLEEP_SCORE)) g_sleep_comp = persist_read_int(PERSIST_KEY_SLEEP_SCORE);
    if (persist_exists(PERSIST_KEY_READINESS)) g_readiness_comp = persist_read_int(PERSIST_KEY_READINESS);
    restore_snapshot();
}

void health_engine_deinit() {
    data_logging_finish(s_log_session);
}

void health_engine_log_metrics() {
    int data[1] = { g_live_energy_score };
    data_logging_log(s_log_session, data, 1);
}