#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: EVENT WRAPPERS & TICK HANDLERS ---
// ============================================================================
static AppTimer *s_health_refresh_timer;
static bool s_health_subscribed;
#define MORNING_SCAN_DAY_KEY 412
#define HRV_WORKER_LAUNCH_CONTEXT_KEY 414
#define HRV_REST_PENDING_KEY 416
static void morning_scan_maybe_start(void) {
    time_t now = time(NULL);
    if (!g_sleep_data_available || !g_sleep_end || now < g_sleep_end ||
        now - g_sleep_end > 90 * 60) return;
    struct tm *date = localtime(&now);
    if (!date) return;
    int day = date->tm_year * 366 + date->tm_yday;
    if (persist_exists(MORNING_SCAN_DAY_KEY) &&
        persist_read_int(MORNING_SCAN_DAY_KEY) == day) return;
    // Require a quiet recent 15-minute period; no movement is not proof of rest.
    HealthValue steps = health_service_sum(HealthMetricStepCount,
                                          now - 15 * 60, now);
    HealthActivityMask activity = health_service_peek_current_activities();
    if (steps < 0 || steps > 15 ||
        (activity & (HealthActivityWalk | HealthActivityRun |
                     HealthActivityOpenWorkout))) return;
    // Set once per day to avoid interruptions and repeated scans.
    persist_write_int(MORNING_SCAN_DAY_KEY, day);
    ui_stress_start_morning();
}


static void health_refresh_cb(void *data) {
    s_health_refresh_timer = NULL;
    health_engine_update_buffers();
    ui_hub_mark_dirty();
    ui_sleep_mark_dirty();
    morning_scan_maybe_start();
}

static void health_event_cb(HealthEventType event, void *context) {
    if (event == HealthEventHRVUpdate) {
        ui_stress_hrv_update();
        return;
    }
    if (event != HealthEventSleepUpdate &&
        event != HealthEventSignificantUpdate) return;
    if (s_health_refresh_timer) app_timer_cancel(s_health_refresh_timer);
    s_health_refresh_timer = app_timer_register(750, health_refresh_cb, NULL);
}

static void initial_sync_timer_cb(void *data) {
    health_engine_update_buffers();
    ui_hub_mark_dirty();
    ui_sleep_mark_dirty();
    morning_scan_maybe_start();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    health_engine_calculate_energy();
    ui_hub_mark_dirty();
    ui_battery_mark_dirty();
}

static void app_glance_callback(AppGlanceReloadSession *session, size_t limit, void *context) {
    if (limit < 1) return;
    static char glance_subtitle[32];
    snprintf(glance_subtitle, sizeof(glance_subtitle), "Energy Reserve: %d%%", g_live_energy_score);
    
    AppGlanceSlice slice = (AppGlanceSlice) {
        .layout = {
            .icon = APP_GLANCE_SLICE_DEFAULT_ICON, 
            .subtitle_template_string = glance_subtitle
        },
        .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION
    };
    app_glance_add_slice(session, slice);
}

// ============================================================================
// --- SECTION 2: APP LIFECYCLE ---
// ============================================================================
static void init(void) {
    app_message_open(512, 512);
    config_sync_init();
    health_engine_init();
    ui_hub_init();
    ui_stats_init();
    ui_stress_init();

    // Initialize user settings with defaults
    g_dark_mode = persist_exists(PERSIST_KEY_DARK_MODE) ? persist_read_bool(PERSIST_KEY_DARK_MODE) : false;
    g_breath_style = persist_exists(PERSIST_KEY_BREATH_STYLE) ? persist_read_int(PERSIST_KEY_BREATH_STYLE) : 0;
    g_haptic_level = persist_exists(PERSIST_KEY_HAPTIC_LEVEL) ? persist_read_int(PERSIST_KEY_HAPTIC_LEVEL) : 2;
    g_backlight_stay_on = persist_exists(PERSIST_KEY_BACKLIGHT) ? persist_read_bool(PERSIST_KEY_BACKLIGHT) : false;
    
    // Initialize Settings UI
    ui_settings_init();
  
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    s_health_subscribed = health_service_events_subscribe(health_event_cb, NULL);
    if (!s_health_subscribed) APP_LOG(APP_LOG_LEVEL_WARNING, "Health event subscription failed");

    // Launch the background worker to enable silent background monitoring
    app_worker_launch();

    if (launch_reason() == APP_LAUNCH_WORKER) {
        int context = persist_exists(HRV_WORKER_LAUNCH_CONTEXT_KEY) ?
            persist_read_int(HRV_WORKER_LAUNCH_CONTEXT_KEY) : 1;
        persist_delete(HRV_WORKER_LAUNCH_CONTEXT_KEY);
        if (context == 2 && persist_exists(HRV_REST_PENDING_KEY)) {
            time_t requested = persist_read_int(HRV_REST_PENDING_KEY);
            persist_delete(HRV_REST_PENDING_KEY);
            if (requested > 0 && time(NULL) - requested <= 120)
                ui_stress_start_morning();
            else ui_hub_push(false);
        } else ui_stress_start_automatic();
    } else {
        ui_hub_push(true);
        app_timer_register(500, initial_sync_timer_cb, NULL);
    }
}

static void deinit(void) {
    if (s_health_refresh_timer) app_timer_cancel(s_health_refresh_timer);
    if (s_health_subscribed) health_service_events_unsubscribe();
    tick_timer_service_unsubscribe();
    app_glance_reload(app_glance_callback, NULL);
    ui_stress_deinit();
    ui_stats_deinit();
    ui_hub_deinit();
    health_engine_deinit();
    config_sync_deinit();
    ui_settings_deinit();
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}