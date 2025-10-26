#include "inc.h"

#if OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOCOMM
#define NOSERVICE
#define NOATOM
#define NOMINMAX

#include "Windows.h"
#include <shellapi.h>

internal_fn void set_console_text_color(HANDLE std, Severity severity)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD saved_attributes;
    
    // Save current attributes
    GetConsoleScreenBufferInfo(std, &consoleInfo);
    saved_attributes = consoleInfo.wAttributes;
    
    i32 att;
    
    switch (severity) {
        case Severity_Warning:
        att = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        break;
        
        case Severity_Error:
        att = FOREGROUND_RED | FOREGROUND_INTENSITY;
        break;
        
        default:
        att = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
    
    SetConsoleTextAttribute(std, (WORD)att);
}

void os_setup_system_info()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    system_info.page_size = info.dwAllocationGranularity;
    system_info.logical_cores = MAX(info.dwNumberOfProcessors, 1);
    
    LARGE_INTEGER _windows_clock_frequency;
    QueryPerformanceFrequency(&_windows_clock_frequency);
    system_info.timer_frequency = _windows_clock_frequency.QuadPart;
    
    system_info.timer_start = os_timer_get();
}

internal_fn HANDLE get_std_handle() {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

void os_initialize()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void os_shutdown()
{
    set_console_text_color(get_std_handle(), Severity_Info);
}

void os_print(Severity severity, String text)
{
    SCRATCH();
    
    HANDLE std = get_std_handle();
    
    set_console_text_color(std, severity);
    
    String text0 = string_copy(scratch.arena, text);
    
    DWORD written;
    WriteFile(std, text0.data, (u32)text0.size, &written, NULL);
    FlushFileBuffers(std);
    
    //WriteConsoleA(std, text0.data, (u32)text0.size, &written, NULL);
    //OutputDebugStringA(text0.data);
}

void os_console_set_cursor(i64 x, i64 y)
{
    HANDLE std = get_std_handle();
    
    COORD home = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(std, home);
}

void os_console_get_cursor(i64* x, i64* y)
{
    *x = 0;
    *y = 0;
    
    HANDLE std = get_std_handle();
    
    CONSOLE_SCREEN_BUFFER_INFO info = { };
    BOOL ok = GetConsoleScreenBufferInfo(std, &info);
    if (!ok) return;
    
    *x = info.dwCursorPosition.X;
    *y = info.dwCursorPosition.Y;
}

void os_console_clear()
{
    HANDLE std = get_std_handle();
    
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(std, &info);
    DWORD count;
    
    COORD home = {0, 0};
    FillConsoleOutputCharacter(std, ' ', info.dwSize.X * info.dwSize.Y, home, &count);
    FillConsoleOutputAttribute(std, info.wAttributes, info.dwSize.X * info.dwSize.Y, home, &count);
    SetConsoleCursorPosition(std, home);
}

void* os_allocate_heap(u64 size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)size);
}

void os_free_heap(void* address) {
    HeapFree(GetProcessHeap(), 0, address);
}

void* os_reserve_virtual_memory(u32 pages, b32 commit)
{
    u64 bytes = (u64)pages * system_info.page_size;
    u32 flags = MEM_RESERVE;
    if (commit) flags |= MEM_COMMIT;
    return VirtualAlloc(NULL, (size_t)bytes, flags, PAGE_READWRITE);
}
void os_commit_virtual_memory(void* address, u32 page_offset, u32 page_count)
{
    u64 page_size = system_info.page_size;
    u64 byte_offset = (u64)page_offset * page_size;
    u8* ptr = (u8*)address + byte_offset;
    u64 bytes = (u64)page_count * page_size;
    VirtualAlloc(ptr, (size_t)bytes, MEM_COMMIT, PAGE_READWRITE);
}
void os_release_virtual_memory(void* address)
{
    VirtualFree(address, 0, MEM_RELEASE);
}

void os_protect_virtual_memory(void* address, u32 pages)
{
    u64 bytes = (u64)pages * system_info.page_size;
    DWORD old_protect = PAGE_READWRITE;
    VirtualProtect(address, bytes, PAGE_NOACCESS, &old_protect);
}

