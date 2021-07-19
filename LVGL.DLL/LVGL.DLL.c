
#include "LVGL_API.h"
#include "../lvgl/lvgl.h"

#include <Windows.h>
#include <windowsx.h>
#include <VersionHelpers.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef WIN32DRV_MONITOR_ZOOM
#define WIN32DRV_MONITOR_ZOOM 1
#endif


//////////////////////////////////////////////////////////////////////////////
// LCD Dimension
static int g_lcd_width = 720;
static int g_lcd_height = 405;


//////////////////////////////////////////////////////////////////////////////
// Framebuffer
HWND g_simulator_wndhandle;
HDC  g_simulator_framebuffer_dc;
static uint32_t* g_pixel_buffer = NULL;
static size_t g_pixel_buffer_size = 0;

static bool volatile g_mouse_pressed = false;
static LPARAM volatile g_mouse_value = 0;

static bool volatile g_mousewheel_pressed = false;
static int16_t volatile g_mousewheel_value = 0;

static bool volatile g_keyboard_pressed = false;
static WPARAM volatile g_keyboard_value = 0;


LVGLAPI void CALLBACK LVGL_SetLCDHandle(HWND wndHandle)
{
    g_simulator_wndhandle = wndHandle;
}

LVGLAPI int CALLBACK LVGL_GetLCDWidth(void)
{
    return g_lcd_width;
}

LVGLAPI int CALLBACK LVGL_GetLCDHeight(void)
{
    return g_lcd_height;
}

///////////////////////////////////////////////////////////////////////////////
// SDL2 implementation
// 

//#define SDL2

#define USE_MOUSE       1
#define USE_KEYBOARD    1
#define USE_MOUSEWHEEL  1

LVGLAPI int CALLBACK LVGL_DllMain(void)
{
#ifndef SDL2
    main(0, NULL);
 #else
    SDL2_main(0, NULL);
 #endif

    return 0;
}

#ifdef SDL2

#include "SDL2/include/SDL.h"

#ifndef MONITOR_ZOOM
#define MONITOR_ZOOM 1
#endif

typedef struct
{
    SDL_Window * window;
    SDL_Renderer * renderer;
    SDL_Texture * texture;
    volatile bool sdl_refr_qry;
#if MONITOR_DOUBLE_BUFFERED
    uint32_t * tft_fb_act;
#else
    uint32_t * tft_fb;
#endif
} monitor_t;

monitor_t monitor;

static volatile bool sdl_inited   = false;
static volatile bool sdl_quit_qry = false;


static bool left_button_down = false;
static int16_t last_x        = 0;
static int16_t last_y        = 0;


int quit_filter(void * userdata, SDL_Event * event)
{
    (void)userdata;

    if(event->type == SDL_WINDOWEVENT) {
        if(event->window.event == SDL_WINDOWEVENT_CLOSE) {
            sdl_quit_qry = true;
        }
    } else if(event->type == SDL_QUIT) {
        sdl_quit_qry = true;
    }

    return 1;
}


static void monitor_sdl_clean_up(void)
{
    SDL_DestroyTexture(monitor.texture);
    SDL_DestroyRenderer(monitor.renderer);
    SDL_DestroyWindow(monitor.window);

#if MONITOR_DUAL
    SDL_DestroyTexture(monitor2.texture);
    SDL_DestroyRenderer(monitor2.renderer);
    SDL_DestroyWindow(monitor2.window);

#endif

    SDL_Quit();
}


static void window_update(monitor_t * m)
{
#if MONITOR_DOUBLE_BUFFERED == 0
    SDL_UpdateTexture(m->texture, NULL, m->tft_fb, g_lcd_width * sizeof(uint32_t));
#else
    if(m->tft_fb_act == NULL) return;
    SDL_UpdateTexture(m->texture, NULL, m->tft_fb_act, MONITOR_HOR_RES * sizeof(uint32_t));
#endif
    SDL_RenderClear(m->renderer);
#if LV_COLOR_SCREEN_TRANSP
    SDL_SetRenderDrawColor(m->renderer, 0xff, 0, 0, 0xff);
    SDL_Rect r;
    r.x = 0;
    r.y = 0;
    r.w = MONITOR_HOR_RES;
    r.w = MONITOR_VER_RES;
    SDL_RenderDrawRect(m->renderer, &r);
#endif

    /*Update the renderer with the texture containing the rendered image*/
    SDL_RenderCopy(m->renderer, m->texture, NULL, NULL);
    SDL_RenderPresent(m->renderer);
}


