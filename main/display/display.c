/*
 * MimiClaw LCD Display Module - v2 with software rotation
 * ST7789 172x320 via SPI, with button page switching.
 * State 0: landscape sysinfo, 1: landscape image,
 * State 2: portrait sysinfo,  3: portrait image.
 * LCD always driven portrait. Landscape via pixel remapping.
 */
#include "display.h"
#include "mimi_config.h"

#if MIMI_HAS_LCD

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "wifi/wifi_manager.h"
#include "font_8x16.h"
#include "images/img_logo.h"
#include "images/img_logo_land.h"

static const char *TAG = "display";
#define LCD_W   MIMI_LCD_WIDTH
#define LCD_H   MIMI_LCD_HEIGHT
#define FBPX    (LCD_W * LCD_H)
#define FBSZ    (FBPX * sizeof(uint16_t))
#define LAND_W  LCD_H
#define LAND_H  LCD_W
#define NSTATE  4
#define C_BLK   0x0000
#define C_WHT   0xFFFF
#define C_GRN   0xE007
#define C_CYN   0x1F07
#define C_YEL   0xE0FF
#define C_RED   0x00F8
#define C_GRY   0x1084

static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t *s_fb = NULL;
static uint16_t *s_canvas = NULL;
static TaskHandle_t s_dtask = NULL;
static volatile int s_st = 0;
/* portrait pixel */
static inline void px(int x, int y, uint16_t c) {
    if(x>=0&&x<LCD_W&&y>=0&&y<LCD_H) s_fb[y*LCD_W+x]=c;
}
/* portrait char */
static void pch(int x0,int y0,char ch,uint16_t fg,uint16_t bg) {
    uint8_t i=(uint8_t)ch; if(i>127)i='?';
    const uint8_t *g=&font_8x16[i*16];
    for(int r=0;r<16;r++){uint8_t b=g[r];
    for(int c=0;c<8;c++) px(x0+c,y0+r,(b&(0x80>>c))?fg:bg);}
}
/* portrait string */
static void pstr(int x,int y,const char *s,uint16_t fg,uint16_t bg) {
    while(*s){if(x+8>LCD_W)break;pch(x,y,*s,fg,bg);x+=8;s++;}
}
/* landscape pixel: lx=0..319, ly=0..171 -> fb rotated 90 CW */
static inline void lx_px(int lx, int ly, uint16_t c) {
    int fx=(LAND_H-1)-ly, fy=lx;
    if(fx>=0&&fx<LCD_W&&fy>=0&&fy<LCD_H) s_fb[fy*LCD_W+fx]=c;
}
/* landscape char */
static void lch(int x0,int y0,char ch,uint16_t fg,uint16_t bg) {
    uint8_t i=(uint8_t)ch; if(i>127)i='?';
    const uint8_t *g=&font_8x16[i*16];
    for(int r=0;r<16;r++){uint8_t b=g[r];
    for(int c=0;c<8;c++) lx_px(x0+c,y0+r,(b&(0x80>>c))?fg:bg);}
}
/* landscape string */
static void lstr(int x,int y,const char *s,uint16_t fg,uint16_t bg) {
    while(*s){if(x+8>LAND_W)break;lch(x,y,*s,fg,bg);x+=8;s++;}
}
static void fb_clear(uint16_t c){for(int i=0;i<FBPX;i++)s_fb[i]=c;}
/* rotate canvas 320x172 -> fb 172x320 */
static void rot_to_fb(void){
    for(int cy=0;cy<LAND_H;cy++)for(int cx=0;cx<LAND_W;cx++){
        uint16_t p=s_canvas[cy*LAND_W+cx];
        s_fb[cx*LCD_W+(LAND_H-1-cy)]=p;}
}
static void blit(uint16_t *buf,int bw,int bh,int iw,int ih,const uint16_t *img){
    int ox=(bw-iw)/2,oy=(bh-ih)/2;
    for(int y=0;y<ih;y++)for(int x=0;x<iw;x++){
        int dx=ox+x,dy=oy+y;
        if(dx>=0&&dx<bw&&dy>=0&&dy<bh) buf[dy*bw+dx]=img[y*iw+x];}
}
static void flush(void){esp_lcd_panel_draw_bitmap(s_panel,0,0,LCD_W,LCD_H,s_fb);}
static void render_sys(bool land){

    char ln[42]; fb_clear(C_BLK);
    if(land){
        int y=4;
        lstr(4,y,"== MimiClaw ==",C_CYN,C_BLK);y+=20;
        if(wifi_manager_is_connected()){
            lstr(4,y,"WiFi: OK",C_GRN,C_BLK);y+=18;
            snprintf(ln,sizeof(ln),"IP: %s",wifi_manager_get_ip());
            lstr(4,y,ln,C_WHT,C_BLK);
        } else lstr(4,y,"WiFi: --",C_RED,C_BLK);
        y+=18;
        snprintf(ln,sizeof(ln),"Heap:%dK/%dK",
            (int)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024),
            (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024));
        lstr(4,y,ln,C_WHT,C_BLK);y+=18;
        int s=(int)(esp_timer_get_time()/1000000);
        snprintf(ln,sizeof(ln),"Up:%dm%ds",s/60,s%60);
        lstr(4,y,ln,C_YEL,C_BLK);y+=18;
        snprintf(ln,sizeof(ln),"P%d/%d[LAND]",s_st+1,NSTATE);
        lstr(4,y,ln,C_GRY,C_BLK);
    } else {
        int y=4;
        pstr(4,y,"== MimiClaw ==",C_CYN,C_BLK);y+=20;
        if(wifi_manager_is_connected()){
            pstr(4,y,"WiFi: OK",C_GRN,C_BLK);y+=18;
            snprintf(ln,sizeof(ln),"IP: %s",wifi_manager_get_ip());
            pstr(4,y,ln,C_WHT,C_BLK);
        } else pstr(4,y,"WiFi: --",C_RED,C_BLK);
        y+=18;
        snprintf(ln,sizeof(ln),"Heap:%dK/%dK",
            (int)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024),
            (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024));
        pstr(4,y,ln,C_WHT,C_BLK);y+=18;
        int s=(int)(esp_timer_get_time()/1000000);
        snprintf(ln,sizeof(ln),"Up:%dm%ds",s/60,s%60);
        pstr(4,y,ln,C_YEL,C_BLK);y+=18;
        snprintf(ln,sizeof(ln),"P%d/%d[PORT]",s_st+1,NSTATE);
        pstr(4,y,ln,C_GRY,C_BLK);
    }
    flush();
}
static void render_img(bool land){

    if(land){
        memset(s_canvas,0,FBSZ);
        blit(s_canvas,LAND_W,LAND_H,IMG_LOGO_LAND_W,IMG_LOGO_LAND_H,img_logo_land);
        rot_to_fb(); flush();
    } else {
        memset(s_fb,0,FBSZ);
        blit(s_fb,LCD_W,LCD_H,IMG_LOGO_W,IMG_LOGO_H,img_logo);
        flush();
    }
}
static void IRAM_ATTR btn_isr(void *arg){
    BaseType_t w=pdFALSE;
    vTaskNotifyGiveFromISR(s_dtask,&w);
    portYIELD_FROM_ISR(w);
}
static void do_render(void){

    switch(s_st){
    case 0:render_sys(true);break;
    case 1:render_img(true);break;
    case 2:render_sys(false);break;
    case 3:render_img(false);break;
    }
}
static void dtask(void *arg){
    ESP_LOGI(TAG,"dtask go");
    do_render();
    while(1){
        uint32_t n=ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5000));
        if(n>0){
            vTaskDelay(pdMS_TO_TICKS(200));
            ulTaskNotifyTake(pdTRUE,0);
            s_st=(s_st+1)%NSTATE;
            ESP_LOGI(TAG,"btn->%d",s_st);
            do_render();
        } else {
            if(s_st==0||s_st==2) do_render();
        }
    }
}
esp_err_t display_init(void){
    ESP_LOGI(TAG,"init v2 ST7789 %dx%d",LCD_W,LCD_H);
    s_fb=heap_caps_malloc(FBSZ,MALLOC_CAP_SPIRAM);
    if(!s_fb){ESP_LOGE(TAG,"fb fail");return ESP_ERR_NO_MEM;}
    s_canvas=heap_caps_malloc(FBSZ,MALLOC_CAP_SPIRAM);
    if(!s_canvas){ESP_LOGE(TAG,"cv fail");return ESP_ERR_NO_MEM;}
    memset(s_fb,0,FBSZ);memset(s_canvas,0,FBSZ);
    gpio_config_t bl={.pin_bit_mask=1ULL<<MIMI_LCD_PIN_BL,.mode=GPIO_MODE_OUTPUT};
    gpio_config(&bl);gpio_set_level(MIMI_LCD_PIN_BL,MIMI_LCD_BL_ON_LEVEL);
    spi_bus_config_t bus={.mosi_io_num=MIMI_LCD_PIN_MOSI,.miso_io_num=-1,
        .sclk_io_num=MIMI_LCD_PIN_SCLK,.quadwp_io_num=-1,.quadhd_io_num=-1,
        .max_transfer_sz=FBSZ};
    ESP_ERROR_CHECK(spi_bus_initialize(MIMI_LCD_SPI_HOST,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io=NULL;
    esp_lcd_panel_io_spi_config_t ioc={.dc_gpio_num=MIMI_LCD_PIN_DC,
        .cs_gpio_num=MIMI_LCD_PIN_CS,.pclk_hz=MIMI_LCD_SPI_FREQ_HZ,
        .lcd_cmd_bits=8,.lcd_param_bits=8,.spi_mode=0,.trans_queue_depth=10};
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(MIMI_LCD_SPI_HOST,&ioc,&io));
    esp_lcd_panel_dev_config_t pc={.reset_gpio_num=MIMI_LCD_PIN_RST,
        .rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16};
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io,&pc,&s_panel));
    esp_lcd_panel_reset(s_panel);esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel,true);
    esp_lcd_panel_set_gap(s_panel,34,0);esp_lcd_panel_disp_on_off(s_panel,true);
    gpio_config_t bt={.pin_bit_mask=1ULL<<MIMI_LCD_BTN_PIN,.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,.intr_type=GPIO_INTR_NEGEDGE};
    gpio_config(&bt);gpio_install_isr_service(0);
    gpio_isr_handler_add(MIMI_LCD_BTN_PIN,btn_isr,NULL);
    ESP_LOGI(TAG,"init v2 done");
    return ESP_OK;
}
esp_err_t display_start(void){
    xTaskCreatePinnedToCore(dtask,"display",MIMI_DISPLAY_STACK,NULL,
        MIMI_DISPLAY_PRIO,&s_dtask,MIMI_DISPLAY_CORE);
    ESP_LOGI(TAG,"started v2");
    return ESP_OK;
}
#else
esp_err_t display_init(void){return ESP_OK;}
esp_err_t display_start(void){return ESP_OK;}
#endif
