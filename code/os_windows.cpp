#include "inc.h"

#if OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOCOMM
#define NOSERVICE
#define NOATOM
#define NOMINMAX

#include "Windows.h"
#include <shellapi.h>

struct Windows {
    u32 page_size;
    HANDLE console_handle;
} windows;

internal_fn void set_console_text_color(Severity severity)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD saved_attributes;
    
    // Save current attributes
    GetConsoleScreenBufferInfo(windows.console_handle, &consoleInfo);
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
    
    SetConsoleTextAttribute(windows.console_handle, (WORD)att);
}

void os_initialize()
{
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    windows.page_size = system_info.dwAllocationGranularity;
    
    windows.console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
}

void os_shutdown()
{
    set_console_text_color(Severity_Info);
}

void os_print(Severity severity, String text)
{
    SCRATCH();
    
    set_console_text_color(severity);
    
    String text0 = string_copy(scratch.arena, text);
    
    DWORD written;
    //WriteConsoleA(windows.console_handle, text0.data, (u32)text0.size, &written, NULL);
    WriteFile(windows.console_handle, text0.data, (u32)text0.size, &written, NULL);
    FlushFileBuffers(windows.console_handle);
    OutputDebugStringA(text0.data);
}

u64 os_get_page_size() {
    return windows.page_size;
}

u32 os_pages_from_bytes(u64 bytes) {
    return (u32)u64_divide_high(bytes, windows.page_size);
}

void* os_allocate_heap(u64 size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void os_free_heap(void* address) {
    HeapFree(GetProcessHeap(), 0, address);
}

void* os_reserve_virtual_memory(u32 pages, b32 commit)
{
    u64 bytes = (u64)pages * os_get_page_size();
    u32 flags = MEM_RESERVE;
    if (commit) flags |= MEM_COMMIT;
    return VirtualAlloc(NULL, bytes, flags, PAGE_READWRITE);
}
void os_commit_virtual_memory(void* address, u32 page_offset, u32 page_count)
{
    u64 page_size = os_get_page_size();
    u64 byte_offset = (u64)page_offset * page_size;
    u8* ptr = (u8*)address + byte_offset;
    u64 bytes = (u64)page_count * page_size;
    VirtualAlloc(ptr, bytes, MEM_COMMIT, PAGE_READWRITE);
}
void os_release_virtual_memory(void* address)
{
    VirtualFree(address, 0, MEM_RELEASE);
}

internal_fn String string_from_win_error(DWORD error)
{
    if (error == 0) return {};
    if (error == ERROR_FILE_NOT_FOUND) return STR("File not found");
    if (error == ERROR_PATH_NOT_FOUND) return STR("Path not found");
    if (error == ERROR_BAD_PATHNAME) return STR("Invalid path");
    if (error == ERROR_INVALID_NAME) return STR("Invalid name");
    if (error == ERROR_ACCESS_DENIED) return STR("Access denied");
    if (error == ERROR_NOT_ENOUGH_MEMORY) return STR("Not enough memory");
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) return STR("Operation blocked by other process");
    if (error == ERROR_FILENAME_EXCED_RANGE) return STR("Path exceded limit");
    if (error == ERROR_FILE_EXISTS) return STR("File already exists");
    if (error == ERROR_ALREADY_EXISTS) return STR("Already exists");
    if (error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL ) return STR("Disk is full");
    if (error == ERROR_WRITE_PROTECT) return STR("Can't write");
    if (error == ERROR_OPERATION_ABORTED) return STR("Operation aborted");
    if (error == ERROR_VIRUS_INFECTED) return STR("Operation aborted, virus detected");
    if (error == ERROR_FILE_TOO_LARGE) return STR("File too large");
    if (error == ERROR_DIRECTORY) return STR("It's a directory");
    return STR("Unknown error");
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

b32 os_read_entire_file(Arena* arena, String path, RawBuffer* result)
{
    SCRATCH(arena);
    
    *result = {};
    String path0 = string_copy(scratch.arena, path);
    
    HANDLE file = CreateFile(path0.data, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return false;
    
    RawBuffer buffer;
    buffer.size = (u32)GetFileSize(file, NULL);
    buffer.data = (u8*)arena_push(arena, (u32)buffer.size);
    
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ReadFile(file, buffer.data, (u32)buffer.size, NULL, NULL);
    
    CloseHandle(file);
    
    *result = buffer;
    return true;
}

Result os_copy_file(String dst_path, String src_path, b32 override)
{
    SCRATCH();
    String dst_path0 = string_copy(scratch.arena, dst_path);
    String src_path0 = string_copy(scratch.arena, src_path);
    
    b32 fail_if_exists = !override;
    
    Result res{};
    res.success = CopyFile(src_path0.data, dst_path0.data, fail_if_exists) != 0;
    if (!res.success) res.message = string_from_last_error();
    return res;
}

Result os_move_file(String dst_path, String src_path)
{
    SCRATCH();
    String dst_path0 = string_copy(scratch.arena, dst_path);
    String src_path0 = string_copy(scratch.arena, src_path);
    
    Result res{};
    res.success = MoveFile(src_path0.data, dst_path0.data) != 0;
    if (!res.success) res.message = string_from_last_error();
    return res;
}

Result os_delete_file(String path)
{
    SCRATCH();
    String path0 = string_copy(scratch.arena, path);
    
    if (SetFileAttributes(path0.data, FILE_ATTRIBUTE_NORMAL) == 0) {
        return Result { false, string_from_last_error() };
    }
    
    Result res{};
    res.success = DeleteFile(path0.data) != 0;
    if (!res.success) res.message = string_from_last_error();
    return res;
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
                return Result{ false, string_from_win_error(error) };
            }
        }
    }
    
    return RESULT_SUCCESS;
}

