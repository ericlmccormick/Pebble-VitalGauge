#include "vitalgauge.h"

#define CHUNK_SIZE 60
#define SLEEP_LOOKBACK (36 * 3600)
#define SLEEP_JOIN_GAP (60 * 60)

int g_live_energy_score = -1;
int g_sleep_comp = -1;
int g_readiness_comp = -1;
bool g_sleep_data_available;
int g_card_eff_comp, g_exertion_comp, g_stress_comp;
int g_morning_hr_comp, g_rest_hr_baseline;
int g_hr_recovery_minutes = -1;
unsigned g_readiness_inputs;
int g_volume_level = 100;
int g_target_hr = 60;
VGReserve g_reserve;
bool g_activity_available;
int g_today_steps;

uint16_t g_act_map[144];
uint8_t g_stress_map[144];
uint8_t g_sleep_map[MINUTES_PER_DAY];
time_t g_24h_start, g_sleep_start, g_sleep_end;
int g_sleep_deep_sec, g_sleep_light_sec, g_longest_sleep_duration;
int g_sleep_inferred_sec, g_sleep_unknown_sec, g_sleep_awake_sec, g_nap_sec;
int g_hist_energy[7], g_hist_sleep[7], g_hist_stress[7], g_hist_activity[7];
bool g_hist_legacy[7];
int g_hist_archived[4][7];
bool g_hist_archive_available;

static uint8_t s_steps[MINUTES_PER_DAY], s_hr[MINUTES_PER_DAY];
static uint8_t s_motion[MINUTES_PER_DAY]; // 0 unavailable, 1 quiet, 2 active
static HealthMinuteData s_history_chunk[CHUNK_SIZE];
static time_t s_last_sync;
static bool s_force_reload;

enum { SLEEP_UNKNOWN, SLEEP_NATIVE, SLEEP_RESTFUL, SLEEP_INFERRED, SLEEP_AWAKE };

static VGSleepSession s_sessions[24];
static unsigned s_session_count;

static bool collect_sleep(HealthActivity activity, time_t start, time_t end, void *context) {
    if (end <= start) return true;
    if (s_session_count) {
        VGSleepSession *last = &s_sessions[s_session_count - 1];
        if (end >= (time_t)last->start - SLEEP_JOIN_GAP) {
            if (start < (time_t)last->start) {
                last->seconds += (end < (time_t)last->start ? end : (time_t)last->start) - start;
                last->start = start;
            }
            return true;
        }
    }
    if (s_session_count == ARRAY_LENGTH(s_sessions)) return false;
    s_sessions[s_session_count++] = (VGSleepSession){start, end, end - start};
    return true;
}

static bool mark_native_sleep(HealthActivity activity, time_t start, time_t end,
                              void *context) {
    if (start < g_sleep_start) start = g_sleep_start;
    if (end > g_sleep_end) end = g_sleep_end;
    int first = (start - g_24h_start + 59) / 60;
    int last = (end - g_24h_start + 59) / 60;
    if (first < 0) first = 0;
    if (last > MINUTES_PER_DAY) last = MINUTES_PER_DAY;
    int *restful = context;
    if (activity == HealthActivityRestfulSleep && end > start) *restful += end - start;
    for (int i = first; i < last; ++i) {
        if (activity == HealthActivityRestfulSleep) g_sleep_map[i] = SLEEP_RESTFUL;
        else if (g_sleep_map[i] != SLEEP_RESTFUL) g_sleep_map[i] = SLEEP_NATIVE;
    }
    return true;
}

static int hr_histogram_median(const uint16_t counts[231], int samples) {
    for (int bpm = 35, target = (samples + 1) / 2; bpm <= 230; ++bpm) {
        target -= counts[bpm];
        if (target <= 0) return bpm;
    }
    return 0;
}

static int resting_window_hr(void) {
    uint16_t counts[231] = {0};
    int samples = 0;
    time_t from = g_sleep_end - 60 * 60;
    for (int i = 0; i < MINUTES_PER_DAY; ++i) {
        time_t at = g_24h_start + i * 60;
        if (at >= g_sleep_end || at < from || s_motion[i] != 1 || !s_hr[i] ||
            (g_sleep_map[i] != SLEEP_NATIVE && g_sleep_map[i] != SLEEP_RESTFUL))
            continue;
        ++counts[s_hr[i]];
        ++samples;
    }
    return samples >= 12 ? hr_histogram_median(counts, samples) : 0;
}

