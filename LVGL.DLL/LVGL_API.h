#if !defined (__LVGL_API_H__)
#define __LVGL_API_H__

#ifdef  __cplusplus
extern "C" {
#endif

#define CALLBACK __stdcall

# if defined (_WINDLL)
#   define HMIAPI __declspec(dllexport)
# else
#   define HMIAPI __declspec(dllimport)
# endif

HMIAPI int CALLBACK HMI_DllMain(void);
HMIAPI void CALLBACK HMI_SetLCDHandle(HWND);
HMIAPI int CALLBACK HMI_GetLCDWidth(void);
HMIAPI int CALLBACK HMI_GetLCDHeight(void);
HMIAPI int CALLBACK HMI_GetAppIcon(void);

#ifdef  __cplusplus
}
#endif

#endif // __LVGL_API_H__
