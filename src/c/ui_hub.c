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
static void energy_contribution_perimeter(GContext *ctx) {
    draw_perimeter_segment(ctx, 0, 856, 9, GColorLightGray);
    if (g_live_energy_score < 0) return;
    const int changes[] = {g_reserve.recovery, g_reserve.cardiac,
                           g_reserve.activity, g_reserve.stress};
    const GColor colors[] = {GColorJaegerGreen, GColorPictonBlue, GColorOrange, GColorRed};
    int cursor = g_reserve.start_units;
    if (cursor > 0)
        draw_perimeter_segment(ctx, 0, cursor * 856 / 10000, 9, GColorDarkGray);
    for (unsigned i = 0; i < ARRAY_LENGTH(changes); ++i) {
        int next = vg_clamp(cursor + changes[i], 0, 10000);
        int first = cursor < next ? cursor : next;
        int last = cursor > next ? cursor : next;
        if (first != last)
            draw_perimeter_segment(ctx, first * 856 / 10000, last * 856 / 10000, 9, colors[i]);
        cursor = next;
    }
    GPoint point = get_perimeter_point(g_reserve.units * 856 / 10000, 9);
    graphics_context_set_fill_color(ctx, vg_foreground());
    graphics_fill_circle(ctx, point, 6);
}

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

typedef enum {
    HUB_ENERGY, HUB_STRESS, HUB_ACTIVITY, HUB_TRENDS, HUB_SLEEP, HUB_DESTINATION_COUNT
} HubDestination;

static HubDestination s_hub_focus;
static bool s_hub_focus_visible;
static const GRect s_hub_focus_bounds[] = {
    {{64, 78}, {72, 72}}, {{0, 0}, {57, 29}}, {{129, 0}, {71, 29}},
    {{0, 199}, {76, 29}}, {{151, 199}, {49, 29}},
};

static void open_destination(HubDestination destination) {
    static void (*const open[])(void) = {
        recovery_open, ui_stress_push, ui_activity_push, ui_history_push, ui_sleep_push,
    };
    play_haptic_tick();
    open[destination]();
}

static void screen_touch_tap_handler(GPoint point) {
    int x = point.x - 100, y = point.y - 114;
    HubDestination destination = HUB_ENERGY;
    if (x * x + y * y > 35 * 35) {
        destination = y < 0 ? (x < 0 ? HUB_STRESS : HUB_ACTIVITY)
                            : (x < 0 ? HUB_TRENDS : HUB_SLEEP);
    }
    s_hub_focus_visible = false;
    layer_mark_dirty(s_hub_drawing_layer);
    open_destination(destination);
}

static void hub_move_focus(ClickRecognizerRef recognizer, void *context) {
    int direction = click_recognizer_get_button_id(recognizer) == BUTTON_ID_UP ? -1 : 1;
    s_hub_focus = (s_hub_focus + HUB_DESTINATION_COUNT + direction) % HUB_DESTINATION_COUNT;
    s_hub_focus_visible = true;
    play_haptic_tick();
    layer_mark_dirty(s_hub_drawing_layer);
}

static void hub_select(ClickRecognizerRef recognizer, void *context) {
    s_hub_focus_visible = true;
    layer_mark_dirty(s_hub_drawing_layer);
    open_destination(s_hub_focus);
}

static void global_touch_handler(const TouchEvent *event, void *context) { 
    if (window_stack_get_top_window() == s_hub_window && event->type == TouchEvent_Touchdown) {
        screen_touch_tap_handler(GPoint(event->x, event->y)); 
    }
}