static void rebuild_sleep(time_t now) {
    s_session_count = 0;
    g_sleep_start = g_sleep_end = 0;
    g_sleep_deep_sec = g_sleep_light_sec = g_longest_sleep_duration = 0;
    g_sleep_inferred_sec = g_sleep_unknown_sec = g_sleep_awake_sec = g_nap_sec = 0;
    g_morning_hr_comp = 0;
    g_sleep_data_available = false;
    memset(g_sleep_map, 0, sizeof(g_sleep_map));
    HealthServiceAccessibilityMask access = health_service_any_activity_accessible(
        HealthActivitySleep, now - SLEEP_LOOKBACK, now);
    if (!(access & HealthServiceAccessibilityMaskAvailable)) return;
    health_service_activities_iterate(HealthActivitySleep, now - SLEEP_LOOKBACK, now,
                                      HealthIterationDirectionPast, collect_sleep, NULL);
    if (!s_session_count) return;
    unsigned main = vg_main_sleep(s_sessions, s_session_count, now);
    VGSleepSession session = s_sessions[main];
    g_sleep_start = session.start;
    g_sleep_end = session.end;
    g_longest_sleep_duration = session.end - session.start;
    for (unsigned i = 0; i < s_session_count; ++i) {
        if (i != main && (time_t)s_sessions[i].end >= now - 24 * 3600 &&
            s_sessions[i].seconds < 3 * 3600)
            g_nap_sec += s_sessions[i].seconds;
    }
    health_service_activities_iterate(
        HealthActivitySleep | HealthActivityRestfulSleep, session.start, session.end,
        HealthIterationDirectionPast, mark_native_sleep, &g_sleep_deep_sec);
    g_sleep_deep_sec = vg_clamp(g_sleep_deep_sec, 0, session.seconds);
    g_sleep_light_sec = session.seconds - g_sleep_deep_sec;
    g_sleep_data_available = true;
    g_morning_hr_comp = resting_window_hr();

    // Bridge only short internal gaps with both quiet movement and sleep-like pulse.
    int first = vg_clamp((session.start - g_24h_start) / 60, 0, MINUTES_PER_DAY);
    int past_last = vg_clamp((session.end - g_24h_start) / 60, 0, MINUTES_PER_DAY);
    for (int i = first; i < past_last;) {
        if (g_sleep_map[i] != SLEEP_UNKNOWN) { ++i; continue; }
        int begin = i;
        while (i < past_last && g_sleep_map[i] == SLEEP_UNKNOWN) ++i;
        bool bounded = begin > first && i < past_last && i - begin <= 15;
        for (int j = begin; j < i; ++j) {
            if (bounded && s_motion[j] == 1 && s_hr[j] > 0 && g_morning_hr_comp > 0 &&
                s_hr[j] <= g_morning_hr_comp + 12) {
                g_sleep_map[j] = SLEEP_INFERRED;
                g_sleep_inferred_sec += 60;
            } else if (s_motion[j] == 2 &&
                       ((j > begin && s_motion[j - 1] == 2) ||
                        (j + 1 < i && s_motion[j + 1] == 2))) {
                g_sleep_map[j] = SLEEP_AWAKE;
                g_sleep_awake_sec += 60;
            }
        }
    }
    int unresolved = g_longest_sleep_duration - session.seconds -
                     g_sleep_inferred_sec - g_sleep_awake_sec;
    g_sleep_unknown_sec = unresolved > 0 ? unresolved : 0;
}

