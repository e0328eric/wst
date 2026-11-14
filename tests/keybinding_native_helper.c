/* Software-only test input for this process's hidden test window. No physical
   input is injected and the user's keyboard state is never modified. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdint.h>

uintptr_t kb_test_window(void) {
    HWND previous=NULL,found=NULL;
    unsigned count=0;
    while((previous=FindWindowExW(NULL,previous,L"GlossoPlatformWindow",NULL))!=NULL) {
        DWORD process_id=0;
        GetWindowThreadProcessId(previous,&process_id);
        if(process_id==GetCurrentProcessId()) { found=previous; count++; }
    }
    return count==1?(uintptr_t)found:0;
}
static int post(HWND window,UINT key,int up,int system) {
    UINT scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC);
    LPARAM flags=1|((LPARAM)(scan&255)<<16);
    if((scan&0xff00)==0xe000||key==VK_INSERT||key==VK_DELETE||key==VK_HOME||key==VK_END||
       key==VK_PRIOR||key==VK_NEXT||(key>=VK_LEFT&&key<=VK_DOWN)||key==VK_DIVIDE)
        flags|=(LPARAM)1<<24;
    if(system) flags|=(LPARAM)1<<29;
    if(up) flags|=((LPARAM)1<<30)|((LPARAM)1<<31);
    return PostMessageW(window,system?(up?WM_SYSKEYUP:WM_SYSKEYDOWN):
        (up?WM_KEYUP:WM_KEYDOWN),key,flags)!=FALSE;
}
int32_t kb_test_post_key(uintptr_t handle,int32_t key,int32_t mods) {
    HWND window=(HWND)handle; DWORD process_id=0;
    GetWindowThreadProcessId(window,&process_id);
    if(!window||process_id!=GetCurrentProcessId()||mods<0||mods>7) return 0;
    if((mods&2)&&!post(window,VK_LCONTROL,0,0)) return 0;
    if((mods&1)&&!post(window,VK_LSHIFT,0,0)) return 0;
    if((mods&4)&&!post(window,VK_LMENU,0,1)) return 0;
    if(!post(window,(UINT)key,0,(mods&4)!=0)||!post(window,(UINT)key,1,(mods&4)!=0)) return 0;
    if((mods&4)&&!post(window,VK_LMENU,1,1)) return 0;
    if((mods&1)&&!post(window,VK_LSHIFT,1,0)) return 0;
    if((mods&2)&&!post(window,VK_LCONTROL,1,0)) return 0;
    return 1;
}