static void monitor_sdl_refr(lv_timer_t * t)
{
    (void)t;

    /*Refresh handling*/
    if(monitor.sdl_refr_qry != false) {
        monitor.sdl_refr_qry = false;
        window_update(&monitor);
    }

#if MONITOR_DUAL
    if(monitor2.sdl_refr_qry != false) {
        monitor2.sdl_refr_qry = false;
        window_update(&monitor2);
    }
#endif
}


static void window_create(monitor_t * m)
{
#if 0
    m->window =
        SDL_CreateWindow("TFT Simulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, g_lcd_width * MONITOR_ZOOM,
                         g_lcd_height * MONITOR_ZOOM, 0); /*last param. SDL_WINDOW_BORDERLESS to hide borders*/
#else
    m->window = SDL_CreateWindowFrom(g_simulator_wndhandle);
#endif

    m->renderer = SDL_CreateRenderer(m->window, -1, SDL_RENDERER_SOFTWARE);
    m->texture =
        SDL_CreateTexture(m->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, g_lcd_width, g_lcd_height);
    SDL_SetTextureBlendMode(m->texture, SDL_BLENDMODE_BLEND);

    /*Initialize the frame buffer to gray (77 is an empirical value) */
#if MONITOR_DOUBLE_BUFFERED
    SDL_UpdateTexture(m->texture, NULL, m->tft_fb_act, MONITOR_HOR_RES * sizeof(uint32_t));
#else
    m->tft_fb = (uint32_t *)malloc(sizeof(uint32_t) * g_lcd_width * g_lcd_height);
    memset(m->tft_fb, 0x44, g_lcd_width * g_lcd_height * sizeof(uint32_t));
#endif

    m->sdl_refr_qry = true;
}


static void monitor_sdl_init(void)
{
    /*Initialize the SDL*/
    SDL_Init(SDL_INIT_VIDEO);

    SDL_SetEventFilter(quit_filter, NULL);

    window_create(&monitor);

#if MONITOR_DUAL
    window_create(&monitor2);
    int x, y;
    SDL_GetWindowPosition(monitor2.window, &x, &y);
    SDL_SetWindowPosition(monitor.window, x + (MONITOR_HOR_RES * MONITOR_ZOOM) / 2 + 10, y);
    SDL_SetWindowPosition(monitor2.window, x - (MONITOR_HOR_RES * MONITOR_ZOOM) / 2 - 10, y);
#endif

    sdl_inited = true;
}


/// <summary>
/// Mousewheel implementation
/// </summary>

static int16_t enc_diff             = 0;
static lv_indev_state_t state_mouse = LV_INDEV_STATE_RELEASED;

void mousewheel_init(void)
{
    /*Nothing to init*/
}

void mousewheel_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    (void)indev_drv; /*Unused*/

    data->state    = state_mouse;
    data->enc_diff = enc_diff;
    enc_diff       = 0;
}

