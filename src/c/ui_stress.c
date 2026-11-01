#include "vitalgauge.h"

// ============================================================================
// --- SECTION 1: STRESS MODULE CONFIGURATION ---
// ============================================================================
// Define the number of samples for a 60-second EKG scan
#define SAMPLE_COUNT 60

// Declare pointer to the stress window object
static Window *s_stress_window;
// Declare pointer to the stress canvas layer
static Layer *s_stress_canvas;
// Declare pointer to the EKG rendering layer
static Layer *s_ekg_layer;

// Declare pointer to the unified application timer
static AppTimer *s_unified_timer = NULL;
// Declare pointer to the EKG animation timer
static AppTimer *s_ekg_timer = NULL;     
// Declare boolean flag tracking active scan state
static bool s_is_scanning = false;
// Declare integer tracking remaining scan ticks
static int s_scan_ticks = 60;            
// Declare array storing heart rate samples during scan
static int s_bpm_samples[SAMPLE_COUNT];
// Declare integer tracking visual offset of EKG wave
static int s_ekg_offset = 0;

// Declare boolean flag tracking active breathing session
static bool s_is_breathing = false;
// Declare boolean flag indicating display of breathing summary
static bool s_show_breath_summary = false;
// Declare integer tracking current phase of box breathing
static int s_breath_phase = 0;
// Declare integer tracking remaining time in breathing session
static int s_session_time_left = 300;
// Declare integer storing the calculated stress score
static int s_current_stress = -1;
// Declare integer storing heart rate at start of breathing session
static int s_breath_start_hr = 0;
// Declare integer storing heart rate at end of breathing session
static int s_breath_end_hr = 0;

// Declare pointer to the breathing radius animation object
static Animation *s_radius_animation = NULL;
// Declare integer tracking the current radius of the breathing circle
static int s_radius = 20;                    
// Declare integer defining the starting radius for the animation
static int s_radius_from = 0;
// Declare integer defining the ending radius for the animation
static int s_radius_to = 0;
static int s_phase_time_left = 0;

// Define a structure to map EKG wave percentage to vertical offset
struct EKGPoint { int pct; int y; };

// Instantiate the EKG point map as a static constant array in Flash memory
static const struct EKGPoint ekg_pts[] = { 
    {0,0}, {15,0}, {20,-4}, {25,0}, {40,0}, {43, 8}, {45,-35}, {48,15}, {50,0}, {65,0}, {75,-8}, {85,0}, {100,0} 

};

#define HRV_SAMPLES 60
static uint32_t hrv_sqrt(uint32_t n);
static void start_stress_scan(bool automatic);
static uint32_t s_hrv_delta_squares;
static uint16_t s_hrv_delta_count;
static uint16_t s_hrv_accepted_total;
static uint16_t s_hrv_previous;
static uint16_t s_hrv_intervals[HRV_SAMPLES];
static uint8_t s_hrv_count;
static int s_hrv_rmssd;
static bool s_hrv_active;
// HRV history: compact fixed-size circular log (~192 bytes).
// Baseline uses only good manual scans, not elevated-HR auto-trigger scans.
#define HRV_HISTORY_KEY 410
#define HRV_HISTORY_CAP 16
#define HRV_QUALITY_MIN 40
#define HRV_BASELINE_MIN 7
typedef struct {
    uint32_t timestamp;
    uint16_t rmssd;
    uint16_t bpm;
    uint8_t accepted;
    uint8_t rejected;
    uint8_t quality; // 0 poor, 1 fair, 2 good
    uint8_t automatic;
} HRVRecord;
static HRVRecord s_hrv_history[HRV_HISTORY_CAP];
static uint8_t s_hrv_count_history;
static uint8_t s_scan_context; // 0 manual, 1 elevated pulse, 2 morning recovery
#define HRV_MORNING_DAY_KEY 411
static uint8_t s_hrv_rejected;
static uint8_t s_hrv_received;
static time_t s_hrv_last_event;

