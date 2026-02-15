#include "common.h"

#if OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOCOMM
#define NOSERVICE
#define NOATOM
#define NOMINMAX

#include "Windows.h"
#include <shellapi.h>

internal_fn void SetConsoleTextColor(HANDLE std, PrintLevel level)
{
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD saved_attributes;
    
    // Save current attributes
    GetConsoleScreenBufferInfo(std, &consoleInfo);
    saved_attributes = consoleInfo.wAttributes;
    
    I32 att;
    
    if (level == PrintLevel_UserCode) {
        att = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
    else if (level == PrintLevel_DevLog) {
        att = FOREGROUND_GREEN;
    }
    else if (level == PrintLevel_InfoReport) {
        att = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
    else if (level == PrintLevel_WarningReport) {
        att = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    }
    else if (level == PrintLevel_ErrorReport) {
        att = FOREGROUND_RED | FOREGROUND_INTENSITY;
    }
    
    
    SetConsoleTextAttribute(std, (WORD)att);
}

void SetupGlobals()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    system_info.page_size = info.dwAllocationGranularity;
    system_info.logical_cores = Max(info.dwNumberOfProcessors, 1);
    
    LARGE_INTEGER _windows_clock_frequency;
    QueryPerformanceFrequency(&_windows_clock_frequency);
    system_info.timer_frequency = _windows_clock_frequency.QuadPart;
    
    system_info.timer_start = OsTimerGet();
    
    InitializeThread();
    
    // Working path
    {
        U32 buffer_size = GetCurrentDirectory(0, NULL) + 1;
        char* dst = (char*)ArenaPush(context.arena, buffer_size);
        GetCurrentDirectory(buffer_size, dst);
        
        system_info.working_path = PathResolve(context.arena, dst);
        system_info.working_path = StrHeapCopy(system_info.working_path);
    }
    
    // Executable Path
    {
        char buffer[MAX_PATH];
        U32 size = GetModuleFileNameA(NULL, buffer, MAX_PATH);
        char* dst = (char*)ArenaPush(context.arena, size + 1);
        MemoryCopy(dst, buffer, size);
        system_info.executable_path = PathResolve(context.arena, StrMake(dst, size));
        system_info.executable_path = StrHeapCopy(system_info.executable_path);
    }
    
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

internal_fn HANDLE get_std_handle() {
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

void ShutdownGlobals()
{
    ShutdownThread();
    SetConsoleTextColor(get_std_handle(), PrintLevel_UserCode);
}

void OsPrint(PrintLevel level, String text)
{
    HANDLE std = get_std_handle();
    
    SetConsoleTextColor(std, level);
    
    String text0 = StrCopy(context.arena, text);
    
    DWORD written;
    WriteFile(std, text0.data, (U32)text0.size, &written, NULL);
    FlushFileBuffers(std);
    
    //WriteConsoleA(std, text0.data, (U32)text0.size, &written, NULL);
    //OutputDebugStringA(text0.data);
}

void OsConsoleClear()
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

void* OsHeapAllocate(U64 size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)size);
}

void OsHeapFree(void* address) {
    HeapFree(GetProcessHeap(), 0, address);
}

void* OsReserveVirtualMemory(U32 pages, B32 commit)
{
    U64 bytes = (U64)pages * system_info.page_size;
    U32 flags = MEM_RESERVE;
    if (commit) flags |= MEM_COMMIT;
    return VirtualAlloc(NULL, (size_t)bytes, flags, PAGE_READWRITE);
}
void OsCommitVirtualMemory(void* address, U32 page_offset, U32 page_count)
{
    U64 page_size = system_info.page_size;
    U64 byte_offset = (U64)page_offset * page_size;
    U8* ptr = (U8*)address + byte_offset;
    U64 bytes = (U64)page_count * page_size;
    VirtualAlloc(ptr, (size_t)bytes, MEM_COMMIT, PAGE_READWRITE);
}
void OsReleaseVirtualMemory(void* address)
{
    VirtualFree(address, 0, MEM_RELEASE);
}

void OsProtectVirtualMemory(void* address, U32 pages)
{
    U64 bytes = (U64)pages * system_info.page_size;
    DWORD old_protect = PAGE_READWRITE;
    VirtualProtect(address, bytes, PAGE_NOACCESS, &old_protect);
}

DWORD WINAPI win_thread_main(LPVOID param)
{
    InitializeThread();
    defer(ShutdownThread());
    
    defer(OsHeapFree(param));
    ThreadFn* fn = *(ThreadFn**)param;
    void* data = (U8*)param + sizeof(fn);
    return fn(data);
}

OS_Thread OsThreadStart(ThreadFn* fn, RBuffer data)
{
    U32 param_size = sizeof(fn) + data.size;
    U8* param = (U8*)OsHeapAllocate(param_size);
    MemoryCopy(param, &fn, sizeof(fn));
    MemoryCopy(param + sizeof(fn), data.data, data.size);
    
    HANDLE handle = CreateThread(NULL, 0, win_thread_main, param, 0, NULL);
    
    OS_Thread thread = { (U64)handle };
    return thread;
}

void OsThreadWait(OS_Thread thread, U32 millis)
{
    DWORD dw_millis = millis;
    if (millis == U32_MAX) dw_millis = INFINITE;
    
    WaitForSingleObject((HANDLE)thread.value, dw_millis);
}

void OsThreadYield() {
    SwitchToThread();
}

OS_Semaphore OsSemaphoreCreate(U32 initial_count, U32 max_count)
{
    HANDLE s = CreateSemaphoreExA(NULL, initial_count, max_count, NULL, 0, SEMAPHORE_ALL_ACCESS);
    OS_Semaphore sem = { (U64)s };
    return sem;
}

void OsSemaphoreWait(OS_Semaphore semaphore, U32 millis)
{
    Assert(semaphore.value);
    if (semaphore.value == 0) return;
    
    HANDLE s = (HANDLE)semaphore.value;
    WaitForSingleObjectEx(s, millis, FALSE);
}


B32 OsSemaphoreRelease(OS_Semaphore semaphore, U32 count)
{
    Assert(semaphore.value);
    if (semaphore.value == 0) return FALSE;
    
    HANDLE s = (HANDLE)semaphore.value;
    return ReleaseSemaphore(s, count, 0) ? 1 : 0;
}

void OsSemaphoreDestroy(OS_Semaphore semaphore)
{
    if (semaphore.value == 0) return;
    
    HANDLE s = (HANDLE)semaphore.value;
    CloseHandle(s);
}

internal_fn String StringFromWinError(DWORD error)
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
    return StringFromWinError(error);
}

