#include "ui_main.h"
#include "ui_rudder_gauge.h"

/* Set to 0 once real sensor data drives the UI. */
#define UI_DEMO_ANIMATION 1

#define SCREEN_BG   lv_color_hex(0x11141A)
#define HEADER_BG   lv_color_hex(0x1B1F25)
#define TEXT_FG     lv_color_hex(0xE8ECF0)
#define TEXT_DIM    lv_color_hex(0x8A939E)

static ui_rudder_gauge_t s_rudder;

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
#endif

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

    lv_obj_t *bus = lv_label_create(header);
    lv_obj_set_style_text_font(bus, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bus, TEXT_DIM, 0);
    lv_label_set_text(bus, "NMEA 2000: offline");
    lv_obj_align(bus, LV_ALIGN_RIGHT_MID, 0, 0);
}

void ui_main_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, SCREEN_BG, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    header_create(screen);

    /* One gauge for now; the remaining area is where the other sensor tiles go. */
    ui_rudder_gauge_create(&s_rudder, screen, 320, 45);
    lv_obj_align(s_rudder.cont, LV_ALIGN_TOP_LEFT, 24, 80);
    ui_rudder_gauge_set_title(&s_rudder, "RUDDER ANGLE");

#if UI_DEMO_ANIMATION
    lv_timer_create(demo_timer_cb, 40, NULL);
#endif
}

void ui_main_set_rudder_angle(int32_t angle_deg)
{
    ui_rudder_gauge_set_value(&s_rudder, angle_deg);
}

void ui_main_set_rudder_no_data(void)
{
    ui_rudder_gauge_set_no_data(&s_rudder);
}
