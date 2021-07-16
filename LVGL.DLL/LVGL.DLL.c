
#include "LVGL_API.h"
#include "../lvgl/lvgl.h"

#include <Windows.h>
#include <windowsx.h>
#include <VersionHelpers.h>

#include <stdbool.h>
#include <stdint.h>

#ifndef WIN32DRV_MONITOR_ZOOM
#define WIN32DRV_MONITOR_ZOOM 1
#endif


//////////////////////////////////////////////////////////////////////////////
// LCD Dimension
static int g_lcd_width = 640;
static int g_lcd_height = 360;


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

    WNDPROC prevWndProc = (WNDPROC)SetWindowLongPtr(g_simulator_wndhandle, GWL_WNDPROC, (LONG_PTR)&lv_win32_window_message_callback);

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

LVGLAPI int CALLBACK LVGL_DllMain(void)
{
	main(0, NULL);

	return 0;
}