static void read_minute_history(time_t now) {
    time_t origin = (now / 60) * 60 - 24 * 3600;
    long shift = (origin - g_24h_start) / 60;
    bool full = s_force_reload || !s_last_sync || shift < 0 || shift >= MINUTES_PER_DAY;
    time_t start = full ? origin : s_last_sync - 15 * 60;
    if (start < origin) start = origin;
    if (full) {
        memset(s_steps, 0, sizeof(s_steps));
        memset(s_hr, 0, sizeof(s_hr));
        memset(s_motion, 0, sizeof(s_motion));
    } else if (shift > 0) {
        size_t remaining = MINUTES_PER_DAY - shift;
        memmove(s_steps, s_steps + shift, remaining);
        memmove(s_hr, s_hr + shift, remaining);
        memmove(s_motion, s_motion + shift, remaining);
        memset(s_steps + remaining, 0, shift);
        memset(s_hr + remaining, 0, shift);
        memset(s_motion + remaining, 0, shift);
    }
    g_24h_start = origin;
    time_t current_minute = now / 60 * 60;
    for (time_t chunk = start; chunk < current_minute; chunk += CHUNK_SIZE * 60) {
        time_t first = chunk;
        time_t end = chunk + CHUNK_SIZE * 60;
        if (end > current_minute) end = current_minute;
        unsigned fetched = health_service_get_minute_history(s_history_chunk, CHUNK_SIZE,
                                                              &first, &end);
        for (unsigned i = 0; i < fetched; ++i) {
            int slot = (first - origin) / 60 + (int)i;
            if (slot < 0 || slot >= MINUTES_PER_DAY) continue;
            const HealthMinuteData *minute = &s_history_chunk[i];
            if (minute->is_invalid) {
                s_steps[slot] = s_hr[slot] = s_motion[slot] = 0;
                continue;
            }
            s_steps[slot] = minute->steps;
            s_hr[slot] = vg_hr_band(minute->heart_rate_bpm) < 0 ? 0 : minute->heart_rate_bpm;
            s_motion[slot] = minute->steps <= 15 && minute->vmc <= 350 ? 1 : 2;
        }
    }
    s_last_sync = current_minute;
    s_force_reload = false;
    memset(g_act_map, 0, sizeof(g_act_map));
    memset(g_stress_map, 0, sizeof(g_stress_map));
    for (int i = 0; i < MINUTES_PER_DAY; ++i) {
        g_act_map[i / 10] += s_steps[i];
        if (s_hr[i] > g_stress_map[i / 10]) g_stress_map[i / 10] = s_hr[i];
    }
}

int health_engine_minute_hr(int index) {
    return index >= 0 && index < MINUTES_PER_DAY ? s_hr[index] : 0;
}

int health_engine_minute_steps(int index) {
    return index >= 0 && index < MINUTES_PER_DAY && s_motion[index] ? s_steps[index] : -1;
}

void health_engine_invalidate_history(void) { s_force_reload = true; }

static void calculate_readiness(time_t now) {
    VGDailyReading days[20] = {{0}};
    bool has_history = persist_read_data(VG_REST_HR_KEY, days, sizeof(days)) == sizeof(days);
    if (!has_history) memset(days, 0, sizeof(days));
    g_sleep_comp = -1;
    g_readiness_comp = -1;
    g_readiness_inputs = 0;
    g_rest_hr_baseline = 0;
    if (!g_sleep_data_available || now < g_sleep_end || now - g_sleep_end > 30 * 3600) return;

    int net = g_sleep_deep_sec + g_sleep_light_sec + g_sleep_inferred_sec;
    int duration_score = vg_clamp(net * 100 / (8 * 3600), 0, 100);
    int restful_score = net ? vg_clamp(g_sleep_deep_sec * 400 / net, 0, 100) : 0;
    g_sleep_comp = (duration_score * 70 + restful_score * 30) / 100;

    uint32_t day = vg_local_day(g_sleep_end);
    uint16_t counts[231] = {0};
    int samples = 0;
    for (unsigned i = 0; i < ARRAY_LENGTH(days); ++i) {
        if (days[i].day < day && day - days[i].day <= 30 &&
            days[i].value >= 35 && days[i].value <= 230) {
            ++counts[days[i].value];
            ++samples;
        }
    }
    g_rest_hr_baseline = samples >= 5 ? hr_histogram_median(counts, samples) : 0;
    if (g_rest_hr_baseline) {
        if (persist_read_int(VG_REST_BASELINE_KEY) != g_rest_hr_baseline)
            persist_write_int(VG_REST_BASELINE_KEY, g_rest_hr_baseline);
        if (persist_read_int(VG_REST_BASELINE_TIME_KEY) != (int)g_sleep_end)
            persist_write_int(VG_REST_BASELINE_TIME_KEY, g_sleep_end);
    }
    if (g_morning_hr_comp > 0 && day) {
        unsigned slot = 0;
        for (unsigned i = 0; i < ARRAY_LENGTH(days); ++i) {
            if (days[i].day == day) { slot = i; break; }
            if (days[i].day < days[slot].day) slot = i;
        }
        if (days[slot].day != day || days[slot].value != g_morning_hr_comp ||
            days[slot].timestamp != (uint32_t)g_sleep_end) {
            days[slot] = (VGDailyReading){day, g_sleep_end, g_morning_hr_comp};
            persist_write_data(VG_REST_HR_KEY, days, sizeof(days));
        }
    }

    int hrv_days = 0;
    int baseline = ui_stress_hrv_baseline_before(g_sleep_end, &hrv_days);
    int hrv = ui_stress_resting_hrv(g_sleep_end, g_sleep_end + 2 * 3600);
    g_readiness_comp = vg_readiness(g_sleep_comp, g_morning_hr_comp, g_rest_hr_baseline,
                                    hrv, baseline, &g_readiness_inputs);
}

