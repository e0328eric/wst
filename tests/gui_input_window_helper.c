/* Software messages go only to this test process's hidden std.Gui window. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdint.h>

static int owned(HWND window) {
    DWORD process=0; GetWindowThreadProcessId(window,&process);
    return window&&process==GetCurrentProcessId();
}
static LPARAM details(UINT key,int release) {
    UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC);
    LPARAM value=1|((LPARAM)(scan&255)<<16);
    if((scan&0xff00)==0xe000||key==VK_INSERT||key==VK_DELETE||key==VK_HOME||key==VK_END||
       key==VK_PRIOR||key==VK_NEXT||(key>=VK_LEFT&&key<=VK_DOWN)||key==VK_DIVIDE)
        value|=(LPARAM)1<<24;
    if(release) value|=((LPARAM)1<<30)|((LPARAM)1<<31);
    return value;
}
static int post(HWND window,UINT key,int release,int system) {
    LPARAM flags=details(key,release); if(system) flags|=(LPARAM)1<<29;
    return PostMessageW(window,system?(release?WM_SYSKEYUP:WM_SYSKEYDOWN):
        (release?WM_KEYUP:WM_KEYDOWN),(WPARAM)key,flags)!=FALSE;
}
int32_t gui_input_test_key(uintptr_t handle,int32_t key,int32_t modifiers) {
    HWND window=(HWND)handle; if(!owned(window)) return 0;
    if((modifiers&2)&&!post(window,VK_LCONTROL,0,0)) return 0;
    if((modifiers&1)&&!post(window,VK_LSHIFT,0,0)) return 0;
    if((modifiers&4)&&!post(window,VK_LMENU,0,1)) return 0;
    if(!post(window,(UINT)key,0,(modifiers&4)!=0)||!post(window,(UINT)key,1,(modifiers&4)!=0)) return 0;
    if((modifiers&4)&&!post(window,VK_LMENU,1,1)) return 0;
    if((modifiers&1)&&!post(window,VK_LSHIFT,1,0)) return 0;
    if((modifiers&2)&&!post(window,VK_LCONTROL,1,0)) return 0;
    return 1;
}
int32_t gui_input_test_unicode(uintptr_t handle,uint32_t cp) {
    HWND window=(HWND)handle; return owned(window)&&PostMessageW(window,WM_UNICHAR,cp,1);
}
int32_t gui_input_test_pointer(uintptr_t handle,int32_t kind,int32_t x,int32_t y) {
    HWND window=(HWND)handle; if(!owned(window)) return 0;
    UINT message; WPARAM buttons=0;
    switch(kind) {
    case 6:message=WM_LBUTTONDOWN;buttons=MK_LBUTTON;break;
    case 7:message=WM_MOUSEMOVE;buttons=MK_LBUTTON;break;
    case 8:message=WM_LBUTTONUP;break;
    case 10:message=WM_RBUTTONDOWN;buttons=MK_RBUTTON;break;
    case 11:message=WM_CANCELMODE;break;
    case 12:message=WM_RBUTTONUP;break;
    default:return 0;
    }
    SendMessageW(window,message,buttons,MAKELPARAM((SHORT)x,(SHORT)y)); return 1;
}
