/* Software mouse input reaches only this test's WST windows. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>

typedef int32_t (*TabPoint)(int32_t, int32_t, int32_t, int32_t, int32_t *, int32_t *);

typedef struct {
    DWORD process_id;
    HWND windows[2];
    unsigned count;
} WindowSearch;

static BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
    WindowSearch *search = (WindowSearch *)parameter;
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    WCHAR name[64];
    if (process_id == search->process_id && GetClassNameW(window, name, 64) &&
        wcscmp(name, L"GlossoPlatformWindow") == 0) {
        if (search->count < 2) search->windows[search->count] = window;
        search->count++;
    }
    return TRUE;
}

static int alive(HANDLE process) {
    return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

static int has_tabs(HWND window, int tabs) {
    WCHAR title[512], suffix[24];
    int size = GetWindowTextW(window, title, 512);
    int suffix_size = swprintf_s(suffix, 24, L"/%d]", tabs);
    return size > suffix_size && suffix_size > 0 && wcsstr(title, L" | wst [") &&
        wcscmp(title + size - suffix_size, suffix) == 0;
}

static HWND wait_windows(HDESK desktop, const PROCESS_INFORMATION *app, HWND original,
                        int original_tabs, int detached_tabs) {
    ULONGLONG start = GetTickCount64();
    WindowSearch search;
    while (alive(app->hProcess) && GetTickCount64() - start < 10000) {
        search = (WindowSearch){.process_id = app->dwProcessId};
        if (desktop) EnumDesktopWindows(desktop, find_window, (LPARAM)&search);
        else EnumWindows(find_window, (LPARAM)&search);
        if (search.count == 1 && !detached_tabs && (!original || search.windows[0] == original) &&
            has_tabs(search.windows[0], original_tabs)) return search.windows[0];
        if (search.count == 2 && detached_tabs && original) {
            HWND other = search.windows[0] == original ? search.windows[1] : search.windows[0];
            if (other != original && (search.windows[0] == original || search.windows[1] == original) &&
                has_tabs(original, original_tabs) && has_tabs(other, detached_tabs)) return other;
        }
        Sleep(20);
    }
    search = (WindowSearch){.process_id = app->dwProcessId};
    if (desktop) EnumDesktopWindows(desktop, find_window, (LPARAM)&search);
    else EnumWindows(find_window, (LPARAM)&search);
    DWORD code = 0;
    GetExitCodeProcess(app->hProcess, &code);
    fprintf(stderr, "tab drag application state: %u windows, process status %lu\n", search.count, (unsigned long)code);
    for (unsigned i = 0; i < search.count && i < 2; i++) {
        WCHAR title[512] = {0};
        GetWindowTextW(search.windows[i], title, 512);
        fprintf(stderr, "WST window %u: %ls\n", i + 1, title);
    }
    return NULL;
}

static int shell_pids(DWORD parent, DWORD ids[3]) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    PROCESSENTRY32W entry = {.dwSize = sizeof(entry)};
    int count = 0;
    if (Process32FirstW(snapshot, &entry)) do {
        if (entry.th32ParentProcessID != parent || _wcsicmp(entry.szExeFile, L"cmd.exe")) continue;
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (alive(process)) {
            if (count < 3) ids[count] = entry.th32ProcessID;
            count++;
        }
        if (process) CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return count;
}

static HANDLE wait_new_shell(const PROCESS_INFORMATION *app, int previous, DWORD ids[3]) {
    ULONGLONG start = GetTickCount64();
    while (alive(app->hProcess) && GetTickCount64() - start < 10000) {
        DWORD current[3] = {0};
        if (shell_pids(app->dwProcessId, current) == previous + 1) {
            for (int i = 0; i <= previous; i++) {
                int found = 0;
                for (int j = 0; j < previous; j++) if (ids[j] == current[i]) found = 1;
                if (!found) {
                    ids[previous] = current[i];
                    return OpenProcess(SYNCHRONIZE, FALSE, ids[previous]);
                }
            }
        }
        Sleep(20);
    }
    return NULL;
}

static int same_shells(const PROCESS_INFORMATION *app, const HANDLE shells[3]) {
    DWORD current[3];
    return shell_pids(app->dwProcessId, current) == 3 &&
        alive(shells[0]) && alive(shells[1]) && alive(shells[2]);
}

static int write_config(const WCHAR *path) {
    const char config[] = "return {shell='cmd',command='cmd.exe /d /q',font_family='Consolas',"
        "font_size=16,columns=100,rows=30,close_on_exit=false,"
        "keybindings={new_tab='F1',split_vertical='F2',close_pane='F4'}}\n";
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(file, config, sizeof(config) - 1, &written, NULL) && written == sizeof(config) - 1;
    CloseHandle(file);
    if (!ok) DeleteFileW(path);
    return ok;
}

static int key(HWND window, UINT code) {
    LPARAM flags = 1 | ((LPARAM)MapVirtualKeyW(code, MAPVK_VK_TO_VSC) << 16);
    return PostMessageW(window, WM_KEYDOWN, code, flags) &&
        PostMessageW(window, WM_KEYUP, code, flags | ((LPARAM)3 << 30));
}

static int mouse(HWND window, UINT message, POINT point) {
    WPARAM flags = message == WM_LBUTTONUP ? 0 : MK_LBUTTON;
    return PostMessageW(window, message, flags, MAKELPARAM((SHORT)point.x, (SHORT)point.y)) != FALSE;
}

static int drag(HWND window, POINT start, POINT end) {
    return mouse(window, WM_LBUTTONDOWN, start) && mouse(window, WM_MOUSEMOVE, end) &&
        mouse(window, WM_LBUTTONUP, end);
}

static int get_tab_point(TabPoint geometry, HWND window, int count, int index, POINT *point) {
    RECT client;
    int32_t x, y;
    if (!GetClientRect(window, &client) || !geometry(client.right, GetDpiForWindow(window), count, index, &x, &y))
        return 0;
    *point = (POINT){x, y};
    return 1;
}

typedef struct {
    HWND window;
    TabPoint geometry;
    POINT point;
    LONG_PTR old_style;
    int clipped;
} MergeTarget;

static void hide_merge_target(MergeTarget *target) {
    if (!target->clipped || !IsWindow(target->window)) return;
    SetWindowPos(target->window, HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);
    SetWindowRgn(target->window, NULL, FALSE);
    SetWindowLongPtrW(target->window, GWL_EXSTYLE, target->old_style);
    target->clipped = 0;
}

static BOOL CALLBACK expose_target(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM parameter) {
    (void)dc;
    (void)rect;
    MergeTarget *target = (MergeTarget *)parameter;
    MONITORINFO info = {.cbSize = sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    RECT work = info.rcWork;
    int width = (work.right - work.left) / 2;
    int height = (work.bottom - work.top) / 2;
    if (!SetWindowPos(target->window, NULL, work.left + 32, work.top + 48, width, height,
            SWP_NOACTIVATE | SWP_NOZORDER)) return TRUE;
    POINT point;
    RECT bounds;
    if (!get_tab_point(target->geometry, target->window, 1, 0, &point) ||
        !ClientToScreen(target->window, &point) || !GetWindowRect(target->window, &bounds)) return TRUE;
    POINT cursor;
    if (GetCursorPos(&cursor) && cursor.x >= point.x - 16 && cursor.x <= point.x + 16 &&
        cursor.y >= point.y - 16 && cursor.y <= point.y + 16) return TRUE;
    int x = point.x - bounds.left, y = point.y - bounds.top;
    HRGN clip = CreateRectRgn(x - 1, y - 1, x + 2, y + 2);
    if (!clip) return TRUE;
    if (!SetWindowRgn(target->window, clip, FALSE)) {
        DeleteObject(clip);
        return TRUE;
    }
    target->clipped = 1;
    target->old_style = GetWindowLongPtrW(target->window, GWL_EXSTYLE);
    SetWindowLongPtrW(target->window, GWL_EXSTYLE, target->old_style | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    // WindowFromPoint ignores hidden windows. Expose only the drop pixel and
    // its immediate neighbors, with no activation or taskbar entry.
    if (SetWindowPos(target->window, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER) &&
        WindowFromPoint(point) == target->window) {
        target->point = point;
        return FALSE;
    }
    hide_merge_target(target);
    return TRUE;
}

int32_t wst_tab_drag_app_test(TabPoint geometry) {
    int success = 0, config_created = 0;
    DPI_AWARENESS_CONTEXT dpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WCHAR desktop_name[128], config[32768], relative[128], executable[32768], command[65536];
    HDESK desktop = NULL;
    HANDLE job = NULL, shells[3] = {0};
    PROCESS_INFORMATION app = {0};
    MergeTarget target = {.geometry = geometry};
    DWORD ids[3] = {0};
    DWORD process_id = GetCurrentProcessId();
    ULONGLONG unique = GetTickCount64();
#define REQUIRE(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "tab drag application test: %s (Windows error %lu)\n", message, (unsigned long)GetLastError()); \
    goto cleanup; \
} } while (0)
    REQUIRE(swprintf_s(desktop_name, 128, L"wst-tab-drag-%lu-%llu", (unsigned long)process_id, unique) > 0,
        "name the private desktop");
    desktop = CreateDesktopW(desktop_name, NULL, NULL, 0,
        DESKTOP_READOBJECTS | DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_ENUMERATE | DESKTOP_WRITEOBJECTS,
        NULL);
    REQUIRE(desktop || GetLastError() == ERROR_ACCESS_DENIED || GetLastError() == ERROR_PRIVILEGE_NOT_HELD,
        "create a private desktop or fall back on the current desktop");
    REQUIRE(swprintf_s(relative, 128, L"build\\tab_drag_%lu_%llu.lua", (unsigned long)process_id, unique) > 0 &&
        GetFullPathNameW(relative, 32768, config, NULL) && GetFullPathNameW(L"build\\wst.exe", 32768, executable, NULL),
        "resolve isolated application and config paths");
    REQUIRE(write_config(config), "write the test configuration");
    config_created = 1;
    REQUIRE(swprintf_s(command, 65536, L"\"%ls\" --config \"%ls\"", executable, config) > 0,
        "format the test application command line");
    job = CreateJobObjectW(NULL, NULL);
    REQUIRE(job, "create an isolated process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    REQUIRE(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
        "arrange cleanup for the application and shells");
    STARTUPINFOW startup = {.cb = sizeof(startup), .lpDesktop = desktop ? desktop_name : NULL};
    if (!desktop) {
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }
    REQUIRE(CreateProcessW(executable, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup, &app),
        "launch WST on the private desktop");
    REQUIRE(AssignProcessToJobObject(job, app.hProcess) && ResumeThread(app.hThread) != (DWORD)-1,
        "start the isolated application");

    HWND original = wait_windows(desktop, &app, NULL, 1, 0);
    REQUIRE(original, "initial WST window has one tab");
    REQUIRE(!!IsWindowVisible(original) == !!desktop, "initial visibility matches the test desktop");
    shells[0] = wait_new_shell(&app, 0, ids);
    REQUIRE(alive(shells[0]), "track the original tab shell");
    REQUIRE(key(original, VK_F1), "open the tab to move");
    shells[1] = wait_new_shell(&app, 1, ids);
    REQUIRE(alive(shells[1]), "track the new tab shell");
    REQUIRE(key(original, VK_F2), "split the new tab");
    shells[2] = wait_new_shell(&app, 2, ids);
    REQUIRE(alive(shells[2]) && wait_windows(desktop, &app, original, 2, 0), "track the focused split shell");

    RECT work;
    REQUIRE(SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0), "read the desktop work area");
    int tile_width = (work.right - work.left) / 2 - 32;
    int tile_height = (work.bottom - work.top) / 2;
    int right_x = work.right - tile_width - 16;
    int top_y = work.top + 48;
    UINT position_flags = SWP_NOACTIVATE | SWP_NOZORDER;
    REQUIRE(SetWindowPos(original, NULL, work.left + 16, top_y, tile_width, tile_height, position_flags),
        "place the original test window on the left");

    POINT from, outside = {right_x, top_y + 60};
    REQUIRE(get_tab_point(geometry, original, 2, 1, &from) && ScreenToClient(original, &outside),
        "locate the split tab and a release point outside its window");
    REQUIRE(drag(original, from, outside), "drag the split tab to the desktop");
    HWND detached = wait_windows(desktop, &app, original, 1, 1);
    REQUIRE(detached && same_shells(&app, shells), "tear-off creates a window and preserves all three shells");
    REQUIRE(!!IsWindowVisible(detached) == !!desktop, "the detached window inherits the original visibility");

    REQUIRE(SetWindowPos(detached, NULL, right_x, top_y, tile_width, tile_height, position_flags),
        "place the detached test window on the right");
    POINT destination;
    if (desktop) {
        REQUIRE(get_tab_point(geometry, original, 1, 0, &destination) && ClientToScreen(original, &destination),
            "locate the original tab strip");
    } else {
        target.window = original;
        EnumDisplayMonitors(NULL, NULL, expose_target, (LPARAM)&target);
        if (!target.clipped) {
            REQUIRE(PostMessageW(detached, WM_CLOSE, 0, 0) && PostMessageW(original, WM_CLOSE, 0, 0) &&
                WaitForSingleObject(app.hProcess, 10000) == WAIT_OBJECT_0, "close both hidden test windows");
            success = 2;
            goto cleanup;
        }
        destination = target.point;
    }
    REQUIRE(get_tab_point(geometry, detached, 1, 0, &from) && ScreenToClient(detached, &destination),
        "locate the original tab strip in the detached window's coordinates");
    REQUIRE(drag(detached, from, destination), "drag the tab back to the original tab strip");
    REQUIRE(wait_windows(desktop, &app, original, 2, 0) && !IsWindow(detached) && same_shells(&app, shells),
        "merge into the original window and close the empty detached window with every shell still live");
    hide_merge_target(&target);
    REQUIRE(key(original, VK_F4) && WaitForSingleObject(shells[2], 10000) == WAIT_OBJECT_0 &&
        alive(shells[0]) && alive(shells[1]), "the original focused split pane remains independently closable after merge");

    REQUIRE(PostMessageW(original, WM_CLOSE, 0, 0) && WaitForSingleObject(app.hProcess, 10000) == WAIT_OBJECT_0,
        "close the merged application normally");
    DWORD code = 1;
    REQUIRE(GetExitCodeProcess(app.hProcess, &code) && code == 0, "the merged application exits cleanly");
    success = 1;

cleanup:
    hide_merge_target(&target);
    if (alive(app.hProcess)) {
        if (job) TerminateJobObject(job, 1);
        if (alive(app.hProcess)) TerminateProcess(app.hProcess, 1);
        WaitForSingleObject(app.hProcess, 3000);
    }
    for (int i = 0; i < 3; i++) if (shells[i]) CloseHandle(shells[i]);
    if (app.hThread) CloseHandle(app.hThread);
    if (app.hProcess) CloseHandle(app.hProcess);
    if (job) CloseHandle(job);
    if (config_created) DeleteFileW(config);
    if (desktop) CloseDesktop(desktop);
    if (dpi) SetThreadDpiAwarenessContext(dpi);
    return success;
#undef REQUIRE
}