static int hrv_baseline_ms(int *eligible_count) {
    uint16_t good[HRV_HISTORY_CAP];
    int count = 0;
    time_t limit = time(NULL) - 30 * 86400;
    for (int i = 0; i < s_hrv_count_history; ++i) {
        HRVRecord *r = &s_hrv_history[i];
        if (r->quality == 2 && r->automatic == 2 && r->timestamp >= (uint32_t)limit) {
            bool already_recorded_day = false;
            for (int j = i + 1; j < s_hrv_count_history; ++j) {
                HRVRecord *newer = &s_hrv_history[j];
                if (newer->quality == 2 && newer->automatic == 2 &&
                    newer->timestamp / 86400 == r->timestamp / 86400) {
                    already_recorded_day = true;
                    break;
                }
            }
            if (!already_recorded_day) good[count++] = r->rmssd;
        }
    }
    *eligible_count = count;
    if (count < HRV_BASELINE_MIN) return 0;
    for (int i = 1; i < count; ++i) {
        uint16_t v = good[i]; int j = i;
        while (j > 0 && good[j - 1] > v) { good[j] = good[j - 1]; --j; }
        good[j] = v;
    }
    return good[count / 2];
}
static void hrv_save_completed(int bpm) {
    if (!s_hrv_active || s_hrv_delta_count < 20 || s_hrv_rmssd <= 0) return;
    int rejected_percent = s_hrv_received ?
        ((int)s_hrv_rejected * 100 / s_hrv_received) : 100;
    // Quality is conservative until consecutive PPI continuity can be verified.
    uint8_t quality = (s_hrv_delta_count >= HRV_QUALITY_MIN &&
                       rejected_percent <= 10) ? 2 :
                      (s_hrv_delta_count >= 20 && rejected_percent <= 25) ? 1 : 0;
    if (s_hrv_count_history >= HRV_HISTORY_CAP) {
        memmove(s_hrv_history, s_hrv_history + 1,
                (HRV_HISTORY_CAP - 1) * sizeof(HRVRecord));
        s_hrv_count_history = HRV_HISTORY_CAP - 1;
    }
    HRVRecord *record = &s_hrv_history[s_hrv_count_history++];
    *record = (HRVRecord){.timestamp=(uint32_t)time(NULL),
        .rmssd=(uint16_t)s_hrv_rmssd, .bpm=(uint16_t)(bpm > 0 ? bpm : 0),
        .accepted=(uint8_t)(s_hrv_accepted_total > 255 ? 255 : s_hrv_accepted_total), .rejected=s_hrv_rejected,
        .quality=quality, .automatic=s_scan_context};
    persist_write_data(HRV_HISTORY_KEY, s_hrv_history,
                       s_hrv_count_history * sizeof(HRVRecord));
    APP_LOG(APP_LOG_LEVEL_INFO,
            "HRV completed RMSSD=%d quality=%d accepted=%d rejected=%d auto=%d",
            s_hrv_rmssd, quality, s_hrv_accepted_total, s_hrv_rejected, s_scan_context);
    // Legacy BPM-range scoring remains until baseline is calibrated.
    int n = 0, baseline = hrv_baseline_ms(&n);
    if (baseline > 0 && quality == 2) {
        int score = 50 + ((s_hrv_rmssd - baseline) * 100) / baseline;
        if (score < 10) score = 10;
        if (score > 100) score = 100;
        persist_write_int(PERSIST_KEY_SCORE, score);
    }
}



