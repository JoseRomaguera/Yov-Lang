#pragma once

#define YOV_MAJOR_VERSION 0
#define YOV_MINOR_VERSION 2
#define YOV_REVISION_VERSION 0
#define YOV_VERSION STR("v"MACRO_STR(YOV_MAJOR_VERSION)"."MACRO_STR(YOV_MINOR_VERSION)"."MACRO_STR(YOV_REVISION_VERSION))

// DEBUG

#define DEV_ASAN          DEV && 1
#define DEV_LOCATION_INFO DEV && 0

//-

#include <stdint.h>
#include <stdarg.h>
#include <cstring>

typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef int8_t I8;
typedef int16_t I16;
typedef int32_t I32;
typedef int64_t I64;

typedef uint8_t B8;
typedef uint32_t B32;

typedef float F32;
typedef double F64;
static_assert(sizeof(F32) == 4);
static_assert(sizeof(F64) == 8);

struct RangeU32 {
    U32 min, max;
};
struct RangeU64 {
    U64 min, max;
};

typedef volatile U32 Mutex;

#define U8_MAX  0xFF
#define U16_MAX 0xFFFF
#define U32_MAX 0xFFFFFFFF
#define U64_MAX 0xFFFFFFFFFFFFFFFF

#define I16_MIN -32768
#define I32_MIN ((I32)(-2147483648))
#define I64_MIN (-1 - 0x7FFFFFFFFFFFFFFF)

#define I16_MAX 32767
#define I32_MAX 2147483647
#define I64_MAX 0x7FFFFFFFFFFFFFFF

#define F32_MIN -3.402823e+38f
#define F32_MAX 3.402823e+38f

#define Kb(v) (((U64)v) << 10ULL)
#define Mb(v) (((U64)v) << 20ULL)
#define Gb(v) (((U64)v) << 30ULL)
#define Tb(v) (((U64)v) << 40ULL)

#define Min(x, y) (((x) < (y)) ? (x) : (y))
#define Max(x, y) (((x) > (y)) ? (x) : (y))
#define Clamp(x, _min, _max) Max(Min(x, _max), _min)
#define Abs(x) (((x) < 0) ? (-(x)) : (x))
#define Swap(a, b) do { auto& _a = (a); auto& _b = (b); auto aux = _b; _b = _a; _a = aux; } while(0)
#define Bit(x) (1ULL << (x))

#define _Join(x, y) x##y
#define Join(x, y) _Join(x, y)

#if _WIN32
#define OS_WINDOWS 1
#endif

#if _MSC_VER
#define COMPILER_MSVC 1
#else
#define COMPILER_MSVC 0
#endif

#if __clang
#define COMPILER_CLANG 1
#else
#define COMPILER_CLANG 0
#endif

#if __GNUC__
#define COMPILER_GCC 1
#else
#endif

#if COMPILER_MSVC
# define no_asan __declspec(no_sanitize_address)
#elif (COMPILER_CLANG || COMPILER_GCC)
# define no_asan __attribute__((no_sanitize("address")))
#endif
#if !defined(no_asan)
# define no_asan
#endif

#if COMPILER_MSVC
#include <intrin.h>
#define CompilerReadBarrier() _ReadBarrier()
#define CompilerWriteBarrier() do { _WriteBarrier(); _mm_sfence(); } while(0)
#elif (COMPILER_CLANG || COMPILER_GCC)
#define CompilerReadBarrier() __sync_synchronize()
#define CompilerWriteBarrier() __sync_synchronize()
#else
#endif

#if OS_WINDOWS
#pragma section(".rdonly", read)
#define read_only no_asan __declspec(allocate(".rdonly"))
#else
#define read_only
#endif

#define no_inline_fn __declspec(noinline)
#define inline_fn inline
#define internal_fn static
#define global_var extern
#define per_thread_var __declspec(thread)

#define countof(x) (sizeof(x) / sizeof(x[0]))
#define arrayof(x) { x, countof(x) }