DWORD WINAPI win_thread_main(LPVOID param)
{
    yov_initialize_thread();
    DEFER(yov_shutdown_thread());
    
    DEFER(os_free_heap(param));
    ThreadFn* fn = *(ThreadFn**)param;
    void* data = (u8*)param + sizeof(fn);
    return fn(data);
}

u64 os_thread_start(ThreadFn* fn, RawBuffer data)
{
    u32 param_size = sizeof(fn) + data.size;
    u8* param = (u8*)os_allocate_heap(param_size);
    memory_copy(param, &fn, sizeof(fn));
    memory_copy(param + sizeof(fn), data.data, data.size);
    
    HANDLE thread = CreateThread(NULL, 0, win_thread_main, param, 0, NULL);
    return (u64)thread;
}

void os_thread_wait(u64 thread, u32 millis)
{
    DWORD dw_millis = millis;
    if (millis == u32_max) dw_millis = INFINITE;
    
    WaitForSingleObject((HANDLE)thread, dw_millis);
}

internal_fn String string_from_win_error(DWORD error)
{
    if (error == 0) return {};
    if (error == ERROR_FILE_NOT_FOUND) return "File not found";
    if (error == ERROR_PATH_NOT_FOUND) return "Path not found";
    if (error == ERROR_BAD_PATHNAME) return "Invalid path";
    if (error == ERROR_INVALID_NAME) return "Invalid name";
    if (error == ERROR_ACCESS_DENIED) return "Access denied";
    if (error == ERROR_NOT_ENOUGH_MEMORY) return "Not enough memory";
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) return "Operation blocked by other process";
    if (error == ERROR_FILENAME_EXCED_RANGE) return "Path exceded limit";
    if (error == ERROR_FILE_EXISTS) return "File already exists";
    if (error == ERROR_ALREADY_EXISTS) return "Already exists";
    if (error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL ) return "Disk is full";
    if (error == ERROR_WRITE_PROTECT) return "Can't write";
    if (error == ERROR_OPERATION_ABORTED) return "Operation aborted";
    if (error == ERROR_VIRUS_INFECTED) return "Operation aborted, virus detected";
    if (error == ERROR_FILE_TOO_LARGE) return "File too large";
    if (error == ERROR_DIRECTORY) return "It's a directory";
    if (error == ERROR_ENVVAR_NOT_FOUND) return "Environment variable not found";
    return "Unknown WinAPI error";
}

internal_fn String string_from_last_error()
{
    DWORD error = GetLastError();
    return string_from_win_error(error);
}

b32 os_exists(String path)
{
    SCRATCH();
    String path0 = string_copy(scratch.arena, path);
    return GetFileAttributes(path0.data) != INVALID_FILE_ATTRIBUTES;
}

Result os_read_entire_file(Arena* arena, String path, RawBuffer* result)
{
    SCRATCH(arena);
    
    *result = {};
    String path0 = string_copy(scratch.arena, path);
    
    
    HANDLE file = CreateFile(path0.data, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
        return result_failed_make(string_from_last_error());
    }
    
    RawBuffer buffer;
    buffer.size = (u32)GetFileSize(file, NULL);
    buffer.data = (u8*)arena_push(arena, (u32)buffer.size);
    
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ReadFile(file, buffer.data, (u32)buffer.size, NULL, NULL);
    
    CloseHandle(file);
    
    *result = buffer;
    return RESULT_SUCCESS;
}

