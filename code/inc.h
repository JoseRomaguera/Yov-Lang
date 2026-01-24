#pragma once

#define YOV_MAJOR_VERSION 0
#define YOV_MINOR_VERSION 2
#define YOV_REVISION_VERSION 0
#define YOV_VERSION "v"MACRO_STR(YOV_MAJOR_VERSION)"."MACRO_STR(YOV_MINOR_VERSION)"."MACRO_STR(YOV_REVISION_VERSION)

// DEBUG

#define DEV_ASAN          DEV && 0
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

#define ArenaCapture(_arena) U64 _arena_capture_pos = (_arena)->memory_position; DEFER(ArenaPopTo((_arena), _arena_capture_pos))

template<typename T>
inline_fn T* ArenaPushStruct(Arena* arena, U32 count = 1)
{
    T* ptr = (T*)ArenaPush(arena, sizeof(T) * count);
    return ptr;
}

//- OS

void OsSetupSystemInfo();
void OsInitialize();
void OsShutdown();

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

B32 os_ask_yesno(String title, String content);

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

CallOutput os_call(Arena* arena, String working_dir, String command, RedirectStdout redirect_stdout);
CallOutput os_call_exe(Arena* arena, String working_dir, String exe, String args, RedirectStdout redirect_stdout);
CallOutput os_call_script(Arena* arena, String working_dir, String script, String args, String yov_args, RedirectStdout redirect_stdout);

String os_get_working_path(Arena* arena);
String os_get_executable_path(Arena* arena);

B32 os_path_is_absolute(String path);
B32 os_path_is_directory(String path);

Array<String> os_get_args(Arena* arena);

Result os_env_get(Arena* arena, String* out, String name);

void os_thread_sleep(U64 millis);
void os_console_wait();

#define MSVC_Env_None 0
#define MSVC_Env_x64 2
#define MSVC_Env_x86 3

U32    MSVCGetEnvImported();
Result MSVCFindPath(String* out);
Result MSVCImportEnv(U32 mode);

U64 OsTimerGet();
F64 TimerNow();

U32 AtomicIncrementU32(volatile U32* n);
U32 AtomicDecrementU32(volatile U32* n);
U32 AtomicExchangeU32(volatile U32* dst, U32 compare, U32 exchange);
U32 AtomicAddU32(volatile U32* dst, U32 add);

I32 AtomicIncrementI32(volatile I32* n);
I32 AtomicDecrementI32(volatile I32* n);
I32 AtomicExchangeI32(volatile I32* dst, I32 compare, I32 exchange);

//- MULTITHREADING 

struct LaneContext;
typedef void LaneFn(LaneContext* lane);

struct LaneGroup {
    Arena* arena;
    Array<OS_Thread> threads;
    LaneFn* fn;
    
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

LaneGroup* LaneGroupStart(Arena* arena, LaneFn* fn, U32 lane_count = U32_MAX);
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

B32 mutex_try_lock(Mutex* mutex);
void mutex_lock(Mutex* mutex);
void mutex_lock_hot(Mutex* mutex);
B32 mutex_is_locked(Mutex* mutex);
void mutex_unlock(Mutex* mutex);

#define mutex_lock_guard(mutex) mutex_lock(mutex); defer(mutex_unlock(mutex))
#define mutex_lock_hot_guard(mutex) mutex_lock_hot(mutex); defer(mutex_unlock(mutex))


//- MATH

U64 u64_divide_high(U64 n0, U64 n1);
U32 pages_from_bytes(U64 bytes);

//- STRING

String StrMake(const char* cstr, U64 size);
String StrFromCStr(const char* cstr);
String StrFromRBuffer(RBuffer buffer);
String StringAlloc(Arena* arena, U64 size);
String StrCopy(Arena* arena, String src);
String StrSub(String str, U64 offset, U64 size);
B32 StrEquals(String s0, String s1);
B32 string_starts(String str, String with);
B32 string_ends(String str, String with);
B32 u32_from_string(U32* dst, String str);
B32 u32_from_char(U32* dst, char c);
B32 i64_from_string(String str, I64* out);
B32 i32_from_string(String str, I32* out);
String string_from_codepoint(Arena* arena, U32 codepoint);
String StringFromMemory(U64 bytes);
String StringFromEllapsedTime(F64 seconds);
String string_join(Arena* arena, LinkedList<String> ll);
Array<String> string_split(Arena* arena, String str, String separator);
String StrReplace(Arena* arena, String str, String old_str, String new_str);
String string_format_with_args(Arena* arena, String string, va_list args);
String string_format_ex(Arena* arena, String string, ...);
U32 string_get_codepoint(String str, U64* cursor_ptr);
U32 string_calculate_char_count(String str);
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

Array<String> path_subdivide(Arena* arena, String path);
String path_resolve(Arena* arena, String path);
String path_append(Arena* arena, String str0, String str1);
String path_get_last_element(String path);

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

//- MISC

enum BinaryOperator {
    BinaryOperator_Unknown,
    BinaryOperator_None,
    
    BinaryOperator_Addition,
    BinaryOperator_Substraction,
    BinaryOperator_Multiplication,
    BinaryOperator_Division,
    BinaryOperator_Modulo,
    
    BinaryOperator_LogicalNot,
    BinaryOperator_LogicalOr,
    BinaryOperator_LogicalAnd,
    
    BinaryOperator_Equals,
    BinaryOperator_NotEquals,
    BinaryOperator_LessThan,
    BinaryOperator_LessEqualsThan,
    BinaryOperator_GreaterThan,
    BinaryOperator_GreaterEqualsThan,
    
    BinaryOperator_Is,
};

String string_from_binary_operator(BinaryOperator op);
B32 binary_operator_is_arithmetic(BinaryOperator op);

struct LocationInfo {
    U32 line;
    U64 start_line_cursor;
    U32 column;
};

struct Location {
    RangeU64 range;
    I32 script_id;
#if DEV_LOCATION_INFO
    LocationInfo info;
#endif
};

inline_fn Location NoCodeMake() { Location c{}; c.script_id = -1; return c; }
#define NO_CODE NoCodeMake()

Location LocationMake(U64 start, U64 end, I32 script_id);
B32 LocationIsValid(Location location);
inline_fn Location LocationStartScript(I32 script_id) { return LocationMake(0, 0, script_id); }

LocationInfo LocationInfoMake(I32 script_id, U64 cursor);

enum TokenKind {
    TokenKind_None,
    TokenKind_Error,
    TokenKind_Separator,
    TokenKind_Comment,
    TokenKind_Identifier,
    TokenKind_IntLiteral,
    TokenKind_StringLiteral,
    TokenKind_CodepointLiteral,
    TokenKind_Dot,
    TokenKind_Comma,
    TokenKind_Colon,
    TokenKind_Arrow,
    TokenKind_OpenBracket,
    TokenKind_CloseBracket,
    TokenKind_OpenBrace,
    TokenKind_CloseBrace,
    TokenKind_OpenString,
    TokenKind_CloseString,
    TokenKind_OpenParenthesis,
    TokenKind_CloseParenthesis,
    TokenKind_NextLine,
    TokenKind_NextSentence,
    