static uint32_t hrv_sqrt(uint32_t n) {
    uint32_t r = 0, bit = (uint32_t)1 << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static void hrv_begin(void) {
    memset(s_hrv_intervals, 0, sizeof(s_hrv_intervals));
    s_hrv_count = 0;
    s_hrv_rmssd = 0;
    s_hrv_rejected = 0;
    s_hrv_received = 0;
    s_hrv_last_event = 0;
    s_hrv_delta_squares = 0;
    s_hrv_delta_count = 0;
    s_hrv_accepted_total = 0;
    s_hrv_previous = 0;
    s_hrv_active = health_service_set_hrv_sample_period(1);
}

static void hrv_end(void) {
    if (s_hrv_active) health_service_set_hrv_sample_period(0);
    s_hrv_active = false;
}

void ui_stress_hrv_update(void) {
    if (!s_hrv_active || !s_is_scanning) return;
    uint16_t ppi = health_service_peek_hrv_ppi_ms();
    if (s_hrv_received < 255) ++s_hrv_received;
    time_t now = time(NULL);
    if (ppi < 400 || ppi > 1500) {
        if (s_hrv_rejected < 255) ++s_hrv_rejected;
        s_hrv_previous = 0;
        return;
    }
    // Only differences between adjacent, uninterrupted valid beats count.
    if (s_hrv_last_event && now - s_hrv_last_event > 3)
        s_hrv_previous = 0;
    s_hrv_last_event = now;
    if (s_hrv_previous) {
        int delta = (int)ppi - (int)s_hrv_previous;
        if (delta > 250 || delta < -250) {
            if (s_hrv_rejected < 255) ++s_hrv_rejected;
            s_hrv_previous = 0;
            return;
        }
        s_hrv_delta_squares += (uint32_t)(delta * delta);
        ++s_hrv_delta_count;
    }
    s_hrv_previous = ppi;
    ++s_hrv_accepted_total;
    // Use all consecutive accepted pairs, not only the last 60 samples.
    if (s_hrv_delta_count >= 20)
        s_hrv_rmssd = (int)hrv_sqrt(s_hrv_delta_squares / s_hrv_delta_count);
    ui_stress_mark_dirty();
}

int ui_stress_hrv_baseline(int *days) {
    return hrv_baseline_ms(days);
}
int ui_stress_latest_hrv(void) {
    return s_hrv_count_history ?
        s_hrv_history[s_hrv_count_history - 1].rmssd : 0;
}
void ui_stress_start_morning(void) {
    ui_stress_push();
    start_stress_scan(true);
    s_scan_context = 2;
}

// ============================================================================
// --- SECTION 2: MATH HELPERS ---
// ============================================================================
// Function to interpolate vertical Y coordinates for the EKG line
static int get_ekg_y(int pct) { 
    // Iterate through the predefined EKG mapping points
    for (int i=0; i<12; i++) { 
        // Identify the correct segment for the current percentage
        if (pct >= ekg_pts[i].pct && pct <= ekg_pts[i+1].pct) { 
            // Calculate the delta percentage for interpolation
            int dpct = ekg_pts[i+1].pct - ekg_pts[i].pct;
            // Return base Y if there is no delta
            if (dpct == 0) return ekg_pts[i].y;
            // Return mathematically interpolated Y coordinate
            return ekg_pts[i].y + ((ekg_pts[i+1].y - ekg_pts[i].y) * (pct - ekg_pts[i].pct)) / dpct;
        } 
    } 
    // Default return baseline
    return 0;
}

// Function to calculate the spread between highest and lowest heart rates
static int calculate_bpm_range(int *samples, int count) {
    // Initialize minimum variable high
    int min = 999;
    // Initialize maximum variable low
    int max = 0;
    
    // Iterate through sample array
    for(int i = 0; i < count; i++) {
        // Validate sample is a real reading
        if(samples[i] > 0) {
            // Update minimum if lower value found
            if(samples[i] < min) min = samples[i];
            // Update maximum if higher value found
            if(samples[i] > max) max = samples[i];
        }
    }
    
    // Return zero range if no valid samples were processed
    if(min == 999) return 0;
    // Return absolute difference
    return max - min;
}

static int get_phase_duration(int style, int phase) {
    // phase: 0=Inhale, 1=Hold1, 2=Exhale, 3=Hold2
    if (style == 1) { // 4-7-8
        int durations[] = {4, 7, 8, 0};
        return durations[phase];
    } else if (style == 2) { // 4-8
        int durations[] = {4, 0, 8, 0};
        return durations[phase];
    }
    // Default 4-4-4-4 (Box)
    int durations[] = {4, 4, 4, 4};
    return durations[phase];
}


// ============================================================================
// --- SECTION 3: ANIMATION HANDLERS ---
// ============================================================================
// Callback function updating the breathing circle radius dynamically
static void radius_anim_update(Animation *anim, const AnimationProgress progress) { 
  // Calculate new radius based on animation progress scale
  s_radius = s_radius_from + ((s_radius_to - s_radius_from) * (int)progress) / ANIMATION_NORMALIZED_MAX;
  // Request OS to redraw the stress canvas with the new radius
  if (s_stress_canvas) layer_mark_dirty(s_stress_canvas);
} 

// Define animation implementation structure linking the update callback
static const AnimationImplementation s_radius_anim_impl = { .update = radius_anim_update };

// Callback function executing when a radius animation completes
static void anim_stopped_handler(Animation *animation, bool finished, void *context) { 
    // Nullify pointer to release animation lock
    s_radius_animation = NULL;
} 

// Function to initialize and trigger a new breathing animation sequence
static void start_radius_animation(int from, int to, int duration) { 
    // Terminate any currently running animation on this element
    if (s_radius_animation) animation_unschedule(s_radius_animation);
    
    // Set global starting boundary
    s_radius_from = from;
    // Set global ending boundary
    s_radius_to = to;
    
    // Create new animation object in memory
    s_radius_animation = animation_create();
    // Assign total duration in milliseconds
    animation_set_duration(s_radius_animation, duration); 
    // Apply smoothing curve to animation speed
    animation_set_curve(s_radius_animation, AnimationCurveEaseInOut);
    // Bind implementation logic
    animation_set_implementation(s_radius_animation, &s_radius_anim_impl);
    // Bind teardown handler
    animation_set_handlers(s_radius_animation, (AnimationHandlers) { .stopped = anim_stopped_handler }, NULL);
    
    // Command OS to begin animation processing
    animation_schedule(s_radius_animation);
}

// ============================================================================
// --- SECTION 4: TIMERS ---
// ============================================================================
// Timer callback managing the visual EKG scrolling effect
static void ekg_update_timer(void* data) { 
    // Verify scanning state is active
    if (s_is_scanning) { 
        // Shift EKG offset leftwards by 6 pixels
        s_ekg_offset += 6;
        // Request OS redraw if layer exists
        if (s_ekg_layer != NULL) {
            layer_mark_dirty(s_ekg_layer);
        }
        // Reschedule timer for 20 FPS refresh rate
        s_ekg_timer = app_timer_register(50, ekg_update_timer, NULL);
    } else {
        // Nullify timer pointer if scan has ended
        s_ekg_timer = NULL;
    }
} 

// Timer callback linking audio playback to specific breathing phases
static void deferred_audio_callback(void* data) {
  // Cast context data pointer back to integer phase tracking
  int phase = (int)(intptr_t)data;
  // Trigger appropriate audio resource based on current phase
  switch(phase) {
    case 0: audio_engine_play(RESOURCE_ID_inhale); break;
    case 1: audio_engine_play(RESOURCE_ID_hold); break;
    case 2: audio_engine_play(RESOURCE_ID_exhale); break;
    case 3: audio_engine_play(RESOURCE_ID_hold); break;
  }
}

static void unified_timer_callback(void *data) { 
    // Initialize restart flag to false
    bool restart = false;

    // Peek the current Heart Rate from the health service
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
    
    // Check if the heart rate is valid (greater than 0)
    if (bpm > 0) { 
        // Shift all existing samples in the array one position to the left
        for(int i = 0; i < SAMPLE_COUNT - 1; i++) {
            // Assign the next sample to the current index
            s_bpm_samples[i] = s_bpm_samples[i+1];
        }
        // Insert the newest valid BPM reading at the end of the array
        s_bpm_samples[SAMPLE_COUNT - 1] = bpm;
    }

    // Check if the app is currently in scanning mode
    if (s_is_scanning) { 
        // Decrement scan ticks and check if it has reached zero
        if (--s_scan_ticks <= 0) { 
            // Stop the scan flag
            s_is_scanning = false;
            hrv_save_completed((int)bpm);
            hrv_end(); 
            // Hide the EKG graphical layer
            layer_set_hidden(s_ekg_layer, true); 
            // Turn off the active heart rate sampling
            health_service_set_heart_rate_sample_period(0); 
            // Turn off the backlight if the override was active
            if (g_backlight_stay_on) light_enable(false);
            // Calculate the variance range using the LAST 60 collected samples
            int hr_range = calculate_bpm_range(&s_bpm_samples[SAMPLE_COUNT - 60], 60);
            // Calculate the stress score based on the variance
            int score = 100 - (hr_range * 8);
            // Constrain the score between 10 and 100, then write to persistent storage
            int eligible = 0;
            int baseline = hrv_baseline_ms(&eligible);
            if (!(baseline > 0 && s_hrv_delta_count >= HRV_QUALITY_MIN &&
                  s_hrv_received > 0 && (int)s_hrv_rejected * 10 <= s_hrv_received)) {
                persist_write_int(PERSIST_KEY_SCORE, (score < 10) ? 10 : (score > 100 ? 100 : score));
            }
            
            // Trigger a double pulse haptic alert if haptics are enabled
            if (g_haptic_level > 0 && s_scan_context == 0) vibes_double_pulse(); 
            // Mark the main hub UI for a redraw to update the stress ring
            ui_hub_mark_dirty(); 
        }
        // Redraw the stress canvas to update the scan progress visual
        if (s_stress_canvas) layer_mark_dirty(s_stress_canvas); 
        // Set the restart flag to continue the timer loop
        restart = true;
        
    // Check if the app is currently in breathing mode
    } else if (s_is_breathing) { 
        // Check if 5 seconds have elapsed to start calculating live stress
        if (300 - s_session_time_left >= 5) { 
            // Calculate the live stress penalty based on variance
            int stress = 100 - (calculate_bpm_range(s_bpm_samples, SAMPLE_COUNT) * 8);
            // Constrain the live stress value between 10 and 100
            s_current_stress = (stress < 10) ? 10 : (stress > 100 ? 100 : stress);
        } 
        
        // Check if there is still time left in the overall breathing session
        if (s_session_time_left > 0) { 
            // Decrement the current breathing phase time
            s_phase_time_left--; 
            
            // Check if the current phase time has run out
            if (s_phase_time_left <= 0) {
                // Loop to find the next valid breathing phase
                do {
                    // Increment the phase index, wrapping around at 4
                    s_breath_phase = (s_breath_phase + 1) % 4;
                // Continue looping if the newly selected phase has a 0 duration
                } while (get_phase_duration(g_breath_style, s_breath_phase) == 0);
                
                // Assign the time left based on the new valid phase
                s_phase_time_left = get_phase_duration(g_breath_style, s_breath_phase);
                
                // Start expansion animation if it is the inhale phase (0)
                if (s_breath_phase == 0) start_radius_animation(20, 80, s_phase_time_left * 1000);
                // Start contraction animation if it is the exhale phase (2)
                else if (s_breath_phase == 2) start_radius_animation(80, 20, s_phase_time_left * 1000);
                
                // Register a delayed timer to trigger the corresponding audio cue
                app_timer_register(10, deferred_audio_callback, (void*)(intptr_t)s_breath_phase);
            }
            // Decrement the total session time remaining
            s_session_time_left--;
            // Set the restart flag to continue the timer loop
            restart = true;
            
        // Handle the completion of the 5-minute breathing session
        } else { 
            // Stop the breathing flag
            s_is_breathing = false; 
            // Stop any actively playing audio
            audio_engine_stop(); 
            // Enable the summary screen flag
            s_show_breath_summary = true; 
            // Turn off active heart rate sampling
            health_service_set_heart_rate_sample_period(0); 
            // Turn off the backlight if the override was active
            if (g_backlight_stay_on) light_enable(false);
            // Record the ending heart rate value
            s_breath_end_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
            // Trigger a double pulse haptic alert to signal completion
            if (g_haptic_level > 0) vibes_double_pulse(); 
            // Mark the main hub UI for a redraw
            ui_hub_mark_dirty(); 
        }
        // Redraw the stress canvas to update the breathing circle
        if (s_stress_canvas) layer_mark_dirty(s_stress_canvas);
    }
    // Restart the timer for 1 second if the flag was set, otherwise set to NULL
    s_unified_timer = restart ? app_timer_register(1000, unified_timer_callback, NULL) : NULL;
}
// ============================================================================
// --- SECTION 5: RENDERERS ---
// ============================================================================
// Procedure rendering the live EKG wave layer
static void ekg_update_proc(Layer *layer, GContext *ctx) {
    // Query current BPM for rendering speed
    int current_bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
    // Set default baseline if sensor fails to return value
    if (current_bpm == 0) current_bpm = 70;
    
    // Calculate physical pixel width of one complete heartbeat pulse
    int pulse_width = (60 * 80) / current_bpm;
    // Enforce minimum width to prevent rendering collapse
    if (pulse_width < 25) pulse_width = 25;

    // Configure drawing context for red line
    graphics_context_set_stroke_color(ctx, GColorRed);
    graphics_context_set_stroke_width(ctx, 3);
    
    // Initialize tracking variable for line drawing segments
    GPoint last_pt;
    
    // Iterate across the full 160-pixel width of the display window
    for (int x = 0; x <= 160; x++) { 
        // Calculate modular position within the current heartbeat cycle
        int cycle_x = (x + s_ekg_offset) % pulse_width;
        // Convert physical position to mathematical percentage of cycle
        int pct = (cycle_x * 100) / pulse_width;
        // Retrieve vertical offset based on calculated percentage
        int y = 40 + get_ekg_y(pct);
        
        // Define exact coordinate point
        GPoint pt = GPoint(x, y);
        // Draw segment connecting to previous point if not the starting edge
        if (x > 0) graphics_draw_line(ctx, last_pt, pt);
        // Update tracking variable for next loop
        last_pt = pt;
    } 
}

// Procedure rendering the primary stress UI canvas
static void stress_update_proc(Layer *layer, GContext *ctx) { 
    // Enable antialiasing for smoother rendering
    graphics_context_set_antialiased(ctx, true);
    // Set background color to white
    graphics_context_set_fill_color(ctx, g_dark_mode ? GColorBlack : GColorWhite);
    // Paint physical background canvas
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone); 

    // Initialize string buffer for rendering text readouts
    char buf[128];

    // Branch logic based on current application state
    if (s_is_breathing) { 
        // Default breathing circle color to blue
        GColor breath_color = GColorPictonBlue;
        
        // Apply color mapping based on live stress score if calculated
        if (s_current_stress != -1) { 
            if (s_current_stress > 75) breath_color = GColorRed;
            else if (s_current_stress >= 40) breath_color = GColorChromeYellow;
            else breath_color = GColorJaegerGreen;
        } 

        // Render external solid perimeter ring matching state color
        draw_perimeter_segment(ctx, 0, 856, 10, breath_color);
        
       
        // Configure context and render solid inner animating circle
        graphics_context_set_fill_color(ctx, breath_color);
        graphics_fill_circle(ctx, GPoint(100, 114), s_radius);

        // Determine current instructional phase string based on modulo logic
        char* phase_str = (s_breath_phase % 2 == 0) ? ((s_breath_phase==0)?"INHALE":"EXHALE") : "HOLD"; 
        
        // Configure context and render instructional text header
        graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
        graphics_draw_text(ctx, phase_str, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), 
                           GRect(10, 40, 180, 40), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Calculate phase-specific countdown timer (1-4 seconds)
        int secs = s_phase_time_left;
        // Format integer into string buffer
        char sec_buf[8];
        snprintf(sec_buf, sizeof(sec_buf), "%d", secs);
        
        // Render large countdown number in center of screen
       graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
        graphics_draw_text(ctx, sec_buf, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS), 
                           GRect(10, 95, 180, 40), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Render side menu indicating button functionality for volume control
        graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
        graphics_draw_text(ctx, "+", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(170, 5, 20, 30), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        char vol_buf[16];
        snprintf(vol_buf, sizeof(vol_buf), "V\n%d", g_volume_level);
        graphics_draw_text(ctx, vol_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(170, 90, 30, 50), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        graphics_draw_text(ctx, "-", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(170, 190, 20, 30), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // Render post-session summary if flagged
    } else if (s_show_breath_summary) { 
        // Calculate net reduction in heart rate over session
        int drop = s_breath_start_hr - s_breath_end_hr;
        // Restrict negative outputs mathematically
        if (drop < 0) drop = 0;
        
        // Format summary readout into buffer
        snprintf(buf, sizeof(buf), "Session Complete\n\nStart HR: %d\nEnd HR: %d\n\nDropped: %d BPM\n\n[Press Select to Exit]", 
                 s_breath_start_hr, s_breath_end_hr, drop);
                 
        // Configure context and render formatted summary
        graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
        graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                           GRect(10, 30, 180, 180), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // Render diagnostic EKG view if flagged
    } else if (s_is_scanning) { 
        // Query live heart rate from sensor
        int current_bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
        // Set default baseline if sensor fails to return value
        if (current_bpm == 0) current_bpm = 70;

        // Format live BPM readout into buffer
        snprintf(buf, sizeof(buf), "%d BPM", current_bpm);
        // Configure context and render live BPM
        graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
        graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), 
                           GRect(10, 30, 180, 40), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Format countdown string into buffer
        snprintf(buf, sizeof(buf), "Scanning...\n%d sec", s_scan_ticks);
        // Configure context and render countdown
        graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                           GRect(10, 150, 180, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        if (s_hrv_delta_count >= 20) {
            snprintf(buf, sizeof(buf), "RMSSD: %d ms (%d beats)", s_hrv_rmssd, (int)s_hrv_accepted_total);
            graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                               GRect(10, 192, 180, 28), GTextOverflowModeWordWrap,
                               GTextAlignmentCenter, NULL);
        }


    // Render default informational view if idle
    } else { 
        // Establish boundary for end of current day
        time_t midnight = time_start_of_today() + (24 * 3600);
        // Establish boundary for start of waking tracking
        time_t wake_time = (g_sleep_end != 0) ? g_sleep_end : time_start_of_today() + (6 * 3600);
        
        // Calculate array index matching wake time
        int wake_idx = (wake_time - g_24h_start) / 600;
        // Protect against negative array indexing
        if (wake_idx < 0) wake_idx = 0;
        
        // Calculate total minutes to track for the current day
        int total_day_mins = (midnight - wake_time) / 60;
        // Enforce boundary fallback mathematically
        if (total_day_mins <= 0) total_day_mins = 1440;

        // Render default baseline grey track perimeter
        draw_perimeter_segment(ctx, 0, 856, 10, GColorLightGray);

        // Initialize tracking variables for continuous segment drawing
        int group_start_p = -1;
        int current_thickness = 0;
        GColor current_c = GColorClear;
        int current_p = 0;
        
        // Initialize tracking counters for metric aggregation
        int c_normal = 0, c_elevated = 0, c_high = 0, c_peak = 0;

        // Iterate through all 10-minute buckets from wake time onwards
        for (int b = wake_idx; b < 144; b++) { 
            // Query peak heart rate recorded in that bucket
            int last_known_bpm = g_stress_map[b];
            
            // Execute segment rendering if valid reading exists
            if (last_known_bpm > 0) { 
                // Calculate physical line thickness mapping to heart rate intensity
                int thickness = (last_known_bpm - 40) / 4;
                // Enforce physical rendering boundaries
                if (thickness < 4) thickness = 4;
                if (thickness > 25) thickness = 25;

                // Determine segment color and increment appropriate aggregate counter
                GColor c;
                if (last_known_bpm > 120) { c = GColorRed; c_peak+=10; } 
                else if (last_known_bpm > 100) { c = GColorOrange; c_high+=10; } 
                else if (last_known_bpm > 80) { c = GColorJaegerGreen; c_elevated+=10; } 
                else { c = GColorPictonBlue; c_normal+=10; }

                // Map logical time offset to physical perimeter pixel (100% = 856)
                int minute_offset = (b - wake_idx) * 10;
                int p = (minute_offset * 856) / total_day_mins;

                // Start new drawing group if none active
                if (group_start_p == -1) { 
                    group_start_p = p;
                    current_thickness = thickness;
                    current_c = c; 
                // Render continuous group if thickness or color shifts
                } else if (thickness != current_thickness || !gcolor_equal(c, current_c)) { 
                    int end_p = p;
                    // Ensure rendering length is at least 1 pixel
                    if (end_p <= group_start_p) end_p = group_start_p + 1;
                    
                    // Dispatch rendering instruction
                    draw_perimeter_segment(ctx, group_start_p, end_p, current_thickness, current_c);
                    
                    // Update tracking variables for next block
                    group_start_p = p;
                    current_thickness = thickness;
                    current_c = c;
                } 
                // Store trailing position
                current_p = p;
            } 
        } 
        
        // Terminate and render the final trailing drawing segment
        if (group_start_p != -1) { 
            draw_perimeter_segment(ctx, group_start_p, current_p + 1, current_thickness, current_c);
            graphics_context_set_fill_color(ctx, current_c);
            graphics_fill_circle(ctx, get_perimeter_point(current_p, current_thickness), current_thickness / 2);
        } 

        // Query current live BPM
        int live_bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
        // Query stored stress score, defaulting to 50
        int score = persist_exists(PERSIST_KEY_SCORE) ? persist_read_int(PERSIST_KEY_SCORE) : 50; 
        
        // Configure context and render title
        graphics_context_set_text_color(ctx, g_dark_mode ? GColorWhite : GColorBlack);
        graphics_draw_text(ctx, "Stress", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), 
                           GRect(0, 5, 200, 35), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        // Format and render current score and live BPM
        char top_buf[64];
        int days = 0;
        int baseline = hrv_baseline_ms(&days);
        snprintf(top_buf, sizeof(top_buf), "Score: %d/100  HR: %d\nHRV %s: %d/%d days",
                 score, live_bpm, baseline ? "ready" : "calibrate",
                 days, HRV_BASELINE_MIN);
        graphics_draw_text(ctx, top_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 
                           GRect(0, 38, 200, 45), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        if (s_hrv_count_history > 0) {
            HRVRecord *last = &s_hrv_history[s_hrv_count_history - 1];
            char hrv_text[60];
            snprintf(hrv_text, sizeof(hrv_text), "Last HRV %u ms  %s",
                     (unsigned)last->rmssd,
                     last->quality == 2 ? "Good" :
                     last->quality == 1 ? "Fair" : "Poor");
            graphics_draw_text(ctx, hrv_text,
                               fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                               GRect(0, 78, 200, 19), GTextOverflowModeWordWrap,
                               GTextAlignmentCenter, NULL);
        }
        // Establish layout grid for lower statistics column
        int y_start = 100;
        int y_step = 20; int x_sq = 45; int x_txt = 65;
        GFont row_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

        // Render peak statistics row
        graphics_context_set_fill_color(ctx, GColorRed);
        graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
        char buf_peak[32];
        snprintf(buf_peak, sizeof(buf_peak), "Peak: %dh %dm", c_peak / 60, c_peak % 60);
        graphics_draw_text(ctx, buf_peak, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        y_start += y_step;
        
        // Render high statistics row
        graphics_context_set_fill_color(ctx, GColorOrange);
        graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
        char buf_high[32];
        snprintf(buf_high, sizeof(buf_high), "High: %dh %dm", c_high / 60, c_high % 60);
        graphics_draw_text(ctx, buf_high, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        y_start += y_step;
        
        // Render elevated statistics row
        graphics_context_set_fill_color(ctx, GColorJaegerGreen);
        graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
        char buf_elev[32];
        snprintf(buf_elev, sizeof(buf_elev), "Elev.: %dh %dm", c_elevated / 60, c_elevated % 60);
        graphics_draw_text(ctx, buf_elev, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
        y_start += y_step;
        
        // Render normal statistics row
        graphics_context_set_fill_color(ctx, GColorPictonBlue);
        graphics_fill_rect(ctx, GRect(x_sq, y_start + 6, 10, 10), 0, GCornerNone);
        char buf_norm[32];
        snprintf(buf_norm, sizeof(buf_norm), "Norm.: %dh %dm", c_normal / 60, c_normal % 60);
        graphics_draw_text(ctx, buf_norm, row_font, GRect(x_txt, y_start, 135, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

        // Render instructional menu at the bottom of the display
        graphics_context_set_text_color(ctx, GColorDarkGray);
        graphics_draw_text(ctx, "[Sel]: Scan  [Up/Dn]: Breathe", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), 
                           GRect(0, 185, 200, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    } 
}


// ============================================================================
// --- SECTION 6: CONTROL LOGIC (ui_stress.c) ---
// ============================================================================

static void start_breathing_session() {
    s_show_breath_summary = false; s_is_breathing = true; 
    s_session_time_left = 300; memset(s_bpm_samples, 0, sizeof(s_bpm_samples)); s_current_stress = -1;
    
    s_breath_phase = 0; 
    s_phase_time_left = get_phase_duration(g_breath_style, s_breath_phase);
    start_radius_animation(20, 80, s_phase_time_left * 1000);
    app_timer_register(10, deferred_audio_callback, (void*)(intptr_t)s_breath_phase);
    
    health_service_set_heart_rate_sample_period(1);
    s_breath_start_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
    if (s_breath_start_hr == 0) s_breath_start_hr = 70;
    
    if (g_backlight_stay_on) light_enable(true);
    if (!s_unified_timer) s_unified_timer = app_timer_register(1000, unified_timer_callback, NULL);
    if (s_stress_canvas) layer_mark_dirty(s_stress_canvas);
}

static void start_stress_scan(bool automatic) {
    if (s_is_scanning || s_is_breathing) return;
    s_show_breath_summary = false;
    s_is_scanning = true;
    s_scan_context = automatic ? 1 : 0;
    hrv_begin();
    if (s_ekg_layer) layer_set_hidden(s_ekg_layer, false);
    s_scan_ticks = 60;
    memset(s_bpm_samples, 0, sizeof(s_bpm_samples));
    health_service_set_heart_rate_sample_period(1);
    if (g_backlight_stay_on) light_enable(true);
    if (!s_unified_timer)
        s_unified_timer = app_timer_register(1000, unified_timer_callback, NULL);
    if (!s_ekg_timer)
        s_ekg_timer = app_timer_register(50, ekg_update_timer, NULL);
    if (!automatic && g_haptic_level > 0) vibes_short_pulse();
    if (s_stress_canvas) layer_mark_dirty(s_stress_canvas);
}

static void stress_select_handler(ClickRecognizerRef recognizer, void *context) { 
    if (s_show_breath_summary) {
        s_show_breath_summary = false;
        if (s_stress_canvas) layer_mark_dirty(s_stress_canvas);
    } else if (s_is_scanning || s_is_breathing) { 
        s_is_scanning = false; s_is_breathing = false;
        if (s_ekg_layer) layer_set_hidden(s_ekg_layer, true);
        audio_engine_stop(); hrv_end(); health_service_set_heart_rate_sample_period(0);
        if (g_backlight_stay_on) light_enable(false);
        if (s_radius_animation) { animation_unschedule(s_radius_animation); s_radius = 20; }
        if (s_stress_canvas) layer_mark_dirty(s_stress_canvas);
    } else {
        start_stress_scan(false);
    } 
}

static void stress_up_handler(ClickRecognizerRef recognizer, void *context) { 
    if (s_is_breathing) { 
        if (g_volume_level < 100) { 
            g_volume_level += 10;
            persist_write_int(PERSIST_KEY_VOLUME, g_volume_level);
            ui_stress_mark_dirty();
        } 
    } else if (!s_is_scanning && !s_show_breath_summary) {
        if (persist_exists(PERSIST_KEY_VOLUME)) {
            g_volume_level = persist_read_int(PERSIST_KEY_VOLUME);
        } else {
            g_volume_level = 100;
        }
        start_breathing_session();
    }
} 

static void stress_down_handler(ClickRecognizerRef recognizer, void *context) { 
    if (s_is_breathing) { 
        if (g_volume_level > 0) { 
            g_volume_level -= 10;
            persist_write_int(PERSIST_KEY_VOLUME, g_volume_level);
            ui_stress_mark_dirty();
        } 
    } else if (!s_is_scanning && !s_show_breath_summary) {
        g_volume_level = 0;
        start_breathing_session();
    }
}

static void stress_back_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_is_breathing || s_is_scanning || s_show_breath_summary) {
        s_is_breathing = false; s_is_scanning = false; s_show_breath_summary = false;
        if (s_ekg_layer) layer_set_hidden(s_ekg_layer, true);
        audio_engine_stop(); hrv_end(); health_service_set_heart_rate_sample_period(0);
        if (g_backlight_stay_on) light_enable(false);
        if (s_radius_animation) { animation_unschedule(s_radius_animation); s_radius = 20; }
        if (s_stress_canvas) layer_mark_dirty(s_stress_canvas);
    } else {
        window_stack_pop(true);
    }
}

static void stress_click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, stress_select_handler);
    window_single_click_subscribe(BUTTON_ID_UP, stress_up_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, stress_down_handler);
    window_single_click_subscribe(BUTTON_ID_BACK, stress_back_handler);
}

// ============================================================================
// --- SECTION 7: STRESS MODULE LIFECYCLE (ui_stress.c) ---
// ============================================================================
static void stress_view_load(Window *w) { 
    window_set_background_color(w, g_dark_mode ? GColorBlack : GColorWhite);
    s_is_scanning = false; s_is_breathing = false; s_show_breath_summary = false;
    health_service_set_heart_rate_sample_period(0);
    
    if (s_radius_animation) {
        animation_unschedule(s_radius_animation);
        s_radius = 20;
    }

    s_stress_canvas = layer_create(layer_get_bounds(window_get_root_layer(w)));
    layer_set_update_proc(s_stress_canvas, stress_update_proc); 
    layer_add_child(window_get_root_layer(w), s_stress_canvas);
    
    s_ekg_layer = layer_create(GRect(20, 70, 160, 80));
    layer_set_update_proc(s_ekg_layer, ekg_update_proc);
    layer_add_child(s_stress_canvas, s_ekg_layer);
    layer_set_hidden(s_ekg_layer, true);

    window_set_click_config_provider(w, stress_click_config_provider);
}

static void stress_view_unload(Window *w) {
    hrv_end(); 
    audio_engine_stop();
    layer_destroy(s_ekg_layer);
    layer_destroy(s_stress_canvas);
    s_stress_canvas = NULL;
    s_ekg_layer = NULL;
}

void ui_stress_init() {
    s_hrv_count_history = 0;
    if (persist_exists(HRV_HISTORY_KEY)) {
        int bytes = persist_get_size(HRV_HISTORY_KEY);
        if (bytes > 0 && bytes <= (int)sizeof(s_hrv_history) &&
            bytes % (int)sizeof(HRVRecord) == 0) {
            int result = persist_read_data(HRV_HISTORY_KEY, s_hrv_history, bytes);
            if (result == bytes) s_hrv_count_history = bytes / sizeof(HRVRecord);
        }
    }
    s_stress_window = window_create();
    window_set_window_handlers(s_stress_window, (WindowHandlers){ .load = stress_view_load, .unload = stress_view_unload });
}

void ui_stress_deinit() {
    window_destroy(s_stress_window);
}

void ui_stress_start_automatic(void) {
    ui_stress_push();
    start_stress_scan(true);
}

void ui_stress_push() {
    window_stack_push(s_stress_window, true);
}

void ui_stress_mark_dirty() {
    if (s_stress_canvas && window_is_loaded(s_stress_window)) {
        layer_mark_dirty(s_stress_canvas);
    }
}