#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: MAIN HUB WINDOW STATE ---
// ============================================================================
static Window *s_hub_window;
static Layer *s_hub_drawing_layer;
static Window *s_recovery_window;
static Layer *s_recovery_layer;
static uint8_t s_recovery_page;
static uint8_t s_last_recovery_page;

static void recovery_open(void);

// ============================================================================
// --- SECTION 2: TOUCH & CLICK ROUTING ---
// ============================================================================

static void play_haptic_tick() {
    if (g_haptic_level == 0) return; // Off
    if (g_haptic_level == 2) {
        vibes_short_pulse(); // High
        return;
    }
    // Low (Subtle Tick)
    static const uint32_t segments[] = { 15 }; 
    VibePattern pat = { .durations = segments, .num_segments = ARRAY_LENGTH(segments) };
    vibes_enqueue_custom_pattern(pat);
}

static void screen_touch_tap_handler(GPoint tapped_coordinate) { 
    int center_x = 100, center_y = 114;
    
    play_haptic_tick();

    if (((tapped_coordinate.x - center_x) * (tapped_coordinate.x - center_x) + (tapped_coordinate.y - center_y) * (tapped_coordinate.y - center_y)) <= (35 * 35)) { 
        recovery_open(); 
        return; 
    } 
    if (tapped_coordinate.x < center_x && tapped_coordinate.y < center_y) ui_stress_push();
    else if (tapped_coordinate.x >= center_x && tapped_coordinate.y < center_y) ui_activity_push();
    else if (tapped_coordinate.x < center_x && tapped_coordinate.y >= center_y) ui_history_push();
    else if (tapped_coordinate.x >= center_x && tapped_coordinate.y >= center_y) ui_sleep_push();
}

static void global_touch_handler(const TouchEvent *event, void *context) { 
    if (window_stack_get_top_window() == s_hub_window && event->type == TouchEvent_Touchdown) {
        screen_touch_tap_handler(GPoint(event->x, event->y)); 
    }
}

static void force_sync_handler(ClickRecognizerRef recognizer, void *context) {
    vibes_double_pulse();
    health_engine_update_buffers();
    if (s_hub_drawing_layer) layer_mark_dirty(s_hub_drawing_layer);
}

// Add this handler function to SECTION 2 of ui_hub.c
static void hub_long_select_handler(ClickRecognizerRef recognizer, void *context) {
    if (g_haptic_level > 0) vibes_short_pulse();
    ui_settings_push();
}

// Replace the hub_click_config_provider in SECTION 2 of ui_hub.c
static void hub_click_config_provider(void *context) {
    window_long_click_subscribe(BUTTON_ID_UP, 500, (ClickHandler)force_sync_handler, NULL);
    window_long_click_subscribe(BUTTON_ID_SELECT, 500, hub_long_select_handler, NULL);
}

