#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static WCHAR first_directory[32768],second_directory[32768];
static char first_utf8[131072],second_utf8[131072],parent_utf8[131072],home_utf8[131072];
static WCHAR *previous_cwd_variable;
static int first_created,second_created;

static int utf8(const WCHAR *source,char *result,int capacity) {
    return WideCharToMultiByte(CP_UTF8,0,source,-1,result,capacity,NULL,NULL)>0;
}
int32_t profile_native_init(void) {
    WCHAR relative[256],parent[32768],home[32768];
    if(!GetCurrentDirectoryW(32768,parent)||!utf8(parent,parent_utf8,sizeof(parent_utf8))) return 0;
    if(!GetEnvironmentVariableW(L"USERPROFILE",home,32768)||!utf8(home,home_utf8,sizeof(home_utf8))) return 0;
    if(swprintf_s(relative,256,L"build\\profile native cwd %lu %llu",
        (unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64())<0) return 0;
    if(!GetFullPathNameW(relative,32768,first_directory,NULL)||!CreateDirectoryW(first_directory,NULL)) return 0;
    first_created=1;
    if(swprintf_s(second_directory,32768,L"%ls second",first_directory)<0||!CreateDirectoryW(second_directory,NULL)) return 0;
    second_created=1;
    if(!utf8(first_directory,first_utf8,sizeof(first_utf8))||!utf8(second_directory,second_utf8,sizeof(second_utf8))) return 0;
    DWORD required=GetEnvironmentVariableW(L"ST_WIN_NATIVE_TEST_CWD",NULL,0);
    if(required) {
        previous_cwd_variable=(WCHAR*)calloc(required,sizeof(WCHAR));
        if(!previous_cwd_variable||!GetEnvironmentVariableW(L"ST_WIN_NATIVE_TEST_CWD",previous_cwd_variable,required)) return 0;
    }
    return SetEnvironmentVariableW(L"ST_WIN_NATIVE_TEST_CWD",first_directory)!=FALSE;
}
const char *profile_native_directory(int32_t which) {
    if(which==1) return first_utf8;
    if(which==2) return second_utf8;
    if(which==3) return home_utf8;
    return parent_utf8;
}
void profile_native_cleanup(void) {
    SetEnvironmentVariableW(L"ST_WIN_NATIVE_TEST_CWD",previous_cwd_variable);
    free(previous_cwd_variable); previous_cwd_variable=NULL;
    if(second_created) { RemoveDirectoryW(second_directory); second_created=0; }
    if(first_created) { RemoveDirectoryW(first_directory); first_created=0; }
}