Result os_write_entire_file(String path, RawBuffer data)
{
    SCRATCH();
    String path0 = string_copy(scratch.arena, path);
    HANDLE file = CreateFile(path0.data, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (file == INVALID_HANDLE_VALUE) {
        return result_failed_make(string_from_last_error());
    }
    
    WriteFile(file, data.data, (DWORD)data.size, NULL, NULL);
    
    CloseHandle(file);
    
    return RESULT_SUCCESS;
}

Result os_copy_file(String dst_path, String src_path, b32 override)
{
    SCRATCH();
    String dst_path0 = string_copy(scratch.arena, dst_path);
    String src_path0 = string_copy(scratch.arena, src_path);
    
    b32 fail_if_exists = !override;
    
    b32 success = CopyFile(src_path0.data, dst_path0.data, fail_if_exists) != 0;
    if (!success) return result_failed_make(string_from_last_error());
    return RESULT_SUCCESS;
}

Result os_move_file(String dst_path, String src_path)
{
    SCRATCH();
    String dst_path0 = string_copy(scratch.arena, dst_path);
    String src_path0 = string_copy(scratch.arena, src_path);
    
    b32 success = MoveFile(src_path0.data, dst_path0.data) != 0;
    if (!success) return result_failed_make(string_from_last_error());
    return RESULT_SUCCESS;
}

Result os_delete_file(String path)
{
    SCRATCH();
    String path0 = string_copy(scratch.arena, path);
    
    if (SetFileAttributes(path0.data, FILE_ATTRIBUTE_NORMAL) == 0) {
        return result_failed_make(string_from_last_error());
    }
    
    b32 success = DeleteFile(path0.data) != 0;
    if (!success) return result_failed_make(string_from_last_error());
    return RESULT_SUCCESS;
}

internal_fn Result create_recursive_path(String path)
{
    SCRATCH();
    
    Array<String> splits = string_split(scratch.arena, path, STR("/"));
    Array<String> folders = array_make(splits.data, splits.count - 1);
    
    String last_folder = STR("");
    
    foreach(i, folders.count)
    {
        String folder = string_format(scratch.arena, "%S%S/", last_folder, folders[i]);
        last_folder = folder;
        
        if (folder.size <= 3 && folder[1] == ':') continue;
        
        String folder0 = string_copy(scratch.arena, folder);
        if (!CreateDirectory(folder0.data, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                return result_failed_make(string_from_win_error(error));
            }
        }
    }
    
    return RESULT_SUCCESS;
}

Result os_create_directory(String path, b32 recursive)
{
    SCRATCH();
    String path0 = string_copy(scratch.arena, path);
    
	if (recursive) {
        Result res = create_recursive_path(path0);
        if (res.failed) return res;
    }
    
    b32 success = (b32)CreateDirectory(path0.data, NULL);
    
    DWORD error = 0;
    if (!success) {
        error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) success = true;
    }
    
    if (!success) return result_failed_make(string_from_last_error());
    return RESULT_SUCCESS;
}

internal_fn Result delete_directory_recursive(String path)
{
    // NOTE(Jose): "path" must be null terminated!!
    SCRATCH();
    
    String path_querry = string_format(scratch.arena, "%S\\*", path);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFileA(path_querry.data, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        return result_failed_make(string_from_last_error());
    }
    
    while (1)
    {
        String file_name = STR(find_data.cFileName);
        
        b32 file_is_valid = true;
        
        if (string_equals(file_name, STR(".")) || string_equals(file_name, STR(".."))) {
            file_is_valid = false;
        }
        
        if (file_is_valid)
        {
            String file_path = string_format(scratch.arena, "%S/%S", path, file_name);
            
            Result res;
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                res = delete_directory_recursive(file_path);
            } else {
                res = os_delete_file(file_path);
            }
            
            if (res.failed) return res;
        }
        
        if (FindNextFileA(find_handle, &find_data) == 0) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                return result_failed_make(string_from_last_error());
            }
            break;
        }
    }
    
    if (FindClose(find_handle) == 0) {
        return result_failed_make(string_from_last_error());
    }
    
    if (SetFileAttributesA(path.data, FILE_ATTRIBUTE_NORMAL) == 0) {
        return result_failed_make(string_from_last_error());
    }
    
    if (RemoveDirectoryA(path.data) == 0) {
        return result_failed_make(string_from_last_error());
    }
    
    return RESULT_SUCCESS;
}

Result os_delete_directory(String path)
{
    SCRATCH();
    String path0 = string_copy(scratch.arena, path);
    return delete_directory_recursive(path0);
}

internal_fn Result copy_directory_recursive(String dst_path, String src_path, b32 is_moving)
{
    // NOTE(Jose): All paths in parameters must be null terminated!!
    SCRATCH();
    
    {
        Result res = os_create_directory(dst_path, true);
        if (res.failed) return res;
    }
    
    String path_querry = string_format(scratch.arena, "%S\\*", src_path);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFileA(path_querry.data, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        return result_failed_make(string_from_last_error());
    }
    
    while (1)
    {
        String file_name = STR(find_data.cFileName);
        
        b32 file_is_valid = true;
        
        if (string_equals(file_name, STR(".")) || string_equals(file_name, STR(".."))) {
            file_is_valid = false;
        }
        
        if (file_is_valid)
        {
            String file_src = string_format(scratch.arena, "%S/%S", src_path, file_name);
            String file_dst = string_format(scratch.arena, "%S/%S", dst_path, file_name);
            
            Result res;
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                res = copy_directory_recursive(file_dst, file_src, is_moving);
            } else {
                res = os_copy_file(file_dst, file_src, false);
            }
            
            if (res.failed) return res;
        }
        
        if (FindNextFileA(find_handle, &find_data) == 0) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                return result_failed_make(string_from_last_error());
            }
            break;
        }
    }
    
    if (FindClose(find_handle) == 0) {
        return result_failed_make(string_from_last_error());
    }
    
    if (is_moving) {
        Result res = delete_directory_recursive(src_path);
        if (res.failed) return res;
    }
    
    return RESULT_SUCCESS;
}