void mousewheel_handler(SDL_Event * event)
{
    switch(event->type) {
        case SDL_MOUSEWHEEL:
            // Scroll down (y = -1) means positive encoder turn,
            // so invert it
#ifdef __EMSCRIPTEN__
            /*Escripten scales it wrong*/
            if(event->wheel.y < 0) enc_diff++;
            if(event->wheel.y > 0) enc_diff--;
#else
            enc_diff = -event->wheel.y;
#endif
            break;
        case SDL_MOUSEBUTTONDOWN:
            if(event->button.button == SDL_BUTTON_MIDDLE) {
                state_mouse = LV_INDEV_STATE_PRESSED;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if(event->button.button == SDL_BUTTON_MIDDLE) {
                state_mouse = LV_INDEV_STATE_RELEASED;
            }
            break;
        default: break;
    }
}


void mouse_handler(SDL_Event * event)
{
    switch(event->type) {
        case SDL_MOUSEBUTTONUP:
            if(event->button.button == SDL_BUTTON_LEFT) left_button_down = false;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if(event->button.button == SDL_BUTTON_LEFT) {
                left_button_down = true;
                last_x           = event->motion.x / MONITOR_ZOOM;
                last_y           = event->motion.y / MONITOR_ZOOM;
            }
            break;
        case SDL_MOUSEMOTION:
            last_x = event->motion.x / MONITOR_ZOOM;
            last_y = event->motion.y / MONITOR_ZOOM;
            break;

        case SDL_FINGERUP:
            left_button_down = false;
            last_x           = LV_HOR_RES * event->tfinger.x / MONITOR_ZOOM;
            last_y           = LV_VER_RES * event->tfinger.y / MONITOR_ZOOM;
            break;
        case SDL_FINGERDOWN:
            left_button_down = true;
            last_x           = LV_HOR_RES * event->tfinger.x / MONITOR_ZOOM;
            last_y           = LV_VER_RES * event->tfinger.y / MONITOR_ZOOM;
            break;
        case SDL_FINGERMOTION:
            last_x = LV_HOR_RES * event->tfinger.x / MONITOR_ZOOM;
            last_y = LV_VER_RES * event->tfinger.y / MONITOR_ZOOM;
            break;
    }
}


/// <summary>
/// Keyboard implementation
/// </summary>
static uint32_t last_key;
static lv_indev_state_t state_keyboard;

void keyboard_init(void)
{
    /*Nothing to init*/
}

static uint32_t keycode_to_ascii(uint32_t sdl_key)
{
    /*Remap some key to LV_KEY_... to manage groups*/
    switch(sdl_key) {
        case SDLK_RIGHT:
        case SDLK_KP_PLUS: return LV_KEY_RIGHT;

        case SDLK_LEFT:
        case SDLK_KP_MINUS: return LV_KEY_LEFT;

        case SDLK_UP: return LV_KEY_UP;

        case SDLK_DOWN: return LV_KEY_DOWN;

        case SDLK_ESCAPE: return LV_KEY_ESC;

#ifdef LV_KEY_BACKSPACE /*For backward compatibility*/
        case SDLK_BACKSPACE: return LV_KEY_BACKSPACE;
#endif

#ifdef LV_KEY_DEL /*For backward compatibility*/
        case SDLK_DELETE: return LV_KEY_DEL;
#endif
        case SDLK_KP_ENTER:
        case '\r': return LV_KEY_ENTER;

        case SDLK_PAGEDOWN: return LV_KEY_NEXT;

        case SDLK_PAGEUP: return LV_KEY_PREV;

        default: return sdl_key;
    }
}

void keyboard_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    (void)indev_drv; /*Unused*/
    data->state = state_keyboard;
    data->key   = keycode_to_ascii(last_key);
}


void keyboard_handler(SDL_Event * event)
{
    /* We only care about SDL_KEYDOWN and SDL_KEYUP events */
    switch(event->type) {
        case SDL_KEYDOWN:                      /*Button press*/
            last_key = event->key.keysym.sym;  /*Save the pressed key*/
            state_keyboard    = LV_INDEV_STATE_PRESSED; /*Save the key is pressed now*/
            break;
        case SDL_KEYUP:                      /*Button release*/
            state_keyboard = LV_INDEV_STATE_RELEASED; /*Save the key is released but keep the last key*/
            break;
        default: break;
    }
}


static void sdl_event_handler(lv_timer_t * t)
{
    (void)t;

    /*Refresh handling*/
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
#if USE_MOUSE != 0
        mouse_handler(&event);
#endif

#if USE_MOUSEWHEEL != 0
        mousewheel_handler(&event);
#endif

#if USE_KEYBOARD
        keyboard_handler(&event);
#endif
        if((&event)->type == SDL_WINDOWEVENT) {
            switch((&event)->window.event) {
#if SDL_VERSION_ATLEAST(2, 0, 5)
                case SDL_WINDOWEVENT_TAKE_FOCUS:
#endif
                case SDL_WINDOWEVENT_EXPOSED: window_update(&monitor);
#if MONITOR_DUAL
                    window_update(&monitor2);
#endif
                    break;
                default: break;
            }
        }
    }

    /*Run until quit event not arrives*/
    if(sdl_quit_qry) {
        monitor_sdl_clean_up();
        exit(0);
    }
}


