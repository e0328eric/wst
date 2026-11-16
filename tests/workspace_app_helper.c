/* Exercise the real application with an isolated hidden process tree.
   Key and resize messages target only that application's window. */
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

typedef struct Window_Search { DWORD process_id; HWND window; unsigned count; } Window_Search;
static BOOL CALLBACK find_window(HWND window,LPARAM parameter) {
    Window_Search *search=(Window_Search*)parameter;
    DWORD process_id=0;
    GetWindowThreadProcessId(window,&process_id);
    if(process_id==search->process_id) {
        WCHAR class_name[64];
        if(GetClassNameW(window,class_name,64)&&wcscmp(class_name,L"GlossoPlatformWindow")==0) {
            search->window=window; search->count++;
        }
    }
    return TRUE;
}
static HWND app_window(DWORD process_id) {
    Window_Search search={process_id,NULL,0};
    EnumWindows(find_window,(LPARAM)&search);
    return search.count==1?search.window:NULL;
}
static int alive(HANDLE process) { return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT; }
static int cmd_children(DWORD parent_id,DWORD *ids) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W entry={0}; entry.dwSize=sizeof(entry);
    int count=0;
    if(Process32FirstW(snapshot,&entry)) do {
        if(entry.th32ParentProcessID==parent_id&&_wcsicmp(entry.szExeFile,L"cmd.exe")==0) {
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,entry.th32ProcessID);
            if(process) {
                if(alive(process)) {
                    if(count<32) ids[count]=entry.th32ProcessID;
                    count++;
                }
                CloseHandle(process);
            }
        }
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    return count;
}
static int write_config(const WCHAR *path,const char *new_tab) {
    char text[1024];
    int length=sprintf_s(text,sizeof(text),
        "return {shell='cmd',command='cmd.exe /d /q',font_family='Consolas',font_size=16,columns=100,rows=30,scrollback=100,padding=12,close_on_exit=false,keybindings={new_tab='%s',split_vertical='F2',split_horizontal='F3',close_pane='F4',next_tab='F5',previous_tab='F6',close_tab='F7',next_pane='F8'}}\n",new_tab);
    if(length<=0) return 0;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return 0;
    DWORD written=0;
    BOOL ok=WriteFile(file,text,(DWORD)length,&written,NULL)&&written==(DWORD)length;
    if(ok) ok=FlushFileBuffers(file);
    CloseHandle(file);
    return ok!=FALSE;
}
static int post_key(HWND window,UINT key) {
    LPARAM flags=1|((LPARAM)MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16);
    return PostMessageW(window,WM_KEYDOWN,key,flags)!=FALSE;
}
static int wait_children(HANDLE app,DWORD process_id,HWND window,int expected,DWORD *ids) {
    ULONGLONG start=GetTickCount64();
    int observed= -1;
    while(GetTickCount64()-start<15000) {
        if(!alive(app)||app_window(process_id)!=window||IsWindowVisible(window)) return 0;
        observed=cmd_children(process_id,ids);
        if(observed==expected) return 1;
        Sleep(20);
    }
    fprintf(stderr,"workspace app test: expected %d live direct cmd children, observed %d\n",expected,observed);
    return 0;
}
static DWORD new_child(const DWORD *ids,int count,const DWORD *known,int known_count) {
    for(int i=0;i<count;i++) {
        int found=0;
        for(int j=0;j<known_count;j++) if(ids[i]==known[j]) found=1;
        if(!found) return ids[i];
    }
    return 0;
}
static int same_children(const DWORD *ids,int count,const DWORD *known,int known_count) {
    if(count!=known_count) return 0;
    for(int i=0;i<count;i++) {
        int found=0;
        for(int j=0;j<known_count;j++) if(ids[i]==known[j]) found=1;
        if(!found) return 0;
    }
    return 1;
}

