/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "core/lv_obj_pos.h"
#include "extra/layouts/flex/lv_flex.h"
#include "font/lv_font.h"
#include "font/lv_symbol_def.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"


#include "esp_err.h"
#include "esp_log.h"
#include "hal/lv_hal_disp.h"
#include "lv_conf_internal.h"
#include "lvgl.h"
#include "misc/lv_area.h"
#include "misc/lv_color.h"
#include "misc/lv_txt.h"
#include "widgets/lv_arc.h"
#include "resources/fan_icon.h"

#define CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01 1


#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
#include "esp_lcd_ili9341.h"
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
#include "hal/spi_ll.h"
#include "esp_lcd_gc9a01.h"
#endif

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
#include "esp_lcd_touch_stmpe610.h"
#endif

static const char *TAG = "SCREEN";

// Using SPI2 in the example
#define LCD_HOST  SPI2_HOST

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_SCLK           5
#define EXAMPLE_PIN_NUM_MOSI           23
//#define EXAMPLE_PIN_NUM_MISO           21
#define EXAMPLE_PIN_NUM_LCD_DC         19
#define EXAMPLE_PIN_NUM_LCD_RST        16
#define EXAMPLE_PIN_NUM_LCD_CS         18 //needed to change due to conflict with fan.
//#define EXAMPLE_PIN_NUM_BK_LIGHT       2
//#define EXAMPLE_PIN_NUM_TOUCH_CS       15

// The pixel number in horizontal and vertical
#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
#define EXAMPLE_LCD_H_RES              240
#define EXAMPLE_LCD_V_RES              320
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
#define EXAMPLE_LCD_H_RES              240
#define EXAMPLE_LCD_V_RES              240
#endif
// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8

#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

static SemaphoreHandle_t lvgl_mux = NULL;


lv_obj_t *auto_btn;
#define SCR_ICON_HOME 0
#define SCR_ICON_WIFI 1
#define SCR_ICON_BT   2
#define SCR_ICON_BATT 3
lv_obj_t *icon_labels[4];


lv_obj_t *temp_main_label[4];
lv_obj_t *temp_dec_label[4];

lv_obj_t *main_temp_label;
lv_obj_t *main_temp_label_dec;

lv_obj_t * fan_arc;
lv_obj_t * temp_arc;
lv_obj_t *target_temp_label;
lv_obj_t *fan_pct_txt;

bool screen_initialized=false;



#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
esp_lcd_touch_handle_t tp = NULL;
#endif

#define SCREEN_WIDTH EXAMPLE_LCD_H_RES
#define SCREEN_HEIGHT EXAMPLE_LCD_V_RES



void scr_set_auto(bool b_auto){
    if (!screen_initialized) return;
    if (b_auto){
        lv_obj_set_style_bg_color(auto_btn, lv_color_hex(0x1179EE), LV_PART_MAIN);
    }
    else {
        lv_obj_set_style_bg_color(auto_btn, lv_color_hex(0xBFBFBF), LV_PART_MAIN);
    }
}

void scr_set_home_icon(bool home_connected){
    if (!screen_initialized) return;
    if(home_connected){
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_HOME], lv_color_hex(0x1179EE), 0);
    }
    else{
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_HOME], lv_color_hex(0xBFBFBF), 0);
    }
}

void scr_set_wifi_icon(bool wifi_connected){
    if (!screen_initialized) return;
    if(wifi_connected){
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_WIFI], lv_color_hex(0x1179EE), 0);
    }
    else{
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_WIFI], lv_color_hex(0xBFBFBF), 0);
    }
}


void scr_set_bt_icon(bool bt_connected){
    if (!screen_initialized) return;
    if(bt_connected){
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_BT], lv_color_hex(0x1179EE), 0);
    }
    else{
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_BT], lv_color_hex(0xBFBFBF), 0);
    }
}


