/* Own-process software input on a private desktop; no physical user input. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PROCESS_INFORMATION child;
static HANDLE job;
static HDESK desktop;
static HWND window;
static WCHAR root[32768],config[32768];
static int root_created;
static DPI_AWARENESS_CONTEXT previous_dpi;

static int alive(HANDLE process) { return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT; }
static BOOL CALLBACK find_window(HWND candidate,LPARAM parameter) {
    (void)parameter;
    DWORD pid=0; GetWindowThreadProcessId(candidate,&pid);
    WCHAR name[64];
    if(pid==child.dwProcessId&&GetClassNameW(candidate,name,64)&&!wcscmp(name,L"GlossoPlatformWindow")) window=candidate;
    return TRUE;
}
static int write_text(const WCHAR *path,const char *text) {
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return 0;
    DWORD written=0,length=(DWORD)strlen(text);
    int ok=WriteFile(file,text,length,&written,NULL)&&written==length&&FlushFileBuffers(file);
    CloseHandle(file); return ok;
}
static char *encoded_probe(int index) {
    WCHAR text[1024];
    if(swprintf_s(text,1024,L"[Console]::WriteLine('Profile %02d');$p=Join-Path $env:WST_CHROME_TEST_ROOT ($PID.ToString()+'.txt');[IO.File]::WriteAllText($p,'P%02d');while($true){Start-Sleep -Milliseconds 100}",index,index)<=0) return NULL;
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t size=wcslen(text)*sizeof(WCHAR),length=4*((size+2)/3),at=0;
    char *result=malloc(length+1); if(!result) return NULL;
    const unsigned char *bytes=(const unsigned char*)text;
    for(size_t i=0;i<size;i+=3) {
        unsigned n=(unsigned)bytes[i]<<16;
        if(i+1<size) n|=(unsigned)bytes[i+1]<<8;
        if(i+2<size) n|=bytes[i+2];
        result[at++]=alphabet[(n>>18)&63]; result[at++]=alphabet[(n>>12)&63];
        result[at++]=i+1<size?alphabet[(n>>6)&63]:'='; result[at++]=i+2<size?alphabet[n&63]:'=';
    }
    result[at]=0; return result;
}
static int configuration(void) {
    char *text=calloc(1,32768); if(!text) return 0;
    size_t used=(size_t)sprintf_s(text,32768,"return {font_family='Consolas',font_size=16,columns=100,rows=10,close_on_exit=false,default_profile='p00',profiles={");
    for(int i=0;i<16;i++) {
        char *script=encoded_probe(i); if(!script) { free(text); return 0; }
        int n=sprintf_s(text+used,32768-used,"p%02d={name='Profile %02d',command=[[powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand %s]]},",i,i,script);
        free(script); if(n<=0) { free(text); return 0; } used+=(size_t)n;
    }
    strcpy_s(text+used,32768-used,"},keybindings={}}\n");
    int ok=write_text(config,text); free(text); return ok;
}
static int reports(const char *expected) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W entry={0}; entry.dwSize=sizeof(entry); int count=0,found=expected==NULL;
    if(Process32FirstW(snapshot,&entry)) do {
        if(entry.th32ParentProcessID!=child.dwProcessId||_wcsicmp(entry.szExeFile,L"powershell.exe")) continue;
        HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,entry.th32ProcessID);
        if(!process) continue;
        int live=alive(process); CloseHandle(process); if(!live) continue;
        WCHAR path[32768];
        if(swprintf_s(path,32768,L"%ls\\%lu.txt",root,(unsigned long)entry.th32ProcessID)<=0) { count= -1; break; }
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
        if(file==INVALID_HANDLE_VALUE) { count= -1; break; }
        char label[32]={0}; DWORD bytes=0; int ok=ReadFile(file,label,31,&bytes,NULL); CloseHandle(file);
        if(!ok||bytes==0) { count= -1; break; }
        if(expected&&!strcmp(label,expected)) found=1;
        count++;
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot); return found?count:-1;
}
void chrome_app_close(void) {
    if(alive(child.hProcess)) { if(job) TerminateJobObject(job,1); TerminateProcess(child.hProcess,1); WaitForSingleObject(child.hProcess,3000); }
    if(child.hThread) CloseHandle(child.hThread);
    if(child.hProcess) CloseHandle(child.hProcess);
    if(job) CloseHandle(job);
    if(desktop) CloseDesktop(desktop);
    memset(&child,0,sizeof(child)); job=NULL; desktop=NULL; window=NULL;
    if(root_created) {
        WCHAR pattern[32768],path[32768];
        if(swprintf_s(pattern,32768,L"%ls\\*.txt",root)>0) {
            WIN32_FIND_DATAW data; HANDLE search=FindFirstFileW(pattern,&data);
            if(search!=INVALID_HANDLE_VALUE) {
                do { if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&swprintf_s(path,32768,L"%ls\\%ls",root,data.cFileName)>0) DeleteFileW(path); } while(FindNextFileW(search,&data));
                FindClose(search);
            }
        }
        DeleteFileW(config); RemoveDirectoryW(root); root_created=0;
    }
    if(previous_dpi) { SetThreadDpiAwarenessContext(previous_dpi); previous_dpi=NULL; }
}
int32_t chrome_app_start(void) {
    WCHAR executable[32768],relative[256],name[128],command[65536],old[32768];
    DWORD old_count=GetEnvironmentVariableW(L"WST_CHROME_TEST_ROOT",old,32768); int environment_changed=0;
    chrome_app_close();
    previous_dpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(!GetFullPathNameW(L"build\\wst.exe",32768,executable,NULL)) goto failed;
    if(swprintf_s(relative,256,L"build\\chrome_app_%lu_%llu",(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64())<=0||!GetFullPathNameW(relative,32768,root,NULL)) goto failed;
    if(!CreateDirectoryW(root,NULL)) goto failed; root_created=1;
    if(swprintf_s(config,32768,L"%ls\\config.lua",root)<=0||!configuration()) goto failed;
    if(swprintf_s(name,128,L"wst-chrome-%lu-%llu",(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64())<=0) goto failed;
    // Restricted test tokens can deny private desktops. In that case keep the
    // ordinary child hidden and leave actual max/restore to the native unit test.
    desktop=CreateDesktopW(name,NULL,NULL,0,0xc7,NULL);
    job=CreateJobObjectW(NULL,NULL); if(!job) goto failed;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) goto failed;
    if(!SetEnvironmentVariableW(L"WST_CHROME_TEST_ROOT",root)) goto failed; environment_changed=1;
    if(swprintf_s(command,65536,L"\"%ls\" --config \"%ls\"",executable,config)<=0) goto failed;
    STARTUPINFOW startup={0}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE; startup.lpDesktop=desktop?name:NULL;
    if(!CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&child)) {
        if(!desktop) goto failed;
        CloseDesktop(desktop); desktop=NULL; startup.lpDesktop=NULL;
        if(!CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&child)) goto failed;
    }
    if(old_count&&old_count<32768) SetEnvironmentVariableW(L"WST_CHROME_TEST_ROOT",old); else SetEnvironmentVariableW(L"WST_CHROME_TEST_ROOT",NULL);
    environment_changed=0;
    if(!AssignProcessToJobObject(job,child.hProcess)||ResumeThread(child.hThread)==(DWORD)-1) goto failed;
    CloseHandle(child.hThread); child.hThread=NULL;
    return 1;
failed:
    fprintf(stderr,"chrome app setup failed: Windows error %lu\n",(unsigned long)GetLastError());
    if(environment_changed) { if(old_count&&old_count<32768) SetEnvironmentVariableW(L"WST_CHROME_TEST_ROOT",old); else SetEnvironmentVariableW(L"WST_CHROME_TEST_ROOT",NULL); }
    chrome_app_close(); return 0;
}
int32_t chrome_app_wait(int32_t active,int32_t tabs,int32_t shells,const char *profile) {
    WCHAR token[64],title[2048]={0},label[64];
    swprintf_s(token,64,L"wst [%d/%d]",active,tabs); swprintf_s(label,64,L"Profile %hs",profile?profile+1:"");
    ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<15000) {
        if(!alive(child.hProcess)) break;
        if(!window) { if(desktop) EnumDesktopWindows(desktop,find_window,0); else EnumWindows(find_window,0); }
        if(window&&!desktop&&IsWindowVisible(window)) { fputs("chrome app unexpectedly became visible\n",stderr); return 0; }
        if(window&&GetWindowTextW(window,title,2048)&&wcsstr(title,token)&&(!profile||wcsstr(title,label))&&reports(profile)==shells) return 1;
        Sleep(20);
    }
    fwprintf(stderr,L"chrome app state failed: expected %ls / %ls / %d shells, title=%ls\n",token,label,shells,title);
    return 0;
}
int32_t chrome_app_metrics(int32_t *width,int32_t *height,int32_t *dpi) {
    RECT rect; if(!window||!GetClientRect(window,&rect)) return 0;
    *width=rect.right; *height=rect.bottom; *dpi=(int32_t)GetDpiForWindow(window); return 1;
}
int32_t chrome_app_resize(int32_t width,int32_t height) {
    if(!window) return 0;
    RECT bounds={0,0,width,height};
    if(!AdjustWindowRectExForDpi(&bounds,(DWORD)GetWindowLongPtrW(window,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtrW(window,GWL_EXSTYLE),GetDpiForWindow(window))) return 0;
    int ok=SetWindowPos(window,NULL,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
    Sleep(120); return ok;
}
int32_t chrome_app_click(int32_t x,int32_t y) {
    LPARAM position=MAKELPARAM(x,y);
    return window&&PostMessageW(window,WM_MOUSEMOVE,0,position)&&PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,position)&&PostMessageW(window,WM_LBUTTONUP,0,position);
}
int32_t chrome_app_key(int32_t key) {
    UINT scan=MapVirtualKeyW((UINT)key,MAPVK_VK_TO_VSC); LPARAM flags=1|((LPARAM)scan<<16);
    if(key>=VK_PRIOR&&key<=VK_DOWN) flags|=(LPARAM)1<<24;
    return window&&PostMessageW(window,WM_KEYDOWN,(WPARAM)key,flags)&&PostMessageW(window,WM_KEYUP,(WPARAM)key,flags|((LPARAM)3<<30));
}
int32_t chrome_app_wheel(int32_t x,int32_t y,int32_t delta) {
    POINT point={x,y}; return window&&ClientToScreen(window,&point)&&PostMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,(WORD)delta),MAKELPARAM(point.x,point.y));
}
int32_t chrome_app_zoomed(int32_t expected) {
    ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<5000) { if(!alive(child.hProcess)) return 0; if((IsZoomed(window)!=FALSE)==(expected!=0)) { Sleep(120); return 1; } Sleep(10); }
    return 0;
}
int32_t chrome_app_private_desktop(void) { return desktop!=NULL; }
int32_t chrome_app_stable(int32_t active,int32_t tabs,int32_t shells,const char *profile) {
    Sleep(500); return chrome_app_wait(active,tabs,shells,profile);
}
int32_t chrome_app_closed(void) {
    if(!child.hProcess||WaitForSingleObject(child.hProcess,15000)!=WAIT_OBJECT_0) return 0;
    DWORD code=1; return GetExitCodeProcess(child.hProcess,&code)&&code==0;
}