void monitor_init(void)
{
    monitor_sdl_init();
    lv_timer_create(sdl_event_handler, 10, NULL);
}

void monitor_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    lv_coord_t hres = disp_drv->hor_res;
    lv_coord_t vres = disp_drv->ver_res;

    //    printf("x1:%d,y1:%d,x2:%d,y2:%d\n", area->x1, area->y1, area->x2, area->y2);

    /*Return if the area is out the screen*/
    if(area->x2 < 0 || area->y2 < 0 || area->x1 > hres - 1 || area->y1 > vres - 1) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

#if MONITOR_DOUBLE_BUFFERED
    monitor.tft_fb_act = (uint32_t *)color_p;
#else /*MONITOR_DOUBLE_BUFFERED*/

    int32_t y;
#if LV_COLOR_DEPTH != 24 && LV_COLOR_DEPTH != 32 /*32 is valid but support 24 for backward compatibility too*/
    int32_t x;
    for(y = area->y1; y <= area->y2 && y < disp_drv->ver_res; y++) {
        for(x = area->x1; x <= area->x2; x++) {
            monitor.tft_fb[y * disp_drv->hor_res + x] = lv_color_to32(*color_p);
            color_p++;
        }
    }
#else
    uint32_t w = lv_area_get_width(area);
    for(y = area->y1; y <= area->y2 && y < disp_drv->ver_res; y++) {
        memcpy(&monitor.tft_fb[y * g_lcd_width + area->x1], color_p, w * sizeof(lv_color_t));
        color_p += w;
    }
#endif
#endif /*MONITOR_DOUBLE_BUFFERED*/

    monitor.sdl_refr_qry = true;

    /* TYPICALLY YOU DO NOT NEED THIS
     * If it was the last part to refresh update the texture of the window.*/
    if(lv_disp_flush_is_last(disp_drv)) {
        monitor_sdl_refr(NULL);
    }

    /*IMPORTANT! It must be called to tell the system the flush is ready*/
    lv_disp_flush_ready(disp_drv);
}


static int tick_thread(void * data)
{
    (void)data;

    while(1) {
        SDL_Delay(5);
        lv_tick_inc(5); /*Tell LittelvGL that 5 milliseconds were elapsed*/
    }

    return 0;
}

void mouse_init(void)
{
}

void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    (void)indev_drv; /*Unused*/

    /*Store the collected data*/
    data->point.x = last_x;
    data->point.y = last_y;
    data->state   = left_button_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}


static void hal_init(void)
{
    /* Use the 'monitor' driver which creates window on PC's monitor to simulate a display*/
    monitor_init();
    /* Tick init.
     * You have to call 'lv_tick_inc()' in periodically to inform LittelvGL about
     * how much time were elapsed Create an SDL thread to do this*/
    SDL_CreateThread(tick_thread, "tick", NULL);

    /*Create a display buffer*/
    static lv_disp_draw_buf_t disp_buf1;

    lv_color_t * buf1_1 = (lv_color_t *)malloc(sizeof(lv_color_t) * g_lcd_width * 120);
    lv_color_t * buf1_2 = (lv_color_t *)malloc(sizeof(lv_color_t) * g_lcd_width * 120);

    lv_disp_draw_buf_init(&disp_buf1, buf1_1, buf1_2, g_lcd_width * 100);

    /*Create a display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv); /*Basic initialization*/
    disp_drv.draw_buf     = &disp_buf1;
    disp_drv.flush_cb     = monitor_flush;
    disp_drv.hor_res      = g_lcd_width;
    disp_drv.ver_res      = g_lcd_height;
    disp_drv.antialiasing = 1;

    lv_disp_t * disp = lv_disp_drv_register(&disp_drv);

    lv_theme_t * th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                            LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, th);

    lv_group_t * g = lv_group_create();
    lv_group_set_default(g);

    /* Add the mouse as input device
     * Use the 'mouse' driver which reads the PC's mouse*/
    mouse_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1); /*Basic initialization*/
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;

    /*This function will be called periodically (by the library) to get the mouse position and state*/
    indev_drv_1.read_cb      = mouse_read;
    lv_indev_t * mouse_indev = lv_indev_drv_register(&indev_drv_1);

    keyboard_init();
    static lv_indev_drv_t indev_drv_2;
    lv_indev_drv_init(&indev_drv_2); /*Basic initialization*/
    indev_drv_2.type      = LV_INDEV_TYPE_KEYPAD;
    indev_drv_2.read_cb   = keyboard_read;
    lv_indev_t * kb_indev = lv_indev_drv_register(&indev_drv_2);
    lv_indev_set_group(kb_indev, g);
    mousewheel_init();
    static lv_indev_drv_t indev_drv_3;
    lv_indev_drv_init(&indev_drv_3); /*Basic initialization*/
    indev_drv_3.type    = LV_INDEV_TYPE_ENCODER;
    indev_drv_3.read_cb = mousewheel_read;

    lv_indev_t * enc_indev = lv_indev_drv_register(&indev_drv_3);
    lv_indev_set_group(enc_indev, g);

    /*Set a cursor for the mouse*/
    LV_IMG_DECLARE(mouse_cursor_icon);                   /*Declare the image file.*/
    lv_obj_t * cursor_obj = lv_img_create(lv_scr_act()); /*Create an image object for the cursor */
    lv_img_set_src(cursor_obj, &mouse_cursor_icon);      /*Set the image source*/
    lv_indev_set_cursor(mouse_indev, cursor_obj);        /*Connect the image  object to the driver*/
}

