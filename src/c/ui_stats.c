#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: STATS MODULE WINDOW STATE ---
// ============================================================================
static Window *s_activity_window;   
static Window *s_sleep_window;      
static Window *s_history_window;
static Window *s_battery_window;

static Layer *s_activity_canvas;    
static Layer *s_sleep_canvas;
static Layer *s_history_canvas;
static Layer *s_battery_canvas;    

static int s_current_history_view = 0;

// ============================================================================
// --- SECTION 2: ACTIVITY RENDERER ---
// ============================================================================
static void activity_update_proc(Layer *layer, GContext *ctx) { 
    graphics_context_set_antialiased(ctx, true);
    graphics_context_set_fill_color(ctx, vg_background());
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

    int steps = (int)health_service_sum_today(HealthMetricStepCount);
    draw_perimeter_segment(ctx, 0, 856, 10, GColorLightGray);

    time_t wake_time = (g_sleep_end != 0) ? g_sleep_end : time_start_of_today() + (6 * 3600); 
    time_t now = time(NULL);
    int expected_awake_mins = 16 * 60;
    int c_idle = 0, c_rest = 0, c_light = 0, c_mod = 0, c_max = 0;
    
    int group_start_p = -1;
    GColor current_c = GColorClear;
    int current_p = 0;

    for (int b = 0; b < 144; b++) { 
        time_t bucket_time = g_24h_start + (b * 600);
        if (bucket_time >= wake_time && bucket_time <= now) {
            uint16_t bucket_steps = g_act_map[b];
            GColor c;
            
            if (bucket_steps >= 800) { c = GColorRed; c_max += 10; } 
            else if (bucket_steps >= 400) { c = GColorOrange; c_mod += 10; } 
            else if (bucket_steps > 0) { c = GColorJaegerGreen; c_light += 10; } 
            else { c = GColorDarkGray; c_idle += 10; }

            int minute_offset = (bucket_time - wake_time) / 60;
            int p = (minute_offset * 856) / expected_awake_mins;
            if (p > 856) p = 856;

            if (group_start_p == -1) { 
                group_start_p = p;
                current_c = c;
            } else if (!gcolor_equal(c, current_c)) { 
                int end_p = p;
                if (end_p <= group_start_p) end_p = group_start_p + 1;
                draw_perimeter_segment(ctx, group_start_p, end_p, 10, current_c);
                group_start_p = p;
                current_c = c;
            } 
            current_p = p;
        }
    } 
    
    if (group_start_p != -1) { 
        draw_perimeter_segment(ctx, group_start_p, current_p + 1, 10, current_c);
        graphics_context_set_fill_color(ctx, current_c);
        graphics_fill_circle(ctx, get_perimeter_point(current_p, 10), 6);
    }

    int goal_pct = (steps * 100) / 10000;
    if(goal_pct > 100) goal_pct = 100;

    graphics_context_set_text_color(ctx, vg_foreground());  
    graphics_draw_text(ctx, "ACTIVITY", fonts_get_system_font(VG_FONT_TITLE), 
                       GRect(0, 5, 200, 35), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    char top_buf[32];
    snprintf(top_buf, sizeof(top_buf), "Goal: %d%%\n%d Steps", goal_pct, steps);
    graphics_draw_text(ctx, top_buf, fonts_get_system_font(VG_FONT_BODY), 
                       GRect(0, 40, 200, 45), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    int y_start = 88;
    int y_step = 22;
    int x_sq = 45;
    int x_txt = 65;
    GFont row_font = fonts_get_system_font(VG_FONT_BODY);

    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
    char buf_max[32];
    snprintf(buf_max, sizeof(buf_max), "Max: %dh %dm", c_max / 60, c_max % 60);
    graphics_draw_text(ctx, buf_max, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    y_start += y_step;
    graphics_context_set_fill_color(ctx, GColorOrange);
    graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
    char buf_mod[32];
    snprintf(buf_mod, sizeof(buf_mod), "Med: %dh %dm", c_mod / 60, c_mod % 60);
    graphics_draw_text(ctx, buf_mod, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    y_start += y_step;
    graphics_context_set_fill_color(ctx, GColorJaegerGreen);
    graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
    char buf_light[32];
    snprintf(buf_light, sizeof(buf_light), "Light: %dh %dm", c_light / 60, c_light % 60);
    graphics_draw_text(ctx, buf_light, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    y_start += y_step;
    graphics_context_set_fill_color(ctx, GColorPictonBlue);
    graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
    char buf_rest[32];
    snprintf(buf_rest, sizeof(buf_rest), "Rest: %dh %dm", c_rest / 60, c_rest % 60);
    graphics_draw_text(ctx, buf_rest, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    y_start += y_step;
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
    char buf_idle[32];
    snprintf(buf_idle, sizeof(buf_idle), "Idle: %dh %dm", c_idle / 60, c_idle % 60);
    graphics_draw_text(ctx, buf_idle, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// ============================================================================
// --- SECTION 3: SLEEP RENDERER ---
// ============================================================================
static void sleep_update_proc(Layer *layer, GContext *ctx) { 
    // Enable antialiasing for smoother rendering
    graphics_context_set_antialiased(ctx, true);
    // Set background color to white
    graphics_context_set_fill_color(ctx, vg_background());
    // Paint physical background canvas
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

    // Initialize string buffer for sleep start time
    char sleep_start_str[16] = "--:--";
    // Initialize string buffer for sleep end time
    char sleep_end_str[16] = "--:--";

    // Verify a valid sleep session exists before formatting times
    if (g_sleep_start != 0) { 
        // Convert start timestamp to local time structure
        struct tm *start_tm = localtime(&g_sleep_start);
        // Format start time into 12-hour string
        strftime(sleep_start_str, sizeof(sleep_start_str), "%I:%M %p", start_tm);
        // Convert end timestamp to local time structure
        struct tm *end_tm = localtime(&g_sleep_end);
        // Format end time into 12-hour string
        strftime(sleep_end_str, sizeof(sleep_end_str), "%I:%M %p", end_tm);
    } 

    // Draw the underlying light gray perimeter track
    draw_perimeter_segment(ctx, 0, 856, 10, GColorLightGray);
    
    // Calculate total duration of the sleep block in minutes
    int total_sleep_mins = (g_sleep_end - g_sleep_start) / 60;
    // Calculate the physical pixel endpoint based on the sleep score percentage
    int max_p = 856; // Full clock span; never truncate the timeline by sleep score

    // Render sleep stages if valid data exists
    if (total_sleep_mins > 0 && g_sleep_data_available) {
        // Initialize variable tracking the start of a contiguous color group
        int group_start_p = -1;
        // Initialize variable tracking the current color being rendered
        GColor current_c = GColorClear;
        // Initialize variable tracking the current pixel position
        int current_p = 0;

        // Iterate through every minute of the sleep session
        for (int m = 0; m < total_sleep_mins; m++) { 
            // Calculate the absolute timestamp for this specific minute
            time_t min_time = g_sleep_start + (m * 60);
            // Calculate the minute of the day mapping to the array index
            int minute_of_day = (min_time - g_24h_start) / 60;
            
            // Verify the index is within the bounds of the 24-hour array
            if (minute_of_day >= 0 && minute_of_day < 1440) {
                // Retrieve the sleep state for this minute
                uint8_t state = g_sleep_map[minute_of_day];
                // Assign DukeBlue for deep, PictonBlue for light, Red for awake
                GColor c = (state == 2) ? GColorDukeBlue : ((state == 1) ? GColorPictonBlue : ((state == 3) ? GColorPictonBlue : GColorRed));
                // Map the minute sequence mathematically to the physical perimeter
                int p = (m * max_p) / total_sleep_mins;

                // Start a new color group if none is active
                if (group_start_p == -1) { 
                    // Set the starting pixel for this group
                    group_start_p = p;
                    // Set the active color for this group
                    current_c = c;
                // Draw the group and start a new one if the color changes
                } else if (!gcolor_equal(c, current_c)) { 
                    // Define the end pixel for the drawing operation
                    int end_p = p;
                    // Ensure the segment is at least 1 pixel wide
                    if (end_p <= group_start_p) end_p = group_start_p + 1;
                    // Execute the drawing command for the contiguous segment
                    draw_perimeter_segment(ctx, group_start_p, end_p, 10, current_c);
                    // Update the start pixel for the next group
                    group_start_p = p;
                    // Update the active color for the next group
                    current_c = c;
                } 
                // Update the trailing pixel tracker
                current_p = p;
            } 
        } 
        
        // Draw the final remaining segment after the loop completes
        if (group_start_p != -1) { 
            // Execute the drawing command for the final segment
            draw_perimeter_segment(ctx, group_start_p, current_p + 1, 10, current_c);
            // Set the color context for the ending circle indicator
            graphics_context_set_fill_color(ctx, current_c);
            // Draw the circular cap at the end of the data track
            graphics_fill_circle(ctx, get_perimeter_point(current_p, 10), 6);
        }
    }

    // Set text color to black for header
    graphics_context_set_text_color(ctx, vg_foreground());
    // Draw the "Sleep" section title
    graphics_draw_text(ctx, "SLEEP", fonts_get_system_font(VG_FONT_TITLE), 
                       GRect(0, 5, 200, 35), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // Initialize buffer for the top readout block, expanded to 128 to prevent string truncation
    char top_buf[128];
    // Format the primary metrics into the display string
    snprintf(top_buf, sizeof(top_buf), "Sleep: %d | Ready: %d\n%s - %s\nMorn HR: %d BPM", 
             g_sleep_comp, g_readiness_comp, sleep_start_str, sleep_end_str, g_morning_hr_comp);
    // Draw the formatted top readout block
    if (!g_sleep_data_available) {
        snprintf(top_buf, sizeof(top_buf), "Sleep data unavailable\nCheck Pebble Health sync");
    }
    graphics_draw_text(ctx, top_buf, fonts_get_system_font(VG_FONT_BODY), 
                       GRect(0, 38, 200, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // Calculate total net sleep in seconds
    int net_sleep_sec = g_sleep_deep_sec + g_sleep_light_sec;
    // Calculate total awake seconds by subtracting net sleep from gross duration
    int awake_sec = g_longest_sleep_duration - net_sleep_sec;
    // Boundary limit to prevent illogical negative outputs
    if (awake_sec < 0) awake_sec = 0;

    // Define the vertical starting coordinate for the stats list
    int y_start = 100;
    // Define the vertical spacing between list items
    int y_step = 22;
    // Define the horizontal coordinate for the color indicator squares
    int x_sq = 45;
    // Define the horizontal coordinate for the text labels
    int x_txt = 65;
    // Load the standard font for the list rows
    GFont row_font = fonts_get_system_font(VG_FONT_BODY);
    
    // Set context color to deep blue for deep sleep indicator
    graphics_context_set_fill_color(ctx, GColorDukeBlue);
    // Draw the deep sleep color square
    graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
    // Initialize string buffer for deep sleep duration
    char buf_deep[32];
    // Format the deep sleep duration into hours and minutes
    snprintf(buf_deep, sizeof(buf_deep), "Restful: %dh %dm", g_sleep_deep_sec / 3600, (g_sleep_deep_sec % 3600) / 60);
    // Draw the formatted deep sleep string
    graphics_draw_text(ctx, buf_deep, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    // Increment the vertical position for the next row
    y_start += y_step;
    
    // Set context color to light blue for light sleep indicator
    graphics_context_set_fill_color(ctx, GColorPictonBlue);
    // Draw the light sleep color square
    graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
    // Initialize string buffer for light sleep duration
    char buf_light[32];
    // Format the light sleep duration into hours and minutes
    snprintf(buf_light, sizeof(buf_light), "Light: %dh %dm", g_sleep_light_sec / 3600, (g_sleep_light_sec % 3600) / 60);
    // Draw the formatted light sleep string
    graphics_draw_text(ctx, buf_light, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    // Increment the vertical position for the next row
    y_start += y_step;
    
    // Set context color to red for awake indicator
    graphics_context_set_fill_color(ctx, GColorRed);
    // Draw the awake color square
    graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
    // Initialize string buffer for awake duration
    char buf_awake[32];
    // Format the awake duration into minutes
    snprintf(buf_awake, sizeof(buf_awake), "Awake: %dm", awake_sec / 60);
    // Draw the formatted awake string
    graphics_draw_text(ctx, buf_awake, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}
// ============================================================================
// --- SECTION 4: HISTORY RENDERER ---
// ============================================================================
static void history_update_proc(Layer *layer, GContext *ctx) { 
    graphics_context_set_antialiased(ctx, true);
    graphics_context_set_fill_color(ctx, vg_background());
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

    char *title = "";
    int *data_arr = NULL;

    if (s_current_history_view == 0) { title = "Energy Trend"; data_arr = g_hist_energy; }
    else if (s_current_history_view == 1) { title = "Sleep Trend"; data_arr = g_hist_sleep; }
    else if (s_current_history_view == 2) { title = "Stress Trend"; data_arr = g_hist_stress; }
    else if (s_current_history_view == 3) { title = "Activity Trend"; data_arr = g_hist_activity; }

    graphics_context_set_text_color(ctx, vg_foreground());
    graphics_draw_text(ctx, title, fonts_get_system_font(VG_FONT_TITLE), 
                       GRect(0, 5, 200, 35), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    int sum = 0, valid_days = 0, bar_w = 16, spacing = 6;
    int start_x = (200 - (7 * bar_w + 6 * spacing)) / 2;
    int base_y = 160, max_h = 75;

    for(int i = 0; i < 7; i++) {
        int val = data_arr[i];
        if (val > 0 || i == 6) { sum += val; valid_days++; }

        int h = (val * max_h) / 100;
        if (h < 2) h = 2;

        GColor c = GColorRed;
        if (val > 40) c = GColorChromeYellow;
        if (val > 70) c = GColorJaegerGreen;

        if (s_current_history_view == 2) {
            if (val < 40) c = GColorJaegerGreen;
            else if (val < 70) c = GColorChromeYellow;
            else c = GColorRed;
        }

        int x = start_x + (i * (bar_w + spacing));
        int y = base_y - h;

        graphics_context_set_fill_color(ctx, c);
        graphics_fill_rect(ctx, GRect(x, y, bar_w, h), 2, GCornersAll);

        char day_lbl[4];
        if (i == 6) snprintf(day_lbl, sizeof(day_lbl), "T");
        else snprintf(day_lbl, sizeof(day_lbl), "-%d", 6-i);

        graphics_context_set_text_color(ctx, vg_foreground());
        graphics_draw_text(ctx, day_lbl, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(x - 4, base_y + 2, bar_w + 8, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }

    int avg = valid_days > 0 ? sum / valid_days : 0;
    char avg_buf[32];
    snprintf(avg_buf, sizeof(avg_buf), "7-Day Avg: %d", avg);
    graphics_draw_text(ctx, avg_buf, fonts_get_system_font(VG_FONT_BODY),
                       GRect(0, 42, 200, 30), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    graphics_context_set_text_color(ctx, vg_foreground());
    graphics_draw_text(ctx, "UP/DOWN  Browse     BACK  Return", fonts_get_system_font(VG_FONT_HINT), 
                       GRect(0, 185, 200, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// ============================================================================
// --- SECTION 5: BATTERY/ENERGY RENDERER ---
// ============================================================================
static void battery_update_proc(Layer *layer, GContext *ctx) { 
    graphics_context_set_antialiased(ctx, true);
    graphics_context_set_fill_color(ctx, vg_background());
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

    int p_end = (g_live_energy_score * 856) / 100;
    GColor c = (g_live_energy_score > 60) ? GColorJaegerGreen : (g_live_energy_score > 30 ? GColorChromeYellow : GColorRed);
    
    draw_perimeter_segment(ctx, 0, 856, 10, GColorLightGray);
    draw_perimeter_segment(ctx, 0, p_end, 10, c);
    
    graphics_context_set_fill_color(ctx, c);
    graphics_fill_circle(ctx, get_perimeter_point(p_end, 10), 6);

    graphics_context_set_text_color(ctx, vg_foreground());
    graphics_draw_text(ctx, "ENERGY RESERVE", fonts_get_system_font(VG_FONT_TITLE), 
                       GRect(0, 5, 200, 30), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    char pct_buf[16];
    snprintf(pct_buf, sizeof(pct_buf), "%d%%", g_live_energy_score);
    graphics_draw_text(ctx, pct_buf, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), 
                       GRect(0, 35, 200, 35), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    char buf[160];
    snprintf(buf, sizeof(buf), "+ Sleep Score: %d\n+ Readiness Score: %d\n+ Cardiac Efficiency: %d\n- Exertion Drain: %d\n- Stress Penalty: %d", 
             g_sleep_comp, g_readiness_comp, g_card_eff_comp / 10, g_exertion_comp, g_stress_comp);

    graphics_draw_text(ctx, buf, fonts_get_system_font(VG_FONT_BODY), 
                       GRect(5, 75, 190, 150), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// ============================================================================
// --- SECTION 6: STATS MODULE ROUTING ---
// ============================================================================
static void history_up_handler(ClickRecognizerRef recognizer, void *context) {
    s_current_history_view--;
    if (s_current_history_view < 0) s_current_history_view = 3;
    layer_mark_dirty(s_history_canvas);
}

static void history_down_handler(ClickRecognizerRef recognizer, void *context) {
    s_current_history_view++;
    if (s_current_history_view > 3) s_current_history_view = 0;
    layer_mark_dirty(s_history_canvas);
}

static void history_select_handler(ClickRecognizerRef recognizer, void *context) {
    window_stack_pop(true);
}

static void history_click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_UP, history_up_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, history_down_handler);
    window_single_click_subscribe(BUTTON_ID_BACK, history_select_handler);
}

static void act_load(Window *w) { s_activity_canvas = layer_create(layer_get_bounds(window_get_root_layer(w))); layer_set_update_proc(s_activity_canvas, activity_update_proc); layer_add_child(window_get_root_layer(w), s_activity_canvas); }
static void act_unload(Window *w) { layer_destroy(s_activity_canvas); }

static void slp_load(Window *w) { s_sleep_canvas = layer_create(layer_get_bounds(window_get_root_layer(w))); layer_set_update_proc(s_sleep_canvas, sleep_update_proc); layer_add_child(window_get_root_layer(w), s_sleep_canvas); }
static void slp_unload(Window *w) { layer_destroy(s_sleep_canvas); }

static void his_load(Window *w) { s_history_canvas = layer_create(layer_get_bounds(window_get_root_layer(w))); layer_set_update_proc(s_history_canvas, history_update_proc); layer_add_child(window_get_root_layer(w), s_history_canvas); window_set_click_config_provider(w, history_click_config_provider); }
static void his_unload(Window *w) { layer_destroy(s_history_canvas); }

static void bat_load(Window *w) { s_battery_canvas = layer_create(layer_get_bounds(window_get_root_layer(w))); layer_set_update_proc(s_battery_canvas, battery_update_proc); layer_add_child(window_get_root_layer(w), s_battery_canvas); }
static void bat_unload(Window *w) { layer_destroy(s_battery_canvas); }

void ui_stats_init() {
    s_activity_window = window_create(); window_set_window_handlers(s_activity_window, (WindowHandlers){ .load = act_load, .unload = act_unload }); window_set_background_color(s_activity_window, vg_background());
    s_sleep_window = window_create(); window_set_window_handlers(s_sleep_window, (WindowHandlers){ .load = slp_load, .unload = slp_unload }); window_set_background_color(s_sleep_window, vg_background());
    s_history_window = window_create(); window_set_window_handlers(s_history_window, (WindowHandlers){ .load = his_load, .unload = his_unload }); window_set_background_color(s_history_window, vg_background());
    s_battery_window = window_create(); window_set_window_handlers(s_battery_window, (WindowHandlers){ .load = bat_load, .unload = bat_unload }); window_set_background_color(s_battery_window, vg_background());
}

void ui_stats_deinit() {
    window_destroy(s_activity_window);
    window_destroy(s_sleep_window);
    window_destroy(s_history_window);
    window_destroy(s_battery_window);
}

void ui_activity_push() { window_stack_push(s_activity_window, true); }
void ui_sleep_push() { window_stack_push(s_sleep_window, true); }
void ui_sleep_mark_dirty(void) {
    if (s_sleep_canvas && window_is_loaded(s_sleep_window))
        layer_mark_dirty(s_sleep_canvas);
}
void ui_history_push() { window_stack_push(s_history_window, true); }
void ui_battery_push() { window_stack_push(s_battery_window, true); }

void ui_battery_mark_dirty() {
    if (s_battery_canvas && window_is_loaded(s_battery_window)) layer_mark_dirty(s_battery_canvas);
}