    TokenKind_Assignment,
    
    TokenKind_PlusSign, // +
    TokenKind_MinusSign, // -
    TokenKind_Asterisk, // *
    TokenKind_Slash, // /
    TokenKind_Modulo, // %
    TokenKind_Ampersand, // &
    TokenKind_Exclamation, // !
    
    TokenKind_LogicalOr, // ||
    TokenKind_LogicalAnd, // &&
    
    TokenKind_CompEquals, // ==
    TokenKind_CompNotEquals, // !=
    TokenKind_CompLess, // <
    TokenKind_CompLessEquals, // <=
    TokenKind_CompGreater, // >
    TokenKind_CompGreaterEquals, // >=
    
    TokenKind_NullKeyword,
    TokenKind_IfKeyword,
    TokenKind_ElseKeyword,
    TokenKind_WhileKeyword,
    TokenKind_ForKeyword,
    TokenKind_IsKeyword,
    TokenKind_FuncKeyword,
    TokenKind_EnumKeyword,
    TokenKind_StructKeyword,
    TokenKind_ArgKeyword,
    TokenKind_ReturnKeyword,
    TokenKind_ContinueKeyword,
    TokenKind_BreakKeyword,
    TokenKind_ImportKeyword,
    
    TokenKind_BoolLiteral,
};

struct Token {
    TokenKind kind;
    String value;
    
    Location location;
    U64 cursor;
    U32 skip_size;
    
    union {
        BinaryOperator assignment_binary_operator;
    };
};

String string_from_tokens(Arena* arena, Array<Token> tokens);
String debug_info_from_token(Arena* arena, Token token);

BinaryOperator binary_operator_from_token(TokenKind token);
B32 token_is_sign_or_binary_op(TokenKind token);
B32 TokenIsFlowModifier(TokenKind token);
TokenKind TokenKindFromOpenScope(TokenKind open_token);

Location LocationFromTokens(Array<Token> tokens);

#include "templates.h"

//- TYPE SYSTEM

// TODO(Jose):
struct Interpreter;
struct FunctionDefinition;
struct IR_Object;
struct Object;
struct IR_Unit;
struct IR_Context;
struct StructDefinition;
struct EnumDefinition;
struct ArgDefinition;

enum PrimitiveType {
    PrimitiveType_I64,
    PrimitiveType_B32,
    PrimitiveType_String,
};

enum VKind {
    VKind_Nil,
    VKind_Void,
    VKind_Any,
    VKind_Primitive,
    VKind_Struct,
    VKind_Enum,
    VKind_Array,
    VKind_Reference,
};

struct VType {
    String base_name;
    VKind kind;
    U32 array_dimensions;
    union {
        StructDefinition* _struct;
        EnumDefinition* _enum;
        PrimitiveType primitive_type;
    };
    I32 base_index;
};

struct Reference {
    Object* parent;
    VType vtype;
    void* address;
};

struct Global {
    String identifier;
    VType vtype;
    B32 is_constant;
    Reference reference;
};

enum ValueKind {
    ValueKind_None,
    ValueKind_LValue,            // LValue
    ValueKind_Register,          // RValue
    ValueKind_StringComposition, // RValue
    ValueKind_Array,             // RValue
    ValueKind_MultipleReturn,    // RValue
    ValueKind_Literal,     // Compile-Time RValue
    ValueKind_ZeroInit,    // Compile-Time RValue
};

struct Value {
    VType vtype;
    ValueKind kind;
    union {
        struct {
            I32 index;
            I32 reference_op; // 0 -> None; 1 -> Take Reference; -1 -> Dereference
        } reg;
        I64 literal_int;
        B32 literal_bool;
        String literal_string;
        VType literal_type;
        struct {
            Array<Value> values;
            B8 is_empty;
        } array;
        
        Array<Value> string_composition;
        Array<Value> multiple_return;
    };
};

enum UnitKind {
    // IR only
    UnitKind_Error,
    UnitKind_Empty,
    
    UnitKind_Copy,
    UnitKind_Store,
    UnitKind_FunctionCall,
    UnitKind_Return,
    UnitKind_Jump,
    UnitKind_BinaryOperation,
    // Int Arithmetic
    // Int Logic
    // Bool Logic
    // String Logic
    // Enum Logic
    // String Concat
    // String-Codepoint Concat
    // Path Concat
    // Array Concat
    // Array Append
    // Op Overflow -> Function Call?
    
    UnitKind_SignOperation,
    UnitKind_Child,
    UnitKind_ResultEval,
};

struct Unit {
    UnitKind kind;
    Location location;
    I32 dst_index;
    Value src;
    union {
        struct {
            FunctionDefinition* fn;
            Array<Value> parameters;
        } function_call;
        
        struct {
            I32 condition; // 0 -> None; 1 -> true; -1 -> false
            I32 offset;
        } jump;
        
        struct {
            Value src1;
            BinaryOperator op;
        } binary_op;
        
        struct {
            BinaryOperator op;
        } sign_op;
        
