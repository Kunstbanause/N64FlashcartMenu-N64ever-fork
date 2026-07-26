#include <stdbool.h>
#include <stdio.h>
#include <libdragon.h>
#include <sys/time.h>
#include "../sound.h"
#include "../ui_components/constants.h"
#include "views.h"

#define MAX(a,b)  ({ typeof(a) _a = a; typeof(b) _b = b; _a > _b ? _a : _b; })
#define MIN(a,b)  ({ typeof(a) _a = a; typeof(b) _b = b; _a < _b ? _a : _b; })
#define CLAMP(x, min, max) (MIN(MAX((x), (min)), (max)))
#define WRAP(x, min, max)  ({ \
    typeof(x) _x = x; typeof(min) _min = min; typeof(max) _max = max; \
    _x < _min ? _max : _x > _max ? _min : _x; \
})

#define YEAR_MIN 1996
#define YEAR_MAX 2095

typedef enum {
    RTC_EDIT_YEAR,
    RTC_EDIT_MONTH,
    RTC_EDIT_DAY,
    RTC_EDIT_HOUR,
    RTC_EDIT_MIN,
    RTC_EDIT_SEC,
} rtc_field_t;

static const char* const DAYS_OF_WEEK[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static struct tm rtc_tm = {0};
static bool is_editing_mode;
static rtc_field_t editing_field_type;


void adjust_rtc_time( struct tm *t, int incr ) {
    switch(editing_field_type)
    {
        case RTC_EDIT_YEAR:
            t->tm_year = WRAP( t->tm_year + incr, YEAR_MIN - 1900, YEAR_MAX - 1900 );
            break;
        case RTC_EDIT_MONTH:
            t->tm_mon = WRAP( t->tm_mon + incr, 0, 11 );
            break;
        case RTC_EDIT_DAY:
            t->tm_mday = WRAP( t->tm_mday + incr, 1, 31 );
            break;
        case RTC_EDIT_HOUR:
            t->tm_hour = WRAP( t->tm_hour + incr, 0, 23 );
            break;
        case RTC_EDIT_MIN:
            t->tm_min = WRAP( t->tm_min + incr, 0, 59 );
            break;
        case RTC_EDIT_SEC:
            t->tm_sec = WRAP( t->tm_sec + incr, 0, 59 );
            break;
    }
    // Recalculate day-of-week and day-of-year
    time_t timestamp = mktime( t );
    *t = *gmtime( &timestamp );
}


/**
 * Draws the RTC date/time editor UI component.
 * 
 * @param t The struct tm containing the current date and time values to display.
 * @param selected_field The field currently selected for editing (year, month, day, etc.).
 */
void rtc_ui_component_editdatetime_draw ( struct tm t, rtc_field_t selected_field ) {
    static const char *hdrs[7] = { "YYYY", "MM", "DD", "hh", "mm", "ss", "DoW" };
    char vals[7][6];
    snprintf(vals[0], sizeof(vals[0]), "%04d", CLAMP(t.tm_year + 1900, YEAR_MIN, YEAR_MAX));
    snprintf(vals[1], sizeof(vals[1]), "%02d", CLAMP(t.tm_mon + 1, 1, 12));
    snprintf(vals[2], sizeof(vals[2]), "%02d", CLAMP(t.tm_mday, 1, 31));
    snprintf(vals[3], sizeof(vals[3]), "%02d", CLAMP(t.tm_hour, 0, 23));
    snprintf(vals[4], sizeof(vals[4]), "%02d", CLAMP(t.tm_min, 0, 59));
    snprintf(vals[5], sizeof(vals[5]), "%02d", CLAMP(t.tm_sec, 0, 59));
    snprintf(vals[6], sizeof(vals[6]), "%s", DAYS_OF_WEEK[CLAMP(t.tm_wday, 0, 6)]);

    /* Self-contained popup: a rainbow-rimmed box holding the 7 fields and the
       control hints, sized snugly to the content (no full-screen takeover). */
    const int n = 7, col_w = 48, pad = 18, line_h = 16, gap = 12;
    int box_w = n * col_w + pad * 2;
    int box_h = pad + line_h * 2 /* header + value */ + gap + line_h * 2 /* 2 hint lines */ + pad;

    ui_components_dialog_draw(box_w, box_h);

    int bx0  = DISPLAY_CENTER_X - box_w / 2;
    int by0  = DISPLAY_CENTER_Y - box_h / 2;
    int fx0  = bx0 + pad;
    int hy   = by0 + pad;          /* header row */
    int vy   = hy + line_h;        /* value row  */

    /* Highlight the selected field's column behind its text. */
    int hx = fx0 + (int)selected_field * col_w;
    /* rdpq_text_print's y is the glyph baseline (text ascends above it), so anchor
       the highlight to the visual top of the header row down to just below the value
       -- otherwise it sits a row low and overhangs into empty space. */
    ui_components_box_draw(hx, hy - line_h + 2, hx + col_w, vy + 4, RGBA32(0x4C, 0x4C, 0x60, 0xFF));

    for (int i = 0; i < n; i++) {
        int cx = fx0 + i * col_w;
        rdpq_textparms_t tp = { .width = col_w, .align = ALIGN_CENTER, .wrap = WRAP_NONE };
        rdpq_text_print(&tp, FNT_DEFAULT, cx, hy, hdrs[i]);
        rdpq_text_print(&tp, FNT_DEFAULT, cx, vy, vals[i]);
    }

    int hint_y = vy + line_h + gap;
    rdpq_textparms_t hp = { .width = box_w - pad * 2, .align = ALIGN_CENTER, .wrap = WRAP_NONE };
    rdpq_text_print(&hp, FNT_DEFAULT, fx0, hint_y,          "Up/Down: Adjust   Left/Right: Field");
    rdpq_text_print(&hp, FNT_DEFAULT, fx0, hint_y + line_h, "Z: Save   B: Back");
}

static void process (menu_t *menu) {
    if (menu->actions.back && !is_editing_mode) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = menu->load.load_return_mode ? menu->load.load_return_mode : MENU_MODE_BROWSER;
        menu->load.load_return_mode = 0;
    }
    else if (menu->actions.enter && !is_editing_mode && menu->current_time >= 0) {
        rtc_tm = *gmtime(&menu->current_time);
        is_editing_mode = true;
    }
    
    if (is_editing_mode) {
        if (menu->actions.go_left) {
            if ( editing_field_type <= RTC_EDIT_YEAR ) { editing_field_type = RTC_EDIT_SEC; }
            else { editing_field_type = editing_field_type - 1; }
        }
        else if (menu->actions.go_right) {
            if ( editing_field_type >= RTC_EDIT_SEC ) { editing_field_type = RTC_EDIT_YEAR; }
            else { editing_field_type = editing_field_type + 1; }
        }
        else if (menu->actions.go_up) {
            adjust_rtc_time( &rtc_tm, +1 );
        }
        else if (menu->actions.go_down) {
            adjust_rtc_time( &rtc_tm, -1 );
        }
        else if (menu->actions.options) { // Z button = save
            if( rtc_get_source() == RTC_SOURCE_JOYBUS && rtc_is_source_available( RTC_SOURCE_JOYBUS ) ) {
                struct timeval new_time = { .tv_sec = mktime(&rtc_tm) };
                int res = settimeofday(&new_time, NULL);

                if (res != 0) {
                    menu_show_error(menu, "Failed to set RTC time");
                }
            }
            else {
                menu_show_error(menu, "RTC is not writable");
            }
            is_editing_mode = false;
        }
        else if (menu->actions.back) { // cancel
            is_editing_mode = false;
        }
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    /* Both states are popups over the grid backdrop — no full-screen takeover. */
    view_games_grid_draw_background(menu, d);

    if (!is_editing_mode) {
        /* Compact popup instead of a full-screen panel. */
        if (menu->current_time >= 0) {
            ui_components_messagebox_draw(
                "Real Time Clock\n"
                "\n"
                "Current: %s"
                "\n"
                "A: Adjust    B: Back",
                ctime(&menu->current_time)
            );
        } else {
            ui_components_messagebox_draw(
                "Real Time Clock\n"
                "\n"
                "This cart has no real time clock.\n"
                "\n"
                "B: Back"
            );
        }
    } else {
        /* Editing: a single self-framed popup (fields + hints inside one box). */
        rtc_ui_component_editdatetime_draw(rtc_tm, editing_field_type);
    }

    rdpq_detach_show();
}


void view_rtc_init (menu_t *menu) {
    /* Resync the time from the hardware RTC */
    rtc_set_source( rtc_get_source() );
    is_editing_mode = false;
    editing_field_type = RTC_EDIT_YEAR;
}

void view_rtc_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}
