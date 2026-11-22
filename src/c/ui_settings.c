#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: SETTINGS UI STATE ---
// ============================================================================
int g_breath_style = 0; 
int g_haptic_level = 2; 
bool g_backlight_stay_on = false;
bool g_background_enabled;
bool g_alerts_enabled;

static Window *s_settings_window;
static SimpleMenuLayer *s_simple_menu_layer;
static SimpleMenuSection s_menu_sections[1];
static SimpleMenuItem s_menu_items[6];

// ============================================================================
// --- SECTION 2: MENU CALLBACKS ---
// ============================================================================
void ui_settings_refresh(void) {
    if (!s_simple_menu_layer) return;
    static const char *const theme_names[] = {"Day", "Night", "Auto"};
    s_menu_items[0].subtitle = theme_names[g_theme_mode];
    MenuLayer *menu = simple_menu_layer_get_menu_layer(s_simple_menu_layer);
    menu_layer_set_normal_colors(menu, vg_background(), vg_foreground());
    menu_layer_set_highlight_colors(menu, vg_foreground(), vg_background());
    window_set_background_color(s_settings_window, vg_background());
    s_menu_items[1].subtitle = (g_breath_style == 0) ? "4-4-4-4 (Box)" : ((g_breath_style == 1) ? "4-7-8 (Relax)" : "4-8 (Sleep)");
    s_menu_items[2].subtitle = (g_haptic_level == 0) ? "Off" : ((g_haptic_level == 1) ? "Low" : "High");
    s_menu_items[3].subtitle = g_backlight_stay_on ? "Stay On" : "Auto";
    s_menu_items[4].subtitle = app_worker_is_running() ? "Active" :
                                    g_background_enabled ? "Requested / inactive" : "Off";
    s_menu_items[5].subtitle = g_alerts_enabled ? "On (experimental)" : "Off";
    layer_mark_dirty(simple_menu_layer_get_layer(s_simple_menu_layer));
}

static Window *s_backlight_window;
static SimpleMenuLayer *s_backlight_menu;
static SimpleMenuItem s_backlight_items[2];
static SimpleMenuSection s_backlight_section;

static void backlight_select(int index, void *context) {
    g_backlight_stay_on = index == 1;
    persist_write_bool(PERSIST_KEY_BACKLIGHT, g_backlight_stay_on);
    config_sync_send_settings();
    ui_settings_refresh();
    window_stack_pop(true);
}

static void backlight_load(Window *window) {
    s_backlight_items[0] = (SimpleMenuItem){
        .title = "Auto", .subtitle = "Use system backlight", .callback = backlight_select
    };
    s_backlight_items[1] = (SimpleMenuItem){
        .title = "Stay On", .subtitle = "During scan or breathing", .callback = backlight_select
    };
    s_backlight_section = (SimpleMenuSection){.items = s_backlight_items, .num_items = 2};
    Layer *root = window_get_root_layer(window);
    s_backlight_menu = simple_menu_layer_create(layer_get_bounds(root), window,
                                                &s_backlight_section, 1, NULL);
    MenuLayer *menu = simple_menu_layer_get_menu_layer(s_backlight_menu);
    menu_layer_set_normal_colors(menu, vg_background(), vg_foreground());
    menu_layer_set_highlight_colors(menu, vg_foreground(), vg_background());
    window_set_background_color(window, vg_background());
    layer_add_child(root, simple_menu_layer_get_layer(s_backlight_menu));
    simple_menu_layer_set_selected_index(s_backlight_menu, g_backlight_stay_on ? 1 : 0, false);
}

static void backlight_unload(Window *window) {
    simple_menu_layer_destroy(s_backlight_menu);
    s_backlight_menu = NULL;
}

static void menu_select_callback(int index, void *context) {
    if (g_haptic_level > 0) vibes_short_pulse();
    switch (index) {
        case 0:
            theme_set_mode((g_theme_mode + 1) % 3);
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
            window_stack_push(s_backlight_window, true);
            return;
        case 4:
            g_background_enabled = !g_background_enabled;
            persist_write_bool(VG_BG_KEY, g_background_enabled);
            vitalgauge_apply_worker_setting();
            break;
        case 5:
            g_alerts_enabled = !g_alerts_enabled;
            persist_write_bool(VG_ALERT_OPT_IN_KEY, g_alerts_enabled);
            break;
    }
    ui_settings_refresh();
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
        .title = "Theme",
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

    s_menu_items[num_a_items++] = (SimpleMenuItem){
        .title = "Background monitoring", .callback = menu_select_callback
    };
    s_menu_items[num_a_items++] = (SimpleMenuItem){
        .title = "Automatic stress prompts", .callback = menu_select_callback
    };

    s_menu_sections[0] = (SimpleMenuSection) {
        .num_items = num_a_items,
        .items = s_menu_items,
    };

    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_frame(window_layer);

    s_simple_menu_layer = simple_menu_layer_create(bounds, window, s_menu_sections, 1, NULL);
    layer_add_child(window_layer, simple_menu_layer_get_layer(s_simple_menu_layer));
    
    ui_settings_refresh();
}

static void settings_window_unload(Window *window) {
    simple_menu_layer_destroy(s_simple_menu_layer);
    s_simple_menu_layer = NULL;
}

void ui_settings_init(void) {
    s_backlight_window = window_create();
    window_set_window_handlers(s_backlight_window, (WindowHandlers){
        .load = backlight_load, .unload = backlight_unload
    });
    s_settings_window = window_create();
    window_set_window_handlers(s_settings_window, (WindowHandlers) {
        .load = settings_window_load,
        .unload = settings_window_unload,
    });
}

void ui_settings_deinit(void) {
    window_destroy(s_backlight_window);
    window_destroy(s_settings_window);
}

void ui_settings_push(void) {
    window_stack_push(s_settings_window, true);
}