        struct {
            Value child_index;
            B32 child_is_member;
        } child;
    };
};

enum RegisterKind {
    RegisterKind_None,
    RegisterKind_Local,
    RegisterKind_Parameter,
    RegisterKind_Return,
    RegisterKind_Global,
};

struct Register {
    RegisterKind kind;
    B32 is_constant;
    VType vtype;
};

inline_fn B32 RegisterIsValid(Register reg) { return reg.kind != RegisterKind_None; }

struct IR {
    B32 success;
    Value value;
    Array<Unit> instructions;
    Array<Register> local_registers;
    U32 parameter_count;
};

struct IR_Group {
    B32 success;
    Value value;
    U32 unit_count;
    IR_Unit* first;
    IR_Unit* last;
};

struct Object {
    U32 ID;
    I32 ref_count;
    VType vtype;
    Object* prev;
    Object* next;
};

struct ObjectData_Array {
    U32 count;
    U8* data;
};

struct ObjectData_Ref {
    Object* parent;
    void* address;
};

struct ObjectData_String {
    char* chars;
    U64 capacity;
    U64 size;
};

inline_fn VType MakePrimitive(const char* name, PrimitiveType type, I32 base_index) {
    VType vtype = {};
    vtype.base_name = name;
    vtype.kind = VKind_Primitive;
    vtype.primitive_type = type;
    vtype.base_index = base_index;
    return vtype;
}

#define VType_Nil (VType{ "Nil", VKind_Nil })
#define VType_Void (VType{ "void", VKind_Void })
#define VType_Any (VType{ "Any", VKind_Any })
#define VType_Int MakePrimitive("Int", PrimitiveType_I64, 0)
#define VType_Bool MakePrimitive("Bool", PrimitiveType_B32, 1)
#define VType_String MakePrimitive("String", PrimitiveType_String, 2)

#define VType_Type vtype_from_name("Type")
#define VType_Result vtype_from_name("Result")
#define VType_CopyMode vtype_from_name("CopyMode")
#define VType_YovInfo vtype_from_name("YovInfo")
#define VType_Context vtype_from_name("Context")
#define VType_CallsContext vtype_from_name("CallsContext")
#define VType_OS vtype_from_name("OS")
#define VType_CallOutput vtype_from_name("CallOutput")
#define VType_FileInfo vtype_from_name("FileInfo")
#define VType_YovParseOutput vtype_from_name("YovParseOutput")
#define VType_ObjectDefinition vtype_from_name("ObjectDefinition")
#define VType_FunctionDefinition vtype_from_name("FunctionDefinition")
#define VType_StructDefinition vtype_from_name("StructDefinition")
#define VType_EnumDefinition vtype_from_name("EnumDefinition")

#define VType_IntArray vtype_from_dimension(VType_Int, 1)
#define VType_BoolArray vtype_from_dimension(VType_Bool, 1)
#define VType_StringArray vtype_from_dimension(VType_String, 1)

global_var Object* nil_obj;
global_var Object* null_obj;

struct ObjectDefinition {
    VType vtype;
    B32 is_constant;
    String name;
    Value value;
    Location location;
};

ObjectDefinition ObjDefMake(String name, VType vtype, Location location, B32 is_constant, Value value);



enum DefinitionStage {
    DefinitionStage_None,
    DefinitionStage_Identified,
    DefinitionStage_Defined,
    DefinitionStage_Ready,
};

enum DefinitionType {
    DefinitionType_Unknown,
    DefinitionType_Function,
    DefinitionType_Struct,
    DefinitionType_Enum,
    DefinitionType_Arg,
};

String StringFromDefinitionType(DefinitionType type);

struct CodeDefinition {
    DefinitionType type;
    String identifier;
    U32 index;
    
    Location entire_location;
    union {
        struct {
            Location body_location;
            Location parameters_location;
            Location returns_location;
            B32 return_is_list;
        } function;
        struct {
            Location body_location;
        } enum_or_struct;
        struct {
        } global;
        struct {
            Location type_location;
            Location body_location;
        } arg;
    };
};

typedef void IntrinsicFunction(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location);

struct DefinitionHeader {
    DefinitionType type;
    
    String identifier;
    Location location;
    volatile DefinitionStage stage;
};

struct FunctionDefinition : DefinitionHeader {
    Array<ObjectDefinition> parameters;
    Array<ObjectDefinition> returns;
    
    struct {
        IntrinsicFunction* fn;
    } intrinsic;
    
    struct {
        IR ir;
    } defined;
    
    B8 is_intrinsic;
};

struct ArgDefinition : DefinitionHeader {
    String name;
    String description;
    VType vtype;
    B32 required;
    Value default_value;
};

struct StructDefinition : DefinitionHeader {
    Array<String> names;
    Array<VType> vtypes;
    Array<U32> offsets;
    B32 needs_internal_release;
    U32 size;
};

struct EnumDefinition : DefinitionHeader {
    Array<String> names;
    Array<Location> expression_locations;
    Array<I64> values;
};

struct Definition {
    union {
        DefinitionHeader header;
        FunctionDefinition function;
        StructDefinition _struct;
        EnumDefinition _enum;
        ArgDefinition arg;
    };
};

struct VariableTypeChild {
    B32 is_member;
    String name;
    I32 index;
    VType vtype;
};

void YovInitializeTypesTable();

B32 vtype_equals(VType v0, VType v1);
inline_fn B32 operator==(VType t0, VType t1) { return vtype_equals(t0, t1); }
inline_fn B32 operator!=(VType t0, VType t1) { return !vtype_equals(t0, t1); }

B32 VTypeValid(VType vtype);
VType VTypeNext(VType vtype);

B32 VTypeIsReady(VType vtype);
B32 VTypeIsSizeReady(VType vtype);
U32 VTypeGetSize(VType vtype);
B32 VTypeNeedsInternalRelease(VType vtype);
String VTypeGetName(VType vtype);

VType vtype_from_index(U32 index);
VType vtype_from_name(String name);
VType vtype_from_dimension(VType element, U32 dimension);
VType vtype_from_reference(VType base_type);
B32 vtype_is_enum(VType vtype);
B32 vtype_is_array(VType vtype);
B32 vtype_is_struct(VType vtype);
B32 vtype_is_reference(VType vtype);
VType vtype_get_child_at(VType vtype, I32 index, B32 is_member);
VariableTypeChild vtype_get_child(VType vtype, String name);
VariableTypeChild vtype_get_member(VType vtype, String member);
VariableTypeChild VTypeGetProperty(VType vtype, String property);
Array<VariableTypeChild> VTypeGetProperties(VType vtype);
VType vtype_from_binary_operation(VType left, VType right, BinaryOperator op);
VType vtype_from_sign_operation(VType src, BinaryOperator op);
Array<VType> vtypes_from_definitions(Arena* arena, Array<ObjectDefinition> defs);


void DefinitionIdentify(U32 index, DefinitionType type, String identifier, Location location);

void EnumDefine(EnumDefinition* def, Array<String> names, Array<Location> expression_locations);
void EnumResolve(EnumDefinition* def, Array<I64> values);

void StructDefine(StructDefinition* def, Array<ObjectDefinition> members);
void StructResolve(StructDefinition* def);

void FunctionDefine(FunctionDefinition* def, Array<ObjectDefinition> parameters, Array<ObjectDefinition> returns);
void FunctionResolveIntrinsic(FunctionDefinition* def, IntrinsicFunction* fn);
void FunctionResolve(FunctionDefinition* def, IR ir);

void ArgDefine(ArgDefinition* def, VType vtype);
void ArgResolve(ArgDefinition* def, String name, String description, B32 required, Value default_value);

Definition* DefinitionFromIdentifier(String identifier);
Definition* DefinitionFromIndex(U32 index);
B32 DefinitionExists(String identifier);
StructDefinition* StructFromIdentifier(String identifier);
StructDefinition* StructFromIndex(U32 index);
EnumDefinition* EnumFromIdentifier(String identifier);
EnumDefinition* EnumFromIndex(U32 index);
FunctionDefinition* FunctionFromIdentifier(String identifier);
FunctionDefinition* FunctionFromIndex(U32 index);
ArgDefinition* ArgFromIndex(U32 index);
ArgDefinition* ArgFromName(String name);

void GlobalDefine(U32 index, VType vtype, B32 is_constant);
I32 GlobalIndexFromIdentifier(String identifier);
Global* GlobalFromIdentifier(String identifier);
Global* GlobalFromIndex(U32 index);
Global* GlobalFromRegisterIndex(I32 index);

struct ExpresionContext {
    VType vtype;
    U32 assignment_count;
};

ExpresionContext ExpresionContext_from_void();
ExpresionContext ExpresionContext_from_inference(U32 assignment_count);
ExpresionContext ExpresionContext_from_vtype(VType vtype, U32 assignment_count);

B32 ValueIsCompiletime(Value value);
I32 ValueGetRegister(Value value);
B32 ValueIsRValue(Value value);
B32 ValueIsNull(Value value);

B32 ValueEquals(Value v0, Value v1);
Value ValueCopy(Arena* arena, Value src);
Array<Value> ValueArrayCopy(Arena* arena, Array<Value> src);

Value ValueNone();
Value ValueNull();
Value ValueFromIrObject(IR_Object* object);
Value ValueFromRegister(I32 index, VType vtype, B32 is_lvalue);
Value ValueFromReference(Value value);
Value ValueFromDereference(Value value);
Value ValueFromInt(I64 value);
Value ValueFromEnum(VType vtype, I64 value);
Value ValueFromBool(B32 value);
Value ValueFromString(Arena* arena, String value);
Value ValueFromStringArray(Arena* arena, Array<Value> values);
Value ValueFromType(VType type);
Value ValueFromArray(Arena* arena, VType array_vtype, Array<Value> elements);
Value ValueFromEmptyArray(Arena* arena, VType base_vtype, Array<Value> dimensions);
Value ValueFromZero(VType vtype);
Value ValueFromGlobal(U32 global_index);
Value ValueFromStringExpression(Arena* arena, String str, VType vtype);
Value value_from_return(Arena* arena, Array<Value> values);

Array<Value> ValuesFromReturn(Arena* arena, Value value, B32 empty_on_void);

String StrFromValue(Arena* arena, Value value, B32 raw = false);

//- PROGRAM CONTEXT

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
};

