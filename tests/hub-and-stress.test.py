from pathlib import Path
import subprocess
import tempfile

hub = Path("src/c/ui_hub.c").read_text()
stress = Path("src/c/ui_stress.c").read_text()
navigation = hub[hub.index("typedef enum {"):hub.index("static void global_touch_handler")]
score = stress[stress.index("static int hrv_stress_score"):stress.index("static void hrv_save_completed")]
spread = stress[stress.index("static int calculate_bpm_range"):stress.index("static int get_phase_duration")]
with tempfile.TemporaryDirectory() as temp:
    fixture = Path(temp) / "test.c"
    fixture.write_text("""
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
typedef struct { int x, y; } GPoint;
typedef struct { GPoint origin, size; } GRect;
typedef int ClickRecognizerRef;
enum { BUTTON_ID_UP, BUTTON_ID_DOWN };
static int s_hub_drawing_layer;
static int opened = -1;
static void play_haptic_tick(void) {}
static void layer_mark_dirty(int layer) {}
static int click_recognizer_get_button_id(int button) { return button; }
static void recovery_open(void) { opened = 0; }
static void ui_stress_push(void) { opened = 1; }
static void ui_activity_push(void) { opened = 2; }
static void ui_history_push(void) { opened = 3; }
static void ui_sleep_push(void) { opened = 4; }
""" + navigation + score + spread + """
int main(void) {
    hub_select(0, NULL);
    assert(opened == HUB_ENERGY);
    for (int expected = HUB_STRESS; expected <= HUB_SLEEP; ++expected) {
        hub_move_focus(BUTTON_ID_DOWN, NULL);
        assert((int)s_hub_focus == expected);
        hub_select(0, NULL);
        assert(opened == expected);
    }
    hub_move_focus(BUTTON_ID_DOWN, NULL);
    assert(s_hub_focus == HUB_ENERGY);
    hub_move_focus(BUTTON_ID_UP, NULL);
    assert(s_hub_focus == HUB_SLEEP);
    screen_touch_tap_handler((GPoint){10, 10});
    assert(opened == HUB_STRESS && !s_hub_focus_visible);
    screen_touch_tap_handler((GPoint){100, 114});
    assert(opened == HUB_ENERGY);
    assert(calculate_bpm_range((int[]){0, 0, 0, 70}, 4) < 0);
    assert(calculate_bpm_range((int[]){70, 70, 70, 70}, 4) < 0);
    assert(calculate_bpm_range((int[]){0, 74, 70, 80}, 4) == 10);
    assert(hrv_stress_score(40, 40) == 50);
    assert(hrv_stress_score(50, 40) < hrv_stress_score(30, 40));
    return 0;
}
""")
    binary = Path(temp) / "test"
    subprocess.run(["cc", "-std=c99", str(fixture), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("VitalGauge buttons, touch navigation, flat samples and HRV regressions passed")