internal_fn Result copy_or_move_directory(String dst_path, String src_path, b32 is_moving)
{
    // NOTE(Jose): All paths in parameters must be null terminated!!
    
    if (os_exists(dst_path)) {
        return result_failed_make("The destination already exists");
    }
    
    return copy_directory_recursive(dst_path, src_path, is_moving);
}

Result os_copy_directory(String dst_path, String src_path)
{
    SCRATCH();
    
    String dst_path0 = string_copy(scratch.arena, dst_path);
    String src_path0 = string_copy(scratch.arena, src_path);
    
    return copy_or_move_directory(dst_path0, src_path0, false);
}

Result os_move_directory(String dst_path, String src_path)
{
    SCRATCH();
    
    String dst_path0 = string_copy(scratch.arena, dst_path);
    String src_path0 = string_copy(scratch.arena, src_path);
    
    return copy_or_move_directory(dst_path0, src_path0, true);
}

inline_fn Date Date_from_SYSTEMTIME(SYSTEMTIME sys)
{
	Date date;
	date.year = (u32)sys.wYear;
	date.month = (u32)sys.wMonth;
	date.day = (u32)sys.wDay;
	date.hour = (u32)sys.wHour;
	date.minute = (u32)sys.wMinute;
	date.second = (u32)sys.wSecond;
	date.millisecond = (u32)sys.wMilliseconds;
	return date;
}

inline_fn Date Date_from_FILETIME(FILETIME file)
{
	SYSTEMTIME sys;
	FileTimeToSystemTime(&file, &sys);
	return Date_from_SYSTEMTIME(sys);
}

inline_fn FileInfo FileInfo_from_WIN32_FIND_DATAA(Arena* arena, WIN32_FIND_DATAA d, String path)
{
    FileInfo info{};
    
    info.is_directory = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    info.create_date = Date_from_FILETIME(d.ftCreationTime);
    info.last_write_date = Date_from_FILETIME(d.ftLastWriteTime);
    info.last_access_date = Date_from_FILETIME(d.ftLastAccessTime);
    
    const char* filename = (const char*)d.cFileName;
    
    SCRATCH(arena);
    StringBuilder builder = string_builder_make(scratch.arena);
    append(&builder, path);
    if (path.size != 0 && path[path.size - 1] != '/') append_char(&builder, '/');
    append(&builder, filename);
    
    file_info_set_path(&info, string_from_builder(arena, &builder));
    return info;
}

Result os_file_get_info(Arena* arena, String path, FileInfo* ret)
{
    *ret = {};
    FileInfo info = {};
    
    String path0 = string_copy(arena, path);
    file_info_set_path(&info, path0);
    
    WIN32_FILE_ATTRIBUTE_DATA att;
    if (!GetFileAttributesExA(path0.data, GetFileExInfoStandard, &att)) {
        return result_failed_make(string_from_last_error());
    }
    
    info.is_directory = (att.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    
    info.create_date      = Date_from_FILETIME(att.ftCreationTime);
    info.last_write_date  = Date_from_FILETIME(att.ftLastWriteTime);
    info.last_access_date = Date_from_FILETIME(att.ftLastAccessTime);
    
    *ret = info;
    return RESULT_SUCCESS;
}

Result os_dir_get_files_info(Arena* arena, String path, Array<FileInfo>* ret)
{
    SCRATCH(arena);
    
    *ret = {};
    
    // Clear path
    String path0;
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        if (!os_path_is_absolute(path)) {
            append(&builder, ".\\");
        }
        
        foreach(i, path.size) {
            if (path[i] == '/') append_char(&builder, '\\');
            else append_char(&builder, path[i]);
        }
        
        if (path[path.size - 1] != '/') append_char(&builder, '\\');
        append_char(&builder, '*');
        
        path0 = string_from_builder(scratch.arena, &builder);
    }
    
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(path0.data, &data);
    
    if (find == INVALID_HANDLE_VALUE) return {};
    
    auto files = pooled_array_make<FileInfo>(scratch.arena, 32);
    
    while (1) {
        if (!string_equals(data.cFileName, ".") && !string_equals(data.cFileName, "..")) {
            array_add(&files, FileInfo_from_WIN32_FIND_DATAA(arena, data, path));
        }
        if (!FindNextFileA(find, &data)) break;
    }
    
    FindClose(find);
    
    *ret = array_from_pooled_array(arena, files);
    return RESULT_SUCCESS;
}