int SDL2_main(int argc, char ** argv)
{
    (void)argc; /*Unused*/
    (void)argv; /*Unused*/

    /*Initialize LVGL*/
    lv_init();

    /*Initialize the HAL (display, input devices, tick) for LVGL*/
    hal_init();

    //  lv_example_switch_1();
    //  lv_example_calendar_1();
    //  lv_example_btnmatrix_2();
    //  lv_example_checkbox_1();
    //  lv_example_colorwheel_1();
    //  lv_example_chart_6();
    //  lv_example_table_2();
    //  lv_example_scroll_2();
    //  lv_example_textarea_1();
    //  lv_example_msgbox_1();
    //  lv_example_dropdown_2();
    //  lv_example_btn_1();
    //  lv_example_scroll_1();
    //  lv_example_tabview_1();
    //  lv_example_tabview_1();
    //  lv_example_flex_3();
    //  lv_example_label_1();

    lv_demo_widgets();

    while(1) {
        /* Periodically call the lv_task handler.
         * It could be done in a timer interrupt or an OS task too.*/
        lv_timer_handler();
        // usleep(5 * 1000);
        //Sleep(100);

        //lv_task_handler();
    }

    return 0;
}
#endif


/// <summary>
/// WIN32 implementation
/// </summary>

static void lv_win32_display_driver_flush_callback(
    lv_disp_drv_t* disp_drv,
    const lv_area_t* area,
    lv_color_t* color_p)
{
#if LV_COLOR_DEPTH == 32
    UNREFERENCED_PARAMETER(area);
    memcpy(g_pixel_buffer, color_p, g_pixel_buffer_size);
#else
    for (int y = area->y1; y <= area->y2; ++y)
    {
        for (int x = area->x1; x <= area->x2; ++x)
        {
            g_pixel_buffer[y * disp_drv->hor_res + x] = lv_color_to32(*color_p);
            color_p++;
        }
    }
#endif

    HDC hWindowDC = GetDC(g_simulator_wndhandle);
    if (hWindowDC)
    {
        StretchBlt(
            hWindowDC,
            0,
            0,
            disp_drv->hor_res * WIN32DRV_MONITOR_ZOOM,
            disp_drv->ver_res * WIN32DRV_MONITOR_ZOOM,
            g_simulator_framebuffer_dc,
            0,
            0,
            disp_drv->hor_res,
            disp_drv->ver_res,
            SRCCOPY);

        ReleaseDC(g_simulator_wndhandle, hWindowDC);
    }

    lv_disp_flush_ready(disp_drv);
}

static void lv_win32_display_driver_rounder_callback(
    lv_disp_drv_t* disp_drv,
    lv_area_t* area)
{
    area->x1 = 0;
    area->x2 = disp_drv->hor_res - 1;
    area->y1 = 0;
    area->y2 = disp_drv->ver_res - 1;
}