struct ScriptArg {
    String name;
    String value;
};

struct YovScript {
    I32 id;
    String path;
    String name;
    String dir;
    String text;
};

struct YovSettings {
    B8 analyze_only;
    B8 trace;
    B8 user_assert;
    B8 wait_end;
    B8 no_user;
    B8 ignore_core;
};

struct Yov {
    Arena* arena;
    
    String main_script_path;
    String caller_dir;
    
    PooledArray<YovScript> scripts;
    Mutex scripts_mutex;
    
    Array<ScriptArg> script_args;
    
    // Type System
    Array<VType> vtypes;
    Array<Definition> definitions;
    U32 function_count;
    U32 struct_count;
    U32 enum_count;
    U32 arg_count;
    
    Interpreter* inter;
    Array<Global> globals;
    IR globals_initialize_ir;
    IR args_initialize_ir;
    
    PooledArray<Report> reports;
    Mutex reports_mutex;
    
    YovSettings settings;
    
    I64 exit_code;
    B8 exit_code_is_set;
    B8 exit_requested;
};

struct YovThreadContext {
    Arena* arena;
};

struct YovSystemInfo {
    U64 page_size;
    U32 logical_cores;
    
    U64 timer_start;
    U64 timer_frequency;
};

extern YovSystemInfo system_info; 
extern Yov* yov;
extern per_thread_var YovThreadContext context;

void yov_initialize_thread();
void yov_shutdown_thread();

void yov_initialize();
void yov_shutdown();

void yov_config_from_args();
void yov_config(String path, YovSettings settings, Array<ScriptArg> script_args);

String resolve_import_path(Arena* arena, String caller_script_dir, String path);

void yov_set_exit_code(I64 exit_code);

void yov_print_script_help(Interpreter* inter);

YovScript* GetScript(I32 script_id);
String yov_get_line_sample(Arena* arena, Location location);

ScriptArg* yov_find_script_arg(String name);
String yov_get_inherited_args(Arena* arena);

B32 yov_ask_yesno(String title, String message);

void report_error_ex(Location location, String text, ...);

void yov_print_reports();

#define report_error(code, text, ...) report_error_ex(code, text, __VA_ARGS__);

String string_from_report(Arena* arena, Report report);
void print_report(Report report);

//- FRONT 

struct FrontContext {
    Arena* arena;
    
    Mutex mutex;
    PooledArray<CodeDefinition> definition_list;
    PooledArray<Location> global_location_list;
    
    Array<CodeDefinition> definitions;
    PooledArray<Global> global_list;
    IR_Group global_initialize_group;
    U32 number_of_registers_for_global_initialize;
    
    U32 function_count;
    U32 struct_count;
    U32 enum_count;
    U32 arg_count;
    
    volatile U32 index_counter;
    
    volatile U32 resolve_count;
    U32 last_resolve_count;
    U32 resolve_iterations;
};



//- SYNTACTIC REPORTS