#define foreach(it, count) for (U32 (it) = 0; (it) < (count); (it)++)

#if DEV
#define Assert(x) do { if ((x) == 0) AssertionFailed(#x, __FILE__, __LINE__); } while (0)
#define InvalidCodepath() AssertionFailed("Invalid Codepath", __FILE__, __LINE__);
#else
#define Assert(x) do {} while (0)
#define InvalidCodepath() do {} while (0)
#endif

inline_fn void EmptyFunction(...) {}

template <typename F>
struct _defer {
	F f;
	_defer(F f) : f(f) {}
	~_defer() { f(); }
};

template <typename F>
_defer<F> _defer_func(F f) {
	return _defer<F>(f);
}

#define _defer_name(x) Join(x, __COUNTER__)
#define defer(code)   auto _defer_name(_defer_) = _defer_func([&](){code;})

void AssertionFailed(const char* text, const char* file, U32 line);

#define MemoryCopy(dst, src, size) memcpy(dst, src, size)
#define MemoryZero(dst, size) memset(dst, 0, size)

#define _MACRO_STR(x) #x
#define MACRO_STR(x) _MACRO_STR(x)

//- C STRING

U32 CStrSize(const char* str);
U32 CStrSet(char* dst, const char* src, U32 src_size, U32 buff_size);
U32 CStrCopy(char* dst, const char* src, U32 buff_size);
U32 CStrAppend(char* dst, const char* src, U32 buff_size);
void CStrFromU64(char* dst, U64 value, U32 base = 10);
void CStrFromI64(char* dst, I64 value, U32 base = 10);
void CStrFromF64(char* dst, F64 value, U32 decimals);

//- BASE STRUCTS

struct Arena;

struct RBuffer {
    void* data;
    U64 size;
    
    inline U8& operator[](U64 index) {
        Assert(index < size);
        return ((U8*)data)[index];
    }
};

struct String {
    char* data;
    U64 size;
    
    inline U8& operator[](U64 index) {
        Assert(index < size);
        return ((U8*)data)[index];
    }
    
    String() = default;
    
    String(const char* cstr) {
        size = CStrSize(cstr);
        data = (char*)cstr;
    }
};

template<typename T>
struct Array {
    T* data;
    U32 count;
    
    inline T& operator[](U64 index) {
        Assert(index < count);
        return data[index];
    }
};

struct LLNode {
    LLNode* next;
    LLNode* prev;
};

template<typename T>
struct LinkedList {
    Arena* arena;
    LLNode* root;
    LLNode* tail;
    U32 count;
};

struct StringBuilder {
    LinkedList<String> ll;
    Arena* arena;
    char* buffer;
    U64 buffer_size;
    U64 buffer_pos;
};

struct Date
{
	U32 year;
	U32 month;
	U32 day;
	U32 hour;
	U32 minute;
	U32 second;
	U32 millisecond;
};

#define DATE_HIGHEST DateMake(U32_MAX, U32_MAX, U32_MAX, U32_MAX, U32_MAX, U32_MAX, U32_MAX)

Date DateMake(U32 year = 0, U32 month = 0, U32 day = 0, U32 hour = 0, U32 minute = 0, U32 second = 0, U32 milliseconds = 0);
B32 DateEquals(Date d0, Date d1);
B32 DateLessThan(Date d0, Date d1);

//- ARENA

struct Arena {
    void* memory;
    U64 memory_position;
    U32 reserved_pages;
    U32 commited_pages;
    U32 alignment;
    Mutex mutex;
};

Arena* ArenaAlloc(U64 capacity, U32 alignment);
void ArenaFree(Arena* arena);

void* ArenaPush(Arena* arena, U64 size);
void ArenaPopTo(Arena* arena, U64 position);

#if DEV
void ArenaProtectAndReset(Arena* arena);
#endif

#define ArenaCapture(_arena) U64 _arena_capture_pos = (_arena)->memory_position; defer(ArenaPopTo((_arena), _arena_capture_pos))