typedef struct {
    uint32_t day;
    int8_t values[4];
    uint8_t model;
} TrendDay;

typedef struct {
    TrendDay current[7];
    TrendDay legacy[7];
} TrendHistory;

static TrendHistory s_trends;

static void load_trends(void) {
    if (persist_read_data(VG_TRENDS_KEY, &s_trends, sizeof(s_trends)) == sizeof(s_trends))
        return;
    memset(&s_trends, 0, sizeof(s_trends));
    if (!persist_exists(PERSIST_KEY_HIST_DAY)) return;
    int old_day = persist_read_int(PERSIST_KEY_HIST_DAY);
    const int keys[] = {PERSIST_KEY_HIST_ENERGY, PERSIST_KEY_HIST_SLEEP,
                        PERSIST_KEY_HIST_STRESS, PERSIST_KEY_HIST_ACTIVITY};
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    uint32_t today = vg_local_day(now);
    int difference = local ? local->tm_yday - old_day : -1;
    if (difference < 0 && local && old_day >= 358 && local->tm_yday < 7) {
        int year = local->tm_year + 1899;
        int year_days = vg_calendar_day(year + 1, 0) - vg_calendar_day(year, 0);
        difference = year_days - old_day + local->tm_yday;
    }
    for (int kind = 0; kind < 4; ++kind) {
        int values[7];
        if (persist_read_data(keys[kind], values, sizeof(values)) != sizeof(values)) continue;
        for (int i = 0; i < 7; ++i) {
            s_trends.legacy[i].values[kind] = values[i] > 0 && values[i] <= 100 ? values[i] : -1;
            if (difference >= 0 && difference <= 6)
                s_trends.legacy[i].day = today - difference - (6 - i);
        }
    }
    persist_write_data(VG_TRENDS_KEY, &s_trends, sizeof(s_trends));
}

void health_engine_refresh_trends(void) {
    time_t now = time(NULL);
    uint32_t day = vg_local_day(now);
    if (!day) return;
    TrendDay *entry = &s_trends.current[vg_trend_slot(day)];
    TrendDay updated = *entry;
    if (entry->day != day) {
        updated = (TrendDay){.day = day, .values = {-1, -1, -1, -1}, .model = VG_RESERVE_MODEL};
    }
    if (health_engine_reserve_fresh(now)) updated.values[0] = g_live_energy_score;
    if (g_sleep_data_available) {
        if (vg_local_day(g_sleep_end) == day) updated.values[1] = g_sleep_comp;
        else updated.values[1] = -1;
    }
    int stress = health_engine_stress_score();
    if (stress >= 0 && vg_local_day(vg_saved_stress().timestamp) == day) updated.values[2] = stress;
    if (g_activity_available) updated.values[3] = vg_clamp(g_today_steps / 100, 0, 100);
    if (entry->day != updated.day || memcmp(entry->values, updated.values, sizeof(updated.values))) {
        *entry = updated;
        persist_write_data(VG_TRENDS_KEY, &s_trends, sizeof(s_trends));
    }

    int *history[] = {g_hist_energy, g_hist_sleep, g_hist_stress, g_hist_activity};
    for (int i = 0; i < 7; ++i) {
        uint32_t target = day - (6 - i);
        const TrendDay *row = &s_trends.current[vg_trend_slot(target)];
        g_hist_legacy[i] = false;
        if (row->day != target) {
            row = NULL;
            for (unsigned j = 0; j < ARRAY_LENGTH(s_trends.legacy); ++j) {
                if (s_trends.legacy[j].day == target) {
                    row = &s_trends.legacy[j];
                    g_hist_legacy[i] = true;
                    break;
                }
            }
        }
        for (int kind = 0; kind < 4; ++kind)
            history[kind][i] = row ? row->values[kind] : -1;
    }
}