#define report_common_missing_closing_bracket(_code) report_error(_code, "Missing closing bracket");
#define report_common_missing_opening_bracket(_code) report_error(_code, "Missing opening bracket");
#define report_common_missing_closing_parenthesis(_code) report_error(_code, "Missing closing parenthesis");
#define report_common_missing_opening_parenthesis(_code) report_error(_code, "Missing opening parenthesis");
#define report_common_missing_closing_brace(_code) report_error(_code, "Missing closing brace");
#define report_common_missing_opening_brace(_code) report_error(_code, "Missing opening brace");
#define report_common_missing_block(_code) report_error(_code, "Missing block");
#define report_common_expecting_valid_expresion(_code) report_error(_code, "Expecting a valid expresion: {line}");
#define report_common_expecting_parenthesis(_code, _for_what) report_error(_code, "Expecting parenthesis for %S", STR(_for_what));
#define report_syntactic_unknown_op(_code) report_error(_code, "Unknown operation: {line}");
#define report_expr_invalid_binary_operation(_code, _op_str) report_error(_code, "Invalid binary operation: '%S'", _op_str);
#define report_expr_syntactic_unknown(_code, _expr_str) report_error(_code, "Unknown expresion: '%S'", _expr_str);
#define report_expr_is_empty(_code) report_error(_code, "Empty expresion: {line}");
#define report_expr_empty_member(_code) report_error(_code, "Member is not specified");
#define report_array_expr_expects_an_arrow(_code) report_error(_code, "Array expresion expects an arrow");
#define report_array_indexing_expects_expresion(_code) report_error(_code, "Array indexing expects an expresion");
#define report_objdef_expecting_colon(_code) report_error(_code, "Expecting colon in object definition");
#define report_objdef_expecting_type_identifier(_code) report_error(_code, "Expecting type identifier");
#define report_objdef_expecting_assignment(_code) report_error(_code, "Expecting an assignment for object definition");
#define report_else_not_found_if(_code) report_error(_code, "'else' is not valid without the corresponding 'if'");
#define report_for_unknown(_code) report_error(_code, "Unknown for-statement: {line}");
#define report_foreach_expecting_identifier(_code) report_error(_code, "Expecting an identifier for the itarator");
#define report_foreach_expecting_expresion(_code) report_error(_code, "Expecting an array expresion");
#define report_foreach_expecting_colon(_code) report_error(_code, "Expecting a colon separating identifiers and array expresion");
#define report_foreach_expecting_comma_separated_identifiers(_code) report_error(_code, "Foreach-Statement expects comma separated identifiers");
#define report_assign_operator_not_found(_code) report_error(_code, "Operator not found for an assignment");
#define report_enumdef_expecting_comma_separated_identifier(_code) report_error(_code, "Expecting comma separated identifier for enum values");
#define report_expecting_object_definition(_code) report_error(_code, "Expecting an object definition");
#define report_expecting_string_literal(_code) report_error(_code, "Expecting string literal");
#define report_expecting_semicolon(_code) report_error(_code, "Expecting semicolon");
#define report_expecting_assignment(_code) report_error(_code, "Expecting assignment");
#define report_unknown_parameter(_code, _v) report_error(_code, "Unknown parameter '%S'", _v);
#define report_invalid_escape_sequence(_code, _v) report_error(_code, "Invalid escape sequence '\\%S'", _v);
#define report_invalid_codepoint_literal(_code, _v) report_error(_code, "Invalid codepoint literal: %S", _v);

//- SEMANTIC REPORTS

#define report_zero_division(_code) report_error(_code, "Divided by zero");
#define report_right_path_cant_be_absolute(_code) report_error(_code, "Right path can't be absolute");
#define report_nested_definition(_code) report_error(_code, "This definition can't be nested");
#define report_unsupported_operations(_code) report_error(_code, "Unsupported operations outside main script");
#define report_type_missmatch_append(_code, _v0, _v1) report_error(_code, "Type missmatch, can't append a '%S' into '%S'", _v0, _v1);
#define report_type_missmatch_array_expr(_code, _v0, _v1) report_error(_code, "Type missmatch in array expresion, expecting '%S' but found '%S'", _v0, _v1);
#define report_type_missmatch_assign(_code, _v0, _v1) report_error(_code, "Type missmatch, can't assign '%S' to '%S'", _v0, _v1);
#define report_assignment_is_constant(_code, _v0) report_error(_code, "Can't assign a value to '%S', is constant", _v0);
#define report_unknown_array_definition(_code) report_error(_code, "Unknown type for array definition");
#define report_invalid_binary_op(_code, _v0, _op, _v1) report_error(_code, "Invalid binary operation: '%S' %S '%S'", _v0, _op, _v1);
#define report_invalid_signed_op(_code, _op, _v) report_error(_code, "Invalid signed operation '%S %S'", _op, _v);
#define report_symbol_not_found(_code, _v) report_error(_code, "Symbol '%S' not found", _v);
#define report_symbol_duplicated(_code, _v) report_error(_code, "Duplicated symbol '%S'", _v);
#define report_object_not_found(_code, _v) report_error(_code, "Object '%S' not found", _v);
#define report_object_type_not_found(_code, _t) report_error(_code, "Object Type '%S' not found", _t);
#define report_object_duplicated(_code, _v) report_error(_code, "Duplicated object '%S'", _v);
#define report_object_invalid_type(_code, _t) report_error(_code, "Invalid Type '%S'", STR(_t));
#define report_member_not_found_in_object(_code, _v, _t) report_error(_code, "Member '%S' not found in a '%S'", _v, _t);
#define report_member_not_found_in_type(_code, _v, _t) report_error(_code, "Member '%S' not found in '%S'", _v, _t);
#define report_member_invalid_symbol(_code, _v) report_error(_code, "'%S' does not have members to access", _v);
#define report_function_not_found(_code, _v) report_error(_code, "Function '%S' not found", _v);
#define report_function_expecting_parameters(_code, _v, _c) report_error(_code, "Function '%S' is expecting %u parameters", _v, _c);
#define report_function_wrong_parameter_type(_code, _f, _t, _c) report_error(_code, "Function '%S' is expecting a '%S' as a parameter %u", _f, _t, _c);
#define report_function_expects_ref_as_parameter(_code, _f, _c) report_error(_code, "Function '%S' is expecting a reference as a parameter %u", _f, _c);
#define report_function_expects_noref_as_parameter(_code, _f, _c) report_error(_code, "Function '%S' is not expecting a reference as a parameter %u", _f, _c);
#define report_function_wrong_return_type(_code, _t) report_error(_code, "Expected a '%S' as a return", _t);
#define report_function_expects_ref_as_return(_code) report_error(_code, "Expected a reference as a return");
#define report_function_expects_no_ref_as_return(_code) report_error(_code, "Can't return a reference");
#define report_function_no_return(_code, _f) report_error(_code, "Not all paths of '%S' have a return", _f);
#define report_function_no_return_named(_code, _f, _n) report_error(_code, "Not all paths of '%S' have a return for '%S'", _f, _n);
#define report_symbol_not_invokable(_code, _v) report_error(_code, "Not invokable symbol '%S'", _v);
#define report_indexing_expects_an_int(_code) report_error(_code, "Indexing expects an Int");
#define report_indexing_not_allowed(_code, _t) report_error(_code, "Indexing not allowed for a '%S'", _t);
#define report_indexing_out_of_bounds(_code) report_error(_code, "Index out of bounds");
#define report_dimensions_expects_an_int(_code) report_error(_code, "Expecting an integer for the dimensions of the array");
#define report_dimensions_must_be_positive(_code) report_error(_code, "Expecting a positive integer for the dimensions of the array");
#define report_expr_expects_bool(_code, _what)  report_error(_code, "%S expects a Bool", _what);
#define report_expr_expects_lvalue(_code) report_error(_code, "Expresion expects a lvalue: {line}");
#define report_expr_semantic_unknown(_code)  report_error(_code, "Unknown expresion: {line}");
#define report_for_expects_an_array(_code)  report_error(_code, "Foreach-Statement expects an array");
#define report_semantic_unknown_op(_code) report_error(_code, "Unknown operation: {line}");
#define report_struct_recursive(_code) report_error(_code, "Recursive struct definition");
#define report_struct_circular_dependency(_code) report_error(_code, "Struct has circular dependency");
#define report_struct_implicit_member_type(_code) report_error(_code, "Implicit member type is not allowed in structs");
#define report_intrinsic_not_resolved(_code, _n) report_error(_code, "Intrinsic '%S' can't be resolved", _n);
#define report_intrinsic_not_match(_code, _n) report_error(_code, "Intrinsic '%S' does not match", STR(_n));
#define report_ref_expects_lvalue(_code) report_error(_code, "Can't get a reference of a rvalue");
#define report_ref_expects_non_constant(_code) report_error(_code, "Can't get a reference of a constant");
#define report_arg_invalid_name(_code, _v) report_error(_code, "Invalid arg name '%S'", _v);
#define report_arg_duplicated_name(_code, _v) report_error(_code, "Duplicated arg name '%S'", _v);
#define report_arg_is_required(_code, _v) report_error(_code, "Argument '%S' is required", _v);
#define report_arg_wrong_value(_code, _n, _v) report_error(_code, "Invalid argument assignment '%S = %S'", _n, _v);
#define report_arg_unknown(_code, _v) report_error(_code, "Unknown argument '%S'", _v);
#define report_break_inside_loop(_code) report_error(_code, "Break keyword must be used inside a loop");
#define report_continue_inside_loop(_code) report_error(_code, "Continue keyword must be used inside a loop");