template<typename T>
inline_fn T* ArenaPushStruct(Arena* arena, U32 count = 1)
{
    T* ptr = (T*)ArenaPush(arena, sizeof(T) * count);
    return ptr;
}

//- OS

void SetupGlobals();
void ShutdownGlobals();

struct Result {
    String message;
    I64 code;
    B32 failed;
};

inline_fn Result ResultMakeFailed(String message, I64 error_code = -1) { return Result{ message, error_code, true }; }

#define RESULT_SUCCESS Result{ "", 0, false }

enum PrintLevel {
    PrintLevel_UserCode,
    PrintLevel_DevLog,
    PrintLevel_InfoReport,
    PrintLevel_WarningReport,
    PrintLevel_ErrorReport,
};

void OsPrint(PrintLevel level, String text);
void OsConsoleClear();

void* OsHeapAllocate(U64 size);
void  OsHeapFree(void* address);

void* OsReserveVirtualMemory(U32 pages, B32 commit);
void OsCommitVirtualMemory(void* address, U32 page_offset, U32 page_count);
void OsReleaseVirtualMemory(void* address);
void OsProtectVirtualMemory(void* address, U32 pages);

struct OS_Thread { U64 value; };
struct OS_Semaphore { U64 value; };

typedef I32 ThreadFn(void*);
OS_Thread OsThreadStart(ThreadFn* fn, RBuffer data);
void OsThreadWait(OS_Thread thread, U32 millis);
void OsThreadYield();

OS_Semaphore OsSemaphoreCreate(U32 initial_count, U32 max_count);
void OsSemaphoreWait(OS_Semaphore semaphore, U32 millis);
B32  OsSemaphoreRelease(OS_Semaphore semaphore, U32 count);
void OsSemaphoreDestroy(OS_Semaphore semaphore);

struct FileInfo {
    // Example of C:/Folder/foo.txt
    String path; // C:/Folder/foo.txt
    String folder; // C:/Folder/
    String name; // foo.txt
    String name_without_extension; // foo
    String extension; // txt
    
    Date create_date;
    Date last_write_date;
    Date last_access_date;
    
    B32 is_directory;
};

void FileInfoSetPath(FileInfo* info, String path);

B32 OsPathExists(String path);
Result OsReadEntireFile(Arena* arena, String path, RBuffer* result);
Result OsWriteEntireFile(String path, RBuffer data);
Result OsCopyFile(String dst_path, String src_path, B32 override);
Result OsMoveFile(String dst_path, String src_path);
Result OsDeleteFile(String path);
Result OsCreateDirectory(String path, B32 recursive);
Result OsDeleteDirectory(String path);
Result OsCopyDirectory(String dst_path, String src_path);
Result OsMoveDirectory(String dst_path, String src_path);
Result OsFileGetInfo(Arena* arena, String path, FileInfo* ret);
Result OsDirGetFilesInfo(Arena* arena, String path, Array<FileInfo>* ret);

B32 OsAskYesNo(String title, String content);

enum RedirectStdout {
    RedirectStdout_Console,
    RedirectStdout_Ignore,
    RedirectStdout_Script,
    RedirectStdout_ImportEnv,
};

struct CallOutput {
    Result result;
    String stdout;
};

CallOutput OsCall(Arena* arena, String working_dir, String command, RedirectStdout redirect_stdout);
CallOutput OsCallExe(Arena* arena, String working_dir, String exe, String args, RedirectStdout redirect_stdout);

B32 OsPathIsAbsolute(String path);
B32 OsPathIsDirectory(String path);

Array<String> OsGetArgs(Arena* arena);
Result OsEnvGet(Arena* arena, String* out, String name);

void OsThreadSleep(U64 millis);
void OsConsoleWait();

#define MSVC_Env_None 0
#define MSVC_Env_x64 2
#define MSVC_Env_x86 3

U32    MSVCGetEnvImported();
Result MSVCFindPath(String* out);
Result MSVCImportEnv(U32 mode);

U64 OsTimerGet();
F64 TimerNow();