static void lv_win32_mouse_driver_read_callback(
    lv_indev_drv_t* indev_drv,
    lv_indev_data_t* data)
{
    UNREFERENCED_PARAMETER(indev_drv);

    data->state = (lv_indev_state_t)(
        g_mouse_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL);
    data->point.x = GET_X_LPARAM(g_mouse_value) / WIN32DRV_MONITOR_ZOOM;
    data->point.y = GET_Y_LPARAM(g_mouse_value) / WIN32DRV_MONITOR_ZOOM;
}

static void lv_win32_keyboard_driver_read_callback(
    lv_indev_drv_t* indev_drv,
    lv_indev_data_t* data)
{
    UNREFERENCED_PARAMETER(indev_drv);

    data->state = (lv_indev_state_t)(
        g_keyboard_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL);

    WPARAM KeyboardValue = g_keyboard_value;

    switch (KeyboardValue)
    {
    case VK_UP:
        data->key = LV_KEY_UP;
        break;
    case VK_DOWN:
        data->key = LV_KEY_DOWN;
        break;
    case VK_LEFT:
        data->key = LV_KEY_LEFT;
        break;
    case VK_RIGHT:
        data->key = LV_KEY_RIGHT;
        break;
    case VK_ESCAPE:
        data->key = LV_KEY_ESC;
        break;
    case VK_DELETE:
        data->key = LV_KEY_DEL;
        break;
    case VK_BACK:
        data->key = LV_KEY_BACKSPACE;
        break;
    case VK_RETURN:
        data->key = LV_KEY_ENTER;
        break;
    case VK_NEXT:
        data->key = LV_KEY_NEXT;
        break;
    case VK_PRIOR:
        data->key = LV_KEY_PREV;
        break;
    case VK_HOME:
        data->key = LV_KEY_HOME;
        break;
    case VK_END:
        data->key = LV_KEY_END;
        break;
    default:
        if (KeyboardValue >= 'A' && KeyboardValue <= 'Z')
        {
            KeyboardValue += 0x20;
        }

        data->key = (uint32_t)KeyboardValue;

        break;
    }
}

static void lv_win32_mousewheel_driver_read_callback(
    lv_indev_drv_t* indev_drv,
    lv_indev_data_t* data)
{
    UNREFERENCED_PARAMETER(indev_drv);

    data->state = (lv_indev_state_t)(
        g_mousewheel_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL);
    data->enc_diff = g_mousewheel_value;
    g_mousewheel_value = 0;
}

static HDC lv_win32_create_frame_buffer(
    HWND WindowHandle,
    LONG Width,
    LONG Height,
    UINT32** PixelBuffer,
    SIZE_T* PixelBufferSize)
{
    HDC hFrameBufferDC = NULL;

    if (PixelBuffer && PixelBufferSize)
    {
        HDC hWindowDC = GetDC(WindowHandle);
        if (hWindowDC)
        {
            hFrameBufferDC = CreateCompatibleDC(hWindowDC);
            ReleaseDC(WindowHandle, hWindowDC);
        }

        if (hFrameBufferDC)
        {
            BITMAPINFO BitmapInfo = { 0 };
            BitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            BitmapInfo.bmiHeader.biWidth = Width;
            BitmapInfo.bmiHeader.biHeight = -Height;
            BitmapInfo.bmiHeader.biPlanes = 1;
            BitmapInfo.bmiHeader.biBitCount = 32;
            BitmapInfo.bmiHeader.biCompression = BI_RGB;

            HBITMAP hBitmap = CreateDIBSection(
                hFrameBufferDC,
                &BitmapInfo,
                DIB_RGB_COLORS,
                (void**)PixelBuffer,
                NULL,
                0);
            if (hBitmap)
            {
                *PixelBufferSize = Width * Height * sizeof(UINT32);
                DeleteObject(SelectObject(hFrameBufferDC, hBitmap));
                DeleteObject(hBitmap);
            }
            else
            {
                DeleteDC(hFrameBufferDC);
                hFrameBufferDC = NULL;
            }
        }
    }

    return hFrameBufferDC;
}