b32 os_ask_yesno(String title, String content)
{
    SCRATCH();
    String title0 = string_copy(scratch.arena, title);
    String content0 = string_copy(scratch.arena, content);
    return MessageBox(0, content0.data, title0.data, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

CallOutput os_call(Arena* arena, String working_dir, String command, RedirectStdout redirect_stdout)
{
    SCRATCH(arena);
    
    if (working_dir.size == 0) working_dir = ".";
    
    String env_separator = "\"===>YOV SEPARATOR<===\"";
    
    if (redirect_stdout == RedirectStdout_ImportEnv) {
        command = string_format(scratch.arena, "cmd.exe /c \"\"%S\" && echo %S && set\"", command, env_separator);
    }
    
    String command0 = string_copy(scratch.arena, command);
    String working_dir0 = string_copy(scratch.arena, working_dir);
    
    SECURITY_ATTRIBUTES security_attr = {};
    security_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attr.bInheritHandle = true;
    security_attr.lpSecurityDescriptor = NULL;
    
    HANDLE stdout_read = 0;
    HANDLE stdout_write = 0;
    
    CallOutput res{};
    
    if (!CreatePipe(&stdout_read, &stdout_write, &security_attr, 0)) {
        res.result = result_failed_make(string_from_last_error());
        return res;
    }
    
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFO si = { sizeof(STARTUPINFO) };
    
    if (redirect_stdout == RedirectStdout_Ignore) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = NULL;
        si.hStdError  = NULL;
        si.hStdInput  = NULL;
    }
    else if (redirect_stdout == RedirectStdout_Script || redirect_stdout == RedirectStdout_ImportEnv) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = stdout_write;
        si.hStdError  = stdout_write;
        si.hStdInput  = NULL;
    }
    
    PROCESS_INFORMATION pi;
    
    b32 inherit_handles = redirect_stdout != RedirectStdout_Console;
    
    u32 creation_flags = 0;
    
    if (redirect_stdout == RedirectStdout_ImportEnv) {
        creation_flags |= CREATE_NO_WINDOW;
    }
    
    if (!CreateProcessA(NULL, command0.data, NULL, NULL, inherit_handles, creation_flags, NULL, working_dir0.data, &si, &pi)) {
        res.result = result_failed_make(string_from_last_error());
        return res;
    }
    
    CloseHandle(stdout_write);
    
    // Read stdout
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        u32 buffer_size = 4096;
        CHAR* buffer = (CHAR*)arena_push(scratch.arena, buffer_size);
        
        while (TRUE)
        {
            DWORD bytes_available = 0;
            if (!PeekNamedPipe(stdout_read, NULL, 0, NULL, &bytes_available, NULL) || bytes_available == 0)
            {
                DWORD wait_result = WaitForSingleObject(pi.hProcess, 0);
                
                if (wait_result == WAIT_TIMEOUT) {
                    Yield();
                }
                else {
                    break;
                }
                
                continue;
            }
            
            DWORD bytes_read = 0;
            BOOL success = ReadFile(stdout_read, buffer, buffer_size - 1, &bytes_read, NULL);
            
            if (!success || bytes_read <= 0) {
                break;
            }
            
            buffer[bytes_read] = '\0';
            append(&builder, string_make(buffer, bytes_read));
        }
        
        res.stdout = string_from_builder(arena, &builder);
        
        if (res.stdout.size > 0) {
            if (res.stdout[res.stdout.size - 1] == '\n') {
                res.stdout.size--;
            }
        }
        if (res.stdout.size > 0) {
            if (res.stdout[res.stdout.size - 1] == '\r') {
                res.stdout.size--;
            }
        }
    }
    
    // Read exit code
    {
        DWORD _exit_code;
        i32 exit_code;
        if (GetExitCodeProcess(pi.hProcess, &_exit_code) == 0) {
            exit_code = -1;
        }
        else {
            exit_code = _exit_code;
        }
        
        if (exit_code != 0) res.result = result_failed_make("Exited with code != 0", exit_code);
        else {
            res.result = RESULT_SUCCESS;
            res.result.code = exit_code;
        }
    }
    
    // Import env
    if (res.result.code == 0 && redirect_stdout == RedirectStdout_ImportEnv)
    {
        Array<String> split = string_split(scratch.arena, res.stdout, env_separator);
        if (split.count != 2) {
            res.result = result_failed_make("Wrong environment format");
            return res;
        }
        
        Array<String> envs = string_split(scratch.arena, split[1], "\r\n");
        foreach(i, envs.count)
        {
            String env = envs[i];
            
            i64 equals_index = -1;
            for (i64 i = 0; i < env.size; i++) {
                if (env[i] == '=') {
                    equals_index = i;
                    break;
                }
            }
            
            if (equals_index < 0) continue;
            
            String name = string_substring(env, 0, equals_index);
            String value = string_substring(env, equals_index + 1, env.size - equals_index - 1);
            
            name = string_copy(scratch.arena, name);
            value = string_copy(scratch.arena, value);
            
            SetEnvironmentVariableA(name.data, value.data);
        }
    }
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(stdout_read);
    
    return res;
}