//- LANG REPORTS

#define lang_report_stack_is_broken() report_error(NO_CODE, "[LANG_ERROR] The stack is broken");
#define lang_report_unfreed_objects() report_error(NO_CODE, "[LANG_ERROR] Not all objects have been freed");
#define lang_report_unfreed_dynamic() report_error(NO_CODE, "[LANG_ERROR] Not all dynamic allocations have been freed");

//- PARSER 

enum SentenceKind {
    SentenceKind_Unknown,
    SentenceKind_Assignment,
    SentenceKind_FunctionCall,
    SentenceKind_Return,
    SentenceKind_Break,
    SentenceKind_Continue,
    
    SentenceKind_ObjectDef,
    SentenceKind_FunctionDef,
    SentenceKind_StructDef,
    SentenceKind_EnumDef,
    SentenceKind_ArgDef,
};

struct Parser {
    I32 script_id;
    String text;
    RangeU64 range;
    U64 cursor;
    // TODO(Jose): Cache tokens
};

Parser* ParserAlloc(String text, RangeU64 range, I32 script_id);
Parser* ParserFromLocation(Location location);
Location LocationFromParser(Parser* parser, U64 end);

Token peek_token(Parser* parser, I64 cursor_offset = 0);
void  skip_token(Parser* parser, Token token);
void  assume_token(Parser* parser, TokenKind kind);
void  MoveCursor(Parser* parser, U64 cursor);
Token consume_token(Parser* parser);
Array<Token> ConsumeAllTokens(Parser* parser);

Location FindUntil(Parser* parser, B32 include_match, TokenKind match0, TokenKind match1 = TokenKind_None);
U64 find_token_with_depth_check(Parser* parser, B32 parenthesis, B32 braces, B32 brackets, TokenKind match0, TokenKind match1 = TokenKind_None);
Location FindScope(Parser* parser, TokenKind open_token, B32 include_delimiters);
Location FindCode(Parser* parser);

Location FetchUntil(Parser* parser, B32 include_match, TokenKind match0, TokenKind match1 = TokenKind_None);
Location FetchScope(Parser* parser, TokenKind open_token, B32 include_delimiters);
Location FetchCode(Parser* parser);

SentenceKind find_sentence_kind(Parser* parser);

void ReadEnumDefinition(CodeDefinition* code);
void ResolveEnumDefinition(EnumDefinition* def);

void ReadStructDefinition(CodeDefinition* code);
B32 ResolveStructDefinition(StructDefinition* def);

void ReadFunctionDefinition(CodeDefinition* code);
void ResolveFunctionDefinition(FunctionDefinition* def, CodeDefinition* code);

void ReadArgDefinition(CodeDefinition* code);
void ResolveArgDefinition(CodeDefinition* code);

IR_Group ReadExpression(IR_Context* ir, Location location, ExpresionContext context);
IR_Group ReadCode(IR_Context* ir, Location location);
IR_Group ReadSentence(IR_Context* ir, Location location);

IR_Group ReadFunctionCall(IR_Context* ir, ExpresionContext context, Location location);

struct ObjectDefinitionResult {
    Array<ObjectDefinition> objects;
    IR_Group out;
    B32 success;
};

ObjectDefinitionResult ReadObjectDefinition(Arena* arena, Location location, B32 require_single, RegisterKind register_kind, IR_Context* ir);
ObjectDefinitionResult ReadDefinitionList(Arena* arena, Location location, RegisterKind register_kind, IR_Context* ir);

IR_Group ReadExpressionList(Arena* arena, IR_Context* ir, VType vtype, Array<VType> expected_vtypes, Location location);
VType ReadObjectType(Location location);

Token read_token(String text, U64 cursor, I32 script_id);
Token read_valid_token(String text, U64 cursor, U64 end_cursor, I32 script_id);

B32 check_tokens_are_couple(Array<Token> tokens, U32 open_index, U32 close_index, TokenKind open_token, TokenKind close_token);
Array<Array<Token>> split_tokens_in_parameters(Arena* arena, Array<Token> tokens);

//- Intermediate Representation

struct IR_Unit {
    UnitKind kind;
    Location location;
    I32 dst_index;
    Value src;
    union {
        struct {
            FunctionDefinition* fn;
            Array<Value> parameters;
        } function_call;
        