static LRESULT CALLBACK lv_win32_window_message_callback(
    HWND   hWnd,
    UINT   uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    {
        g_mouse_value = lParam;
        if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP)
        {
            g_mouse_pressed = (uMsg == WM_LBUTTONDOWN);
        }
        else if (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP)
        {
            g_mousewheel_pressed = (uMsg == WM_MBUTTONDOWN);
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_KEYUP:
    {
        g_keyboard_pressed = (uMsg == WM_KEYDOWN);
        g_keyboard_value = wParam;
        break;
    }
    case WM_MOUSEWHEEL:
    {
        g_mousewheel_value = -(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
        break;
    }
    
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    return 0;
}


int main(int argc, char** argv)
{
    lv_init();

    WNDPROC prevWndProc = (WNDPROC)SetWindowLongPtr(g_simulator_wndhandle, GWLP_WNDPROC, (LONG_PTR)&lv_win32_window_message_callback);

    g_simulator_framebuffer_dc = lv_win32_create_frame_buffer(
        g_simulator_wndhandle,
        g_lcd_width,
        g_lcd_height,
        &g_pixel_buffer,
        &g_pixel_buffer_size);

#if LV_VERSION_CHECK(8, 0, 0)
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(
        &disp_buf,
        (lv_color_t*)malloc(g_lcd_width * g_lcd_height * sizeof(lv_color_t)),
        NULL,
        g_lcd_width * g_lcd_height);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = g_lcd_width;
    disp_drv.ver_res = g_lcd_height;
    disp_drv.flush_cb = lv_win32_display_driver_flush_callback;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.rounder_cb = lv_win32_display_driver_rounder_callback;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lv_win32_mouse_driver_read_callback;
    lv_indev_drv_register(&indev_drv);

    static lv_indev_drv_t kb_drv;
    lv_indev_drv_init(&kb_drv);
    kb_drv.type = LV_INDEV_TYPE_KEYPAD;
    kb_drv.read_cb = lv_win32_keyboard_driver_read_callback;
    lv_indev_drv_register(&kb_drv);

    static lv_indev_drv_t enc_drv;
    lv_indev_drv_init(&enc_drv);
    enc_drv.type = LV_INDEV_TYPE_ENCODER;
    enc_drv.read_cb = lv_win32_mousewheel_driver_read_callback;
    lv_indev_drv_register(&enc_drv);
#else
    static lv_disp_buf_t disp_buf;
    lv_disp_buf_init(
        &disp_buf,
        (lv_color_t*)malloc(hor_res * ver_res * sizeof(lv_color_t)),
        NULL,
        hor_res * ver_res);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = hor_res;
    disp_drv.ver_res = ver_res;
    disp_drv.flush_cb = lv_win32_display_driver_flush_callback;
    disp_drv.buffer = &disp_buf;
    disp_drv.rounder_cb = lv_win32_display_driver_rounder_callback;
    g_display = lv_disp_drv_register(&disp_drv);

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lv_win32_mouse_driver_read_callback;
    lv_indev_drv_register(&indev_drv);

    lv_indev_drv_t kb_drv;
    lv_indev_drv_init(&kb_drv);
    kb_drv.type = LV_INDEV_TYPE_KEYPAD;
    kb_drv.read_cb = lv_win32_keyboard_driver_read_callback;
    lv_indev_drv_register(&kb_drv);

    lv_indev_drv_t enc_drv;
    lv_indev_drv_init(&enc_drv);
    enc_drv.type = LV_INDEV_TYPE_ENCODER;
    enc_drv.read_cb = lv_win32_mousewheel_driver_read_callback;
    lv_indev_drv_register(&enc_drv);
#endif

    /*
     * Demos, benchmarks, and tests.
     *
     * Uncomment any one (and only one) of the functions below to run that
     * item.
     */

     // ----------------------------------
     // my application
     // ----------------------------------

     ///*Init freetype library
     // *Cache max 64 faces and 1 size*/
     //lv_freetype_init(64, 1, 0);

     ///*Create a font*/
     //static lv_ft_info_t info;
     //info.name = "./lv_lib_freetype/arial.ttf";
     //info.weight = 36;
     //info.style = FT_FONT_STYLE_NORMAL;
     //lv_ft_font_init(&info);

     ///*Create style with the new font*/
     //static lv_style_t style;
     //lv_style_init(&style);
     //lv_style_set_text_font(&style, info.font);

     ///*Create a label with the new style*/
     //lv_obj_t* label = lv_label_create(lv_scr_act());
     //lv_obj_add_style(label, &style, 0);
     //lv_label_set_text(label, "FreeType Arial Test");

     // ----------------------------------
     // Demos from lv_examples
     // ----------------------------------

    lv_demo_widgets();           // ok
    // lv_demo_benchmark();
    // lv_demo_keypad_encoder();    // ok
    // lv_demo_music();             // removed from repository
    // lv_demo_printer();           // removed from repository
    // lv_demo_stress();            // ok

    // ----------------------------------
    // LVGL examples
    // ----------------------------------

    /*
     * There are many examples of individual widgets found under the
     * lvgl\exampless directory.  Here are a few sample test functions.
     * Look in that directory to find all the rest.
     */

     // lv_ex_get_started_1();
     // lv_ex_get_started_2();
     // lv_ex_get_started_3();

     // lv_example_flex_1();
     // lv_example_flex_2();
     // lv_example_flex_3();
     // lv_example_flex_4();
     // lv_example_flex_5();
     // lv_example_flex_6();        // ok

     // lv_example_grid_1();
     // lv_example_grid_2();
     // lv_example_grid_3();
     // lv_example_grid_4();
     // lv_example_grid_5();
     // lv_example_grid_6();

     // lv_port_disp_template();
     // lv_port_fs_template();
     // lv_port_indev_template();

     // lv_example_scroll_1();
     // lv_example_scroll_2();
     // lv_example_scroll_3();

     // lv_example_style_1();
     // lv_example_style_2();
     // lv_example_style_3();
     // lv_example_style_4();        // ok
     // lv_example_style_6();        // file has no source code
     // lv_example_style_7();
     // lv_example_style_8();
     // lv_example_style_9();
     // lv_example_style_10();
     // lv_example_style_11();       // ok

     // ----------------------------------
     // LVGL widgets examples
     // ----------------------------------

     // lv_example_arc_1();
     // lv_example_arc_2();

     // lv_example_bar_1();          // ok
     // lv_example_bar_2();
     // lv_example_bar_3();
     // lv_example_bar_4();
     // lv_example_bar_5();
     // lv_example_bar_6();          // issues

     // lv_example_btn_1();
     // lv_example_btn_2();
     // lv_example_btn_3();

     // lv_example_btnmatrix_1();
     // lv_example_btnmatrix_2();
     // lv_example_btnmatrix_3();

     // lv_example_calendar_1();

     // lv_example_canvas_1();
     // lv_example_canvas_2();

     // lv_example_chart_1();        // ok
     // lv_example_chart_2();        // ok
     // lv_example_chart_3();        // ok
     // lv_example_chart_4();        // ok
     // lv_example_chart_5();        // ok
     // lv_example_chart_6();        // ok

     // lv_example_checkbox_1();

     // lv_example_colorwheel_1();   // ok

     // lv_example_dropdown_1();
     // lv_example_dropdown_2();
     // lv_example_dropdown_3();

     // lv_example_img_1();
     // lv_example_img_2();
     // lv_example_img_3();
     // lv_example_img_4();         // ok

     // lv_example_imgbtn_1();

     // lv_example_keyboard_1();    // ok

     // lv_example_label_1();
     // lv_example_label_2();       // ok

     // lv_example_led_1();

     // lv_example_line_1();

     // lv_example_list_1();

     // lv_example_meter_1();
     // lv_example_meter_2();
     // lv_example_meter_3();
     // lv_example_meter_4();       // ok

     // lv_example_msgbox_1();

     // lv_example_obj_1();         // ok

     // lv_example_roller_1();
     // lv_example_roller_2();      // ok

     // lv_example_slider_1();      // ok
     // lv_example_slider_2();      // issues
     // lv_example_slider_3();      // issues

     // lv_example_spinbox_1();

     // lv_example_spinner_1();     // ok

     // lv_example_switch_1();      // ok

     // lv_example_table_1();
     // lv_example_table_2();       // ok

     // lv_example_tabview_1();

     // lv_example_textarea_1();    // ok
     // lv_example_textarea_2();
     // lv_example_textarea_3();    // ok, but not all button have functions

     // lv_example_tileview_1();    // ok

     // lv_example_win_1();         // ok

     // ----------------------------------
     // Task handler loop
     // ----------------------------------

    while (1)
    {
        lv_task_handler();
    }

    return 0;
}