#if OS_WINDOWS

#define AtomicIncrement32(_dst) _AtomicIncrement32((volatile U32*)(_dst))
#define AtomicDecrement32(_dst) _AtomicDecrement32((volatile U32*)(_dst))
#define AtomicAdd32(_dst, _add) _AtomicAdd32((volatile U32*)(_dst), _add)
#define AtomicCompareExchange32_Full(_dst, _compare, _exchange) _AtomicCompareExchange32_Full((volatile U32*)(_dst), (U32)_compare, (U32)_exchange)
#define AtomicCompareExchange32_Acquire(_dst, _compare, _exchange) _AtomicCompareExchange32_Acquire((volatile U32*)(_dst), (U32)_compare, (U32)_exchange)
#define AtomicCompareExchange32_Release(_dst, _compare, _exchange) _AtomicCompareExchange32_Release((volatile U32*)(_dst), (U32)_compare, (U32)_exchange)
#define AtomicStore32(_dst, _src) _AtomicStore32((volatile U32*)(_dst), (U32)_src)

#endif

U32 _AtomicIncrement32(volatile U32* dst);
U32 _AtomicDecrement32(volatile U32* dst);
U32 _AtomicAdd32(volatile U32* dst, U32 add);
U32 _AtomicCompareExchange32_Full(volatile U32* dst, U32 compare, U32 exchange);
U32 _AtomicCompareExchange32_Acquire(volatile U32* dst, U32 compare, U32 exchange);
U32 _AtomicCompareExchange32_Release(volatile U32* dst, U32 compare, U32 exchange);
U32 _AtomicStore32(volatile U32* dst, U32 src);

//- MULTITHREADING 

struct LaneContext;
typedef void LaneFn(LaneContext* lane);

struct LaneGroup {
    Arena* arena;
    Array<OS_Thread> threads;
    LaneFn* fn;
    void* user_data;
    
    volatile U32 barrier_counter; // Used on "lane_barrier()"
    
    union {
        void* ptr;
    } sync;
    
    // Non-Uniform task distrubution
    volatile U32 task_next;
    volatile U32 task_total;
    volatile U32 task_finished; // For dynamic tasks
};

struct LaneContext {
    LaneGroup* group;
    U32 id;
    U32 count;
};

LaneGroup* LaneGroupStart(Arena* arena, LaneFn* fn, void* user_data, U32 lane_count = U32_MAX);
void LaneGroupWait(LaneGroup* group);

void LaneBarrier(LaneContext* lane);
B32 LaneNarrow(LaneContext* lane, U32 index = 0);

void LaneSyncPtr(LaneContext* lane, void** ptr, U32 index);

RangeU32 LaneDistributeUniformWork(LaneContext* lane, U32 count);

void LaneTaskStart(LaneContext* lane, U32 count);
void LaneTaskAdd(LaneGroup* group, U32 count);
B32 LaneTaskFetch(LaneGroup* group, U32* index);

B32 LaneDynamicTaskIsBusy(LaneGroup* group);
void LaneDynamicTaskFinish(LaneGroup* group);

B32 MutexTryLock(Mutex* mutex);
void MutexLock(Mutex* mutex);
B32 MutexIsLocked(Mutex* mutex);
void MutexUnlock(Mutex* mutex);

#define MutexLockGuard(mutex) MutexLock(mutex); defer(MutexUnlock(mutex))


//- MATH

U64 U64DivideHigh(U64 n0, U64 n1);
U32 U32DivideHigh(U32 n0, U32 n1);
U32 PagesFromBytes(U64 bytes);

//- STRING