void scr_set_battery_icon(int percentage){
    if (!screen_initialized) return;
    if(percentage>=75){
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_BATT], lv_color_hex(0x1179EE), 0);
        lv_label_set_text(icon_labels[SCR_ICON_BATT], LV_SYMBOL_BATTERY_FULL);
    }
    else if (percentage>=50){
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_BATT], lv_color_hex(0x1179EE), 0);
        lv_label_set_text(icon_labels[SCR_ICON_BATT], LV_SYMBOL_BATTERY_3);
    }
    else if (percentage>=25){
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_BATT], lv_color_hex(0x1179EE), 0);
        lv_label_set_text(icon_labels[SCR_ICON_BATT], LV_SYMBOL_BATTERY_2);
    }
    else{
        lv_obj_set_style_text_color(icon_labels[SCR_ICON_BATT], lv_color_hex(0xbd4040), 0); //red
        lv_label_set_text(icon_labels[SCR_ICON_BATT], LV_SYMBOL_BATTERY_1);
    }
}


void scr_set_t(int n, float temp){
    if (!screen_initialized) return;
    
    ESP_LOGI(TAG, "setting [%d] temp to %f", n, temp);
    n-=1;
    if(n>=4)return;

    int main = (int)temp;
    float dec = temp-main;

    if(temp==0){
        lv_label_set_text(temp_main_label[n], "-");
        lv_label_set_text(temp_dec_label[n], ".-");
    }
    else{
        char tmp[10]="";
        sprintf(tmp, "%d", (int)temp);
        lv_label_set_text(temp_main_label[n], tmp);
        sprintf(tmp, ".%d", (int)(dec*10));
        lv_label_set_text(temp_dec_label[n], tmp);
    }

}

void scr_set_fan_speed(int percentage){
    if (!screen_initialized) return;
    lv_arc_set_value(fan_arc, percentage);
    char tmp[10]="";
    sprintf(tmp, "%d%%", percentage);
    lv_label_set_text(fan_pct_txt, tmp);
}

void update_right_arc(int percentage){
    lv_arc_set_value(temp_arc, percentage);
}