int health_engine_stress_score(void) {
    VGStressReading reading = vg_saved_stress();
    return vg_stress_valid(reading, time(NULL)) ? reading.score : -1;
}

bool health_engine_reserve_fresh(time_t now) {
    return g_reserve.observed && now >= (time_t)g_reserve.observed &&
           now - (time_t)g_reserve.observed <= VG_RESERVE_FRESHNESS;
}

void health_engine_calculate_energy(void) {
    time_t now = time(NULL);
    g_reserve = vg_latest_reserve(now);
    if (vg_update_reserve(&g_reserve, now)) {
        int result = persist_write_data(VG_FOREGROUND_RESERVE_KEY, &g_reserve, sizeof(g_reserve));
        if (result != sizeof(g_reserve)) APP_LOG(APP_LOG_LEVEL_WARNING, "Reserve save failed: %d", result);
    }
    g_live_energy_score = g_reserve.observed ? (g_reserve.units + 50) / 100 : -1;
    g_card_eff_comp = g_reserve.cardiac;
    g_exertion_comp = -g_reserve.activity;
    g_stress_comp = -g_reserve.stress;
    time_t today = time_start_of_today();
    g_activity_available = vg_metric_available(HealthMetricStepCount, today, now);
    g_today_steps = g_activity_available ? health_service_sum_today(HealthMetricStepCount) : 0;
}

static int heart_rate_recovery_minutes(void) {
    if (!g_rest_hr_baseline) return -1;
    // Find the most recent sustained movement period with a raised pulse.
    for (int i = MINUTES_PER_DAY - 4; i >= MINUTES_PER_DAY - 360; --i) {
        if (s_motion[i] != 2 || s_motion[i - 1] != 2 || s_motion[i - 2] != 2 ||
            s_hr[i] <= g_rest_hr_baseline + 20 || s_motion[i + 1] != 1) continue;
        int limit = vg_clamp(i + 21, 0, MINUTES_PER_DAY);
        for (int j = i + 1; j + 1 < limit; ++j) {
            if (s_motion[j] != 1 || !s_hr[j] || s_motion[j + 1] != 1 || !s_hr[j + 1]) break;
            if (s_hr[j] <= g_rest_hr_baseline + 10 && s_hr[j + 1] <= g_rest_hr_baseline + 10)
                return j - i;
        }
        return -1;
    }
    return -1;
}

void health_engine_update_buffers(void) {
    time_t now = time(NULL);
    read_minute_history(now);
    rebuild_sleep(now);
    calculate_readiness(now);
    g_hr_recovery_minutes = heart_rate_recovery_minutes();
    health_engine_calculate_energy();
    health_engine_refresh_trends();
}

void health_engine_init(void) {
    load_trends();
    for (int kind = 0; kind < 4; ++kind) {
        for (int i = 0; i < 7; ++i) {
            int value = s_trends.legacy[i].values[kind];
            g_hist_archived[kind][i] = value > 0 ? value : -1;
            if (value > 0) g_hist_archive_available = true;
        }
    }
    if (!g_hist_archive_available) {
        for (int i = 0; i < 7; ++i) {
            g_hist_energy[i] = g_hist_sleep[i] = g_hist_stress[i] = g_hist_activity[i] = -1;
        }
    }
}
void health_engine_deinit(void) {}
