/* Posts mouse messages only to this test's hidden application window. Shell
   probes are supplied at launch, never typed through the terminal UI. */
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
typedef struct Grid { int columns,rows; } Grid;
typedef struct Probe { DWORD command_id,power_id; HANDLE process; WCHAR report[32768]; Grid grid; } Probe;
typedef int (*Grid_Predicate)(const Grid *,const Grid *);

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
static DWORD child_named(DWORD parent,const WCHAR *name,const Probe *known,int count) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry={0}; entry.dwSize=sizeof(entry);
    DWORD result=0;
    if(Process32FirstW(snapshot,&entry)) do {
        if(entry.th32ParentProcessID!=parent||_wcsicmp(entry.szExeFile,name)!=0) continue;
        int seen=0;
        for(int i=0;i<count;i++) if(known[i].command_id==entry.th32ProcessID) seen=1;
        if(!seen) { result=entry.th32ProcessID; break; }
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    return result;
}
static int read_grid(Probe *probe) {
    HANDLE file=CreateFileW(probe->report,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return 0;
    char text[80]; DWORD count=0;
    BOOL ok=ReadFile(file,text,sizeof(text)-1,&count,NULL);
    CloseHandle(file);
    if(!ok||!count) return 0;
    text[count]=0;
    Grid grid={0};
    if(sscanf_s(text,"%d,%d",&grid.columns,&grid.rows)!=2||grid.columns<=0||grid.rows<=0) return 0;
    probe->grid=grid;
    return 1;
}
static int collect_probe(Probe *probes,int index,DWORD app,HANDLE process,HWND window) {
    ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<15000) {
        if(!alive(process)||app_window(app)!=window||IsWindowVisible(window)) return 0;
        Probe *probe=&probes[index];
        if(!probe->command_id) probe->command_id=child_named(app,L"cmd.exe",probes,index);
        if(probe->command_id&&!probe->power_id) {
            probe->power_id=child_named(probe->command_id,L"powershell.exe",NULL,0);
            if(probe->power_id) {
                if(swprintf_s(probe->report,32768,L"build\\divider_probe_%lu.txt",(unsigned long)probe->power_id)<=0) return 0;
                probe->process=OpenProcess(SYNCHRONIZE,FALSE,probe->command_id);
            }
        }
        if(probe->power_id&&alive(probe->process)&&read_grid(probe)) return 1;
        Sleep(25);
    }
    fprintf(stderr,"divider app test: missing probe%d cmd=%lu powershell=%lu\n",index,
        (unsigned long)probes[index].command_id,(unsigned long)probes[index].power_id);
    return 0;
}
static int same_grid(Grid a,Grid b) { return a.columns==b.columns&&a.rows==b.rows; }
static int unchanged(const Grid *current,const Grid *before) {
    for(int i=0;i<3;i++) if(!same_grid(current[i],before[i])) return 0;
    return 1;
}
static int initial_geometry(const Grid *current,const Grid *before) {
    (void)before;
    return current[0].columns>10&&current[1].columns>10&&current[2].columns>10&&
        current[0].rows>current[1].rows&&current[1].rows==current[2].rows;
}
static int wider_left(const Grid *current,const Grid *before) {
    return current[0].columns>before[0].columns&&current[1].columns<before[1].columns&&
        current[2].columns<before[2].columns&&current[0].rows==before[0].rows&&
        current[1].rows==before[1].rows&&current[2].rows==before[2].rows;
}
static int taller_upper(const Grid *current,const Grid *before) {
    return current[1].rows>before[1].rows&&current[2].rows<before[2].rows&&
        current[0].rows==before[0].rows&&current[0].columns==before[0].columns&&
        current[1].columns==before[1].columns&&current[2].columns==before[2].columns;
}
static int minimum_left(const Grid *current,const Grid *before) {
    (void)before;
    return current[0].columns>=2&&current[0].columns<=3&&current[1].columns>3&&current[2].columns>3;
}
static int minimum_right(const Grid *current,const Grid *before) {
    (void)before;
    return current[0].columns>3&&current[1].columns>=2&&current[1].columns<=3&&
        current[2].columns>=2&&current[2].columns<=3;
}
static int restored_columns(const Grid *current,const Grid *before) {
    for(int i=0;i<3;i++) if(abs(current[i].columns-before[i].columns)>1) return 0;
    return 1;
}
static int restored_geometry(const Grid *current,const Grid *before) {
    for(int i=0;i<3;i++) if(abs(current[i].columns-before[i].columns)>1||
        abs(current[i].rows-before[i].rows)>1) return 0;
    return 1;
}
static int minimum_upper(const Grid *current,const Grid *before) {
    return current[1].rows>=1&&current[1].rows<=2&&current[2].rows>2&&current[0].rows==before[0].rows;
}
static int all_larger(const Grid *current,const Grid *before) {
    for(int i=0;i<3;i++) if(current[i].columns<=before[i].columns||current[i].rows<=before[i].rows) return 0;
    return 1;
}
static int wait_grids(Probe *probes,HANDLE process,DWORD app,HWND window,const Grid *before,
        Grid_Predicate predicate,Grid *result,const char *phase) {
    ULONGLONG start=GetTickCount64();
    int consecutive=0;
    while(GetTickCount64()-start<15000) {
        if(!alive(process)||app_window(app)!=window||IsWindowVisible(window)) return 0;
        int ready=1;
        for(int i=0;i<3;i++) {
            if(!alive(probes[i].process)||!read_grid(&probes[i])) ready=0;
            result[i]=probes[i].grid;
        }
        if(ready&&predicate(result,before)) {
            if(++consecutive>=4) return 1;
        } else consecutive=0;
        Sleep(30);
    }
    fprintf(stderr,"divider app test: %s timed out; grids=%dx%d %dx%d %dx%d\n",phase,
        result[0].columns,result[0].rows,result[1].columns,result[1].rows,result[2].columns,result[2].rows);
    return 0;
}
static int stable_grids(Probe *probes,HANDLE process,DWORD app,HWND window,const Grid *expected) {
    ULONGLONG start=GetTickCount64(); int reads=0;
    while(GetTickCount64()-start<500) {
        if(!alive(process)||app_window(app)!=window||IsWindowVisible(window)) return 0;
        Grid current[3]; int ready=1;
        for(int i=0;i<3;i++) {
            if(!alive(probes[i].process)||!read_grid(&probes[i])) ready=0;
            current[i]=probes[i].grid;
        }
        if(ready) { if(!unchanged(current,expected)) return 0; reads++; }
        Sleep(25);
    }
    return reads>0;
}
static char *encode_script(const WCHAR *script) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t bytes=wcslen(script)*sizeof(WCHAR),length=4*((bytes+2)/3);
    char *encoded=(char*)malloc(length+1);
    if(!encoded) return NULL;
    const unsigned char *source=(const unsigned char*)script;
    size_t out=0;
    for(size_t i=0;i<bytes;i+=3) {
        unsigned value=(unsigned)source[i]<<16;
        if(i+1<bytes) value|=(unsigned)source[i+1]<<8;
        if(i+2<bytes) value|=source[i+2];
        encoded[out++]=alphabet[(value>>18)&63]; encoded[out++]=alphabet[(value>>12)&63];
        encoded[out++]=i+1<bytes?alphabet[(value>>6)&63]:'=';
        encoded[out++]=i+2<bytes?alphabet[value&63]:'=';
    }
    encoded[out]=0; return encoded;
}
static int write_config(const WCHAR *path,const char *script) {
    size_t capacity=strlen(script)+1024;
    char *text=(char*)malloc(capacity);
    if(!text) return 0;
    int length=sprintf_s(text,capacity,
        "return {shell='cmd',command='cmd.exe /d /q /c powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand %s',font_family='Consolas',font_size=16,columns=100,rows=30,scrollback=100,padding=12,close_on_exit=false,keybindings={new_tab='F1',split_vertical='F2',split_horizontal='F3',next_tab='F5',previous_tab='F6',close_window='F11'}}\n",script);
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    DWORD written=0;
    int ok=length>0&&file!=INVALID_HANDLE_VALUE&&WriteFile(file,text,(DWORD)length,&written,NULL)&&written==(DWORD)length;
    if(file!=INVALID_HANDLE_VALUE) { if(ok) ok=FlushFileBuffers(file)!=FALSE; CloseHandle(file); }
    free(text); return ok;
}
static int key(HWND window,UINT code) {
    return PostMessageW(window,WM_KEYDOWN,code,1|((LPARAM)MapVirtualKeyW(code,MAPVK_VK_TO_VSC)<<16))!=FALSE;
}
static int mouse(HWND window,UINT message,int x,int y) {
    WPARAM flags=message==WM_LBUTTONUP?0:MK_LBUTTON;
    return PostMessageW(window,message,flags,MAKELPARAM((SHORT)x,(SHORT)y))!=FALSE;
}
static int drag(HWND window,int x,int y,int next_x,int next_y) {
    if(!mouse(window,WM_LBUTTONDOWN,x,y)) return 0;
    Sleep(60);
    if(!mouse(window,WM_MOUSEMOVE,next_x,next_y)) return 0;
    Sleep(60);
    return mouse(window,WM_LBUTTONUP,next_x,next_y);
}