Result os_create_directory(String path, b32 recursive)
{
    SCRATCH();
    String path0 = string_copy(scratch.arena, path);
    
    Result res = RESULT_SUCCESS;
    
	if (recursive) {
        res = create_recursive_path(path0);
        if (!res.success) return res;
    }
    
    res.success = (b32)CreateDirectory(path0.data, NULL);
    
    DWORD error = 0;
    if (!res.success) {
        error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) res.success = true;
    }
    
    if (!res.success) res.message = string_from_win_error(error);
    return res;
}

internal_fn Result delete_directory_recursive(String path)
{
    // NOTE(Jose): "path" must be null terminated!!
    SCRATCH();
    
    String path_querry = string_format(scratch.arena, "%S\\*", path);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFileA(path_querry.data, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        Result res{};
        res.message = string_from_last_error();
        return res;
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
            
            Result res{};
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                res = delete_directory_recursive(file_path);
            } else {
                res = os_delete_file(file_path);
            }
            
            if (!res.success) return res;
        }
        
        if (FindNextFileA(find_handle, &find_data) == 0) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                return Result{ false, string_from_last_error() };
            }
            break;
        }
    }
    
    if (FindClose(find_handle) == 0) {
        return Result{ false, string_from_last_error() };
    }
    
    if (SetFileAttributesA(path.data, FILE_ATTRIBUTE_NORMAL) == 0) {
        return Result{ false, string_from_last_error() };
    }
    
    if (RemoveDirectoryA(path.data) == 0) {
        return Result{ false, string_from_last_error() };
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
    
    Result res = os_create_directory(dst_path, true);
    if (!res.success) return res;
    
    String path_querry = string_format(scratch.arena, "%S\\*", src_path);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFileA(path_querry.data, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        Result res{};
        res.message = string_from_last_error();
        return res;
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
            
            Result res{};
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                res = copy_directory_recursive(file_dst, file_src, is_moving);
            } else {
                res = os_copy_file(file_dst, file_src, false);
            }
            
            if (!res.success) return res;
        }
        
        if (FindNextFileA(find_handle, &find_data) == 0) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                return Result{ false, string_from_last_error() };
            }
            break;
        }
    }
    
    if (FindClose(find_handle) == 0) {
        return Result{ false, string_from_last_error() };
    }
    
    if (is_moving) {
        res = delete_directory_recursive(src_path);
        if (!res.success) return res;
    }
    
    return RESULT_SUCCESS;
}

internal_fn Result copy_or_move_directory(String dst_path, String src_path, b32 is_moving)
{
    // NOTE(Jose): All paths in parameters must be null terminated!!
    
    if (os_exists(dst_path)) {
        return Result{ false, STR("The destination already exists") };
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

b32 os_ask_yesno(String title, String content)
{
    SCRATCH();
    String title0 = string_copy(scratch.arena, title);
    String content0 = string_copy(scratch.arena, content);
    return MessageBox(0, content0.data, title0.data, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

i32 os_call(String working_dir, String command)
{
    SCRATCH();
    
    String command0 = string_copy(scratch.arena, command);
    String working_dir0 = string_copy(scratch.arena, working_dir);
    
    STARTUPINFO si = { sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi;
    
    if (!CreateProcessA(NULL, command0.data, NULL, NULL, FALSE, 0, NULL, working_dir0.data, &si, &pi)) {
        return -1;
    }
    
    WaitForSingleObject(pi.hProcess, INFINITE);
    
    DWORD _exit_code;
    i32 exit_code;
    if (GetExitCodeProcess(pi.hProcess, &_exit_code) == 0) {
        exit_code = -1;
    }
    else {
        exit_code = _exit_code;
    }
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return exit_code;
}

i32 os_call_exe(String working_dir, String exe, String params)
{
    SCRATCH();
    String command = string_format(scratch.arena, "\"%S.exe\" %S", exe, params);
    return os_call(working_dir, command);
}

String os_get_working_path(Arena* arena)
{
    SCRATCH(arena);
    u32 buffer_size = GetCurrentDirectory(0, NULL) + 1;
    char* dst = (char*)arena_push(scratch.arena, buffer_size);
    GetCurrentDirectory(buffer_size, dst);
    
    return path_resolve(arena, STR(dst));
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

void os_thread_sleep(u64 millis) {
    Sleep((DWORD)millis);
}

void os_console_wait()
{
    print_info("\nPress RETURN to continue...\n");
    
    HANDLE window = GetConsoleWindow();
    
    while (1) {
        if (window == GetForegroundWindow() && GetAsyncKeyState(VK_RETURN) & 0x8000) {
            return;
        }
        os_thread_sleep(50);
    }
}

#endif