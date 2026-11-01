#pragma once
#include <pebble.h>

// ============================================================================
// --- SECTION 1: PERSISTENCE KEYS ---
// ============================================================================
#define PERSIST_KEY_DARK_MODE 102
#define PERSIST_KEY_BREATH_STYLE 103
#define PERSIST_KEY_HAPTIC_LEVEL 104
#define PERSIST_KEY_BACKLIGHT 105
#define PERSIST_KEY_ENERGY_RESERVE 205 
#define PERSIST_KEY_SCORE 201       
#define PERSIST_KEY_READINESS 204   
#define PERSIST_KEY_SLEEP_SCORE 206 
#define PERSIST_KEY_VOLUME 101      
#define PERSIST_KEY_RHR 202         
#define PERSIST_KEY_HIST_DAY 300
#define PERSIST_KEY_HIST_ENERGY 301
#define PERSIST_KEY_HIST_SLEEP 302
#define PERSIST_KEY_HIST_STRESS 303
#define PERSIST_KEY_HIST_ACTIVITY 304


// ============================================================================
// --- SECTION 2: GLOBAL STATE VARIABLES ---
// ============================================================================
extern int g_live_energy_score;
extern int g_sleep_comp;
extern bool g_sleep_data_available;
extern int g_readiness_comp;
extern int g_card_eff_comp;
extern int g_exertion_comp;
extern int g_stress_comp;
extern int g_morning_hr_comp;
extern int g_volume_level;
extern int g_target_hr;

extern uint16_t g_act_map[144];
extern uint8_t g_stress_map[144];
extern uint8_t g_sleep_map[1440];

extern time_t g_24h_start;
extern time_t g_sleep_start;
extern time_t g_sleep_end;
extern int g_sleep_deep_sec;
extern int g_sleep_light_sec;
extern int g_longest_sleep_duration;

extern int g_hist_energy[7];
extern int g_hist_sleep[7];
extern int g_hist_stress[7];
extern int g_hist_activity[7];

extern bool g_dark_mode;
extern int g_breath_style; // 0: 4-4-4-4, 1: 4-7-8, 2: 4-8
extern int g_haptic_level; // 0: Off, 1: Low, 2: High
extern bool g_backlight_stay_on;

// Pebble-inspired shared visual language. Color encodes data in graphs;
// primary labels stay high-contrast and decorative accent is avoided.
#define VG_FONT_TITLE FONT_KEY_GOTHIC_24_BOLD
#define VG_FONT_BODY FONT_KEY_GOTHIC_18_BOLD
#define VG_FONT_HINT FONT_KEY_GOTHIC_14_BOLD
#define VG_FONT_NUMBER FONT_KEY_BITHAM_34_MEDIUM_NUMBERS
static inline GColor vg_foreground(void) {
    return g_dark_mode ? GColorWhite : GColorBlack;
}
static inline GColor vg_background(void) {
    return g_dark_mode ? GColorBlack : GColorWhite;
}


// ============================================================================
// --- SECTION 3: SUBSYSTEM DEFINITIONS ---
// ============================================================================
void health_engine_init(void);
void health_engine_deinit(void);
void health_engine_update_buffers(void);
void health_engine_calculate_energy(void);
void health_engine_log_metrics(void);

void config_sync_init(void);
void config_sync_deinit(void);

void audio_engine_play(uint32_t res_id);
void audio_engine_stop(void);

void draw_perimeter_segment(GContext *ctx, int p_start, int p_end, int thickness, GColor color);
GPoint get_perimeter_point(int p, int thickness);

void ui_hub_init(void);
void ui_hub_deinit(void);
void ui_hub_push(bool animated);
void ui_hub_mark_dirty(void);
bool ui_hub_is_visible(void);

void ui_stats_init(void);
void ui_stats_deinit(void);
void ui_activity_push(void);
void ui_sleep_push(void);
void ui_sleep_mark_dirty(void);
void ui_history_push(void);
void ui_battery_push(void);
void ui_battery_mark_dirty(void);

void ui_stress_init(void);
void ui_stress_deinit(void);
void ui_stress_push(void);
void ui_stress_mark_dirty(void);
void ui_stress_hrv_update(void);
int ui_stress_hrv_baseline(int *days);
int ui_stress_latest_hrv(void);
time_t ui_stress_latest_hrv_time(void);
void ui_stress_start_morning(void);
void ui_stress_start_automatic(void);

void ui_settings_init(void);
void ui_settings_deinit(void);
void ui_settings_push(void);