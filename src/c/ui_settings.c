#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: SETTINGS UI STATE ---
// ============================================================================
bool g_dark_mode = false;
int g_breath_style = 0; 
int g_haptic_level = 2; 
bool g_backlight_stay_on = false;

static Window *s_settings_window;
static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_menu_items[4];

// ============================================================================
// --- SECTION 2: MENU CALLBACKS ---
// ============================================================================
static void update_menu_subtitles() {
    s_menu_items[0].subtitle = g_dark_mode ? "On" : "Off";
    s_menu_items[1].subtitle = (g_breath_style == 0) ? "4-4-4-4 (Box)" : ((g_breath_style == 1) ? "4-7-8 (Relax)" : "4-8 (Sleep)");
    s_menu_items[2].subtitle = (g_haptic_level == 0) ? "Off" : ((g_haptic_level == 1) ? "Low" : "High");
    s_menu_items[3].subtitle = g_backlight_stay_on ? "Stay On" : "Auto";
    layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
}

static void menu_select_callback(int index, void *context) {
    if (g_haptic_level > 0) vibes_short_pulse();
    switch (index) {
        case 0:
            g_dark_mode = !g_dark_mode;
            int result = persist_write_int(PERSIST_KEY_DARK_MODE, g_dark_mode ? 1 : 0);
            APP_LOG(APP_LOG_LEVEL_INFO, "Dark mode watch change: %d, persist result: %d", g_dark_mode, result);
            break;
        case 1:
            g_breath_style = (g_breath_style + 1) % 3;
            persist_write_int(PERSIST_KEY_BREATH_STYLE, g_breath_style);
            break;
        case 2:
            g_haptic_level = (g_haptic_level + 1) % 3;
            persist_write_int(PERSIST_KEY_HAPTIC_LEVEL, g_haptic_level);
            break;
        case 3:
            g_backlight_stay_on = !g_backlight_stay_on;
            persist_write_bool(PERSIST_KEY_BACKLIGHT, g_backlight_stay_on);
            break;
    }
    update_menu_subtitles();
    config_sync_send_settings();
    // Force a redraw of the main hub so changes (like Dark Mode) apply instantly upon exiting
    ui_hub_mark_dirty();
}

// ============================================================================
// --- SECTION 3: LIFECYCLE ---
// ============================================================================
static void settings_window_load(Window *window) {
    int num_a_items = 0;

    s_menu_items[num_a_items++] = (SimpleMenuItem) {
        .title = "Dark Mode",
        .callback = menu_select_callback,
    };
    s_menu_items[num_a_items++] = (SimpleMenuItem) {
        .title = "Breathing Style",
        .callback = menu_select_callback,
    };
    s_menu_items[num_a_items++] = (SimpleMenuItem) {
        .title = "Haptics",
        .callback = menu_select_callback,
    };
    s_menu_items[num_a_items++] = (SimpleMenuItem) {
        .title = "Scan Backlight",
        .callback = menu_select_callback,
    };

    s_menu_sections[0] = (SimpleMenuSection) {
        .num_items = num_a_items,
        .items = s_menu_items,
    };

    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_frame(window_layer);

    s_simple_menu_layer = simple_menu_layer_create(bounds, window, s_menu_sections, 1, NULL);
    layer_add_child(window_layer, simple_menu_layer_get_layer(s_simple_menu_layer));
    
    update_menu_subtitles();
}

static void settings_window_unload(Window *window) {
    simple_menu_layer_destroy(s_simple_menu_layer);
}

void ui_settings_init(void) {
    s_settings_window = window_create();
    window_set_window_handlers(s_settings_window, (WindowHandlers) {
        .load = settings_window_load,
        .unload = settings_window_unload,
    });
}

void ui_settings_deinit(void) {
    window_destroy(s_settings_window);
}

void ui_settings_push(void) {
    window_stack_push(s_settings_window, true);
}