// ============================================================================
// --- SECTION 3: HUB RENDERING ---
// ============================================================================
static void hub_drawing_render_procedure(Layer *layer, GContext *ctx) { 
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, vg_background());
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

  int center_x = 100, center_y = 114, ring_thickness = 14, ring_spacing = 18;
  int r1 = 85, r2 = r1 - ring_spacing, r3 = r2 - ring_spacing;
  GRect rect_activity  = GRect(center_x - r1, center_y - r1, r1 * 2, r1 * 2);
  GRect rect_sleep     = GRect(center_x - r2, center_y - r2, r2 * 2, r2 * 2);
  GRect rect_stress    = GRect(center_x - r3, center_y - r3, r3 * 2, r3 * 2);

  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_radial(ctx, rect_activity, GOvalScaleModeFitCircle, ring_thickness, 0, DEG_TO_TRIGANGLE(360));
  graphics_fill_radial(ctx, rect_sleep, GOvalScaleModeFitCircle, ring_thickness, 0, DEG_TO_TRIGANGLE(360));
  graphics_fill_radial(ctx, rect_stress, GOvalScaleModeFitCircle, ring_thickness, 0, DEG_TO_TRIGANGLE(360));

  int steps = (int)health_service_sum_today(HealthMetricStepCount);
  int step_pct = (steps * 100) / 10000;
  if (step_pct > 100) step_pct = 100;
  int sleep_pct = g_sleep_comp;
  if (sleep_pct > 100) sleep_pct = 100;
  int stress_val = persist_exists(PERSIST_KEY_SCORE) ? persist_read_int(PERSIST_KEY_SCORE) : 50;

  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_radial(ctx, rect_activity, GOvalScaleModeFitCircle, ring_thickness, 0, DEG_TO_TRIGANGLE((step_pct * 360) / 100));
  graphics_context_set_fill_color(ctx, GColorVividViolet);
  graphics_fill_radial(ctx, rect_sleep, GOvalScaleModeFitCircle, ring_thickness, 0, DEG_TO_TRIGANGLE((sleep_pct * 360) / 100));
  graphics_context_set_fill_color(ctx, GColorPictonBlue);
  graphics_fill_radial(ctx, rect_stress, GOvalScaleModeFitCircle, ring_thickness, 0, DEG_TO_TRIGANGLE((stress_val * 360) / 100));

  int tag_h = 28;
  GFont font = fonts_get_system_font(VG_FONT_TITLE);
  
  graphics_context_set_fill_color(ctx, GColorPictonBlue);
  graphics_fill_rect(ctx, GRect(0, 0, 56, tag_h), 0, GCornerNone); 
  graphics_context_set_text_color(ctx, vg_foreground());
  graphics_draw_text(ctx, "Stress", font, GRect(0, -4, 54, tag_h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, GRect(130, 0, 70, tag_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, vg_foreground()); 
  graphics_draw_text(ctx, "Activity", font, GRect(130, -4, 68, tag_h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_fill_color(ctx, GColorChromeYellow); 
  graphics_fill_rect(ctx, GRect(0, 200, 75, tag_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, vg_foreground());
  graphics_draw_text(ctx, "Trends", font, GRect(0, 196, 73, tag_h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_fill_color(ctx, GColorVividViolet);
  graphics_fill_rect(ctx, GRect(152, 200, 48, tag_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, vg_foreground()); 
  graphics_draw_text(ctx, "Sleep", font, GRect(154, 196, 44, tag_h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  char energy_text[8];
  snprintf(energy_text, sizeof(energy_text), "%d", g_live_energy_score); 
  graphics_context_set_text_color(ctx, vg_foreground());
  graphics_draw_text(ctx, energy_text, fonts_get_system_font(VG_FONT_NUMBER), 
                     GRect(center_x - 30, center_y - 20, 60, 40), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}


// Energy / recovery detail area. The center of the existing rings is its
// entry point; Up/Down switches pages and Back returns to the dashboard.
static void recovery_draw(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, vg_background());
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
    graphics_context_set_text_color(ctx, vg_foreground());
    const char *titles[] = {"ENERGY RESERVE", "MORNING RECOVERY", "RECOVERY DETAILS", "SLEEP DETAILS"};
    graphics_draw_text(ctx, titles[s_recovery_page], fonts_get_system_font(VG_FONT_TITLE),
                       GRect(4, 8, 192, 32), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    int primary = s_recovery_page == 0 ? g_live_energy_score :
                  s_recovery_page == 1 ? g_readiness_comp :
                  s_recovery_page == 2 ? ui_stress_latest_hrv() :
                  g_sleep_comp;
    char number[20];
    if (s_recovery_page == 2 && primary == 0) snprintf(number, sizeof(number), "--");
    else snprintf(number, sizeof(number), "%d", primary);
    graphics_draw_text(ctx, number, fonts_get_system_font(VG_FONT_NUMBER),
                       GRect(28, 43, 144, 49), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    char info[180];
    if (s_recovery_page == 0) {
        int kcal = (int)health_service_sum_today(HealthMetricActiveKCalories);
        snprintf(info, sizeof(info), "Sleep %d   Ready %d\nActive %d kcal\nStress component %d\nActivity drain %d",
                 g_sleep_comp, g_readiness_comp, kcal, g_stress_comp, g_exertion_comp);
    } else if (s_recovery_page == 1) {
        int days = 0;
        int baseline = ui_stress_hrv_baseline(&days);
        int latest = ui_stress_latest_hrv();
        if (latest && baseline)
            snprintf(info, sizeof(info), "HRV %d ms   Base %d ms\nDifference %+d%%\nRest HR %d bpm\n%d/7 calibration days",
                     latest, baseline, (latest - baseline) * 100 / baseline,
                     g_morning_hr_comp, days);
        else
            snprintf(info, sizeof(info), "HRV %s\nRest HR %d bpm\nSleep score %d/100\n%d/7 calibration days",
                     latest ? "Calibrating" : "Unavailable",
                     g_morning_hr_comp, g_sleep_comp, days);
    } else if (s_recovery_page == 2) {
        int days = 0;
        int baseline = ui_stress_hrv_baseline(&days);
        time_t measured = ui_stress_latest_hrv_time();
        long age_hours = measured > 0 && time(NULL) >= measured ?
                         (long)((time(NULL) - measured) / 3600) : -1;
        snprintf(info, sizeof(info), "HRV %s\nBaseline %s\nRest HR %d bpm\n%s",
                 primary ? "recorded" : "unavailable",
                 baseline ? "ready" : "calibrating", g_morning_hr_comp,
                 age_hours < 0 ? "No timestamp" :
                 age_hours < 1 ? "Measured within 1h" :
                 age_hours < 24 ? "Measured today" : "Measurement is older");
    } else {
        int total = g_sleep_deep_sec + g_sleep_light_sec;
        snprintf(info, sizeof(info), "Total %dh %02dm\nDeep %dh %02dm\nLight %dh %02dm\n%s",
                 total / 3600, (total / 60) % 60,
                 g_sleep_deep_sec / 3600, (g_sleep_deep_sec / 60) % 60,
                 g_sleep_light_sec / 3600, (g_sleep_light_sec / 60) % 60,
                 g_sleep_data_available ? "Last detected sleep" : "Sleep data unavailable");
    }
    graphics_draw_text(ctx, info, fonts_get_system_font(VG_FONT_BODY),
                       GRect(8, 105, 184, 102), GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "UP/DOWN  Browse", fonts_get_system_font(VG_FONT_HINT),
                       GRect(10, 207, 180, 19), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
}
static void recovery_up(ClickRecognizerRef recognizer, void *context) {
    s_recovery_page = (s_recovery_page + 3) % 4;
    s_last_recovery_page = s_recovery_page;
    layer_mark_dirty(s_recovery_layer);
}
static void recovery_down(ClickRecognizerRef recognizer, void *context) {
    s_recovery_page = (s_recovery_page + 1) % 4;
    s_last_recovery_page = s_recovery_page;
    layer_mark_dirty(s_recovery_layer);
}
static void recovery_clicks(void *context) {
    window_single_click_subscribe(BUTTON_ID_UP, recovery_up);
    window_single_click_subscribe(BUTTON_ID_DOWN, recovery_down);
}
static void recovery_load(Window *window) {
    s_recovery_layer = layer_create(layer_get_bounds(window_get_root_layer(window)));
    layer_set_update_proc(s_recovery_layer, recovery_draw);
    layer_add_child(window_get_root_layer(window), s_recovery_layer);
    window_set_click_config_provider(window, recovery_clicks);
}
static void recovery_unload(Window *window) {
    layer_destroy(s_recovery_layer);
    s_recovery_layer = NULL;
}
static void recovery_open(void) {
    time_t now = time(NULL);
    s_recovery_page = s_last_recovery_page;
    (void)now;
    window_stack_push(s_recovery_window, true);
}

// ============================================================================
// --- SECTION 4: HUB LIFECYCLE ---
// ============================================================================
static void hub_window_load(Window *w) { 
  s_hub_drawing_layer = layer_create(layer_get_bounds(window_get_root_layer(w)));
  layer_set_update_proc(s_hub_drawing_layer, hub_drawing_render_procedure); 
  layer_add_child(window_get_root_layer(w), s_hub_drawing_layer);
  window_set_click_config_provider(w, hub_click_config_provider);
} 

static void hub_window_unload(Window *w) { 
    layer_destroy(s_hub_drawing_layer);
}

void ui_hub_init() {
    s_recovery_window = window_create();
    window_set_window_handlers(s_recovery_window, (WindowHandlers){
        .load = recovery_load, .unload = recovery_unload });
    s_hub_window = window_create();
    window_set_window_handlers(s_hub_window, (WindowHandlers){ .load = hub_window_load, .unload = hub_window_unload });
    touch_service_subscribe(global_touch_handler, NULL);
}

void ui_hub_deinit() {
    touch_service_unsubscribe();
    window_destroy(s_recovery_window);
    window_destroy(s_hub_window);
}

void ui_hub_push(bool animated) {
    window_stack_push(s_hub_window, animated);
}

void ui_hub_mark_dirty() {
    if (s_hub_drawing_layer && window_is_loaded(s_hub_window)) {
        layer_mark_dirty(s_hub_drawing_layer);
    }
}
bool ui_hub_is_visible(void) {
    return window_stack_get_top_window() == s_hub_window;
}