int scr_map(int x, int in_min, int in_max, int out_min, int out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void scr_set_pit_t(float temp){
    if (!screen_initialized) return;
    ESP_LOGI(TAG, "setting pit temp");
    int main = (int)temp;
    float dec = temp-main;

    char tmp[10]="";
    sprintf(tmp, "%d", (int)temp);
    lv_label_set_text(main_temp_label, tmp);
    sprintf(tmp, ".%d", (int)(dec*10));
    lv_label_set_text(main_temp_label_dec, tmp);

    int target_temp=200;

    update_right_arc(scr_map(main, 0, target_temp, 0,100));
}

void scr_set_target_t(float temp){
    if (!screen_initialized) return;
    int main = (int)temp;

    char tmp[10]="";
    sprintf(tmp, "%d", (int)temp);
    lv_label_set_text(target_temp_label, tmp);
    
    int current_temp=100;

    update_right_arc(scr_map(current_temp, 0, main, 0,100));
}





void new_disp(lv_disp_t *disp){

    // Create a screen object
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // Set the screen background color
    //lv_obj_set_style_bg_color(scr, lv_color_hex(0xF5F5F5), LV_PART_MAIN);

    // Create a container for the top section
    lv_obj_t *top_container = lv_obj_create(scr);
    lv_obj_set_size(top_container, SCREEN_WIDTH, 50);
    //lv_obj_set_style_bg_color(top_container, lv_color_hex(0xF5F5F5), LV_PART_MAIN);
    
    lv_obj_set_style_bg_opa(top_container, LV_OPA_TRANSP, LV_PART_MAIN); // Make background transparent
    lv_obj_set_style_border_width(top_container, 0, LV_PART_MAIN); // Remove border
    lv_obj_set_style_pad_top(top_container, 5, 0);
    lv_obj_set_style_pad_bottom(top_container, 0, 0);
    //lv_obj_align(top_container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_container, LV_OBJ_FLAG_SCROLLABLE);

    // Create the "Auto" button
    auto_btn = lv_btn_create(top_container);
    lv_obj_set_size(auto_btn, 50, 15); // Adjust the size of the button
    lv_obj_align(auto_btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(auto_btn, lv_color_hex(0xBFBFBF), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(auto_btn, 0, LV_PART_MAIN); // Remove shadow

    lv_obj_t *btn_label = lv_label_create(auto_btn);
    lv_label_set_text(btn_label, "Auto");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_10, 0);
    lv_obj_center(btn_label);

    // Create a row of icons
    lv_obj_t *icon_container = lv_obj_create(top_container);
    lv_obj_set_size(icon_container, SCREEN_WIDTH-60, 30);
    lv_obj_center(icon_container);
    lv_obj_set_style_bg_opa(icon_container, LV_OPA_TRANSP, LV_PART_MAIN); // Make background transparent
    lv_obj_set_style_border_width(icon_container, 0, LV_PART_MAIN); // Remove border
    lv_obj_set_style_pad_top(icon_container, 10, 0);
    lv_obj_set_style_pad_bottom(icon_container, 0, 0);
    lv_obj_align(icon_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(icon_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(icon_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(icon_container, LV_FLEX_FLOW_ROW_WRAP);
    //lv_obj_set_flex_align(lv_obj_t *obj, lv_flex_align_t main_place, lv_flex_align_t cross_place, lv_flex_align_t track_cross_place)
    lv_obj_set_style_flex_main_place(icon_container, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    //lv_obj_set_style_pad_column(icon_container, 15, 0); // Add some spacing between icons

    // Add icons to the icon container
    const char *icons[] = {LV_SYMBOL_HOME, LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_BATTERY_2}; // Placeholder for home, wifi, bluetooth, and battery icons
    
    for (int i = 0; i < 4; i++) {
        icon_labels[i] = lv_label_create(icon_container);
        lv_label_set_text(icon_labels[i], icons[i]);
        lv_obj_set_style_text_font(icon_labels[i], &lv_font_montserrat_14, 0); // Use appropriate font for icons
        lv_obj_set_style_text_color(icon_labels[i], lv_color_hex(0xBFBFBF), 0);
    }

    
    // Create the main temperatures container
    lv_obj_t *temps_container = lv_obj_create(scr);
    lv_obj_set_size(temps_container, SCREEN_WIDTH-50, 40);
    lv_obj_align_to(temps_container, top_container, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    //lv_obj_align(temps_container, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(temps_container, LV_OPA_TRANSP, LV_PART_MAIN); // Make background transparent
    lv_obj_set_style_border_width(temps_container, 0, LV_PART_MAIN); // Remove border
    lv_obj_set_style_pad_all(temps_container, 0, 0);
    lv_obj_set_style_pad_top(temps_container, 10, 0);
    lv_obj_clear_flag(temps_container, LV_OBJ_FLAG_SCROLLABLE);
    //lv_obj_set_layout(temps_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(temps_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(temps_container, LV_FLEX_ALIGN_SPACE_EVENLY, 0);

    // Add labels for temperatures
    for (int i = 0; i < 4; i++) {
        // Create a container for each temperature label
        lv_obj_t *temp_container = lv_obj_create(temps_container);
        lv_obj_set_size(temp_container, (SCREEN_WIDTH-50)/4, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(temp_container, LV_OPA_TRANSP, LV_PART_MAIN); // Make background transparent
        lv_obj_set_style_border_width(temp_container, 0, LV_PART_MAIN); // Remove border
        //lv_obj_align(temp_container, LV_ALIGN_LEFT_MID, i* 55, 0);
        lv_obj_set_style_pad_all(temp_container, 0, 0);
        lv_obj_set_style_pad_column(temp_container, 0, 0);
        lv_obj_set_flex_flow(temp_container, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(temp_container, LV_OBJ_FLAG_SCROLLABLE);

        // Create the main temperature label (e.g., "56")
        temp_main_label[i] = lv_label_create(temp_container);
        lv_label_set_text(temp_main_label[i], "-");
        lv_obj_set_style_text_color(temp_main_label[i], lv_color_hex(0xF8981D), 0);
        lv_obj_set_style_text_font(temp_main_label[i], &lv_font_montserrat_22, 0);

        // Create the decimal temperature label (e.g., ".5")
        temp_dec_label[i] = lv_label_create(temp_container);
        lv_label_set_text(temp_dec_label[i], ".-");
        lv_obj_set_style_text_color(temp_dec_label[i], lv_color_hex(0xF8981D), 0);
        lv_obj_set_style_text_font(temp_dec_label[i], &lv_font_montserrat_14, 0);

    }


    // Simulate the dial
    fan_arc = lv_arc_create(scr);
    lv_obj_set_size(fan_arc, 240, 240);
    lv_arc_set_rotation(fan_arc, 110);
    lv_obj_remove_style(fan_arc, NULL, LV_PART_KNOB);  
    lv_arc_set_bg_angles(fan_arc, 0, 80);
    lv_arc_set_value(fan_arc, 0);
    lv_obj_set_style_arc_color(fan_arc, lv_color_hex(0x1179EE), LV_PART_INDICATOR);
    lv_obj_center(fan_arc);
    

    temp_arc = lv_arc_create(scr);
    lv_obj_set_size(temp_arc, 240, 240);
    lv_arc_set_mode(temp_arc, LV_ARC_MODE_REVERSE);
    lv_arc_set_rotation(temp_arc, 350);
    lv_obj_remove_style(temp_arc, NULL, LV_PART_KNOB);   /*Be sure the knob is not displayed*/
    lv_arc_set_bg_angles(temp_arc, 0, 80);
    lv_obj_set_style_arc_color(temp_arc, lv_color_hex(0xF8981D), LV_PART_INDICATOR);
    
    lv_arc_set_value(temp_arc, 0);
    lv_obj_center(temp_arc);


    // Main temperature value
    lv_obj_t *main_temp_container = lv_obj_create(scr);    
    lv_obj_set_size(main_temp_container, LV_SIZE_CONTENT , LV_SIZE_CONTENT );
    lv_obj_set_style_bg_opa(main_temp_container, LV_OPA_TRANSP, LV_PART_MAIN); // Make background transparent
    lv_obj_set_style_border_width(main_temp_container, 0, LV_PART_MAIN); // Remove border
    lv_obj_align(main_temp_container, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_pad_column(main_temp_container, 0, 0);
    lv_obj_set_style_pad_all(main_temp_container, 0, 0);
    lv_obj_clear_flag(main_temp_container, LV_OBJ_FLAG_SCROLLABLE);
    
    
    lv_obj_set_layout(main_temp_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(main_temp_container, LV_FLEX_FLOW_ROW);
    //lv_obj_set_style_flex_main_place(main_temp_container, LV_FLEX_ALIGN_START, 0);

    main_temp_label = lv_label_create(main_temp_container);
    lv_label_set_text(main_temp_label, "-");
    lv_obj_set_style_text_color(main_temp_label, lv_color_hex(0xF8981D), 0);
    lv_obj_set_style_text_font(main_temp_label, &lv_font_montserrat_46, 0);

    main_temp_label_dec = lv_label_create(main_temp_container);
    lv_label_set_text(main_temp_label_dec, ".-°");
    lv_obj_set_style_text_color(main_temp_label_dec, lv_color_hex(0xF8981D), 0);
    lv_obj_set_style_text_font(main_temp_label_dec, &lv_font_montserrat_20, 0);

    
    
    


    lv_obj_t *target_temp_container = lv_obj_create(scr);
    lv_obj_set_size(target_temp_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(target_temp_container, LV_OPA_TRANSP, LV_PART_MAIN); // Make background transparent
    lv_obj_set_style_border_width(target_temp_container, 0, LV_PART_MAIN); // Remove border
    lv_obj_align(target_temp_container, LV_ALIGN_CENTER, 60, 0);
    lv_obj_set_style_pad_all(target_temp_container, 0, 0);
    lv_obj_clear_flag(target_temp_container, LV_OBJ_FLAG_SCROLLABLE);

    target_temp_label = lv_label_create(target_temp_container);
    lv_label_set_text(target_temp_label, "-");
    lv_obj_set_style_text_color(target_temp_label, lv_color_hex(0x1179EE), 0);
    lv_obj_set_style_text_font(target_temp_label, &lv_font_montserrat_30, 0);
    lv_obj_align(target_temp_label, LV_ALIGN_CENTER, 0, 0);


    lv_obj_t *fan_pct = lv_obj_create(scr);
    lv_obj_set_size(fan_pct, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(fan_pct, LV_OPA_TRANSP, LV_PART_MAIN); // Make background transparent
    lv_obj_set_style_border_width(fan_pct, 0, LV_PART_MAIN); // Remove border
    lv_obj_align(fan_pct, LV_ALIGN_BOTTOM_MID, -15, 0);
    lv_obj_set_style_pad_all(fan_pct, 0, 0);
    lv_obj_set_style_pad_row(fan_pct, 0, 0);
    lv_obj_set_flex_flow(fan_pct, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(fan_pct, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_clear_flag(fan_pct, LV_OBJ_FLAG_SCROLLABLE);

    fan_pct_txt = lv_label_create(fan_pct);
    lv_label_set_text(fan_pct_txt, "-%");
    lv_obj_set_style_text_color(fan_pct_txt, lv_color_hex(0x1179EE), 0);
    lv_obj_set_style_text_font(fan_pct_txt, &lv_font_montserrat_12, 0);
    
    //LV_IMG_DECLARE(fan-svgrepo-com_map);
    LV_IMG_DECLARE(fan_svgrepo_com_2);
    lv_obj_t * img1 = lv_img_create(fan_pct);
    lv_img_set_src(img1, &fan_svgrepo_com_2);
    lv_obj_set_size(img1, fan_svgrepo_com_2.header.w, fan_svgrepo_com_2.header.h);
    
    


    /* Create a circular object */
    lv_obj_t * circle = lv_obj_create(scr);
    lv_obj_set_size(circle, 200, 100);  // Set size of the circle
    //lv_obj_center(circle);  // Center the circle in the parent object
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, -115);

    /* Apply styles to the circle */
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);  // Make the object circular
    lv_obj_set_style_border_width(circle, 0, LV_PART_MAIN); // Remove border
    lv_obj_set_style_bg_opa(circle, LV_OPA_20, 0);  // Set background opacity to 50%
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xF8981D), 0);  // Set background color (blue)


    LV_IMG_DECLARE(thermometer_temperature_svgrepo_com);
    lv_obj_t * img2 = lv_img_create(scr);
    lv_img_set_src(img2, &thermometer_temperature_svgrepo_com);
    lv_obj_set_size(img2, thermometer_temperature_svgrepo_com.header.w, thermometer_temperature_svgrepo_com.header.h);
    lv_obj_align(img2, LV_ALIGN_BOTTOM_MID, 20, 0);
    
    
    screen_initialized=true;

}

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
static void example_lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;

    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    }
}

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
static void example_lvgl_touch_cb(lv_indev_drv_t * drv, lv_indev_data_t * data)
{
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    /* Read touch controller data */
    esp_lcd_touch_read_data(drv->user_data);

    /* Get coordinates */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

bool example_lvgl_lock(int timeout_ms)
{
    // Convert timeout in milliseconds to FreeRTOS ticks
    // If `timeout_ms` is set to -1, the program will block until the condition is met
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void example_lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mux);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (example_lvgl_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            // Release the mutex
            example_lvgl_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}




void lcd_start(void)
{
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions


    /*ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    */

    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_SCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
//        .miso_io_num = EXAMPLE_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = example_notify_lvgl_flush_ready,
        .user_ctx = &disp_drv,
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
    ESP_LOGI(TAG, "Install ILI9341 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    ESP_LOGI(TAG, "Install GC9A01 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
#if CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));

    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_STMPE610_CONFIG(EXAMPLE_PIN_NUM_TOUCH_CS);
    // Attach the TOUCH to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
    ESP_LOGI(TAG, "Initialize touch controller STMPE610");
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_stmpe610(tp_io_handle, &tp_cfg, &tp));
#endif // CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
#endif // CONFIG_EXAMPLE_LCD_TOUCH_ENABLED

    //ESP_LOGI(TAG, "Turn on LCD backlight");
    //gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = heap_caps_malloc(EXAMPLE_LCD_H_RES * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(EXAMPLE_LCD_H_RES * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * 20);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.drv_update_cb = example_lvgl_port_update_callback;
    disp_drv.draw_buf = &disp_buf;
//    disp_drv.rotated = LV_DISP_ROT_90;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    //update the driver to rotate
    lv_disp_drv_update(disp,&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    static lv_indev_drv_t indev_drv;    // Input device driver (Touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    indev_drv.user_data = tp;

    lv_indev_drv_register(&indev_drv);
#endif

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);
    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (example_lvgl_lock(-1)) {
        new_disp(disp);
        // Release the mutex
        example_lvgl_unlock();
    }
}
