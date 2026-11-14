/* Exercise profiles through an isolated hidden application and software keys.
   Every shell command is supplied in Lua before launch, never typed into UI. */
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

typedef struct Window_Search { DWORD pid; HWND window; unsigned count; } Window_Search;
typedef struct Report { DWORD pid; char label[32]; WCHAR directory[1024]; } Report;
static BOOL CALLBACK find_window(HWND window,LPARAM parameter) {
    Window_Search *search=(Window_Search*)parameter;
    DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    if(pid==search->pid) {
        WCHAR name[64];
        if(GetClassNameW(window,name,64)&&wcscmp(name,L"GlossoPlatformWindow")==0) {
            search->window=window; search->count++;
        }
    }
    return TRUE;
}
static HWND app_window(DWORD pid) {
    Window_Search search={pid,NULL,0}; EnumWindows(find_window,(LPARAM)&search);
    return search.count==1?search.window:NULL;
}
static int alive(HANDLE process) { return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT; }
static int direct_children(DWORD parent,DWORD *ids) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W entry={0}; entry.dwSize=sizeof(entry);
    int count=0;
    if(Process32FirstW(snapshot,&entry)) do {
        if(entry.th32ParentProcessID==parent&&_wcsicmp(entry.szExeFile,L"powershell.exe")==0) {
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,entry.th32ProcessID);
            if(process) {
                if(alive(process)) { if(count<32) ids[count]=entry.th32ProcessID; count++; }
                CloseHandle(process);
            }
        }
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot); return count;
}
static int write_text(const WCHAR *path,const char *text) {
    HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return 0;
    DWORD length=(DWORD)strlen(text),written=0;
    BOOL ok=WriteFile(file,text,length,&written,NULL)&&written==length;
    if(ok) ok=FlushFileBuffers(file);
    CloseHandle(file); return ok!=FALSE;
}
static char *encoded_probe(const WCHAR *label) {
    WCHAR script[2048];
    if(swprintf_s(script,2048,
        L"[Console]::WriteLine('%ls profile');$cwd=[Environment]::CurrentDirectory;"
        L"$path=Join-Path $env:ST_WIN_PROFILE_APP_ROOT (\"$PID.txt\");"
        L"[IO.File]::WriteAllText($path,\"$PID|%ls|$cwd\");"
        L"while($true){Start-Sleep -Milliseconds 100}",label,label)<=0) return NULL;
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t bytes=wcslen(script)*sizeof(WCHAR),length=4*((bytes+2)/3);
    char *result=(char*)malloc(length+1);
    if(!result) return NULL;
    const unsigned char *source=(const unsigned char*)script; size_t out=0;
    for(size_t i=0;i<bytes;i+=3) {
        unsigned value=(unsigned)source[i]<<16;
        if(i+1<bytes) value|=(unsigned)source[i+1]<<8;
        if(i+2<bytes) value|=source[i+2];
        result[out++]=alphabet[(value>>18)&63]; result[out++]=alphabet[(value>>12)&63];
        result[out++]=i+1<bytes?alphabet[(value>>6)&63]:'=';
        result[out++]=i+2<bytes?alphabet[value&63]:'=';
    }
    result[out]=0; return result;
}
static int write_config(const WCHAR *path,const char *alpha,const char *beta,
    const char *default_profile,const char *binding_profile,int missing_command) {
    const char *prefix="\"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoLogo -NoProfile -NonInteractive -EncodedCommand ";
    size_t capacity=strlen(alpha)+strlen(beta)+4096;
    size_t beta_capacity=strlen(prefix)+strlen(beta)+64;
    char *beta_command=(char*)malloc(beta_capacity);
    char *text=(char*)malloc(capacity+strlen(prefix)+strlen(beta)+64);
    if(!text||!beta_command) { free(text); free(beta_command); return 0; }
    if(missing_command) strcpy_s(beta_command,beta_capacity,"st_win_missing_profile_app_61f9.exe");
    else sprintf_s(beta_command,beta_capacity,"%s%s",prefix,beta);
    int length=sprintf_s(text,capacity+strlen(prefix)+strlen(beta)+64,
        "return {shell='cmd',command='',font_family='Consolas',font_size=16,columns=100,rows=30,close_on_exit=false,default_profile='%s',"
        "profiles={alpha={name='Alpha Developer',command=[[%s%s]],starting_directory=[[%%ST_WIN_PROFILE_APP_ROOT%%\\Alpha Space]]},"
        "beta={name='Beta Developer',command=[[%s]],starting_directory=[[%%ST_WIN_PROFILE_APP_ROOT%%\\Beta Space]]}},"
        "keybindings={new_tab={{key='F1',profile='%s'},'F3'},split_vertical={key='F2',profile='alpha'},"
        "split_horizontal='F9',previous_tab='F6',close_window='F12'}}\n",
        default_profile,prefix,alpha,beta_command,binding_profile);
    int ok=length>0&&write_text(path,text);
    free(text); free(beta_command); return ok;
}
static int post_key(HWND window,UINT key) {
    return PostMessageW(window,WM_KEYDOWN,key,1|((LPARAM)MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16))!=FALSE;
}
static int read_report(const WCHAR *root,DWORD pid,Report *report) {
    WCHAR path[32768];
    if(swprintf_s(path,32768,L"%ls\\%lu.txt",root,(unsigned long)pid)<=0) return 0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return 0;
    char text[4096]; DWORD bytes=0;
    BOOL ok=ReadFile(file,text,sizeof(text)-1,&bytes,NULL); CloseHandle(file);
    if(!ok||!bytes) return 0;
    text[bytes]=0;
    char *start=text;
    if(bytes>=3&&(unsigned char)text[0]==0xef&&(unsigned char)text[1]==0xbb&&(unsigned char)text[2]==0xbf) start+=3;
    char *first=strchr(start,'|'); if(!first) return 0; *first++=0;
    char *second=strchr(first,'|'); if(!second) return 0; *second++=0;
    DWORD reported=(DWORD)strtoul(start,NULL,10);
    if(reported!=pid||strlen(first)>=sizeof(report->label)) return 0;
    report->pid=pid; strcpy_s(report->label,sizeof(report->label),first);
    return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,second,-1,report->directory,1024)>0;
}
static int reports_now(const WCHAR *root,DWORD parent,Report *reports) {
    DWORD ids[32]; int count=direct_children(parent,ids);
    if(count<0||count>32) return -1;
    for(int i=0;i<count;i++) if(!read_report(root,ids[i],&reports[i])) return -1;
    return count;
}
static int label_count(const Report *reports,int count,const char *label) {
    int found=0; for(int i=0;i<count;i++) if(strcmp(reports[i].label,label)==0) found++;
    return found;
}
static int wait_reports(HANDLE app,DWORD pid,HWND *window,const WCHAR *root,
    int expected,int alpha_count,int beta_count,const char *beta_label,Report *reports) {
    ULONGLONG start=GetTickCount64(); int count= -1;
    while(GetTickCount64()-start<15000) {
        if(!alive(app)) return 0;
        HWND found=app_window(pid);
        if(found) {
            if((*window&&*window!=found)||IsWindowVisible(found)) return 0;
            *window=found;
            count=reports_now(root,pid,reports);
            if(count==expected&&label_count(reports,count,"Alpha")==alpha_count&&
                label_count(reports,count,beta_label)==beta_count) return 1;
        }
        Sleep(25);
    }
    fprintf(stderr,"profile app test: expected %d reports (%d Alpha/%d %s), observed %d\n",
        expected,alpha_count,beta_count,beta_label,count);
    if(count>0) for(int i=0;i<count;i++) fprintf(stderr,"  %lu %s\n",(unsigned long)reports[i].pid,reports[i].label);
    return 0;
}
static DWORD new_pid(const Report *reports,int count,const DWORD *known,int known_count,const char *label) {
    for(int i=0;i<count;i++) if(strcmp(reports[i].label,label)==0) {
        int found=0; for(int j=0;j<known_count;j++) if(known[j]==reports[i].pid) found=1;
        if(!found) return reports[i].pid;
    }
    return 0;
}
static int directories_match(const Report *reports,int count,const WCHAR *alpha,const WCHAR *beta) {
    for(int i=0;i<count;i++) {
        const WCHAR *expected=strcmp(reports[i].label,"Alpha")==0?alpha:beta;
        if(_wcsicmp(reports[i].directory,expected)!=0) return 0;
    }
    return 1;
}
static int same_reports(const Report *reports,int count,const DWORD *ids,int expected) {
    if(count!=expected) return 0;
    for(int i=0;i<count;i++) {
        int found=0; for(int j=0;j<expected;j++) if(reports[i].pid==ids[j]) found=1;
        if(!found) return 0;
    }
    return 1;
}
static void clear_reports(const WCHAR *root) {
    WCHAR search[32768],path[32768];
    if(swprintf_s(search,32768,L"%ls\\*.txt",root)<=0) return;
    WIN32_FIND_DATAW entry; HANDLE find=FindFirstFileW(search,&entry);
    if(find==INVALID_HANDLE_VALUE) return;
    do {
        if(!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&
            swprintf_s(path,32768,L"%ls\\%ls",root,entry.cFileName)>0) DeleteFileW(path);
    } while(FindNextFileW(find,&entry));
    FindClose(find);
}

