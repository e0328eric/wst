/* Runs the real GUI executable invisibly. Only a posted unbound function key
   reaches its own window; all shell commands come from the CLI or Lua files. */
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
static DWORD cmd_child(DWORD parent_id) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry={0}; entry.dwSize=sizeof(entry);
    DWORD found=0; unsigned count=0;
    if(Process32FirstW(snapshot,&entry)) do {
        if(entry.th32ParentProcessID==parent_id&&_wcsicmp(entry.szExeFile,L"cmd.exe")==0) {
            found=entry.th32ProcessID; count++;
        }
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    return count==1?found:0;
}
static int write_config(const WCHAR *path,const char *text) {
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return 0;
    DWORD length=(DWORD)strlen(text),written=0;
    BOOL ok=WriteFile(file,text,length,&written,NULL)&&written==length;
    if(ok) ok=FlushFileBuffers(file);
    CloseHandle(file); return ok!=FALSE;
}
static int alive(HANDLE process) { return WaitForSingleObject(process,0)==WAIT_TIMEOUT; }
static int read_grid(const WCHAR *path,int *columns,int *rows) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return 0;
    char text[64]; DWORD length=0;
    BOOL ok=ReadFile(file,text,sizeof(text)-1,&length,NULL);
    CloseHandle(file);
    if(!ok||!length) return 0;
    text[length]=0;
    return sscanf_s(text,"%d,%d",columns,rows)==2&&*columns>0&&*rows>0;
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
        encoded[out++]=alphabet[(value>>18)&63];
        encoded[out++]=alphabet[(value>>12)&63];
        encoded[out++]=i+1<bytes?alphabet[(value>>6)&63]:'=';
        encoded[out++]=i+2<bytes?alphabet[value&63]:'=';
    }
    encoded[out]=0;
    return encoded;
}
static int client_size(HWND window,int *width,int *height) {
    RECT client;
    if(!GetClientRect(window,&client)||client.right<=0||client.bottom<=0) return 0;
    *width=client.right; *height=client.bottom;
    return 1;
}

