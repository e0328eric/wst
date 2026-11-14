/* Only software messages to this process's hidden test window are used. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdint.h>
static HCURSOR saved_cursor;

void pointer_test_save_cursor(void) { saved_cursor=GetCursor(); }
void pointer_test_restore_cursor(void) { SetCursor(saved_cursor); }

static int own_window(HWND window) {
    DWORD process_id=0;
    GetWindowThreadProcessId(window,&process_id);
    return process_id==GetCurrentProcessId();
}
int32_t pointer_test_current_cursor(void) {
    HCURSOR cursor=GetCursor();
    if(cursor==LoadCursorW(NULL,IDC_SIZEWE)) return 1;
    if(cursor==LoadCursorW(NULL,IDC_SIZENS)) return 2;
    return 0;
}
int32_t pointer_test_message(uintptr_t handle,int32_t kind,int32_t x,int32_t y,int32_t buttons) {
    HWND window=(HWND)handle;
    if(!own_window(window)) return -1;
    UINT message;
    switch(kind) {
    case 6: message=WM_LBUTTONDOWN; break;
    case 7: message=WM_MOUSEMOVE; break;
    case 8: message=WM_LBUTTONUP; break;
    case 11: message=WM_CANCELMODE; break;
    case 12: message=WM_KILLFOCUS; break;
    case 13:
        /* Simulate actual capture loss, including the native capture state. */
        if(GetCapture()==window) ReleaseCapture();
        else SendMessageW(window,WM_CAPTURECHANGED,0,0);
        return pointer_test_current_cursor();
    default: return -1;
    }
    HCURSOR previous=GetCursor();
    SendMessageW(window,message,(WPARAM)buttons,MAKELPARAM((SHORT)x,(SHORT)y));
    HCURSOR cursor=GetCursor();
    SetCursor(previous);
    if(cursor==LoadCursorW(NULL,IDC_SIZEWE)) return 1;
    if(cursor==LoadCursorW(NULL,IDC_SIZENS)) return 2;
    return 0;
}
int32_t pointer_test_capture(uintptr_t handle) {
    HWND window=(HWND)handle;
    return own_window(window)&&GetCapture()==window;
}