B32 OsPathExists(String path)
{
    String path0 = StrCopy(context.arena, path);
    return GetFileAttributes(path0.data) != INVALID_FILE_ATTRIBUTES;
}

Result OsReadEntireFile(Arena* arena, String path, RBuffer* result)
{
    *result = {};
    String path0 = StrCopy(context.arena, path);
    
    
    HANDLE file = CreateFile(path0.data, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    RBuffer buffer;
    buffer.size = (U32)GetFileSize(file, NULL);
    buffer.data = (U8*)ArenaPush(arena, (U32)buffer.size);
    
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    ReadFile(file, buffer.data, (U32)buffer.size, NULL, NULL);
    
    CloseHandle(file);
    
    *result = buffer;
    return RESULT_SUCCESS;
}

Result OsWriteEntireFile(String path, RBuffer data)
{
    String path0 = StrCopy(context.arena, path);
    HANDLE file = CreateFile(path0.data, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (file == INVALID_HANDLE_VALUE) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    WriteFile(file, data.data, (DWORD)data.size, NULL, NULL);
    
    CloseHandle(file);
    
    return RESULT_SUCCESS;
}

Result OsCopyFile(String dst_path, String src_path, B32 override)
{
    String dst_path0 = StrCopy(context.arena, dst_path);
    String src_path0 = StrCopy(context.arena, src_path);
    
    B32 fail_if_exists = !override;
    
    B32 success = CopyFile(src_path0.data, dst_path0.data, fail_if_exists) != 0;
    if (!success) return ResultMakeFailed(string_from_last_error());
    return RESULT_SUCCESS;
}

Result OsMoveFile(String dst_path, String src_path)
{
    String dst_path0 = StrCopy(context.arena, dst_path);
    String src_path0 = StrCopy(context.arena, src_path);
    
    B32 success = MoveFile(src_path0.data, dst_path0.data) != 0;
    if (!success) return ResultMakeFailed(string_from_last_error());
    return RESULT_SUCCESS;
}

Result OsDeleteFile(String path)
{
    String path0 = StrCopy(context.arena, path);
    
    if (SetFileAttributes(path0.data, FILE_ATTRIBUTE_NORMAL) == 0) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    B32 success = DeleteFile(path0.data) != 0;
    if (!success) return ResultMakeFailed(string_from_last_error());
    return RESULT_SUCCESS;
}

internal_fn Result CreateRecursivePath(String path)
{
    Array<String> splits = StrSplit(context.arena, path, "/");
    Array<String> folders = array_make(splits.data, splits.count - 1);
    
    String last_folder = "";
    
    foreach(i, folders.count)
    {
        String folder = StrFormat(context.arena, "%S%S/", last_folder, folders[i]);
        last_folder = folder;
        
        if (folder.size <= 3 && folder[1] == ':') continue;
        
        String folder0 = StrCopy(context.arena, folder);
        if (!CreateDirectory(folder0.data, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                return ResultMakeFailed(StringFromWinError(error));
            }
        }
    }
    
    return RESULT_SUCCESS;
}

Result OsCreateDirectory(String path, B32 recursive)
{
    String path0 = StrCopy(context.arena, path);
    
	if (recursive) {
        Result res = CreateRecursivePath(path0);
        if (res.failed) return res;
    }
    
    B32 success = (B32)CreateDirectory(path0.data, NULL);
    
    DWORD error = 0;
    if (!success) {
        error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) success = true;
    }
    
    if (!success) return ResultMakeFailed(string_from_last_error());
    return RESULT_SUCCESS;
}

internal_fn Result DeleteDirectoryRecursive(String path)
{
    // NOTE(Jose): "path" must be null terminated!!
    
    String path_querry = StrFormat(context.arena, "%S\\*", path);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFileA(path_querry.data, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    while (1)
    {
        String file_name = find_data.cFileName;
        
        B32 file_is_valid = true;
        
        if (file_name == "." || file_name == "..") {
            file_is_valid = false;
        }
        
        if (file_is_valid)
        {
            String file_path = StrFormat(context.arena, "%S/%S", path, file_name);
            
            Result res;
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                res = DeleteDirectoryRecursive(file_path);
            } else {
                res = OsDeleteFile(file_path);
            }
            
            if (res.failed) return res;
        }
        
        if (FindNextFileA(find_handle, &find_data) == 0) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                return ResultMakeFailed(string_from_last_error());
            }
            break;
        }
    }
    
    if (FindClose(find_handle) == 0) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    if (SetFileAttributesA(path.data, FILE_ATTRIBUTE_NORMAL) == 0) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    if (RemoveDirectoryA(path.data) == 0) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    return RESULT_SUCCESS;
}

