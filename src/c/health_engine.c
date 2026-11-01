#include "vitalgauge.h"

#define CHUNK_SIZE 240

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
// Native activity intervals are the source of truth for sleep. The minute
// history API may have gaps, so it must not determine whether sleep occurred.
static uint8_t s_native_sleep_mask[1440];
static time_t s_latest_sleep_end;
static bool s_found_sleep;

static bool find_latest_sleep_cb(HealthActivity activity, time_t start,
                                 time_t end, void *context) {
    if (activity != HealthActivitySleep || end <= start) return true;
    if (!s_found_sleep) {
        s_found_sleep = true;
        g_sleep_start = start;
        g_sleep_end = end;
        s_latest_sleep_end = end;
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
    time_t now = time(NULL); g_24h_start = now - (24 * 3600);
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
    int native_sleep_minutes = 0;
    int native_restful_minutes = 0;
    for (int i = 0; i < 1440; ++i) {
        if (s_native_sleep_mask[i]) {
            ++native_sleep_minutes;
            g_sleep_map[i] = s_native_restful_mask[i] ? 2 : 1;
            if (s_native_restful_mask[i]) ++native_restful_minutes;
        }
    }
    g_sleep_deep_sec = native_restful_minutes * 60;
    g_sleep_light_sec = (native_sleep_minutes - native_restful_minutes) * 60;
    g_longest_sleep_duration = s_found_sleep ? (int)(g_sleep_end - g_sleep_start) : 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "Sleep session=%d start=%ld end=%ld native=%d restful=%d",
            (int)s_found_sleep, (long)g_sleep_start, (long)g_sleep_end,
            native_sleep_minutes, native_restful_minutes);

    time_t rhr_window_end = (g_sleep_end != 0) ? g_sleep_end : time_start_of_today() + (6 * 3600);
    int m_hr = 999;
    HealthMinuteData chunk[CHUNK_SIZE];
    int valid_minute_records = 0;
    for (int c = 0; c < 6; c++) {
        time_t chunk_start = g_24h_start + (c * CHUNK_SIZE * 60), chunk_end = chunk_start + (CHUNK_SIZE * 60);
        uint32_t fetched = health_service_get_minute_history(chunk, CHUNK_SIZE, &chunk_start, &chunk_end);
        for (uint32_t i = 0; i < fetched; i++) {
            if (chunk[i].is_invalid) continue;
            ++valid_minute_records;
            int min_of_day = (chunk_start + (i * 60) - g_24h_start) / 60, b = min_of_day / 10;
            if (b >= 0 && b < 144) { g_act_map[b] += chunk[i].steps; if (chunk[i].heart_rate_bpm > g_stress_map[b]) g_stress_map[b] = chunk[i].heart_rate_bpm; }
            if (chunk_start + ((time_t)i * 60) >= rhr_window_end - (90 * 60) && chunk_start + ((time_t)i * 60) <= rhr_window_end) if (chunk[i].heart_rate_bpm > 0 && chunk[i].vmc == 0 && chunk[i].heart_rate_bpm < m_hr) m_hr = chunk[i].heart_rate_bpm;
        }
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "Minute history valid records=%d", valid_minute_records);
    g_morning_hr_comp = (m_hr == 999) ? 60 : m_hr;
    persist_write_int(PERSIST_KEY_RHR, g_morning_hr_comp);
    int net = g_sleep_deep_sec + g_sleep_light_sec;
    g_sleep_data_available = (s_found_sleep && net > 0);
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
}

// ============================================================================
// --- SECTION 4: DATALOGGING & LIFECYCLE ---
// ============================================================================
void health_engine_init() { 
    s_log_session = data_logging_create(0xDEADBEEF, DATA_LOGGING_INT, 4, false); 
    if (persist_exists(PERSIST_KEY_ENERGY_RESERVE)) g_live_energy_score = persist_read_int(PERSIST_KEY_ENERGY_RESERVE);
    if (persist_exists(PERSIST_KEY_SLEEP_SCORE)) g_sleep_comp = persist_read_int(PERSIST_KEY_SLEEP_SCORE);
    if (persist_exists(PERSIST_KEY_READINESS)) g_readiness_comp = persist_read_int(PERSIST_KEY_READINESS);
}

void health_engine_deinit() {
    data_logging_finish(s_log_session);
}

void health_engine_log_metrics() {
    int data[1] = { g_live_energy_score };
    data_logging_log(s_log_session, data, 1);
}