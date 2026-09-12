#include "ui_main.h"
#include "ui_heading_gauge.h"
#include "ui_rudder_gauge.h"
#include "n2k_bridge.h"
#include "rudder_sensor.h"

#include <math.h>

/* Set to 1 to sweep the needle from a canned animation instead of the sensor,
 * for checking the layout without the rudder feedback unit connected. */
#define UI_DEMO_ANIMATION 0

/* Display refresh rate for the rudder reading. The driver measures at ~100 Hz;
 * there is nothing to gain from redrawing faster than the eye resolves. */
#define UI_RUDDER_POLL_MS 100

/* Bus data arrives at 10 Hz at most and the status changes over seconds, so
 * this side of the panel is polled far more slowly than the local sensor. */
#define UI_N2K_POLL_MS 250

/* Tile geometry. Two 320 px tiles centred across the 800 px panel, below the
 * header; the numbers are named so a third tile is an arithmetic change. */
#define TILE_SIZE       320
#define TILE_MARGIN_X   48
#define TILE_GAP_X      64
#define TILE_TOP_Y      108

#define SCREEN_BG   lv_color_hex(0x11141A)
#define HEADER_BG   lv_color_hex(0x1B1F25)
#define TEXT_FG     lv_color_hex(0xE8ECF0)
#define TEXT_DIM    lv_color_hex(0x8A939E)
#define STATUS_OK   lv_color_hex(0x30B050)
#define STATUS_WARN lv_color_hex(0xE0A030)
#define STATUS_BAD  lv_color_hex(0xE03030)

static ui_rudder_gauge_t s_rudder;
static ui_heading_gauge_t s_heading;
static lv_obj_t *s_bus_status_label;

#if UI_DEMO_ANIMATION
/* Sweeps the needle back and forth so the layout can be checked on hardware
 * before the GPIO sensor task exists. */
static void demo_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    static int32_t angle = 0;
    static int32_t step = 1;

    angle += step;
    if (angle >= 35 || angle <= -35) {
        step = -step;
    }
    ui_rudder_gauge_set_value(&s_rudder, angle);
}
#else
/* Runs in the LVGL task, which already holds the lock. */
static void rudder_poll_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    rudder_sensor_reading_t reading;
    if (rudder_sensor_read(&reading) == ESP_OK && reading.valid) {
        ui_rudder_gauge_set_value(&s_rudder, (int32_t)lroundf(reading.angle_deg));
    } else {
        ui_rudder_gauge_set_no_data(&s_rudder);
    }
}
#endif

/* Redraws the bus status line only when it actually changes: the label is
 * repainted by LVGL on every set_text, change or not. */
static void bus_status_update(void)
{
    static bool initialised;
    static n2k_bridge_status_t last;

    n2k_bridge_status_t status;
    n2k_bridge_get_status(&status);

    if (initialised && status.state == last.state && status.address == last.address) {
        return;
    }
    last = status;
    initialised = true;

    lv_color_t color;
    switch (status.state) {
    case N2K_BUS_ONLINE:
        /* The claimed source address is worth showing: it is what identifies
         * this device in a capture or on another display's device list. */
        lv_label_set_text_fmt(s_bus_status_label, "NMEA 2000: online (addr %d)", (int)status.address);
        color = STATUS_OK;
        break;
    case N2K_BUS_CLAIMING:
        lv_label_set_text(s_bus_status_label, "NMEA 2000: claiming address");
        color = STATUS_WARN;
        break;
    case N2K_BUS_ERROR:
        /* Error-passive: frames are going out but nothing is acknowledging
         * them. Usually an unplugged drop cable or a missing terminator. */
        lv_label_set_text(s_bus_status_label, "NMEA 2000: no response");
        color = STATUS_WARN;
        break;
    case N2K_BUS_OFF:
        lv_label_set_text(s_bus_status_label, "NMEA 2000: bus off");
        color = STATUS_BAD;
        break;
    case N2K_BUS_STOPPED:
    default:
        lv_label_set_text(s_bus_status_label, "NMEA 2000: offline");
        color = TEXT_DIM;
        break;
    }
    lv_obj_set_style_text_color(s_bus_status_label, color, 0);
    lv_obj_align(s_bus_status_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* Runs in the LVGL task, which already holds the lock. */
static void n2k_poll_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    n2k_heading_t heading;
    if (n2k_bridge_read_heading(&heading) == ESP_OK && heading.valid) {
        ui_heading_gauge_set_value(&s_heading,
                                   (int32_t)lroundf(heading.heading_deg),
                                   heading.reference == N2K_HEADING_REF_MAGNETIC);
    } else {
        ui_heading_gauge_set_no_data(&s_heading);
    }

    bus_status_update();
}

static void header_create(lv_obj_t *parent)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), 56);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, HEADER_BG, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 16, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, TEXT_FG, 0);
    lv_label_set_text(title, "Sensor Bridge");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    /* Kept in a static so the poll timer can retitle it; the label is created
     * in its worst-case state and corrected on the first poll. */
    s_bus_status_label = lv_label_create(header);
    lv_obj_set_style_text_font(s_bus_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_bus_status_label, TEXT_DIM, 0);
    lv_label_set_text(s_bus_status_label, "NMEA 2000: offline");
    lv_obj_align(s_bus_status_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

void ui_main_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, SCREEN_BG, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    header_create(screen);

    /* Left tile: the rudder this device measures itself. */
    ui_rudder_gauge_create(&s_rudder, screen, TILE_SIZE, 45);
    lv_obj_align(s_rudder.cont, LV_ALIGN_TOP_LEFT, TILE_MARGIN_X, TILE_TOP_Y);
    ui_rudder_gauge_set_title(&s_rudder, "RUDDER ANGLE");

    /* Right tile: heading, read off the bus rather than measured here. */
    ui_heading_gauge_create(&s_heading, screen, TILE_SIZE);
    lv_obj_align(s_heading.cont, LV_ALIGN_TOP_LEFT,
                 TILE_MARGIN_X + TILE_SIZE + TILE_GAP_X, TILE_TOP_Y);
    ui_heading_gauge_set_title(&s_heading, "HEADING");

#if UI_DEMO_ANIMATION
    lv_timer_create(demo_timer_cb, 40, NULL);
#else
    ui_rudder_gauge_set_no_data(&s_rudder);
    lv_timer_create(rudder_poll_cb, UI_RUDDER_POLL_MS, NULL);
#endif

    lv_timer_create(n2k_poll_cb, UI_N2K_POLL_MS, NULL);
}

void ui_main_set_rudder_angle(int32_t angle_deg)
{
    ui_rudder_gauge_set_value(&s_rudder, angle_deg);
}

void ui_main_set_rudder_no_data(void)
{
    ui_rudder_gauge_set_no_data(&s_rudder);
}
