#include "vitalgauge.h"

static AppTimer *s_health_refresh_timer;
static bool s_health_subscribed;
static time_t s_last_refresh;

static void publish_settings_cb(void *context) { config_sync_send_settings(); }

static void health_refresh_cb(void *context) {
    s_health_refresh_timer = NULL;
    s_last_refresh = time(NULL);
    health_engine_update_buffers();
    ui_health_views_mark_dirty();
}

static void schedule_refresh(unsigned delay) {
    if (s_health_refresh_timer) app_timer_cancel(s_health_refresh_timer);
    s_health_refresh_timer = app_timer_register(delay, health_refresh_cb, NULL);
}

static void health_event_cb(HealthEventType event, void *context) {
    if (event == HealthEventHRVUpdate) {
        ui_stress_hrv_update();
    } else if (event == HealthEventHeartRateUpdate) {
        ui_stress_bpm_update();
    } else if (event == HealthEventSignificantUpdate || event == HealthEventSleepUpdate) {
        if (event == HealthEventSignificantUpdate) health_engine_invalidate_history();
        schedule_refresh(2500);
    } else if (event == HealthEventMovementUpdate && !s_health_refresh_timer &&
               time(NULL) - s_last_refresh >= 60) {
        schedule_refresh(2500);
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    if (!s_health_refresh_timer && time(NULL) - s_last_refresh >= 5 * 60)
        schedule_refresh(100);
    else if (!s_health_refresh_timer) {
        health_engine_calculate_energy();
        health_engine_refresh_trends();
        ui_health_views_mark_dirty();
    }
}

void vitalgauge_apply_worker_setting(void) {
    if (g_background_enabled) {
        if (!app_worker_is_running()) {
            AppWorkerResult result = app_worker_launch();
            APP_LOG(APP_LOG_LEVEL_INFO, "Background worker launch result: %d", result);
        }
    } else if (app_worker_is_running()) {
        app_worker_kill();
    }
}

static void app_glance_callback(AppGlanceReloadSession *session, size_t limit, void *context) {
    if (!limit || !health_engine_reserve_fresh(time(NULL))) return;
    static char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "Energy Reserve: %d%%", g_live_energy_score);
    AppGlanceSlice slice = {
        .layout = {.icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = subtitle},
        .expiration_time = g_reserve.observed + VG_RESERVE_FRESHNESS
    };
    app_glance_add_slice(session, slice);
}

static void init(void) {
    app_message_open(512, 512);
    config_sync_init();
    health_engine_init();
    ui_hub_init();
    ui_stats_init();
    ui_stress_init();
    ui_settings_init();
    app_timer_register(1500, publish_settings_cb, NULL);
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    s_health_subscribed = health_service_events_subscribe(health_event_cb, NULL);
    if (!s_health_subscribed) APP_LOG(APP_LOG_LEVEL_WARNING, "Health subscription unavailable");

    vitalgauge_apply_worker_setting();
    if (launch_reason() == APP_LAUNCH_WORKER && g_background_enabled &&
        g_alerts_enabled && persist_read_int(VG_WORKER_LAUNCH_CONTEXT_KEY) == 1) {
        persist_delete(VG_WORKER_LAUNCH_CONTEXT_KEY);
        ui_stress_start_automatic();
    } else {
        ui_hub_push(true);
    }
    schedule_refresh(2500);
}

static void deinit(void) {
    if (s_health_refresh_timer) app_timer_cancel(s_health_refresh_timer);
    tick_timer_service_unsubscribe();
    app_glance_reload(app_glance_callback, NULL);
    ui_stress_deinit();
    if (s_health_subscribed) health_service_events_unsubscribe();
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