CallOutput os_call_exe(Arena* arena, String working_dir, String exe, String args, RedirectStdout redirect_stdout)
{
    SCRATCH(arena);
    String command = string_format(scratch.arena, "\"%S.exe\" %S", exe, args);
    return os_call(arena, working_dir, command, redirect_stdout);
}

CallOutput os_call_script(Arena* arena, String working_dir, String script, String args, String yov_args, RedirectStdout redirect_stdout)
{
    SCRATCH(arena);
    String inherited_yov_args = yov_get_inherited_args(scratch.arena);
    String yov_path = os_get_executable_path(scratch.arena);
    String command = string_format(scratch.arena, "\"%S\" %S %S %S %S", yov_path, inherited_yov_args, yov_args, script, args);
    return os_call(arena, working_dir, command, redirect_stdout);
}

String os_get_working_path(Arena* arena)
{
    SCRATCH(arena);
    u32 buffer_size = GetCurrentDirectory(0, NULL) + 1;
    char* dst = (char*)arena_push(scratch.arena, buffer_size);
    GetCurrentDirectory(buffer_size, dst);
    
    return path_resolve(arena, STR(dst));
}

String os_get_executable_path(Arena* arena)
{
    SCRATCH(arena);
    char buffer[MAX_PATH];
    u32 size = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    char* dst = (char*)arena_push(scratch.arena, size + 1);
    memory_copy(dst, buffer, size);
    return path_resolve(arena, string_make(dst, size));
}

b32 os_path_is_absolute(String path)
{
    if (path.size < 3) return false;
    if (path[1] != ':') return false;
    if (path[2] != '/') return false;
    return true;
}