Result OsDeleteDirectory(String path)
{
    String path0 = StrCopy(context.arena, path);
    return DeleteDirectoryRecursive(path0);
}

internal_fn Result CopyDirectoryRecursive(String dst_path, String src_path, B32 is_moving)
{
    // NOTE(Jose): All paths in parameters must be null terminated!!
    
    {
        Result res = OsCreateDirectory(dst_path, true);
        if (res.failed) return res;
    }
    
    String path_querry = StrFormat(context.arena, "%S\\*", src_path);
    
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFileA(path_querry.data, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    while (1)
    {
        String file_name = find_data.cFileName;
        
        B32 file_is_valid = true;
        
        if (file_name == "." || file_name == "..") {
            file_is_valid = false;
        }
        
        if (file_is_valid)
        {
            String file_src = StrFormat(context.arena, "%S/%S", src_path, file_name);
            String file_dst = StrFormat(context.arena, "%S/%S", dst_path, file_name);
            
            Result res;
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                res = CopyDirectoryRecursive(file_dst, file_src, is_moving);
            } else {
                res = OsCopyFile(file_dst, file_src, false);
            }
            
            if (res.failed) return res;
        }
        
        if (FindNextFileA(find_handle, &find_data) == 0) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                return ResultMakeFailed(string_from_last_error());
            }
            break;
        }
    }
    
    if (FindClose(find_handle) == 0) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    if (is_moving) {
        Result res = DeleteDirectoryRecursive(src_path);
        if (res.failed) return res;
    }
    
    return RESULT_SUCCESS;
}