String StrMake(const char* cstr, U64 size);
String StrFromCStr(const char* cstr);
String StrFromRBuffer(RBuffer buffer);
String StrAlloc(Arena* arena, U64 size);
String StrCopy(Arena* arena, String src);
Array<String> StrArrayCopy(Arena* arena, Array<String> src);
String StrHeapCopy(String src);
String StrSub(String str, U64 offset, U64 size);
B32 StrEquals(String s0, String s1);
B32 StrStarts(String str, String with);
B32 StrEnds(String str, String with);
B32 U32FromString(U32* dst, String str);
B32 U32FromChar(U32* dst, char c);
B32 I64FromString(String str, I64* out);
B32 I32FromString(String str, I32* out);
String StringFromCodepoint(Arena* arena, U32 codepoint);
String StringFromMemory(U64 bytes);
String StringFromEllapsedTime(F64 seconds);
String StrJoin(Arena* arena, LinkedList<String> ll);
Array<String> StrSplit(Arena* arena, String str, String separator);
String StrReplace(Arena* arena, String str, String old_str, String new_str);
String string_format_with_args(Arena* arena, String string, va_list args);
String string_format_ex(Arena* arena, String string, ...);
U32 StrGetCodepoint(String str, U64* cursor_ptr);
U32 StrCalculateCharCount(String str);
String escape_string_from_raw_string(Arena* arena, String raw);

inline_fn B32 operator==(String s0, String s1) { return StrEquals(s0, s1); }
inline_fn B32 operator!=(String s0, String s1) { return !StrEquals(s0, s1); }

inline_fn String STR(String str) { return str; }
inline_fn String STR(const char* cstr) { return StrFromCStr(cstr); }

B32 codepoint_is_separator(U32 codepoint);
B32 codepoint_is_number(U32 codepoint);
B32 codepoint_is_text(U32 codepoint);

#define StrFormat(arena, str, ...) string_format_ex(arena, str, __VA_ARGS__)

//- PATH

Array<String> PathSubdivide(Arena* arena, String path);
String PathResolve(Arena* arena, String path);
String PathResolveImport(Arena* arena, String caller_script_dir, String path);
String PathAppend(Arena* arena, String str0, String str1);
String PathGetLastElement(String path);
String PathGetFolder(String path);

//- STRING BUILDER

StringBuilder string_builder_make(Arena* arena);
void appendf_ex(StringBuilder* builder, String str, ...);
void append(StringBuilder* builder, String str);
void append_codepoint(StringBuilder* builder, U32 codepoint);
void append_i64(StringBuilder* builder, I64 v, U32 base = 10);
void append_i32(StringBuilder* builder, I32 v, U32 base = 10);
void append_u64(StringBuilder* builder, U64 v, U32 base = 10);
void append_u32(StringBuilder* builder, U32 v, U32 base = 10);
void append_f64(StringBuilder* builder, F64 v, U32 decimals);
void append_char(StringBuilder* builder, char c);
String string_from_builder(Arena* arena, StringBuilder* builder);

#define appendf(builder, str, ...) appendf_ex(builder, str, __VA_ARGS__)

//- POOLED ARRAY

struct PooledArrayBlock
{
    PooledArrayBlock* next;
    U32 capacity;
    U32 count;
};

struct PooledArrayR
{
    Arena* arena;
    PooledArrayBlock* root;
    PooledArrayBlock* tail;
    PooledArrayBlock* current;
    U64 stride;
    
    U32 default_block_capacity;
    U32 count;
};

PooledArrayR pooled_array_make(Arena* arena, U64 stride, U32 block_capacity);
void array_reset(PooledArrayR* array);
void* array_add(PooledArrayR* array);
void array_erase(PooledArrayR* array, U32 index);
void array_pop(PooledArrayR* array);
U32 array_calculate_index(PooledArrayR* array, void* ptr);


#include "templates.h"

//- LOCATION

struct Location {
    RangeU64 range;
    I32 script_id;
};

inline_fn Location _NoCodeMake() { Location c{}; c.script_id = -1; return c; }
#define NO_CODE _NoCodeMake()

Location LocationMake(U64 start, U64 end, I32 script_id);
B32 LocationIsValid(Location location);
inline_fn Location LocationStartScript(I32 script_id) { return LocationMake(0, 0, script_id); }

//- REPORTER 

void PrintEx(PrintLevel level, String str, ...);
void LogInternal(String tag, String str, ...);