        struct {
            I32 condition; // 0 -> None; 1 -> true; -1 -> false
            IR_Unit* unit;
        } jump;
        
        struct {
            Value src1;
            BinaryOperator op;
        } binary_op;
        
        struct {
            BinaryOperator op;
        } sign_op;
        
        struct {
            Value child_index;
            B32 child_is_member;
        } child;
    };
    
    IR_Unit* prev;
    IR_Unit* next;
};

struct IR_Object {
    String identifier;
    VType vtype;
    U32 assignment_count;
    I32 register_index;
    I32 scope;
};

struct IR_LoopingScope {
    IR_Unit* continue_unit;
    IR_Unit* break_unit;
};

struct IR_Context {
    Arena* arena;
    PooledArray<Register> local_registers;
    PooledArray<IR_Object> objects;
    PooledArray<IR_LoopingScope> looping_scopes;
    I32 scope;
};

IR_Group ir_failed();
IR_Group ir_from_none(Value value = ValueNone());
IR_Group ir_from_single(IR_Unit* unit, Value value = ValueNone());
IR_Group ir_append(IR_Group o0, IR_Group o1);
IR_Group IRFromDefineObject(IR_Context* ir, RegisterKind register_kind, String identifier, VType vtype, B32 constant, Location location);
IR_Group IRFromDefineTemporal(IR_Context* ir, VType vtype, Location location);
IR_Group ir_from_reference(IR_Context* ir, B32 expects_lvalue, Value value, Location location);
IR_Group ir_from_dereference(IR_Context* ir, Value value, Location location);
IR_Group ir_from_symbol(IR_Context* ir, String identifier, Location location);
IR_Group ir_from_function_call(IR_Context* ir, String identifier, Array<Value> parameters, ExpresionContext context, Location location);
IR_Group ir_from_default_initializer(IR_Context* ir, VType vtype, Location location);
IR_Group ir_from_store(IR_Context* ir, Value dst, Value src, Location location);
IR_Group ir_from_assignment(IR_Context* ir, B32 expects_lvalue, Value dst, Value src, BinaryOperator op, Location location);
IR_Group ir_from_multiple_assignment(IR_Context* ir, B32 expects_lvalue, Array<Value> destinations, Value src, BinaryOperator op, Location location);
IR_Group ir_from_binary_operator(IR_Context* ir, Value left, Value right, BinaryOperator op, B32 reuse_left, Location location);
IR_Group ir_from_sign_operator(IR_Context* ir, Value src, BinaryOperator op, Location location);
IR_Group ir_from_child(IR_Context* ir, Value src, Value index, B32 is_member, VType vtype, Location location);
IR_Group ir_from_child_access(IR_Context* ir, Value src, String child_name, ExpresionContext context, Location location);
IR_Group IRFromIfStatement(IR_Context* ir, Value condition, IR_Group success, IR_Group failure, Location location);
IR_Group IRFromLoop(IR_Context* ir, IR_Group init, IR_Group condition, IR_Group content, IR_Group update, Location location);
IR_Group IRFromFlowModifier(IR_Context* ir, B32 is_break, Location location);
IR_Group IRFromReturn(IR_Context* ir, IR_Group expression, Location location);

IR MakeIR(Arena* arena, Array<Register> local_registers, IR_Group group);
IR_Context* ir_context_alloc();
Array<VType> ReturnsFromRegisters(Arena* arena, Array<Register> registers);

IR ir_generate_from_value(Value value);

B32 ir_validate_return_path(Array<Unit> units);

enum SymbolType {
    SymbolType_None,
    SymbolType_Object,
    SymbolType_Function,
    SymbolType_Type,
};

struct Symbol {
    SymbolType type;
    String identifier;
    
    IR_Object* object;
    FunctionDefinition* function;
    VType vtype;
};

IR_Object* ir_find_object(IR_Context* ir, String identifier, B32 parent_scopes);
IR_Object* ir_find_object_from_value(IR_Context* ir, Value value);
IR_Object* ir_find_object_from_register(IR_Context* ir, I32 register_index);
IR_Object* ir_define_object(IR_Context* ir, String identifier, VType vtype, I32 scope, I32 register_index);
IR_Object* ir_assume_object(IR_Context* ir, IR_Object* object, VType vtype);
Symbol ir_find_symbol(IR_Context* ir, String identifier);

IR_LoopingScope* ir_looping_scope_push(IR_Context* ir, Location location);
void ir_looping_scope_pop(IR_Context* ir);
IR_LoopingScope* ir_get_looping_scope(IR_Context* ir);

void ir_scope_push(IR_Context* ir);
void ir_scope_pop(IR_Context* ir);

I32 IRRegisterAlloc(IR_Context* ir, VType vtype, RegisterKind kind, B32 constant);
Register IRRegisterGet(IR_Context* ir, I32 register_index);
Register IRRegisterFromValue(IR_Context* ir, Value value);

I32 RegIndexFromGlobal(U32 global_index);
I32 RegIndexFromLocal(U32 local_index);
I32 LocalFromRegIndex(I32 register_index);

String StringFromRegister(Arena* arena, I32 index);
String StringFromUnitKind(Arena* arena, UnitKind unit);

#if DEV

String StringFromUnit(Arena* arena, U32 index, U32 index_digits, U32 line_digits, Unit unit);
void PrintUnits(Array<Unit> instructions);
void PrintIr(String name, IR ir);

#endif

//- INTERPRETER

void InitializeLanguageGlobals();
B32 interpreter_run(FunctionDefinition* fn, Array<Value> params);
void interpreter_run_main();
void interpreter_finish();

struct Scope {
    Array<Reference> registers;
    I32 pc; // Program Counter
    B32 return_requested;
    U32 scratch_arena_index;
    Scope* prev;
};

struct Interpreter {
    U32 object_id_counter;
    
    struct {
        Object* object_list;
        I32 object_count;
        I32 allocation_count;
    } gc;
    
    Scope* global_scope;
    Scope* current_scope;
    
    struct {
        Reference yov;
        Reference os;
        Reference context;
        Reference calls;
    } common_globals;
};

void interpreter_exit(Interpreter* inter, I64 exit_code);
void interpreter_report_runtime_error(Interpreter* inter, Location location, Result result);
Result user_assertion(Interpreter* inter, String message);

Reference get_cd(Interpreter* inter);
String get_cd_value(Interpreter* inter);
String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path);
RedirectStdout get_calls_redirect_stdout(Interpreter* inter);

// Once defined the IR, this will be the functions used to execute/analyze