internal_fn Result CopyOrMoveDirectory(String dst_path, String src_path, B32 is_moving)
{
    // NOTE(Jose): All paths in parameters must be null terminated!!
    
    if (OsPathExists(dst_path)) {
        return ResultMakeFailed("The destination already exists");
    }
    
    return CopyDirectoryRecursive(dst_path, src_path, is_moving);
}

Result OsCopyDirectory(String dst_path, String src_path)
{
    String dst_path0 = StrCopy(context.arena, dst_path);
    String src_path0 = StrCopy(context.arena, src_path);
    
    return CopyOrMoveDirectory(dst_path0, src_path0, false);
}

Result OsMoveDirectory(String dst_path, String src_path)
{
    String dst_path0 = StrCopy(context.arena, dst_path);
    String src_path0 = StrCopy(context.arena, src_path);
    
    return CopyOrMoveDirectory(dst_path0, src_path0, true);
}

inline_fn Date DateFromSYSTEMTIME(SYSTEMTIME sys)
{
	Date date;
	date.year = (U32)sys.wYear;
	date.month = (U32)sys.wMonth;
	date.day = (U32)sys.wDay;
	date.hour = (U32)sys.wHour;
	date.minute = (U32)sys.wMinute;
	date.second = (U32)sys.wSecond;
	date.millisecond = (U32)sys.wMilliseconds;
	return date;
}

inline_fn Date DateFromFILETIME(FILETIME file)
{
	SYSTEMTIME sys;
	FileTimeToSystemTime(&file, &sys);
	return DateFromSYSTEMTIME(sys);
}

inline_fn FileInfo FileInfoFrom_WIN32_FIND_DATAA(Arena* arena, WIN32_FIND_DATAA d, String path)
{
    FileInfo info{};
    
    info.is_directory = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    info.create_date = DateFromFILETIME(d.ftCreationTime);
    info.last_write_date = DateFromFILETIME(d.ftLastWriteTime);
    info.last_access_date = DateFromFILETIME(d.ftLastAccessTime);
    
    const char* filename = (const char*)d.cFileName;
    
    StringBuilder builder = string_builder_make(context.arena);
    append(&builder, path);
    if (path.size != 0 && path[path.size - 1] != '/') append_char(&builder, '/');
    append(&builder, filename);
    
    FileInfoSetPath(&info, string_from_builder(arena, &builder));
    return info;
}