static void force_sync_handler(ClickRecognizerRef recognizer, void *context) {
    if (g_haptic_level) vibes_double_pulse();
    health_engine_invalidate_history();
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
    window_single_click_subscribe(BUTTON_ID_UP, hub_move_focus);
    window_single_click_subscribe(BUTTON_ID_DOWN, hub_move_focus);
    window_single_click_subscribe(BUTTON_ID_SELECT, hub_select);
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

  int step_pct = vg_clamp(g_today_steps / 100, 0, 100);
  int sleep_pct = vg_clamp(g_sleep_comp, 0, 100);
  int stress_val = vg_clamp(health_engine_stress_score(), 0, 100);

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

  char energy_text[12];
  if (g_live_energy_score >= 0) snprintf(energy_text, sizeof(energy_text), "%d", g_live_energy_score);
  else snprintf(energy_text, sizeof(energy_text), "--");
  graphics_context_set_text_color(ctx, vg_foreground());
  graphics_draw_text(ctx, energy_text, fonts_get_system_font(VG_FONT_NUMBER), 
                     GRect(center_x - 30, center_y - 20, 60, 40), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  if (g_live_energy_score >= 0 && !health_engine_reserve_fresh(time(NULL)))
    graphics_draw_text(ctx, "Last", fonts_get_system_font(VG_FONT_HINT),
                       GRect(75, 127, 50, 17), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  if (s_hub_focus_visible) {
    graphics_context_set_stroke_color(ctx, vg_foreground());
    graphics_context_set_stroke_width(ctx, 2);
    if (s_hub_focus == HUB_ENERGY) {
      graphics_draw_circle(ctx, GPoint(center_x, center_y), 35);
    } else {
      graphics_draw_rect(ctx, grect_inset(s_hub_focus_bounds[s_hub_focus], GEdgeInsets(2)));
    }
  }
}


// Energy / recovery detail area. The center of the existing rings is its
// entry point; Up/Down switches pages and Back returns to the dashboard.
static void recovery_draw(Layer *layer, GContext *ctx) {
    graphics_context_set_fill_color(ctx, vg_background());
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
    graphics_context_set_text_color(ctx, vg_foreground());
    if (s_recovery_page == 0 || s_recovery_page == 1) energy_contribution_perimeter(ctx);
    const char *titles[] = {"ENERGY RESERVE", "ENERGY CONTRIBUTORS", "MORNING RECOVERY"};
    graphics_draw_text(ctx, titles[s_recovery_page], fonts_get_system_font(VG_FONT_TITLE),
                       GRect(4, 8, 192, 32), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    int primary = s_recovery_page == 2 ? g_readiness_comp : g_live_energy_score;
    char number[20];
    if (primary >= 0) snprintf(number, sizeof(number), "%d", primary);
    else snprintf(number, sizeof(number), "--");
    graphics_draw_text(ctx, number, fonts_get_system_font(VG_FONT_NUMBER),
                       GRect(28, 43, 144, 49), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
    char info[180];
    time_t now = time(NULL);
    if (s_recovery_page == 0) {
        if (g_live_energy_score < 0) {
            snprintf(info, sizeof(info), "Collecting Health data\nWear the watch to update");
        } else {
            time_t observed = g_reserve.observed;
            struct tm *local = localtime(&observed);
            char stamp[24] = "--";
            if (local) strftime(stamp, sizeof(stamp), "%b %d %H:%M", local);
            snprintf(info, sizeof(info), "%s %s\nBackground %s",
                     health_engine_reserve_fresh(now) ? "Updated" : "Last reading",
                     stamp, app_worker_is_running() ? "active" : "inactive");
        }
    } else if (s_recovery_page == 2) {
        int days = 0;
        int baseline = g_sleep_end ?
                       ui_stress_hrv_baseline_before(g_sleep_end, &days) :
                       ui_stress_hrv_baseline(&days);
        int hrv = ui_stress_resting_hrv(g_sleep_end, g_sleep_end + 2 * 3600);
        char pulse[32] = "Rest HR unavailable";
        char variability[38];
        if (g_morning_hr_comp && g_rest_hr_baseline)
            snprintf(pulse, sizeof(pulse), "Rest HR %d / base %d", g_morning_hr_comp, g_rest_hr_baseline);
        else if (g_morning_hr_comp)
            snprintf(pulse, sizeof(pulse), "Rest HR %d (learning)", g_morning_hr_comp);
        if (baseline && hrv)
            snprintf(variability, sizeof(variability), "HRV %d / base %d ms", hrv, baseline);
        else if (baseline)
            snprintf(variability, sizeof(variability), "No morning HRV scan");
        else
            snprintf(variability, sizeof(variability), "HRV calibration %d/7 days", days);
        snprintf(info, sizeof(info), "%s\n%s\n%s",
                 g_readiness_comp < 0 ? "Sleep unavailable" :
                 g_readiness_inputs == 7 ? "Sleep, resting HR and HRV" : "Partial estimate",
                 pulse, variability);
    } else {
        char recovery[36] = "Return to baseline: --";
        if (g_hr_recovery_minutes >= 0)
            snprintf(recovery, sizeof(recovery), "Return to baseline: %d min", g_hr_recovery_minutes);
        snprintf(info, sizeof(info),
                 "Latest update (points)\nSleep +%d.%02d  Activity -%d.%02d\nHR recovery +%d.%02d\nStress -%d.%02d\n%s",
                 g_reserve.recovery / 100, g_reserve.recovery % 100,
                 g_exertion_comp / 100, g_exertion_comp % 100,
                 g_card_eff_comp / 100, g_card_eff_comp % 100,
                 g_stress_comp / 100, g_stress_comp % 100, recovery);
    }
    graphics_draw_text(ctx, info, fonts_get_system_font(VG_FONT_BODY),
                       GRect(8, 105, 184, 102), GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "UP/DOWN  Browse", fonts_get_system_font(VG_FONT_HINT),
                       GRect(10, 207, 180, 19), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
}
static void recovery_up(ClickRecognizerRef recognizer, void *context) {
    s_recovery_page = (s_recovery_page + 2) % 3;
    s_last_recovery_page = s_recovery_page;
    layer_mark_dirty(s_recovery_layer);
}
static void recovery_down(ClickRecognizerRef recognizer, void *context) {
    s_recovery_page = (s_recovery_page + 1) % 3;
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
    s_recovery_page = s_last_recovery_page;
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

void ui_health_views_mark_dirty(void) {
    ui_hub_mark_dirty();
    ui_stats_mark_dirty();
    ui_stress_mark_dirty();
    if (s_recovery_layer) layer_mark_dirty(s_recovery_layer);
}