Reference RefFromValue(Scope* scope, Value value);
void ExecuteIr(IR ir, Array<Reference> output, Array<Value> params, Location location);
Reference ExecuteIrSingleReturn(IR ir, Array<Value> params, Location location);

void run_instruction(Interpreter* inter, Unit unit);
void run_store(Interpreter* inter, I32 dst_index, Reference src, Location location);
void run_copy(Interpreter* inter, I32 dst_index, Reference src, Location location);
void run_return(Interpreter* inter, Location location);
void run_jump(Interpreter* inter, Reference ref, I32 condition, I32 offset, Location location);
void run_function_call(Interpreter* inter, I32 dst_index, FunctionDefinition* fn, Array<Value> parameters, Location location);
Reference run_binary_operation(Interpreter* inter, Reference dst, Reference left, Reference right, BinaryOperator op, Location location);
Reference run_sign_operation(Interpreter* inter, Reference value, BinaryOperator op, Location location);

//- SCOPE

void scope_clear(Interpreter* inter, Scope* scope);

void RegisterSave(Scope* scope, I32 register_index, Reference ref);
void GlobalSave(String identifier, Reference ref);
Reference RegisterGet(Scope* scope, I32 register_index);
Reference GlobalGet(String identifier);

//- OBJECT

String string_from_object(Arena* arena, Interpreter* inter, Object* object, B32 raw = true);
String string_from_ref(Arena* arena, Reference ref, B32 raw = true);

String StringFromCompiletime(Arena* arena, Value value);
B32 B32FromCompiletime(Value value);
VType TypeFromCompiletime(Value value);

Reference ref_from_object(Object* object);
Reference ref_from_address(Object* parent, VType vtype, void* address);

void ref_set_member(Interpreter* inter, Reference ref, U32 index, Reference member);

Reference ref_get_child(Interpreter* inter, Reference ref, U32 index, B32 is_member);
Reference ref_get_member(Interpreter* inter, Reference ref, U32 index);
Reference ref_get_property(Interpreter* inter, Reference ref, U32 index);

U32 ref_get_child_count(Reference ref, B32 is_member);
U32 ref_get_property_count(Reference ref);
U32 ref_get_member_count(Reference ref);

Reference alloc_int(Interpreter* inter, I64 value);
Reference alloc_bool(Interpreter* inter, B32 value);
Reference alloc_string(Interpreter* inter, String value);
Reference alloc_array(Interpreter* inter, VType element_vtype, I64 count);
Reference alloc_array_multidimensional(Interpreter* inter, VType base_vtype, Array<I64> dimensions);
Reference alloc_array_from_enum(Interpreter* inter, VType enum_vtype);
Reference alloc_enum(Interpreter* inter, VType vtype, I64 index);
Reference alloc_reference(Interpreter* inter, Reference ref);

B32 is_valid(Reference ref);
B32 is_unknown(Reference ref);
B32 is_const(Reference ref);
B32 is_null(Reference ref);
B32 is_int(Reference ref);
B32 is_bool(Reference ref);
B32 is_string(Reference ref);
B32 is_array(Reference ref);
B32 is_enum(Reference ref);
B32 is_reference(Reference ref);

I64 get_int(Reference ref);
B32 get_bool(Reference ref);
I64 get_enum_index(Reference ref);
String get_string(Reference ref);
ObjectData_Array* get_array(Reference ref);
Reference dereference(Reference ref);

I64 get_int_member(Interpreter* inter, Reference ref, String member);
B32 get_bool_member(Interpreter* inter, Reference ref, String member);
String get_string_member(Interpreter* inter, Reference ref, String member);

void set_int(Reference ref, I64 v);
void set_bool(Reference ref, B32 v);
void set_enum_index(Reference ref, I64 v);

ObjectData_String* ref_string_get_data(Interpreter* inter, Reference ref);
void ref_string_prepare(Interpreter* inter, Reference ref, U64 new_size, B32 can_discard);
void ref_string_clear(Interpreter* inter, Reference ref);
void ref_string_set(Interpreter* inter, Reference ref, String v);
void ref_string_append(Interpreter* inter, Reference ref, String v);

void set_reference(Interpreter* inter, Reference ref, Reference src);

void set_int_member(Interpreter* inter, Reference ref, String member, I64 v);
void ref_member_set_bool(Interpreter* inter, Reference ref, String member, B32 v);
void set_enum_index_member(Interpreter* inter, Reference ref, String member, I64 v);
void ref_member_set_string(Interpreter* inter, Reference ref, String member, String v);

enum CopyMode {
    CopyMode_NoOverride,
    CopyMode_Override,
};
inline_fn CopyMode get_enum_CopyMode(Reference ref) {
    return (CopyMode)get_enum_index(ref);
}

void ref_assign_Result(Interpreter* inter, Reference ref, Result res);
void ref_assign_CallOutput(Interpreter* inter, Reference ref, CallOutput out);
void ref_assign_FileInfo(Interpreter* inter, Reference ref, FileInfo info);
void ref_assign_FunctionDefinition(Interpreter* inter, Reference ref, FunctionDefinition* fn);
void ref_assign_StructDefinition(Interpreter* inter, Reference ref, VType vtype);
void ref_assign_EnumDefinition(Interpreter* inter, Reference ref, VType vtype);
void ref_assign_ObjectDefinition(Interpreter* inter, Reference ref, ObjectDefinition def);

void ref_assign_Type(Interpreter* inter, Reference ref, VType vtype);
VType get_Type(Interpreter* inter, Reference ref);

Reference ref_from_Result(Interpreter* inter, Result res);
Result Result_from_ref(Interpreter* inter, Reference ref);

U32 object_generate_id(Interpreter* inter);
Reference object_alloc(Interpreter* inter, VType vtype);
void object_free(Interpreter* inter, Object* obj, B32 release_internal_refs);

void object_increment_ref(Object* obj);
void object_decrement_ref(Object* obj);

void ref_release_internal(Interpreter* inter, Reference ref, B32 release_refs);
void RefCopy(Reference dst, Reference src);
Reference ref_alloc_and_copy(Interpreter* inter, Reference src);

void* object_dynamic_allocate(Interpreter* inter, U64 size);
void object_dynamic_free(Interpreter* inter, void* ptr);
void object_free_unused_memory(Interpreter* inter);
void ObjectFreeAll(Interpreter* inter);

void* gc_allocate(Interpreter* inter, U64 size);
void gc_free(Interpreter* inter, void* ptr);
void gc_free_unused(Interpreter* inter);

void LogMemoryUsage(Interpreter* inter);

IntrinsicFunction* IntrinsicFromIdentifier(String identifier);