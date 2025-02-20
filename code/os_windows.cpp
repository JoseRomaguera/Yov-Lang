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

b32 os_copy_file(String dst_path, String src_path, b32 override)
{
    SCRATCH();
    String dst_path0 = string_copy(scratch.arena, dst_path);
    String src_path0 = string_copy(scratch.arena, src_path);
    
    b32 fail_if_exists = !override;
    
    return CopyFile(src_path0.data, dst_path0.data, fail_if_exists) != 0;
}

inline_fn b32 create_path(String path)
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
        if (!CreateDirectory(folder0.data, NULL) && ERROR_ALREADY_EXISTS != GetLastError())
            return FALSE;
    }
    
    return TRUE;
}

b32 os_folder_create(String path, b32 recursive)
{
    SCRATCH();
    String path0 = string_copy(scratch.arena, path);
	if (recursive) create_path(path0);
    return (b32)CreateDirectory(path0.data, NULL);
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