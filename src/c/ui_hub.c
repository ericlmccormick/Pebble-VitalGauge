#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: MAIN HUB WINDOW STATE ---
// ============================================================================
static Window *s_hub_window;
static Layer *s_hub_drawing_layer;
static Window *s_recovery_window;
static Layer *s_recovery_layer;
static bool s_show_morning;

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
  graphics_context_set_fill_color(ctx, g_dark_mode ? GColorBlack : GColorWhite);
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

  // Native-feeling, uncluttered center: retain the original three rings.
  graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, "VITALGAUGE", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(62, 78, 76, 18), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  int tag_h = 28;
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  
  graphics_context_set_fill_color(ctx, GColorPictonBlue);
  graphics_fill_rect(ctx, GRect(0, 0, 56, tag_h), 0, GCornerNone); 
  graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, "Stress", font, GRect(0, -4, 54, tag_h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, GRect(130, 0, 70, tag_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack); 
  graphics_draw_text(ctx, "Activity", font, GRect(130, -4, 68, tag_h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_fill_color(ctx, GColorChromeYellow); 
  graphics_fill_rect(ctx, GRect(0, 200, 75, tag_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, "Trends", font, GRect(0, 196, 73, tag_h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_fill_color(ctx, GColorVividViolet);
  graphics_fill_rect(ctx, GRect(152, 200, 48, tag_h), 0, GCornerNone);
  graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack); 
  graphics_draw_text(ctx, "Sleep", font, GRect(154, 196, 44, tag_h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  char energy_text[8];
  snprintf(energy_text, sizeof(energy_text), "%d", g_live_energy_score); 
  graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
  graphics_draw_text(ctx, energy_text, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS), 
                     GRect(center_x - 30, center_y - 11, 60, 40), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, "ENERGY", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(center_x - 27, center_y + 25, 54, 17),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}


// Energy / recovery detail area. The center of the existing rings is its
// entry point; Up/Down switches pages and Back returns to the dashboard.
static void recovery_draw(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, g_dark_mode ? GColorBlack : GColorWhite);
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
    GColor fg = g_dark_mode ? GColorWhite : GColorBlack;
    graphics_context_set_text_color(ctx, fg);
    const char *heading = s_show_morning ? "MORNING RECOVERY" : "ENERGY RESERVE";
    graphics_draw_text(ctx, heading, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(8, 10, 184, 30), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    int value = s_show_morning ? g_readiness_comp : g_live_energy_score;
    char number[16];
    snprintf(number, sizeof(number), "%d", value);
    graphics_draw_text(ctx, number, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS),
                       GRect(36, 44, 128, 48), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    graphics_context_set_stroke_color(ctx, GColorPictonBlue);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(25, 102), GPoint(175, 102));
    char details[160];
    if (s_show_morning) {
        int days = 0;
        int baseline = ui_stress_hrv_baseline(&days);
        int latest = ui_stress_latest_hrv();
        if (latest > 0) {
            snprintf(details, sizeof(details),
                     "Sleep %d/100   Rest HR %d\nHRV %d ms\nCalibration %d/7 days",
                     g_sleep_comp, g_morning_hr_comp, latest, days);
        } else {
            snprintf(details, sizeof(details),
                     "Sleep %d/100   Rest HR %d\nHRV not measured\nCalibration %d/7 days",
                     g_sleep_comp, g_morning_hr_comp, days);
        }
        (void)baseline;
    } else {
        int kcal = (int)health_service_sum_today(HealthMetricActiveKCalories);
        snprintf(details, sizeof(details),
                 "Sleep %d  Ready %d\nActive %d kcal\nReserve updates during day",
                 g_sleep_comp, g_readiness_comp, kcal);
    }
    graphics_draw_text(ctx, details, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(12, 112, 176, 79), GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, g_dark_mode ? GColorLightGray : GColorDarkGray);
    graphics_draw_text(ctx, "UP/DOWN  Change view", fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(10, 202, 180, 20), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
}
static void recovery_switch(ClickRecognizerRef recognizer, void *context) {
    s_show_morning = !s_show_morning;
    if (s_recovery_layer) layer_mark_dirty(s_recovery_layer);
}
static void recovery_clicks(void *context) {
    window_single_click_subscribe(BUTTON_ID_UP, recovery_switch);
    window_single_click_subscribe(BUTTON_ID_DOWN, recovery_switch);
    window_single_click_subscribe(BUTTON_ID_SELECT, recovery_switch);
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
    s_show_morning = g_sleep_end > 0 && now >= g_sleep_end &&
                     now - g_sleep_end < 2 * 3600;
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