int32_t workspace_app_test(void) {
    int success=0;
    WCHAR executable[32768],config[32768];
    WCHAR *command=NULL;
    HANDLE job=NULL,children[5]={0};
    PROCESS_INFORMATION process={0};
    HWND window=NULL;
    DWORD ids[32]={0},known[5]={0};
    if(!GetFullPathNameW(L"build\\wst.exe",32768,executable,NULL)||
       !GetFullPathNameW(L"build\\workspace_app_test.lua",32768,config,NULL)) {
        fputs("workspace app test: cannot resolve test paths\n",stderr); return 0;
    }
#define REQUIRE(condition,message) do { if(!(condition)) { \
    fprintf(stderr,"workspace app test: %s (Windows error %lu)\n",message,(unsigned long)GetLastError()); \
    goto cleanup; } } while(0)
    REQUIRE(write_config(config,"F1"),"write initial tab and pane shortcuts");
    job=CreateJobObjectW(NULL,NULL);
    REQUIRE(job!=NULL,"create owned test-process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    REQUIRE(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"isolate the app and every child shell");
    size_t capacity=wcslen(executable)+wcslen(config)+64;
    command=(WCHAR*)calloc(capacity,sizeof(WCHAR));
    REQUIRE(command!=NULL,"allocate launch command");
    REQUIRE(swprintf_s(command,capacity,L"\"%ls\" --config \"%ls\"",executable,config)>0,"format app launch command");
    STARTUPINFOW startup={0}; startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    REQUIRE(CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&process),"launch hidden application");
    REQUIRE(AssignProcessToJobObject(job,process.hProcess),"assign app to its cleanup job");
    REQUIRE(ResumeThread(process.hThread)!=(DWORD)-1,"resume hidden application");
    CloseHandle(process.hThread); process.hThread=NULL;

    ULONGLONG started=GetTickCount64();
    while(GetTickCount64()-started<15000) {
        REQUIRE(alive(process.hProcess),"application must remain alive during startup");
        window=app_window(process.dwProcessId);
        if(window&&cmd_children(process.dwProcessId,ids)==1) break;
        Sleep(20);
    }
    REQUIRE(window&&cmd_children(process.dwProcessId,ids)==1&&!IsWindowVisible(window),"initial tab must own one hidden cmd session");
    known[0]=ids[0]; children[0]=OpenProcess(SYNCHRONIZE,FALSE,known[0]);
    REQUIRE(alive(children[0]),"track initial tab shell A");

    const UINT creation_keys[3]={VK_F1,VK_F2,VK_F3};
    for(int i=0;i<3;i++) {
        REQUIRE(post_key(window,creation_keys[i]),"post a configured new-tab or split action");
        REQUIRE(wait_children(process.hProcess,process.dwProcessId,window,i+2,ids),"tab/split action must create one independent live cmd child");
        known[i+1]=new_child(ids,i+2,known,i+1);
        REQUIRE(known[i+1]!=0,"created pane must have a distinct process id");
        children[i+1]=OpenProcess(SYNCHRONIZE,FALSE,known[i+1]);
        REQUIRE(alive(children[i+1]),"track the newly created pane shell");
        for(int j=0;j<=i;j++) REQUIRE(alive(children[j]),"creating tabs or panes must preserve every previous session");
    }

    // Force a swap-chain resize beyond the D3D11 texture size limit. The real
    // window stays the same size, so its replacement can render normally.
    HWND original=window;
    REQUIRE(PostMessageW(original,WM_SIZE,SIZE_RESTORED,MAKELPARAM(32767,1)),"trigger a renderer failure");
    started=GetTickCount64();
    while(GetTickCount64()-started<10000) {
        REQUIRE(alive(process.hProcess),"the application must survive the renderer failure");
        window=app_window(process.dwProcessId);
        if(window&&window!=original) break;
        Sleep(20);
    }
    REQUIRE(window&&window!=original&&!IsWindowVisible(window),"restore the hidden workspace window");
    REQUIRE(same_children(ids,cmd_children(process.dwProcessId,ids),known,4),"preserve the four original shells through recovery");

    // Remap the new-tab action without replacing any shell or restarting the
    // application. An unbound F1 is only a function-key event, not shell code.
    REQUIRE(write_config(config,"F9"),"hot reload a changed new-tab shortcut");
    Sleep(1200);
    REQUIRE(post_key(window,VK_F1),"post the released old new-tab shortcut");
    started=GetTickCount64();
    while(GetTickCount64()-started<700) {
        REQUIRE(alive(process.hProcess)&&app_window(process.dwProcessId)==window&&!IsWindowVisible(window),"reload must preserve the same hidden app/window");
        int count=cmd_children(process.dwProcessId,ids);
        REQUIRE(same_children(ids,count,known,4),"released new-tab shortcut must not create or replace any shell");
        Sleep(20);
    }
    REQUIRE(post_key(window,VK_F9),"post the new hot-reloaded new-tab shortcut");
    REQUIRE(wait_children(process.hProcess,process.dwProcessId,window,5,ids),"new shortcut must create a tab without app restart");
    known[4]=new_child(ids,5,known,4);
    REQUIRE(known[4]!=0,"new shortcut must create another independent cmd child");
    children[4]=OpenProcess(SYNCHRONIZE,FALSE,known[4]);
    REQUIRE(alive(children[4]),"track the remapped-key tab shell E");

    REQUIRE(post_key(window,VK_F7),"close the newly opened tab E");
    REQUIRE(wait_children(process.hProcess,process.dwProcessId,window,4,ids),"closing a tab must close only its shell tree");
    REQUIRE(!alive(children[4]),"close-tab must terminate E");
    for(int i=0;i<4;i++) REQUIRE(alive(children[i]),"close-tab must preserve other tabs and splits");

    // Tab2 keeps B/C/D and focuses D. Previous/next/previous must end on Tab1,
    // so closing the focused pane terminates A and removes only its empty tab.
    REQUIRE(post_key(window,VK_F6),"switch to the previous tab"); Sleep(50);
    REQUIRE(post_key(window,VK_F5),"switch to the next tab"); Sleep(50);
    REQUIRE(post_key(window,VK_F6),"switch back to the first tab"); Sleep(50);
    REQUIRE(post_key(window,VK_F4),"close the focused pane in the first tab");
    REQUIRE(wait_children(process.hProcess,process.dwProcessId,window,3,ids),"closing a tab's final pane must remove that tab");
    REQUIRE(!alive(children[0])&&alive(children[1])&&alive(children[2])&&alive(children[3]),"tab switching must direct pane-close to A only");

    REQUIRE(post_key(window,VK_F8),"cycle focus from D to the first pane B"); Sleep(50);
    REQUIRE(post_key(window,VK_F4),"close the cycled-to pane B");
    REQUIRE(wait_children(process.hProcess,process.dwProcessId,window,2,ids),"pane cycling and closing must collapse the split tree");
    REQUIRE(!alive(children[1])&&alive(children[2])&&alive(children[3]),"pane focus must close B and preserve C/D");
    REQUIRE(post_key(window,VK_F4),"close C after its sibling subtree expands");
    REQUIRE(wait_children(process.hProcess,process.dwProcessId,window,1,ids),"nested split closure must leave one live pane");
    REQUIRE(!alive(children[2])&&alive(children[3]),"closing C must preserve the surviving D pane");
    REQUIRE(post_key(window,VK_F7),"close the last tab");
    REQUIRE(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0,"closing the last tab must close the terminal window");
    DWORD exit_code=1;
    REQUIRE(GetExitCodeProcess(process.hProcess,&exit_code)&&exit_code==0,"last-tab closure must exit cleanly");
    REQUIRE(WaitForSingleObject(children[3],3000)==WAIT_OBJECT_0,"application exit must close the final shell");
    success=1;
cleanup:
    if(!success&&alive(process.hProcess)) {
        if(job) TerminateJobObject(job,1);
        TerminateProcess(process.hProcess,1);
        WaitForSingleObject(process.hProcess,2000);
    }
    for(int i=0;i<5;i++) if(children[i]) CloseHandle(children[i]);
    if(process.hThread) CloseHandle(process.hThread);
    if(process.hProcess) CloseHandle(process.hProcess);
    if(job) CloseHandle(job);
    free(command);
    DeleteFileW(config);
    return success;
#undef REQUIRE
}