int32_t divider_app_test(void) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int success=0;
    WCHAR executable[32768],config[32768];
    WCHAR *command=NULL; char *encoded=NULL;
    HANDLE job=NULL;
    PROCESS_INFORMATION process={0};
    HWND window=NULL;
    Probe probes[4]={0};
    Grid initial[3]={0},mid[3]={0},wide[3]={0},tall[3]={0},current[3]={0},resized[3]={0};
    if(!GetFullPathNameW(L"build\\wst.exe",32768,executable,NULL)||
       !GetFullPathNameW(L"build\\divider_app_test.lua",32768,config,NULL)) return 0;
#define REQUIRE(condition,message) do { if(!(condition)) { \
    fprintf(stderr,"divider app test: %s (Windows error %lu)\n",message,(unsigned long)GetLastError()); \
    goto cleanup; } } while(0)
    const WCHAR script[]=L"$p='build/divider_probe_'+$PID+'.txt';"
        L"while ($true) {$w=[Console]::WindowWidth;$h=[Console]::WindowHeight;"
        L"[IO.File]::WriteAllText($p,\"$w,$h\");Start-Sleep -Milliseconds 60}";
    encoded=encode_script(script);
    REQUIRE(encoded&&write_config(config,encoded),"write the isolated shell probe configuration");
    job=CreateJobObjectW(NULL,NULL);
    REQUIRE(job!=NULL,"create owned test process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    REQUIRE(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"configure owned tree cleanup");
    size_t capacity=wcslen(executable)+wcslen(config)+64;
    command=(WCHAR*)calloc(capacity,sizeof(WCHAR));
    REQUIRE(command&&swprintf_s(command,capacity,L"\"%ls\" --config \"%ls\"",executable,config)>0,"format app launch");
    STARTUPINFOW startup={0}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    REQUIRE(CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&process),"launch hidden terminal");
    REQUIRE(AssignProcessToJobObject(job,process.hProcess),"assign only this application to its test job");
    REQUIRE(ResumeThread(process.hThread)!=(DWORD)-1,"resume hidden app");
    CloseHandle(process.hThread); process.hThread=NULL;
    ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<15000) {
        REQUIRE(alive(process.hProcess),"app remains running during startup");
        window=app_window(process.dwProcessId); if(window) break; Sleep(25);
    }
    REQUIRE(window&&collect_probe(probes,0,process.dwProcessId,process.hProcess,window),"initial pane starts its grid probe");
    REQUIRE(key(window,VK_F2)&&collect_probe(probes,1,process.dwProcessId,process.hProcess,window),"vertical split starts an independent grid probe");
    REQUIRE(key(window,VK_F3)&&collect_probe(probes,2,process.dwProcessId,process.hProcess,window),"nested horizontal split starts a third probe");
    // WriteAllText briefly truncates a report before writing its next sample.
    // Startup must poll coherent reports just like the later drag phases;
    // a fixed delay followed by one read can land in that empty-file interval.
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,initial,
        initial_geometry,initial,"initial nested grids"),"read coherent initial nested pane grids");
    REQUIRE(initial[0].columns>10&&initial[1].columns>10&&initial[2].columns>10&&
        initial[0].rows>initial[1].rows&&initial[1].rows==initial[2].rows,"initial split grids are usable and correctly oriented");

    RECT client;
    REQUIRE(GetClientRect(window,&client),"read own application client dimensions");
    int dpi=(int)GetDpiForWindow(window); float scale=(float)dpi/96.0f;
    int gap=6*dpi/96,gutter=12*dpi/96;
    int body_x=(int)(12*scale),body_y=(int)(48*scale);
    int body_width=(int)((int)((float)client.right/scale)-24)*scale;
    int body_height=(int)((int)((float)client.bottom/scale)-60)*scale;
    int root_x=body_x+(body_width-gap)/2;
    int horizontal_y=body_y+(body_height-gap)/2;
    int divider_y=body_y+body_height/3;
    int cell_width=(body_width-gutter)/100;
    REQUIRE(cell_width>0&&gap>=1,"derive scaled divider geometry from the configured full grid");

    // Keep the button down while checking the first update: resizing is live.
    REQUIRE(mouse(window,WM_LBUTTONDOWN,root_x+gap/2,divider_y),"press the root divider"); Sleep(60);
    int moved_x=root_x+body_width/7;
    REQUIRE(mouse(window,WM_MOUSEMOVE,moved_x+gap/2,divider_y),"drag the vertical divider while held");
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,initial,wider_left,mid,"live vertical drag"),"vertical drag updates every affected live shell");
    // The release coordinate is further right than the last move.
    root_x=moved_x+body_width/20;
    REQUIRE(mouse(window,WM_LBUTTONUP,root_x+gap/2,divider_y),"release at a new final divider position");
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,mid,wider_left,wide,"release position"),"mouse-up commits its final position even without another move");
    int right_x=root_x+gap,right_width=body_x+body_width-right_x;
    int horizontal_x=right_x+right_width/2;
    int moved_y=horizontal_y+body_height/6;
    REQUIRE(drag(window,horizontal_x,horizontal_y+gap/2,horizontal_x,moved_y+gap/2),"drag the nested horizontal divider");
    horizontal_y=moved_y;
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,wide,taller_upper,tall,"nested horizontal drag"),"horizontal drag changes only its two child heights");

    REQUIRE(drag(window,root_x+gap/2,divider_y,-2000,divider_y),"drag beyond the left client boundary");
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,tall,minimum_left,current,"left clamp"),"dragging cannot collapse the first pane below two columns");
    int minimum_width=2*cell_width+gutter;
    REQUIRE(drag(window,body_x+minimum_width+gap/2,divider_y,body_x+body_width+2000,divider_y),"drag beyond the right client boundary");
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,tall,minimum_right,current,"right clamp"),"both leaves in the right subtree retain a usable minimum width");
    REQUIRE(drag(window,body_x+body_width-gap-minimum_width+gap/2,divider_y,root_x+gap/2,divider_y),"restore the chosen root split");
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,tall,restored_columns,current,"restore vertical"),"the vertical divider can be moved away from its clamp");
    REQUIRE(drag(window,horizontal_x,horizontal_y+gap/2,horizontal_x,-2000),"drag the nested divider above the body");
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,tall,minimum_upper,current,"upper clamp"),"nested horizontal drag retains at least one row per leaf");
    int cell_height=body_height/30;
    REQUIRE(drag(window,horizontal_x,body_y+cell_height+gap/2,horizontal_x,horizontal_y+gap/2),"restore the nested split");
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,tall,restored_geometry,current,"restore horizontal"),"restored pane sizes match the chosen split ratios within cell rounding");
    for(int i=0;i<3;i++) tall[i]=current[i];

    REQUIRE(mouse(window,WM_LBUTTONDOWN,root_x+gap/2,divider_y),"begin a drag that will be canceled"); Sleep(60);
    REQUIRE(PostMessageW(window,WM_CANCELMODE,0,0),"cancel capture for this test's window"); Sleep(60);
    REQUIRE(mouse(window,WM_MOUSEMOVE,root_x-body_width/5,divider_y)&&
        mouse(window,WM_LBUTTONUP,root_x-body_width/5,divider_y),"send stale held-button motion after cancellation");
    REQUIRE(stable_grids(probes,process.hProcess,process.dwProcessId,window,tall),"canceling a drag prevents subsequent movement from resizing panes");

    REQUIRE(mouse(window,WM_LBUTTONDOWN,root_x+gap/2,divider_y),"begin a divider drag to cancel with Escape"); Sleep(60);
    // std.Gui exposes control keys as Key events without WM_CHAR text.
    // Use the same key message as a physical Escape press.
    REQUIRE(key(window,VK_ESCAPE),"post Escape to this window's Gui input path"); Sleep(60);
    REQUIRE(mouse(window,WM_MOUSEMOVE,root_x+body_width/6,divider_y)&&
        mouse(window,WM_LBUTTONUP,root_x+body_width/6,divider_y),"send stale drag motion after Escape");
    REQUIRE(stable_grids(probes,process.hProcess,process.dwProcessId,window,tall),"Escape cancels divider resizing through its Gui key event");

    RECT outer;
    REQUIRE(GetWindowRect(window,&outer)&&SetWindowPos(window,NULL,0,0,
        outer.right-outer.left+body_width/5,outer.bottom-outer.top+body_height/5,
        SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"resize only this test's outer window");
    REQUIRE(wait_grids(probes,process.hProcess,process.dwProcessId,window,tall,all_larger,resized,"outer window resize"),"all panes resize with the outer window");
    double old_width_ratio=(double)tall[0].columns/(tall[0].columns+tall[1].columns);
    double new_width_ratio=(double)resized[0].columns/(resized[0].columns+resized[1].columns);
    double old_height_ratio=(double)tall[1].rows/(tall[1].rows+tall[2].rows);
    double new_height_ratio=(double)resized[1].rows/(resized[1].rows+resized[2].rows);
    REQUIRE(abs((int)((old_width_ratio-new_width_ratio)*1000))<40&&
        abs((int)((old_height_ratio-new_height_ratio)*1000))<60,"outer resizing preserves manually chosen split proportions");
    REQUIRE(key(window,VK_F1)&&collect_probe(probes,3,process.dwProcessId,process.hProcess,window),"open another tab without replacing the three resized sessions");
    REQUIRE(key(window,VK_F6),"return to the tab with resized panes");
    REQUIRE(stable_grids(probes,process.hProcess,process.dwProcessId,window,resized),"tab switching preserves the resized pane geometry and sessions");
    REQUIRE(key(window,VK_F11)&&WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0,"close the owned application cleanly");
    DWORD exit_code=1;
    REQUIRE(GetExitCodeProcess(process.hProcess,&exit_code)&&exit_code==0,"dragged workspace exits successfully");
    success=1;
cleanup:
    if(!success&&alive(process.hProcess)) {
        if(job) TerminateJobObject(job,1);
        TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,2000);
    }
    for(int i=0;i<4;i++) {
        if(probes[i].process) CloseHandle(probes[i].process);
        if(probes[i].report[0]) DeleteFileW(probes[i].report);
    }
    if(process.hThread) CloseHandle(process.hThread);
    if(process.hProcess) CloseHandle(process.hProcess);
    if(job) CloseHandle(job);
    free(command); free(encoded); DeleteFileW(config);
    return success;
#undef REQUIRE
}