int32_t config_reload_app_test(void) {
    int success=0;
    WCHAR executable[32768],config[32768],replacement[32768],report[32768];
    WCHAR *command=NULL;
    char *script_command=NULL;
    char *reload_config=NULL;
    HANDLE job=NULL,child=NULL,replacement_child=NULL;
    PROCESS_INFORMATION process={0};
    HWND window=NULL;
    DWORD original_child=0;
    if(!GetFullPathNameW(L"build\\wst.exe",32768,executable,NULL)||
       !GetFullPathNameW(L"build\\config_reload_app_test.lua",32768,config,NULL)||
       !GetFullPathNameW(L"build\\config_reload_app_test.tmp",32768,replacement,NULL)||
       !GetFullPathNameW(L"build\\config_reload_app_grid.txt",32768,report,NULL)) {
        fputs("app reload test: cannot resolve test paths\n",stderr); return 0;
    }
#define REQUIRE(condition,message) do { if(!(condition)) { \
    fprintf(stderr,"app reload test: %s (Windows error %lu)\n",message,(unsigned long)GetLastError()); \
    goto cleanup; } } while(0)
    DeleteFileW(report);
    REQUIRE(write_config(config,"return {font_size=16,background='#11151c',keybindings={close_window='F11'}}\n"),"write initial valid config");
    job=CreateJobObjectW(NULL,NULL);
    REQUIRE(job!=NULL,"create owned test-process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    REQUIRE(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"configure owned-process cleanup");

    // This ordinary command is passed at process creation, never entered via
    // the UI. The child reports its actual ConPTY grid while keeping cmd alive.
    const WCHAR script[]=L"[Console]::WriteLine('Hot reload appearance probe');"
        L"while ($true) {$w=[Console]::WindowWidth;$h=[Console]::WindowHeight;"
        L"[IO.File]::WriteAllText('build/config_reload_app_grid.txt',\"$w,$h\");"
        L"Start-Sleep -Milliseconds 100}";
    script_command=encode_script(script);
    REQUIRE(script_command!=NULL,"encode grid-report command");
    size_t capacity=wcslen(executable)+wcslen(config)+strlen(script_command)+256;
    command=(WCHAR*)calloc(capacity,sizeof(WCHAR));
    REQUIRE(command!=NULL,"allocate launch command line");
    REQUIRE(swprintf_s(command,capacity,L"\"%ls\" --config \"%ls\" --command \"cmd.exe /d /q /c powershell.exe -NoLogo -NoProfile -EncodedCommand %hs\" --hold",executable,config,script_command)>0,
        "format launch command line");
    STARTUPINFOW startup={0}; startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    REQUIRE(CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&process),
        "launch current GUI executable");
    REQUIRE(AssignProcessToJobObject(job,process.hProcess),"isolate the launched process tree");
    REQUIRE(ResumeThread(process.hThread)!=(DWORD)-1,"resume hidden application");
    CloseHandle(process.hThread); process.hThread=NULL;

    int initial_columns=0,initial_rows=0,initial_width=0,initial_height=0;
    ULONGLONG started=GetTickCount64();
    while(GetTickCount64()-started<15000) {
        REQUIRE(alive(process.hProcess),"application must start and remain running");
        window=app_window(process.dwProcessId);
        original_child=cmd_child(process.dwProcessId);
        if(window&&original_child&&read_grid(report,&initial_columns,&initial_rows)&&
            client_size(window,&initial_width,&initial_height)) break;
        Sleep(25);
    }
    if(!(window&&original_child&&initial_columns>0&&initial_rows>0&&initial_width>0&&initial_height>0))
        fprintf(stderr,"app reload initial probe: window=%p child=%lu columns=%d rows=%d size=%dx%d\n",
            (void*)window,(unsigned long)original_child,initial_columns,initial_rows,initial_width,initial_height);
    REQUIRE(window&&original_child&&initial_columns>0&&initial_rows>0&&initial_width>0&&initial_height>0,
        "find the launched window and initial shell grid");
    REQUIRE(!IsWindowVisible(window),"test startup must keep the application window hidden");
    child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,original_child);
    REQUIRE(child!=NULL&&alive(child),"track the original live shell process");

    // Let an invalid edit settle. Its F12 key is not the old close binding, so
    // posting it must not terminate the application or replace its shell.
    REQUIRE(write_config(config,"return {keybindings={"),"write an invalid edit");
    Sleep(1000);
    REQUIRE(alive(process.hProcess)&&alive(child)&&cmd_child(process.dwProcessId)==original_child,
        "invalid edit must retain the original application and shell");
    REQUIRE(!IsWindowVisible(window),"background reload must not show the hidden window");
    int current_width=0,current_height=0,current_columns=0,current_rows=0;
    REQUIRE(read_grid(report,&current_columns,&current_rows)&&current_columns==initial_columns&&current_rows==initial_rows,
        "invalid save must retain the previous font-derived shell grid");
    LPARAM key_flags=1|((LPARAM)MapVirtualKeyW(VK_F12,MAPVK_VK_TO_VSC)<<16);
    REQUIRE(PostMessageW(window,WM_KEYDOWN,VK_F12,key_flags),"post unbound F12 after invalid edit");
    Sleep(250);
    REQUIRE(alive(process.hProcess)&&alive(child)&&cmd_child(process.dwProcessId)==original_child,
        "invalid edit must not activate the new close shortcut");

    // Match editors that save by replacing the file instead of writing in place.
    REQUIRE(write_config(replacement,"return {font_family='Consolas',font_size=24,foreground='#fedcba',background='#203040',accent='#62c0a0',cursor='#ffcc00',keybindings={close_window='F12'}}\n"),"write valid replacement config");
    REQUIRE(MoveFileExW(replacement,config,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"atomically replace edited config");
    // Hidden cross-process PrintWindow captures return black on this host.
    // Model/renderer tests verify theme pixels; this test proves live font/grid
    // application and session continuity while the same save changes the theme.
    int appearance_applied=0;
    started=GetTickCount64();
    while(GetTickCount64()-started<15000) {
        REQUIRE(alive(process.hProcess)&&alive(child)&&cmd_child(process.dwProcessId)==original_child,
            "appearance reload must preserve the original application and shell");
        if(read_grid(report,&current_columns,&current_rows)&&
            current_columns<initial_columns&&current_rows<initial_rows&&
            client_size(window,&current_width,&current_height)) {
            appearance_applied=1; break;
        }
        Sleep(25);
    }
    REQUIRE(appearance_applied,"font changes must update the running application's actual shell grid");
    REQUIRE(current_width==initial_width&&current_height==initial_height,"font reload must keep the window dimensions unchanged");
    REQUIRE(alive(process.hProcess)&&alive(child)&&cmd_child(process.dwProcessId)==original_child,
        "valid reload must keep the same live shell before the shortcut runs");
    REQUIRE(!IsWindowVisible(window),"hot reload must remain hidden during this test");

    // A syntactically valid candidate must roll back every setting when its
    // new command cannot launch. In particular, layout cannot commit early.
    int appearance_columns=current_columns,appearance_rows=current_rows;
    int appearance_width=current_width,appearance_height=current_height;
    REQUIRE(write_config(config,"return {shell='cmd',command='st_win_intentionally_missing.exe',columns=67,rows=17,padding=26,scrollback=6,close_on_exit=false,font_family='Consolas',font_size=24,foreground='#fedcba',background='#203040',accent='#62c0a0',cursor='#ffcc00',keybindings={close_window='F12'}}\n"),
        "write an invalid launch candidate with changed layout");
    int invalid_grid_reads=0;
    started=GetTickCount64();
    while(GetTickCount64()-started<1500) {
        REQUIRE(alive(process.hProcess)&&alive(child)&&cmd_child(process.dwProcessId)==original_child,
            "failed launch must preserve the original application and shell");
        REQUIRE(app_window(process.dwProcessId)==window&&client_size(window,&current_width,&current_height)&&
            current_width==appearance_width&&current_height==appearance_height,
            "failed launch must preserve the same window and client dimensions");
        // The probe rewrites its tiny report in place; a concurrent read may
        // see the brief empty interval between truncation and its next write.
        if(read_grid(report,&current_columns,&current_rows)) {
            REQUIRE(current_columns==appearance_columns&&current_rows==appearance_rows,
                "failed launch must preserve the previous shell grid");
            invalid_grid_reads++;
        }
        Sleep(25);
    }
    REQUIRE(invalid_grid_reads>0,"the old probe must keep reporting its retained grid after a failed launch");

    // Changing the file's launch pair now replaces the initial CLI override.
    // Keep the appearance from the previous phase while applying every other
    // live setting, including an exact cell grid and a different padding.
    size_t reload_capacity=strlen(script_command)+1024;
    reload_config=(char*)malloc(reload_capacity);
    REQUIRE(reload_config!=NULL,"allocate the replacement Lua launch candidate");
    REQUIRE(sprintf_s(reload_config,reload_capacity,
        "return {shell='cmd',command='cmd.exe /d /q /c powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand %s',columns=60,rows=12,padding=20,scrollback=6,close_on_exit=false,font_family='Consolas',font_size=24,foreground='#fedcba',background='#203040',accent='#62c0a0',cursor='#ffcc00',keybindings={close_window='F12'}}\n",script_command)>0,
        "format the valid replacement Lua launch candidate");
    REQUIRE(write_config(config,reload_config),"write valid launch, geometry, padding and scrollback changes");
    DWORD next_child=0;
    int session_replaced=0;
    started=GetTickCount64();
    while(GetTickCount64()-started<15000) {
        REQUIRE(alive(process.hProcess)&&app_window(process.dwProcessId)==window,
            "launch reload must preserve the same application process and window");
        next_child=cmd_child(process.dwProcessId);
        if(next_child&&next_child!=original_child&&!replacement_child) {
            replacement_child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,next_child);
            REQUIRE(replacement_child!=NULL,"track the replacement direct cmd child");
        }
        if(replacement_child&&alive(replacement_child)&&!alive(child)&&
            next_child!=original_child&&next_child!=0&&
            read_grid(report,&current_columns,&current_rows)&&current_columns==60&&current_rows==12) {
            session_replaced=1; break;
        }
        Sleep(25);
    }
    if(!session_replaced) fprintf(stderr,
        "app reload launch probe: old_pid=%lu next_pid=%lu old_wait=%lu new_wait=%lu grid=%dx%d app_wait=%lu\n",
        (unsigned long)original_child,(unsigned long)next_child,
        (unsigned long)WaitForSingleObject(child,0),
        (unsigned long)(replacement_child?WaitForSingleObject(replacement_child,0):WAIT_FAILED),
        current_columns,current_rows,(unsigned long)WaitForSingleObject(process.hProcess,0));
    REQUIRE(session_replaced,"valid launch reload must replace cmd, terminate its old handle and apply exact 60x12 grid");
    REQUIRE(!IsWindowVisible(window),"launch and geometry changes must keep the hidden window hidden");

    const char held_config[]="return {shell='cmd',command='cmd.exe /d /q /c exit 0',columns=60,rows=12,padding=20,scrollback=6,close_on_exit=false,font_family='Consolas',font_size=24,foreground='#fedcba',background='#203040',accent='#62c0a0',cursor='#ffcc00',keybindings={close_window='F12'}}\n";
    REQUIRE(write_config(config,held_config),"replace the probe with a child that exits while hold is enabled");
    int held=0;
    started=GetTickCount64();
    while(GetTickCount64()-started<15000) {
        REQUIRE(alive(process.hProcess)&&app_window(process.dwProcessId)==window,
            "close_on_exit=false must preserve the same application and window");
        if(!alive(replacement_child)&&cmd_child(process.dwProcessId)==0) { held=1; break; }
        Sleep(25);
    }
    REQUIRE(held,"the replacement probe must exit and the short-lived child must leave no running cmd");
    Sleep(1000);
    REQUIRE(alive(process.hProcess)&&app_window(process.dwProcessId)==window,
        "close_on_exit=false must hold the application after the child has exited");

    // Only close_on_exit changes in this final save. The already held terminal
    // must immediately adopt it without requiring another child or a shortcut.
    REQUIRE(write_config(config,"return {shell='cmd',command='cmd.exe /d /q /c exit 0',columns=60,rows=12,padding=20,scrollback=6,close_on_exit=true,font_family='Consolas',font_size=24,foreground='#fedcba',background='#203040',accent='#62c0a0',cursor='#ffcc00',keybindings={close_window='F12'}}\n"),
        "enable close_on_exit for the already held terminal");
    REQUIRE(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0,
        "reloaded close_on_exit=true must automatically close the already held application");
    DWORD exit_code=1;
    REQUIRE(GetExitCodeProcess(process.hProcess,&exit_code)&&exit_code==0,"application exits cleanly through the reloaded hold policy");
    success=1;
cleanup:
    if(!success) CopyFileW(report,L"build\\config_reload_app_grid_failed.txt",FALSE);
    // Only the process handles/job created above are terminated on failure.
    if(!success&&process.hProcess&&alive(process.hProcess)) {
        if(job) TerminateJobObject(job,1);
        TerminateProcess(process.hProcess,1);
        WaitForSingleObject(process.hProcess,2000);
    }
    if(replacement_child) CloseHandle(replacement_child);
    if(child) CloseHandle(child);
    if(process.hThread) CloseHandle(process.hThread);
    if(process.hProcess) CloseHandle(process.hProcess);
    if(job) CloseHandle(job);
    free(command);
    free(script_command);
    free(reload_config);
    DeleteFileW(replacement); DeleteFileW(config);
    DeleteFileW(report);
    return success;
#undef REQUIRE
}