int32_t profile_app_test(void) {
    int success=0;
    WCHAR executable[32768]={0},config[32768]={0},root[32768]={0},alpha_dir[32768]={0},beta_dir[32768]={0},relative[128];
    WCHAR old_env[32768]; DWORD old_length=GetEnvironmentVariableW(L"ST_WIN_PROFILE_APP_ROOT",old_env,32768);
    int environment_set=0,root_created=0; WCHAR *command=NULL;
    char *alpha=NULL,*beta=NULL,*beta_v2=NULL;
    HANDLE job=NULL,tracked[10]={0}; DWORD known[10]={0}; int tracked_count=0;
    PROCESS_INFORMATION process={0}; HWND window=NULL; Report reports[32];
#define REQUIRE(condition,message) do { if(!(condition)) { \
    fprintf(stderr,"profile app test: %s (Windows error %lu)\n",message,(unsigned long)GetLastError()); \
    goto cleanup; } } while(0)
    REQUIRE(swprintf_s(relative,128,L"build\\profile_app_%lu_%llu",(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64())>0,"make unique owned report path");
    REQUIRE(GetFullPathNameW(L"build\\wst.exe",32768,executable,NULL)&&
        GetFullPathNameW(L"build\\profile_app_test.lua",32768,config,NULL)&&
        GetFullPathNameW(relative,32768,root,NULL),"resolve workspace test paths");
    REQUIRE(swprintf_s(alpha_dir,32768,L"%ls\\Alpha Space",root)>0&&
        swprintf_s(beta_dir,32768,L"%ls\\Beta Space",root)>0,"format profile working directories");
    REQUIRE(CreateDirectoryW(root,NULL),"create owned reports directory"); root_created=1;
    REQUIRE(CreateDirectoryW(alpha_dir,NULL)||GetLastError()==ERROR_ALREADY_EXISTS,"create Alpha working directory");
    REQUIRE(CreateDirectoryW(beta_dir,NULL)||GetLastError()==ERROR_ALREADY_EXISTS,"create Beta working directory");
    REQUIRE(SetEnvironmentVariableW(L"ST_WIN_PROFILE_APP_ROOT",root),"set temporary child-only profile root"); environment_set=1;
    alpha=encoded_probe(L"Alpha"); beta=encoded_probe(L"Beta"); beta_v2=encoded_probe(L"Beta_v2");
    REQUIRE(alpha&&beta&&beta_v2,"encode deterministic profile probes");
    REQUIRE(write_config(config,alpha,beta,"alpha","beta",0),"write initial named profiles and launch bindings");
    job=CreateJobObjectW(NULL,NULL); REQUIRE(job!=NULL,"create owned cleanup job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    REQUIRE(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"isolate profile process tree");
    size_t capacity=wcslen(executable)+wcslen(config)+64;
    command=(WCHAR*)calloc(capacity,sizeof(WCHAR)); REQUIRE(command!=NULL,"allocate app launch command");
    REQUIRE(swprintf_s(command,capacity,L"\"%ls\" --config \"%ls\"",executable,config)>0,"format app launch command");
    STARTUPINFOW startup={0}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    REQUIRE(CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,&process),"launch hidden app");
    if(old_length&&old_length<32768) SetEnvironmentVariableW(L"ST_WIN_PROFILE_APP_ROOT",old_env);
    else SetEnvironmentVariableW(L"ST_WIN_PROFILE_APP_ROOT",NULL);
    environment_set=0;
    REQUIRE(AssignProcessToJobObject(job,process.hProcess),"assign app to isolated job");
    REQUIRE(ResumeThread(process.hThread)!=(DWORD)-1,"resume hidden app"); CloseHandle(process.hThread); process.hThread=NULL;
    REQUIRE(wait_reports(process.hProcess,process.dwProcessId,&window,root,1,1,0,"Beta",reports),"default profile must launch Alpha");
    known[tracked_count]=reports[0].pid; tracked[tracked_count]=OpenProcess(SYNCHRONIZE,FALSE,known[tracked_count]); tracked_count++;
    REQUIRE(alive(tracked[0])&&directories_match(reports,1,alpha_dir,beta_dir),"startup profile expands its working directory");
    REQUIRE(post_key(window,VK_F1)&&wait_reports(process.hProcess,process.dwProcessId,&window,root,2,1,1,"Beta",reports),"profile-specific new-tab binding launches Beta");
    known[tracked_count]=new_pid(reports,2,known,tracked_count,"Beta"); tracked[tracked_count]=OpenProcess(SYNCHRONIZE,FALSE,known[tracked_count]); tracked_count++;
    REQUIRE(alive(tracked[1])&&directories_match(reports,2,alpha_dir,beta_dir),"new tab keeps its own profile directory");
    REQUIRE(post_key(window,VK_F2)&&wait_reports(process.hProcess,process.dwProcessId,&window,root,3,2,1,"Beta",reports),"profile-specific split launches Alpha beside Beta");
    known[tracked_count]=new_pid(reports,3,known,tracked_count,"Alpha"); tracked[tracked_count]=OpenProcess(SYNCHRONIZE,FALSE,known[tracked_count]); tracked_count++;
    REQUIRE(alive(tracked[2]),"track Alpha split session");

    REQUIRE(write_config(config,alpha,beta,"beta","beta",0),"change only the default profile"); Sleep(1000);
    REQUIRE(wait_reports(process.hProcess,process.dwProcessId,&window,root,3,2,1,"Beta",reports),"default change preserves existing sessions");
    REQUIRE(same_reports(reports,3,known,3),"default profile changes apply to future tabs only");
    REQUIRE(post_key(window,VK_F3)&&wait_reports(process.hProcess,process.dwProcessId,&window,root,4,2,2,"Beta",reports),"unqualified new-tab binding uses the reloaded default");
    known[tracked_count]=new_pid(reports,4,known,tracked_count,"Beta"); tracked[tracked_count]=OpenProcess(SYNCHRONIZE,FALSE,known[tracked_count]); tracked_count++;
    REQUIRE(alive(tracked[3]),"track default Beta tab");
    REQUIRE(post_key(window,VK_F6),"return to Alpha-focused split tab"); Sleep(50);
    REQUIRE(post_key(window,VK_F9)&&wait_reports(process.hProcess,process.dwProcessId,&window,root,5,3,2,"Beta",reports),"unqualified split inherits Alpha despite the Beta default");
    known[tracked_count]=new_pid(reports,5,known,tracked_count,"Alpha"); tracked[tracked_count]=OpenProcess(SYNCHRONIZE,FALSE,known[tracked_count]); tracked_count++;
    REQUIRE(alive(tracked[4])&&directories_match(reports,5,alpha_dir,beta_dir),"inherited profile retains its directory");

    REQUIRE(write_config(config,alpha,beta_v2,"beta","beta",0),"reload only Beta's launch command");
    REQUIRE(wait_reports(process.hProcess,process.dwProcessId,&window,root,5,3,2,"Beta_v2",reports),"changed profile replaces only its own sessions");
    REQUIRE(alive(tracked[0])&&alive(tracked[2])&&alive(tracked[4])&&!alive(tracked[1])&&!alive(tracked[3]),"Alpha sessions survive while both Beta handles terminate");
    DWORD current_ids[10]; for(int i=0;i<5;i++) current_ids[i]=reports[i].pid;
    for(int i=0;i<5;i++) if(strcmp(reports[i].label,"Beta_v2")==0) {
        known[tracked_count]=reports[i].pid; tracked[tracked_count]=OpenProcess(SYNCHRONIZE,FALSE,known[tracked_count]); tracked_count++;
    }
    REQUIRE(directories_match(reports,5,alpha_dir,beta_dir),"profile replacements keep the configured directories");
    REQUIRE(write_config(config,alpha,beta_v2,"beta","unknown_profile",0),"write invalid profile reference"); Sleep(1000);
    REQUIRE(reports_now(root,process.dwProcessId,reports)==5&&same_reports(reports,5,current_ids,5),"unknown profile keeps all existing sessions");
    REQUIRE(post_key(window,VK_F1)&&wait_reports(process.hProcess,process.dwProcessId,&window,root,6,3,3,"Beta_v2",reports),"invalid reference preserves the last working profile binding");
    for(int i=0;i<6;i++) current_ids[i]=reports[i].pid;
    REQUIRE(write_config(config,alpha,beta_v2,"beta","beta",1),"write a profile command that cannot launch"); Sleep(1200);
    REQUIRE(reports_now(root,process.dwProcessId,reports)==6&&same_reports(reports,6,current_ids,6),"failed profile replacement preserves every current shell");
    REQUIRE(post_key(window,VK_F3)&&wait_reports(process.hProcess,process.dwProcessId,&window,root,7,3,4,"Beta_v2",reports),"failed replacement preserves the last working default command");
    REQUIRE(post_key(window,VK_F12),"close owned app through configured shortcut");
    REQUIRE(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0,"profile app closes cleanly");
    DWORD exit_code=1; REQUIRE(GetExitCodeProcess(process.hProcess,&exit_code)&&exit_code==0,"profile app exits successfully");
    for(int i=0;i<tracked_count;i++) REQUIRE(WaitForSingleObject(tracked[i],3000)==WAIT_OBJECT_0,"closing app retires each tracked profile session");
    success=1;
cleanup:
    if(environment_set) {
        if(old_length&&old_length<32768) SetEnvironmentVariableW(L"ST_WIN_PROFILE_APP_ROOT",old_env);
        else SetEnvironmentVariableW(L"ST_WIN_PROFILE_APP_ROOT",NULL);
    }
    if(!success&&alive(process.hProcess)) {
        if(job) TerminateJobObject(job,1); TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,2000);
    }
    if(process.hThread) CloseHandle(process.hThread); if(process.hProcess) CloseHandle(process.hProcess);
    if(job) CloseHandle(job); for(int i=0;i<tracked_count;i++) if(tracked[i]) CloseHandle(tracked[i]);
    free(command); free(alpha); free(beta); free(beta_v2); DeleteFileW(config);
    if(root_created) { clear_reports(root); RemoveDirectoryW(alpha_dir); RemoveDirectoryW(beta_dir); RemoveDirectoryW(root); }
    return success;
#undef REQUIRE
}