#define PrintF(str, ...) PrintEx(PrintLevel_InfoReport, str, __VA_ARGS__)

#define LOG_FLOW_ENABLED DEV && 0
#define LOG_TYPE_ENABLED DEV && 0
#define LOG_IR_ENABLED DEV && 0
#define LOG_MEMORY_ENABLED DEV && 0
#define LOG_TRACE_ENABLED DEV && 0

#if LOG_FLOW_ENABLED
#define LogFlow(str, ...) LogInternal("FLOW", str, __VA_ARGS__)
#else
#define LogFlow(str, ...) EmptyFunction(str, __VA_ARGS__)
#endif

#if LOG_TYPE_ENABLED
#define LogType(str, ...) LogInternal("TYPE", STR(str), __VA_ARGS__)
#else
#define LogType(str, ...) EmptyFunction(str, __VA_ARGS__)
#endif

#if LOG_IR_ENABLED
#define LogIR(str, ...) LogInternal("IR", STR(str), __VA_ARGS__)
#else
#define LogIR(str, ...) EmptyFunction(str, __VA_ARGS__)
#endif


#if LOG_MEMORY_ENABLED
#define LogMemory(str, ...) LogInternal("MEMORY", STR(str), __VA_ARGS__)
#else
#define LogMemory(str, ...) EmptyFunction(str, __VA_ARGS__)
#endif

#if LOG_TRACE_ENABLED
#define LogTrace(str, ...) LogInternal("TRACE", STR(str), __VA_ARGS__)
#else
#define LogTrace(str, ...) EmptyFunction(str, __VA_ARGS__)
#endif

#define SEPARATOR_STRING "========================="

struct Report {
    String text;
    Location location;
    
    String path;
    U32 line;
};

struct ScriptArg {
    String name;
    String value;
};

struct YovSettings {
    B8 analyze_only;
    B8 trace;
    B8 user_assert;
    B8 wait_end;
    B8 no_user;
};

struct YovThreadContext {
    Arena* arena;
};

struct YovSystemInfo {
    U64 page_size;
    U32 logical_cores;
    
    U64 timer_start;
    U64 timer_frequency;
    
    String working_path;
    String executable_path;
};

extern YovSystemInfo system_info; 
extern per_thread_var YovThreadContext context;

void InitializeThread();
void ShutdownThread();

struct Reporter {
    Arena* arena;
    Mutex mutex;
    PooledArray<Report> reports;
    
    B8 exit_requested;
    I64 exit_code;
    B8 exit_code_is_set;
};

Reporter* ReporterAlloc(Arena* arena);
void ReportErrorEx(Reporter* reporter, Location location, U32 line, String path, String text, ...);
void ReporterSetExitCode(Reporter* reporter, I64 exit_code);
void ReporterPrint(Reporter* reporter);
String StringFromReport(Arena* arena, Report report);
void PrintReport(Report report);

#define LANG_ARG_ANALYZE STR("-analyze")
#define LANG_ARG_TRACE STR("-trace")
#define LANG_ARG_USER_ASSERT STR("-user_assert")
#define LANG_ARG_WAIT_END STR("-wait_end")
#define LANG_ARG_NO_USER STR("-no_user")

struct Input {
    String main_script_path;
    String caller_dir;
    YovSettings settings;
    Array<ScriptArg> script_args;
};

Input* InputFromArgs(Arena* arena, Reporter* reporter);
ScriptArg* InputFindScriptArg(Input* input, String name);

//- REPORTS 

#define ReportErrorNoCode(_text, ...) ReportErrorEx(reporter, NO_CODE, 0, {}, _text, __VA_ARGS__);

#define report_arg_wrong_value(_n, _v) ReportErrorNoCode("Invalid argument assignment '%S = %S'", _n, _v);
#define report_arg_unknown(_v) ReportErrorNoCode("Unknown argument '%S'", _v);

#define report_intrinsic_not_resolved(_n) ReportErrorNoCode("Intrinsic '%S' can't be resolved", _n);