#if !defined (__LVGL_API_H__)
#define __LVGL_API_H__

#ifdef  __cplusplus
extern "C" {
#endif

#define CALLBACK __stdcall

# if defined (_WINDLL)
#   define LVGLAPI __declspec(dllexport)
# else
#   define LVGLAPI __declspec(dllimport)
# endif

LVGLAPI int  CALLBACK  HMI_DllMain(void);
LVGLAPI void CALLBACK  HMI_SetLCDHandle(HWND);
LVGLAPI int  CALLBACK  HMI_GetLCDWidth(void);
LVGLAPI int  CALLBACK  HMI_GetLCDHeight(void);

#ifdef  __cplusplus
}
#endif

#endif // __LVGL_API_H__