Result OsFileGetInfo(Arena* arena, String path, FileInfo* ret)
{
    *ret = {};
    FileInfo info = {};
    
    String path0 = StrCopy(arena, path);
    FileInfoSetPath(&info, path0);
    
    WIN32_FILE_ATTRIBUTE_DATA att;
    if (!GetFileAttributesExA(path0.data, GetFileExInfoStandard, &att)) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    info.is_directory = (att.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    
    info.create_date      = DateFromFILETIME(att.ftCreationTime);
    info.last_write_date  = DateFromFILETIME(att.ftLastWriteTime);
    info.last_access_date = DateFromFILETIME(att.ftLastAccessTime);
    
    *ret = info;
    return RESULT_SUCCESS;
}

Result OsDirGetFilesInfo(Arena* arena, String path, Array<FileInfo>* ret)
{
    *ret = {};
    
    // Clear path
    String path0;
    {
        StringBuilder builder = string_builder_make(context.arena);
        
        if (!OsPathIsAbsolute(path)) {
            append(&builder, ".\\");
        }
        
        foreach(i, path.size) {
            if (path[i] == '/') append_char(&builder, '\\');
            else append_char(&builder, path[i]);
        }
        
        if (path[path.size - 1] != '/') append_char(&builder, '\\');
        append_char(&builder, '*');
        
        path0 = string_from_builder(context.arena, &builder);
    }
    
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(path0.data, &data);
    
    if (find == INVALID_HANDLE_VALUE) return {};
    
    auto files = pooled_array_make<FileInfo>(context.arena, 32);
    
    while (1) {
        if (!StrEquals(data.cFileName, ".") && !StrEquals(data.cFileName, "..")) {
            array_add(&files, FileInfoFrom_WIN32_FIND_DATAA(arena, data, path));
        }
        if (!FindNextFileA(find, &data)) break;
    }
    
    FindClose(find);
    
    *ret = array_from_pooled_array(arena, files);
    return RESULT_SUCCESS;
}


B32 OsAskYesNo(String title, String content)
{
    String title0 = StrCopy(context.arena, title);
    String content0 = StrCopy(context.arena, content);
    return MessageBox(0, content0.data, title0.data, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

CallOutput OsCall(Arena* arena, String working_dir, String command, RedirectStdout redirect_stdout)
{
    if (working_dir.size == 0) working_dir = ".";
    
    String env_separator = "\"===>YOV SEPARATOR<===\"";
    
    if (redirect_stdout == RedirectStdout_ImportEnv) {
        command = StrFormat(context.arena, "cmd.exe /c \"\"%S\" && echo %S && set\"", command, env_separator);
    }
    
    String command0 = StrCopy(context.arena, command);
    String working_dir0 = StrCopy(context.arena, working_dir);
    
    SECURITY_ATTRIBUTES security_attr = {};
    security_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attr.bInheritHandle = true;
    security_attr.lpSecurityDescriptor = NULL;
    
    HANDLE stdout_read = 0;
    HANDLE stdout_write = 0;
    
    CallOutput res{};
    
    if (!CreatePipe(&stdout_read, &stdout_write, &security_attr, 0)) {
        res.result = ResultMakeFailed(string_from_last_error());
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
    
    B32 inherit_handles = redirect_stdout != RedirectStdout_Console;
    
    U32 creation_flags = 0;
    
    if (redirect_stdout == RedirectStdout_ImportEnv) {
        creation_flags |= CREATE_NO_WINDOW;
    }
    
    if (!CreateProcessA(NULL, command0.data, NULL, NULL, inherit_handles, creation_flags, NULL, working_dir0.data, &si, &pi)) {
        res.result = ResultMakeFailed(string_from_last_error());
        return res;
    }
    
    CloseHandle(stdout_write);
    
    // Read stdout
    {
        StringBuilder builder = string_builder_make(context.arena);
        
        U32 buffer_size = 4096;
        CHAR* buffer = (CHAR*)ArenaPush(context.arena, buffer_size);
        
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
            append(&builder, StrMake(buffer, bytes_read));
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
        I32 exit_code;
        if (GetExitCodeProcess(pi.hProcess, &_exit_code) == 0) {
            exit_code = -1;
        }
        else {
            exit_code = _exit_code;
        }
        
        if (exit_code != 0) res.result = ResultMakeFailed("Exited with code != 0", exit_code);
        else {
            res.result = RESULT_SUCCESS;
            res.result.code = exit_code;
        }
    }
    
    // Import env
    if (res.result.code == 0 && redirect_stdout == RedirectStdout_ImportEnv)
    {
        Array<String> split = StrSplit(context.arena, res.stdout, env_separator);
        if (split.count != 2) {
            res.result = ResultMakeFailed("Wrong environment format");
            return res;
        }
        
        Array<String> envs = StrSplit(context.arena, split[1], "\r\n");
        foreach(i, envs.count)
        {
            String env = envs[i];
            
            I64 equals_index = -1;
            for (I64 i = 0; i < env.size; i++) {
                if (env[i] == '=') {
                    equals_index = i;
                    break;
                }
            }
            
            if (equals_index < 0) continue;
            
            String name = StrSub(env, 0, equals_index);
            String value = StrSub(env, equals_index + 1, env.size - equals_index - 1);
            
            name = StrCopy(context.arena, name);
            value = StrCopy(context.arena, value);
            
            SetEnvironmentVariableA(name.data, value.data);
        }
    }
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(stdout_read);
    
    return res;
}

CallOutput OsCallExe(Arena* arena, String working_dir, String exe, String args, RedirectStdout redirect_stdout)
{
    String command = StrFormat(context.arena, "\"%S.exe\" %S", exe, args);
    return OsCall(arena, working_dir, command, redirect_stdout);
}

B32 OsPathIsAbsolute(String path)
{
    if (path.size < 3) return false;
    if (path[1] != ':') return false;
    if (path[2] != '/') return false;
    return true;
}

B32 OsPathIsDirectory(String path)
{
    String last_element = PathGetLastElement(path);
    foreach(i, last_element.size) {
        if (last_element[i] == '.') return false;
    }
    
    String path0 = StrCopy(context.arena, path);
    DWORD att = GetFileAttributesA(path0.data);
    if (att == INVALID_FILE_ATTRIBUTES) return false;
    
    return (att & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

Array<String> OsGetArgs(Arena* arena)
{
    String line = GetCommandLineA();
    
    PooledArray<String> list = pooled_array_make<String>(context.arena, 16);
    U32 arg_index = 0;
    
    U64 cursor = 0;
    while (cursor < line.size)
    {
        char c = line[cursor];
        
        char sep = ' ';
        
        if (c == '"') {
            sep = '"';
            cursor++;
        }
        
        U64 start_cursor = cursor;
        while (cursor < line.size && line[cursor] != sep) cursor++;
        
        if (arg_index > 0) {
            String arg = StrSub(line, start_cursor, cursor - start_cursor);
            array_add(&list, StrCopy(arena, arg));
        }
        arg_index++;
        
        cursor++;
        while (cursor < line.size && line[cursor] == ' ') cursor++;
    }
    
    return array_from_pooled_array(arena, list);
}

Result OsEnvGet(Arena* arena, String* out, String name)
{
    *out = "";
    String name0 = StrCopy(context.arena, name);
    
    // "size" includes null-terminator
    DWORD buffer_size = GetEnvironmentVariable(name0.data, NULL, 0);
    
    if (buffer_size <= 0) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    out->data = (char*)ArenaPush(arena, buffer_size);
    // This new size does not include null-terminator, thanks Windows for the confusion
    DWORD size = GetEnvironmentVariable(name0.data, out->data, (DWORD)buffer_size);
    out->size = size;
    
    return RESULT_SUCCESS;
}

void OsThreadSleep(U64 millis) {
    Sleep((DWORD)millis);
}

void OsConsoleWait()
{
    PrintF("\nPress RETURN to continue...\n");
    
    HANDLE window = GetConsoleWindow();
    
    while (1) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
            return;
        }
        OsThreadSleep(50);
    }
}

U32 MSVCGetEnvImported() {
    CallOutput res = OsCall(context.arena, {}, "cl", RedirectStdout_Script);
    if (res.result.code != 0) return MSVC_Env_None;
    
    String out = res.stdout;
    
    for (I64 i = 0; i < out.size - 3; i++) {
        if (out[i] != 'x') continue;
        String arch = StrSub(out, i, 3);
        if (StrEquals(arch, "x64")) return MSVC_Env_x64;
        if (StrEquals(arch, "x86")) return MSVC_Env_x86;
        if (StrEquals(arch, "x32")) return MSVC_Env_x86;
    }
    
    return MSVC_Env_None;
}

Result MSVCFindPath(Arena* arena, String* out)
{
    *out = {};
    
    char* program_files_path_buffer = (char*)ArenaPush(context.arena, MAX_PATH);
    if (!ExpandEnvironmentStringsA("%ProgramFiles(x86)%", program_files_path_buffer, MAX_PATH)) {
        return ResultMakeFailed(string_from_last_error());
    }
    
    String program_files_path = PathResolve(context.arena, program_files_path_buffer);
    String vs_where_path = StrFormat(context.arena, "%S/Microsoft Visual Studio/Installer/vswhere.exe", program_files_path);
    String vs_where_command = StrFormat(context.arena, "\"%S\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath", vs_where_path);
    
    CallOutput call_res = OsCall(context.arena, {}, vs_where_command, RedirectStdout_Script);
    if (call_res.result.failed) return call_res.result;
    
    *out = PathResolve(arena, call_res.stdout);
    return RESULT_SUCCESS;
}

Result MSVCImportEnv(U32 mode)
{
    Result res = RESULT_SUCCESS;
    
    // Check if its already initialized
    if (MSVCGetEnvImported() == mode) {
        //print_info("MSVC Env already imported for %s\n", mode == MSVC_Env_x64 ? "x64" : "x86");
        return res;
    }
    
    // Find batch path
    String batch_path = {};
    {
        String vs_path;
        res = MSVCFindPath(context.arena, &vs_path);
        if (res.failed) return res;
        
        String batch_name = (mode == MSVC_Env_x86) ? "vcvars32" : "vcvars64";
        batch_path = StrFormat(context.arena, "%S/VC/Auxiliary/Build/%S.bat", vs_path, batch_name);
    }
    
    // Execute
    CallOutput call_res = OsCall(context.arena, {}, batch_path, RedirectStdout_ImportEnv);
    if (call_res.result.failed) return call_res.result;
    
    if (MSVCGetEnvImported() != mode) {
        return ResultMakeFailed("MSVC environment is not imported properly");
    }
    
    //print_info("MSVC Env ready for %s\n", mode == MSVC_Env_x64 ? "x64" : "x86");
    
    return res;
}

U64 OsTimerGet()
{
    LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
    return now.QuadPart;
}

U32 _AtomicIncrement32(volatile U32* dst)
{
    return InterlockedIncrement((volatile LONG*)dst);
}

U32 _AtomicDecrement32(volatile U32* dst)
{
    return InterlockedDecrement((volatile LONG*)dst);
}

U32 _AtomicAdd32(volatile U32* dst, U32 add)
{
    return InterlockedAdd((volatile LONG*)dst, add);
}

U32 _AtomicCompareExchange32_Full(volatile U32* dst, U32 compare, U32 exchange)
{
    return InterlockedCompareExchange((volatile LONG*)dst, exchange, compare);
}

U32 _AtomicCompareExchange32_Acquire(volatile U32* dst, U32 compare, U32 exchange)
{
    return InterlockedCompareExchangeAcquire((volatile LONG*)dst, exchange, compare);
}

U32 _AtomicCompareExchange32_Release(volatile U32* dst, U32 compare, U32 exchange)
{
    return InterlockedCompareExchangeRelease((volatile LONG*)dst, exchange, compare);
}

U32 _AtomicStore32(volatile U32* dst, U32 src)
{
    return InterlockedExchange((volatile LONG*)dst, src);
}

#endif