b32 os_path_is_directory(String path)
{
    SCRATCH();
    
    String last_element = path_get_last_element(path);
    foreach(i, last_element.size) {
        if (last_element[i] == '.') return false;
    }
    
    String path0 = string_copy(scratch.arena, path);
    DWORD att = GetFileAttributesA(path0.data);
    if (att == INVALID_FILE_ATTRIBUTES) return false;
    
    return (att & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

Array<String> os_get_args(Arena* arena)
{
    SCRATCH(arena);
    
    String line = STR(GetCommandLineA());
    
    PooledArray<String> list = pooled_array_make<String>(scratch.arena, 16);
    u32 arg_index = 0;
    
    u64 cursor = 0;
    while (cursor < line.size)
    {
        char c = line[cursor];
        
        char sep = ' ';
        
        if (c == '"') {
            sep = '"';
            cursor++;
        }
        
        u64 start_cursor = cursor;
        while (cursor < line.size && line[cursor] != sep) cursor++;
        
        if (arg_index > 0) {
            String arg = string_substring(line, start_cursor, cursor - start_cursor);
            array_add(&list, string_copy(arena, arg));
        }
        arg_index++;
        
        cursor++;
        while (cursor < line.size && line[cursor] == ' ') cursor++;
    }
    
    return array_from_pooled_array(arena, list);
}

Result os_env_get(Arena* arena, String* out, String name)
{
    SCRATCH(arena);
    *out = "";
    String name0 = string_copy(scratch.arena, name);
    
    // "size" includes null-terminator
    DWORD buffer_size = GetEnvironmentVariable(name0.data, NULL, 0);
    
    if (buffer_size <= 0) {
        return result_failed_make(string_from_last_error());
    }
    
    out->data = (char*)arena_push(arena, buffer_size);
    // This new size does not include null-terminator, thanks Windows for the confusion
    DWORD size = GetEnvironmentVariable(name0.data, out->data, (DWORD)buffer_size);
    out->size = size;
    
    return RESULT_SUCCESS;
}

void os_thread_sleep(u64 millis) {
    Sleep((DWORD)millis);
}

void os_console_wait()
{
    print_info("\nPress RETURN to continue...\n");
    
    HANDLE window = GetConsoleWindow();
    
    while (1) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
            return;
        }
        os_thread_sleep(50);
    }
}

u32 os_msvc_get_env_imported() {
    SCRATCH();
    CallOutput res = os_call(scratch.arena, {}, "cl", RedirectStdout_Script);
    if (res.result.code != 0) return MSVC_Env_None;
    
    String out = res.stdout;
    
    for (i64 i = 0; i < out.size - 3; i++) {
        if (out[i] != 'x') continue;
        String arch = string_substring(out, i, 3);
        if (string_equals(arch, "x64")) return MSVC_Env_x64;
        if (string_equals(arch, "x86")) return MSVC_Env_x86;
        if (string_equals(arch, "x32")) return MSVC_Env_x86;
    }
    
    return MSVC_Env_None;
}

Result os_msvc_find_path(Arena* arena, String* out)
{
    SCRATCH(arena);
    *out = {};
    
    char* program_files_path_buffer = (char*)arena_push(scratch.arena, MAX_PATH);
    if (!ExpandEnvironmentStringsA("%ProgramFiles(x86)%", program_files_path_buffer, MAX_PATH)) {
        return result_failed_make(string_from_last_error());
    }
    
    String program_files_path = path_resolve(scratch.arena, program_files_path_buffer);
    String vs_where_path = string_format(scratch.arena, "%S/Microsoft Visual Studio/Installer/vswhere.exe", program_files_path);
    String vs_where_command = string_format(scratch.arena, "\"%S\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath", vs_where_path);
    
    CallOutput call_res = os_call(scratch.arena, {}, vs_where_command, RedirectStdout_Script);
    if (call_res.result.failed) return call_res.result;
    
    *out = path_resolve(arena, call_res.stdout);
    return RESULT_SUCCESS;
}

Result os_msvc_import_env(u32 mode)
{
    SCRATCH();
    
    Result res = RESULT_SUCCESS;
    
    // Check if its already initialized
    if (os_msvc_get_env_imported() == mode) {
        //print_info("MSVC Env already imported for %s\n", mode == MSVC_Env_x64 ? "x64" : "x86");
        return res;
    }
    
    // Find batch path
    String batch_path = {};
    {
        String vs_path;
        res = os_msvc_find_path(scratch.arena, &vs_path);
        if (res.failed) return res;
        
        String batch_name = (mode == MSVC_Env_x86) ? "vcvars32" : "vcvars64";
        batch_path = string_format(scratch.arena, "%S/VC/Auxiliary/Build/%S.bat", vs_path, batch_name);
    }
    
    // Execute
    CallOutput call_res = os_call(scratch.arena, {}, batch_path, RedirectStdout_ImportEnv);
    if (call_res.result.failed) return call_res.result;
    
    if (os_msvc_get_env_imported() != mode) {
        return result_failed_make("MSVC environment is not imported properly");
    }
    
    //print_info("MSVC Env ready for %s\n", mode == MSVC_Env_x64 ? "x64" : "x86");
    
    return res;
}

u64 os_timer_get()
{
    LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
    return now.QuadPart - system_info.timer_start;
}

#endif