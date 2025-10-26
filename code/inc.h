#pragma once

#define YOV_MAJOR_VERSION 0
#define YOV_MINOR_VERSION 2
#define YOV_REVISION_VERSION 0
#define YOV_VERSION STR("v"MACRO_STR(YOV_MAJOR_VERSION)"."MACRO_STR(YOV_MINOR_VERSION)"."MACRO_STR(YOV_REVISION_VERSION))

// DEBUG

#define DEV_PRINT_AST DEV && 1
#define DEV_ASAN      DEV && 0

//-

#include <stdint.h>
#include <stdarg.h>
#include <cstring>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t b8;
typedef uint32_t b32;

typedef float f32;
typedef double f64;
static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);

#define u8_max  0xFF
#define u16_max 0xFFFF
#define u32_max 0xFFFFFFFF
#define u64_max 0xFFFFFFFFFFFFFFFF

#define i16_min -32768
#define i32_min ((i32)(-2147483648))
#define i64_min (-1 - 0x7FFFFFFFFFFFFFFF)

#define i16_max 32767
#define i32_max 2147483647
#define i64_max 0x7FFFFFFFFFFFFFFF

#define f32_min -3.402823e+38f
#define f32_max 3.402823e+38f

#define KB(v) (((u64)v) << 10ULL)
#define MB(v) (((u64)v) << 20ULL)
#define GB(v) (((u64)v) << 30ULL)
#define TB(v) (((u64)v) << 40ULL)

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define CLAMP(x, _min, _max) MAX(MIN(x, _max), _min)
#define ABS(x) (((x) < 0) ? (-(x)) : (x))
#define SWAP(a, b) do { auto& _a = (a); auto& _b = (b); auto aux = _b; _b = _a; _a = aux; } while(0)
#define BIT(x) (1ULL << (x))

#define _JOIN(x, y) x##y
#define JOIN(x, y) _JOIN(x, y)

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
#define foreach(it, count) for (u32 (it) = 0; (it) < (count); (it)++)

#if DEV
#define assert(x) do { if ((x) == 0) assertion_failed(#x, __FILE__, __LINE__); } while (0)
#define invalid_codepath() assertion_failed("Invalid Codepath", __FILE__, __LINE__);
#else
#define assert(x) do {} while (0)
#define invalid_codepath() do {} while (0)
#endif

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

#define _DEFER(x) JOIN(x, __COUNTER__)
#define DEFER(code)   auto _DEFER(_defer_) = _defer_func([&](){code;})

#define weak_scope for (u32 ___ = 0; ___ < 1; ___++) 

void assertion_failed(const char* text, const char* file, u32 line);

#define memory_copy(dst, src, size) memcpy(dst, src, size)
#define memory_zero(dst, size) memset(dst, 0, size)

#define STR(x) string_make(x)

#define _MACRO_STR(x) #x
#define MACRO_STR(x) _MACRO_STR(x)

//- C STRING

u32 cstring_size(const char* str);
u32 cstring_set(char* dst, const char* src, u32 src_size, u32 buff_size);
u32 cstring_copy(char* dst, const char* src, u32 buff_size);
u32 cstring_append(char* dst, const char* src, u32 buff_size);
void cstring_from_u64(char* dst, u64 value, u32 base = 10);
void cstring_from_i64(char* dst, i64 value, u32 base = 10);
void cstring_from_f64(char* dst, f64 value, u32 decimals);

//- BASE STRUCTS

struct Arena;

struct RawBuffer {
    void* data;
    u64 size;
    
    inline u8& operator[](u64 index) {
        assert(index < size);
        return ((u8*)data)[index];
    }
};

struct String {
    char* data;
    u64 size;
    
    inline u8& operator[](u64 index) {
        assert(index < size);
        return ((u8*)data)[index];
    }
    
    String() = default;
    
    String(const char* cstr) {
        size = cstring_size(cstr);
        data = (char*)cstr;
    }
};

template<typename T>
struct Array {
    T* data;
    u32 count;
    
    inline T& operator[](u64 index) {
        assert(index < count);
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
    u32 count;
};

struct StringBuilder {
    LinkedList<String> ll;
    Arena* arena;
    char* buffer;
    u64 buffer_size;
    u64 buffer_pos;
};

struct Date
{
	u32 year;
	u32 month;
	u32 day;
	u32 hour;
	u32 minute;
	u32 second;
	u32 millisecond;
};

#define date_highest date_make(u32_max, u32_max, u32_max, u32_max, u32_max, u32_max, u32_max)

inline_fn Date date_make(u32 year = 0, u32 month = 0, u32 day = 0, u32 hour = 0, u32 minute = 0, u32 second = 0, u32 milliseconds = 0) {
	Date date;
	date.year = year;
	date.month = month;
	date.day = day;
	date.hour = hour;
	date.minute = minute;
	date.second = second;
	date.millisecond = milliseconds;
	return date;
}

inline_fn b32 date_equals(Date d0, Date d1) {
	return d0.year == d1.year &&
		d0.month == d1.month &&
		d0.day == d1.day &&
		d0.hour == d1.hour &&
		d0.minute == d1.minute &&
		d0.second == d1.second &&
		d0.millisecond == d1.millisecond;
}

inline_fn b32 date_less_than(Date d0, Date d1) {
	if (d0.year != d1.year) return d0.year < d1.year;
	if (d0.month != d1.month) return d0.month < d1.month;
	if (d0.day != d1.day) return d0.day < d1.day;
	if (d0.hour != d1.hour) return d0.hour < d1.hour;
	if (d0.minute != d1.minute) return d0.minute < d1.minute;
	if (d0.second != d1.second) return d0.second < d1.second;
	if (d0.millisecond != d1.millisecond) return d0.millisecond < d1.millisecond;
	return false;
}

//- ARENA

struct Arena {
    void* memory;
    u64 memory_position;
    u32 reserved_pages;
    u32 commited_pages;
    u32 alignment;
};

Arena* arena_alloc(u64 capacity, u32 alignment);
void arena_free(Arena* arena);

void* arena_push(Arena* arena, u64 size);
void arena_pop_to(Arena* arena, u64 position);

void arena_protect_and_reset(Arena* arena); // This is for debug purposes only

struct ScratchArena {
    u64 start_position;
    Arena* arena;
};

void initialize_scratch_arenas();
void shutdown_scratch_arenas();

ScratchArena arena_create_scratch(Arena* conflict0 = NULL, Arena* conflict1 = NULL);
void arena_destroy_scratch(ScratchArena scratch);

#define SCRATCH(...) ScratchArena scratch = arena_create_scratch(__VA_ARGS__); DEFER(arena_destroy_scratch(scratch))
#define arena_capture(arena) u64 _arena_capture_pos = arena->pos; DEFER(arena_pop_to(arena, _arena_capture_pos))

template<typename T>
inline_fn T* arena_push_struct(Arena* arena, u32 count = 1)
{
    T* ptr = (T*)arena_push(arena, sizeof(T) * count);
    return ptr;
}

//- LANE GROUP 

struct LaneContext;
typedef void LaneFn(LaneContext* lane);

struct LaneGroup {
    Arena* arena;
    Array<u64> threads;
    LaneFn* fn;
};

struct LaneContext {
    LaneGroup* group;
    u32 id;
    u32 count;
};

LaneGroup* lane_group_start(Arena* arena, LaneFn* fn, u32 lane_count = u32_max);
void lane_group_wait(LaneGroup* group);

b32 lane_narrow(LaneContext* lane, u32 index = 0);

//- OS

void os_setup_system_info();
void os_initialize();
void os_shutdown();

struct Result {
    String message;
    i64 code;
    b32 failed;
};

inline_fn Result result_failed_make(String message, i64 error_code = -1) { return Result{ message, error_code, true }; }

#define RESULT_SUCCESS Result{ "", 0, false }

enum Severity {
    Severity_Info,
    Severity_Warning,
    Severity_Error,
};

void os_print(Severity severity, String text);
void os_console_set_cursor(i64 x, i64 y);
void os_console_get_cursor(i64* x, i64* y);
void os_console_clear();

void* os_allocate_heap(u64 size);
void  os_free_heap(void* address);

void* os_reserve_virtual_memory(u32 pages, b32 commit);
void os_commit_virtual_memory(void* address, u32 page_offset, u32 page_count);
void os_release_virtual_memory(void* address);
void os_protect_virtual_memory(void* address, u32 pages);

typedef i32 ThreadFn(void*);
u64 os_thread_start(ThreadFn* fn, RawBuffer data);
void os_thread_wait(u64 thread, u32 millis);

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
    
    b32 is_directory;
};

void file_info_set_path(FileInfo* info, String path);

b32 os_exists(String path);
Result os_read_entire_file(Arena* arena, String path, RawBuffer* result);
Result os_write_entire_file(String path, RawBuffer data);
Result os_copy_file(String dst_path, String src_path, b32 override);
Result os_move_file(String dst_path, String src_path);
Result os_delete_file(String path);
Result os_create_directory(String path, b32 recursive);
Result os_delete_directory(String path);
Result os_copy_directory(String dst_path, String src_path);
Result os_move_directory(String dst_path, String src_path);
Result os_file_get_info(Arena* arena, String path, FileInfo* ret);
Result os_dir_get_files_info(Arena* arena, String path, Array<FileInfo>* ret);

b32 os_ask_yesno(String title, String content);

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

b32 os_path_is_absolute(String path);
b32 os_path_is_directory(String path);

Array<String> os_get_args(Arena* arena);

Result os_env_get(Arena* arena, String* out, String name);

void os_thread_sleep(u64 millis);
void os_console_wait();

#define MSVC_Env_None 0
#define MSVC_Env_x64 2
#define MSVC_Env_x86 3

u32    os_msvc_get_env_imported();
Result os_msvc_find_path(String* out);
Result os_msvc_import_env(u32 mode);

u64 os_timer_get();

//- MATH

u64 u64_divide_high(u64 n0, u64 n1);
u32 pages_from_bytes(u64 bytes);

//- STRING

String string_make(const char* cstr, u64 size);
String string_make(const char* cstr);
String string_make(String str);
String string_make(RawBuffer buffer);
String string_copy(Arena* arena, String src);
String string_substring(String str, u64 offset, u64 size);
b32 string_equals(String s0, String s1);
b32 string_starts(String str, String with);
b32 string_ends(String str, String with);
b32 u32_from_string(u32* dst, String str);
b32 u32_from_char(u32* dst, char c);
b32 i64_from_string(String str, i64* out);
b32 i32_from_string(String str, i32* out);
String string_from_codepoint(Arena* arena, u32 codepoint);
String string_from_memory(Arena* arena, u64 bytes);
String string_from_ellapsed_time(Arena* arena, f64 seconds);
String string_join(Arena* arena, LinkedList<String> ll);
Array<String> string_split(Arena* arena, String str, String separator);
String string_replace(Arena* arena, String str, String old_str, String new_str);
String string_format_with_args(Arena* arena, String string, va_list args);
String string_format_ex(Arena* arena, String string, ...);
u32 string_get_codepoint(String str, u64* cursor_ptr);
u32 string_calculate_char_count(String str);
String escape_string_from_raw_string(Arena* arena, String raw);

b32 codepoint_is_separator(u32 codepoint);
b32 codepoint_is_number(u32 codepoint);
b32 codepoint_is_text(u32 codepoint);

#define string_format(arena, str, ...) string_format_ex(arena, STR(str), __VA_ARGS__)

//- PATH

Array<String> path_subdivide(Arena* arena, String path);
String path_resolve(Arena* arena, String path);
String path_append(Arena* arena, String str0, String str1);
String path_get_last_element(String path);

//- STRING BUILDER

StringBuilder string_builder_make(Arena* arena);
void appendf_ex(StringBuilder* builder, String str, ...);
void append(StringBuilder* builder, String str);
void append_codepoint(StringBuilder* builder, u32 codepoint);
void append_i64(StringBuilder* builder, i64 v, u32 base = 10);
void append_i32(StringBuilder* builder, i32 v, u32 base = 10);
void append_u64(StringBuilder* builder, u64 v, u32 base = 10);
void append_u32(StringBuilder* builder, u32 v, u32 base = 10);
void append_f64(StringBuilder* builder, f64 v, u32 decimals);
void append_char(StringBuilder* builder, char c);
String string_from_builder(Arena* arena, StringBuilder* builder);

#define appendf(builder, str, ...) appendf_ex(builder, STR(str), __VA_ARGS__)

//- POOLED ARRAY

struct PooledArrayBlock
{
    PooledArrayBlock* next;
    u32 capacity;
    u32 count;
};

struct PooledArrayR
{
    Arena* arena;
    PooledArrayBlock* root;
    PooledArrayBlock* tail;
    PooledArrayBlock* current;
    u64 stride;
    
    u32 default_block_capacity;
    u32 count;
};

PooledArrayR pooled_array_make(Arena* arena, u64 stride, u32 block_capacity);
void array_reset(PooledArrayR* array);
void* array_add(PooledArrayR* array);
void array_erase(PooledArrayR* array, u32 index);
void array_pop(PooledArrayR* array);
u32 array_calculate_index(PooledArrayR* array, void* ptr);

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
b32 binary_operator_is_arithmetic(BinaryOperator op);

struct CodeLocation {
    u64 offset;
    u64 start_line_offset;
    u32 line;
    u32 column;
    i32 script_id;
};

inline_fn CodeLocation no_code_make() { CodeLocation c{}; c.script_id = -1; return c; }
#define NO_CODE no_code_make()

inline_fn CodeLocation code_location_make(u64 offset, u64 start_line_offset, u32 line, u32 column, i32 script_id) { return { offset, start_line_offset, line, column, script_id }; }
inline_fn CodeLocation code_location_start_script(i32 script_id) { return code_location_make(0, 0, 1, 0, script_id); }

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
    CodeLocation code;
    
    union {
        BinaryOperator assignment_binary_operator;
    };
};

String string_from_tokens(Arena* arena, Array<Token> tokens);
String debug_info_from_token(Arena* arena, Token token);
void print_tokens(Array<Token> tokens);

#include "templates.h"

//- TYPE SYSTEM

// TODO(Jose):
struct Interpreter;
struct OpNode;
struct OpNode_Block;
struct VariableType;
struct FunctionDefinition;
struct OpNode_StructDefinition;
struct IR_Object;
struct OpNode_ObjectType;

enum ValueKind {
    ValueKind_None,
    ValueKind_LValue,
    ValueKind_Register,          // RValue
    ValueKind_StringComposition, // RValue
    ValueKind_Array,             // RValue
    ValueKind_MultipleReturn,    // RValue
    ValueKind_Literal,     // Compile-Time RValue
    ValueKind_Default,     // Compile-Time RValue
    ValueKind_Constant,    // Compile-Time RValue
};

struct Value {
    VariableType* vtype;
    ValueKind kind;
    union {
        struct {
            i32 index;
            i32 reference_op; // 0 -> None; 1 -> Take Reference; -1 -> Dereference
        } reg;
        i64 literal_int;
        b32 literal_bool;
        String literal_string;
        VariableType* literal_type;
        struct {
            Array<Value> values;
            b8 is_empty;
        } array;
        
        Array<Value> string_composition;
        Array<Value> multiple_return;
        
        String constant_identifier;
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
    CodeLocation code;
    i32 dst_index;
    union {
        struct {
            Value src;
        } copy;
        
        struct {
            Value src;
        } store;
        
        struct {
            FunctionDefinition* fn;
            Array<Value> parameters;
        } function_call;
        
        struct {
            i32 condition; // 0 -> None; 1 -> true; -1 -> false
            Value src;
            i32 offset;
        } jump;
        
        struct {
            Value src0;
            Value src1;
            BinaryOperator op;
        } binary_op;
        
        struct {
            Value src;
            BinaryOperator op;
        } sign_op;
        
        struct {
            Array<Value> sources;
        } string_expression;
        
        struct {
            Value src;
            Value child_index;
            b32 child_is_member;
        } child;
        
        struct {
            Value src;
        } result_eval;
        
        struct {
            Value count;
            VariableType* element_vtype;
        } array;
    };
};

struct IR_Register {
    VariableType* vtype;
    String global_identifier;
};

struct IR {
    b32 success;
    Value value;
    Array<Unit> instructions;
    Array<IR_Register> registers;
    u32 param_count;
};

typedef i64 VTypeID;

enum VariableKind {
    VariableKind_Unknown,
    VariableKind_Void,
    VariableKind_Reference,
    VariableKind_Primitive,
    VariableKind_Array,
    VariableKind_Enum,
    VariableKind_Struct,
};

struct VariableType {
    VTypeID ID;
    String name;
    VariableKind kind;
    u32 size;
    
    u32 array_dimensions;
    
    struct {
        Array<String> names;
        Array<i64> values;
    } _enum;
    
    struct {
        OpNode_StructDefinition* node;
        Array<String> names;
        Array<VariableType*> vtypes;
        Array<u32> offsets;
        Array<IR> irs;
        b32 needs_internal_release;
    } _struct;
    
    VariableType* child_next;
    VariableType* child_base;
};

global_var VariableType* nil_vtype;
global_var VariableType* void_vtype;
global_var VariableType* any_vtype;

struct Object {
    u32 ID;
    i32 ref_count;
    VariableType* vtype;
    Object* prev;
    Object* next;
};

struct ObjectData_Array {
    u32 count;
    u8* data;
};

struct ObjectData_Ref {
    Object* parent;
    void* address;
};

struct ObjectData_String {
    char* chars;
    u64 capacity;
    u64 size;
};

struct Reference {
    Object* parent;
    VariableType* vtype;
    void* address;
};

#define VTypeID_Unknown VTypeID{ -1 }
#define VTypeID_Void VTypeID{ 0 }
#define VTypeID_Int VTypeID{ 1 }
#define VTypeID_Bool VTypeID{ 2 }
#define VTypeID_String VTypeID{ 3 }

#define VType_Int vtype_get(VTypeID_Int)
#define VType_Bool vtype_get(VTypeID_Bool)
#define VType_String vtype_get(VTypeID_String)

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
    VariableType* vtype;
    b32 is_constant;
    String name;
    IR ir;
};

inline_fn ObjectDefinition obj_def_make(String name, VariableType* vtype, b32 is_constant = false, IR ir = {}) {
    ObjectDefinition d{};
    d.name = name;
    d.vtype = vtype;
    d.is_constant = is_constant;
    d.ir = ir;
    return d;
}

typedef void IntrinsicFunction(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);

struct FunctionDefinition {
    String identifier;
    Array<ObjectDefinition> parameters;
    Array<ObjectDefinition> returns;
    
    CodeLocation code;
    
    struct {
        IntrinsicFunction* fn;
    } intrinsic;
    
    struct {
        OpNode_Block* block;
        IR ir;
    } defined;
    
    b8 is_intrinsic;
};

struct ArgDefinition {
    String identifier;
    String name;
    String description;
    VariableType* vtype;
    b32 required;
};

struct VariableTypeChild {
    b32 is_member;
    String name;
    i32 index;
    VariableType* vtype;
};

void types_initialize(b32 import_core);

VariableType* vtype_add(VariableKind kind, String name, u32 size, VariableType* child_next, VariableType* child_base);
VariableType* vtype_get(VTypeID ID);
VariableType* vtype_from_name(String name);
VariableType* vtype_from_dimension(VariableType* element, u32 dimension);
VariableType* vtype_from_reference(VariableType* base_type);
b32 vtype_is_enum(VariableType* vtype);
b32 vtype_is_array(VariableType* vtype);
b32 vtype_is_struct(VariableType* vtype);
b32 vtype_is_reference(VariableType* vtype);
b32 vtype_needs_internal_release(VariableType* vtype);
VariableType* vtype_get_child_at(VariableType* vtype, i32 index, b32 is_member);
VariableTypeChild vtype_get_child(VariableType* vtype, String name);
VariableTypeChild vtype_get_member(VariableType* vtype, String member);
VariableTypeChild vtype_get_property(VariableType* vtype, String property);
Array<VariableTypeChild> vtype_get_properties(VariableType* vtype);
VariableType* vtype_from_node(OpNode_ObjectType* node);
VariableType* vtype_from_binary_operation(VariableType* left, VariableType* right, BinaryOperator op);
VariableType* vtype_from_sign_operation(VariableType* src, BinaryOperator op);
Array<VariableType*> vtypes_from_definitions(Arena* arena, Array<ObjectDefinition> defs);

VariableType* define_enum(String name, Array<String> names, Array<i64> values);
VariableType* vtype_define_struct(String name, OpNode_StructDefinition* node = NULL);
b32 vtype_init_struct(VariableType* vtype, Array<ObjectDefinition> members);

ObjectDefinition define_arg(String identifier, String name, Value value, b32 required, String description);
void define_function(CodeLocation code, String identifier, Array<ObjectDefinition> parameters, Array<ObjectDefinition> returns, OpNode_Block* block);
void define_intrinsic_function(CodeLocation code, IntrinsicFunction* callback, String identifier, Array<ObjectDefinition> parameters, Array<ObjectDefinition> returns);
void define_global(ObjectDefinition def);

FunctionDefinition* find_function(String identifier);
ObjectDefinition* find_global(String identifier);
ArgDefinition* find_arg_definition_by_name(String name);
b32 definition_exists(String identifier);

struct ExpresionContext {
    VariableType* vtype;
    u32 assignment_count;
};

ExpresionContext ExpresionContext_from_void();
ExpresionContext ExpresionContext_from_inference(u32 assignment_count);
ExpresionContext ExpresionContext_from_vtype(VariableType* vtype, u32 assignment_count);

b32 value_is_compiletime(Value value);
b32 value_is_rvalue(Value value);
b32 value_is_null(Value value);

b32 value_equals(Value v0, Value v1);

Value value_none();
Value value_null();
Value value_from_ir_object(IR_Object* object);
Value value_from_register(i32 index, VariableType* vtype, b32 is_lvalue);
Value value_from_reference(Value value);
Value value_from_dereference(Value value);
Value value_from_int(i64 value);
Value value_from_enum(VariableType* vtype, i64 value);
Value value_from_bool(b32 value);
Value value_from_string(Arena* arena, String value);
Value value_from_string_array(Interpreter* inter, Arena* arena, Array<Value> values);
Value value_from_type(VariableType* type);
Value value_from_array(Arena* arena, VariableType* array_vtype, Array<Value> elements);
Value value_from_empty_array(Arena* arena, VariableType* base_vtype, Array<Value> dimensions);
Value value_from_default(VariableType* vtype);
Value value_from_constant(Arena* arena, VariableType* vtype, String identifier);
Value value_from_string_expression(Arena* arena, String str, VariableType* vtype);
Value value_from_return(Arena* arena, Array<Value> values);

Array<Value> values_from_return(Arena* arena, Value value);

String string_from_value(Arena* arena, Value value, b32 raw = false);

//- PROGRAM CONTEXT

void print_ex(Severity severity, String str, ...);
#define print(severity, str, ...) print_ex(severity, STR(str), __VA_ARGS__)
#define print_info(str, ...) print_ex(Severity_Info, STR(str), __VA_ARGS__)
#define print_warning(str, ...) print_ex(Severity_Warning, STR(str), __VA_ARGS__)
#define print_error(str, ...) print_ex(Severity_Error, STR(str), __VA_ARGS__)

#define print_separator() print_ex(Severity_Info, STR("=========================\n"))

struct Report {
    String text;
    CodeLocation code;
};

struct ScriptArg {
    String name;
    String value;
};

struct YovScript {
    String path;
    String name;
    String dir;
    String text;
    Array<Token> tokens;
    OpNode_Block* ast;
};

struct YovSettings {
    b8 analyze_only;
    b8 trace;
    b8 user_assert;
    b8 wait_end;
    b8 no_user;
};

struct Yov {
    Arena* static_arena;
    Arena* temp_arena;
    
    String main_script_path;
    String caller_dir;
    
    PooledArray<YovScript> scripts;
    
    Array<ScriptArg> script_args;
    
    // Type System
    PooledArray<VariableType> vtype_table;
    PooledArray<FunctionDefinition> functions;
    PooledArray<ArgDefinition> arg_definitions;
    PooledArray<ObjectDefinition> globals;
    
    Array<VariableTypeChild> string_properties;
    Array<VariableTypeChild> array_properties;
    Array<VariableTypeChild> enum_properties;
    
    PooledArray<Report> reports;
    
    YovSettings settings;
    
    i64 exit_code;
    b8 exit_code_is_set;
    b8 exit_requested;
};

struct YovThreadContext {
    Arena* scratch_arenas[2];
};

struct YovSystemInfo {
    u64 page_size;
    u32 logical_cores;
    
    u64 timer_start;
    u64 timer_frequency;
};

extern YovSystemInfo system_info; 
extern Yov* yov;
extern per_thread_var YovThreadContext thread_context;

void yov_initialize_thread();
void yov_shutdown_thread();

void yov_initialize(b32 import_core);
void yov_shutdown();

void yov_config_from_args();
void yov_config(String path, YovSettings settings, Array<ScriptArg> script_args);

void yov_set_exit_code(i64 exit_code);

void yov_print_script_help(Interpreter* inter);
i64 yov_run();
i32 yov_import_script(String path);

YovScript* yov_get_script(i32 script_id);
String yov_get_line_sample(Arena* arena, CodeLocation code);

ScriptArg* yov_find_script_arg(String name);
String yov_get_inherited_args(Arena* arena);

b32 yov_ask_yesno(String title, String message);

void report_error_ex(CodeLocation code, String text, ...);

void yov_print_reports();

#define report_error(code, text, ...) report_error_ex(code, STR(text), __VA_ARGS__);

String string_from_report(Arena* arena, Report report);
void print_report(Report report);

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
#define report_invalid_escape_sequence(_code, _v) report_error(_code, "Invalid escape sequence '\\%S'", STR(_v));
#define report_invalid_codepoint_literal(_code, _v) report_error(_code, "Invalid codepoint literal: %S", STR(_v));

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
#define report_expr_expects_bool(_code, _what)  report_error(_code, "%S expects a Bool", STR(_what));
#define report_expr_expects_lvalue(_code) report_error(_code, "Expresion expects a lvalue: {line}");
#define report_expr_semantic_unknown(_code)  report_error(_code, "Unknown expresion: {line}");
#define report_for_expects_an_array(_code)  report_error(_code, "Foreach-Statement expects an array");
#define report_semantic_unknown_op(_code) report_error(_code, "Unknown operation: {line}");
#define report_struct_recursive(_code) report_error(_code, "Recursive struct definition");
#define report_struct_circular_dependency(_code) report_error(_code, "Struct has circular dependency");
#define report_struct_implicit_member_type(_code) report_error(_code, "Implicit member type is not allowed in structs");
#define report_intrinsic_not_resolved(_code, _n) report_error(_code, "Intrinsic '%S' can't be resolved", STR(_n));
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

//- LEXER

struct Lexer {
    PooledArray<Token> tokens;
    String text;
    i32 script_id;
    
    u64 cursor;
    u64 code_start_line_offset;
    u32 code_column;
    u32 code_line;
};

Array<Token> lexer_generate_tokens(Arena* arena, String text, b32 discard_tokens, CodeLocation code);

BinaryOperator binary_operator_from_token(TokenKind token);
b32 token_is_sign_or_binary_op(TokenKind token);

//- AST

enum OpKind {
    OpKind_None,
    
    // Misc
    OpKind_Error,
    OpKind_Block,
    OpKind_ObjectType,
    OpKind_Import,
    
    // Definitions
    OpKind_ObjectDefinition,
    OpKind_EnumDefinition,
    OpKind_StructDefinition,
    OpKind_ArgDefinition,
    OpKind_FunctionDefinition,
    
    // Control Flow
    OpKind_IfStatement,
    OpKind_WhileStatement,
    OpKind_ForStatement,
    OpKind_ForeachArrayStatement,
    OpKind_Return,
    OpKind_Continue,
    OpKind_Break,
    
    // Expressions
    OpKind_Symbol,
    OpKind_ParameterList,
    OpKind_Indexing,
    OpKind_ArrayExpresion,
    OpKind_Binary,
    OpKind_Sign,
    OpKind_Reference,
    OpKind_IntLiteral,
    OpKind_BoolLiteral,
    OpKind_StringLiteral,
    OpKind_CodepointLiteral,
    OpKind_MemberValue,
    OpKind_Null,
    
    // Ops
    OpKind_Assignment,
    OpKind_FunctionCall,
};

struct OpNode {
    OpKind kind;
    CodeLocation code;
};

struct OpNode_Block : OpNode {
    Array<OpNode*> ops;
};

struct OpNode_Assignment : OpNode {
    OpNode* destination;
    OpNode* source;
    BinaryOperator binary_operator;
};

struct OpNode_Indexing : OpNode {
    OpNode* value;
    OpNode* index;
};

struct OpNode_IfStatement : OpNode {
    OpNode* expresion;
    OpNode* success;
    OpNode* failure;
};

struct OpNode_WhileStatement : OpNode {
    OpNode* expresion;
    OpNode* content;
};

struct OpNode_ForStatement : OpNode {
    OpNode* initialize_sentence;
    OpNode* condition_expresion;
    OpNode* update_sentence;
    OpNode* content;
};

struct OpNode_ForeachArrayStatement : OpNode {
    String element_name;
    String index_name;
    OpNode* expresion;
    OpNode* content;
};

struct OpNode_ObjectType : OpNode {
    String name;
    u32 array_dimensions;
    b32 is_reference;
};

struct OpNode_ObjectDefinition : OpNode {
    Array<String> names;
    OpNode_ObjectType* type;
    OpNode* assignment;
    b8 is_constant;
};

struct OpNode_FunctionCall : OpNode {
    String identifier;
    Array<OpNode*> parameters;
};

struct OpNode_ArrayExpresion : OpNode {
    Array<OpNode*> nodes;
    OpNode* type;
    b8 is_empty;
};

struct OpNode_Binary : OpNode {
    OpNode* left;
    OpNode* right;
    BinaryOperator op;
};

struct OpNode_Sign : OpNode {
    OpNode* expresion;
    BinaryOperator op;
};

struct OpNode_Reference : OpNode {
    OpNode* expresion;
};

struct OpNode_NumericLiteral : OpNode {
    union {
        i64 int_literal;
        b8 bool_literal;
        i64 codepoint_literal;
    };
};

struct OpNode_StringLiteral : OpNode {
    String raw_value;
    String value;
    Array<OpNode*> expresions;
};

struct OpNode_Symbol : OpNode {
    String identifier;
};

struct OpNode_ParameterList : OpNode {
    Array<OpNode*> nodes;
};

struct OpNode_MemberValue : OpNode {
    OpNode* expresion;
    String member;
};

struct OpNode_EnumDefinition : OpNode {
    String identifier;
    Array<String> names;
    Array<OpNode*> values;
};
struct OpNode_StructDefinition : OpNode {
    String identifier;
    Array<OpNode_ObjectDefinition*> members;
    u32 dependency_index; // Used in interpreter
};
struct OpNode_ArgDefinition : OpNode {
    String identifier;
    OpNode* type;
    OpNode* name;
    OpNode* description;
    OpNode* required;
    OpNode* default_value;
};
struct OpNode_FunctionDefinition : OpNode {
    String identifier;
    OpNode* block;
    Array<OpNode_ObjectDefinition*> parameters;
    Array<OpNode*> returns;
};

struct OpNode_Return : OpNode {
    OpNode* expresion;
};

struct OpNode_Import : OpNode {
    String path;
};

u32 get_node_size(OpKind kind);
Array<OpNode*> get_node_childs(Arena* arena, OpNode* node);

struct ParserState {
    Array<Token> tokens;
    i32 token_index;
};

struct Parser {
    PooledArray<ParserState> state_stack;
};

void parser_push_state(Parser* parser, Array<Token> tokens);
void parser_pop_state(Parser* parser);
ParserState* parser_get_state(Parser* parser);
Array<Token> parser_get_tokens_left(Parser* parser);


b32 check_tokens_are_couple(Array<Token> tokens, u32 open_index, u32 close_index, TokenKind open_token, TokenKind close_token);

Token peek_token(Parser* parser, i32 offset = 0);
Array<Token> peek_tokens(Parser* parser, i32 offset, u32 count);
void skip_tokens(Parser* parser, u32 count);
void skip_tokens_before_op(Parser* parser);
void skip_sentence(Parser* parser);
Token extract_token(Parser* parser);
Array<Token> extract_tokens(Parser* parser, u32 count);
Array<Token> extract_tokens_until(Parser* parser, b32 require_separator, TokenKind sep0, TokenKind sep1 = TokenKind_None);
Array<Token> extract_tokens_with_depth(Parser* parser, TokenKind open_token, TokenKind close_token, b32 require_separator);

Array<Array<Token>> split_tokens_in_parameters(Arena* arena, Array<Token> tokens);

OpNode* process_function_call(Parser* parser, Array<Token> tokens);

OpNode* extract_expresion_from_array(Parser* parser, Array<Token> tokens);
OpNode* extract_expresion(Parser* parser);
OpNode* extract_if_statement(Parser* parser);
OpNode* extract_while_statement(Parser* parser);
OpNode* extract_for_statement(Parser* parser);
OpNode* extract_enum_definition(Parser* parser);
OpNode* extract_struct_definition(Parser* parser);
OpNode* extract_arg_definition(Parser* parser);
OpNode* extract_function_definition(Parser* parser);
OpNode* extract_assignment(Parser* parser);
OpNode* extract_object_type(Parser* parser);
OpNode* extract_object_definition(Parser* parser);
OpNode* extract_function_call(Parser* parser);
OpNode* extract_return(Parser* parser);
OpNode* extract_continue(Parser* parser);
OpNode* extract_break(Parser* parser);
OpNode* extract_import(Parser* parser);
OpNode* extract_block(Parser* parser, b32 between_braces);
OpNode* extract_op(Parser* parser);

OpNode_Block* generate_ast(Array<Token> tokens);

Parser* parser_alloc(Array<Token> tokens);

Array<OpNode_Import*> imports_from_ast(Arena* arena, OpNode* ast);

void log_ast(OpNode* node, i32 depth);

//- Intermediate Representation

struct IR_Unit {
    UnitKind kind;
    CodeLocation code;
    i32 dst_index;
    union {
        struct {
            Value src;
        } copy;
        
        struct {
            Value src;
        } store;
        
        struct {
            FunctionDefinition* fn;
            Array<Value> parameters;
        } function_call;
        
        struct {
            i32 condition; // 0 -> None; 1 -> true; -1 -> false
            Value src;
            IR_Unit* unit;
        } jump;
        
        struct {
            Value src0;
            Value src1;
            BinaryOperator op;
        } binary_op;
        
        struct {
            Value src;
            BinaryOperator op;
        } sign_op;
        
        struct {
            Array<Value> sources;
        } string_expression;
        
        struct {
            Value src;
            Value child_index;
            b32 child_is_member;
        } child;
        
        struct {
            Value src;
        } result_eval;
        
        struct {
            Value count;
            VariableType* element_vtype;
        } array;
    };
    
    IR_Unit* prev;
    IR_Unit* next;
};

struct IR_Object {
    String identifier;
    VariableType* vtype;
    u32 assignment_count;
    i32 register_index;
    i32 scope;
};

struct IR_Group {
    b32 success;
    Value value;
    u32 unit_count;
    IR_Unit* first;
    IR_Unit* last;
};

struct IR_LoopingScope {
    IR_Unit* continue_unit;
    IR_Unit* break_unit;
};

struct IR_Context {
    Interpreter* inter;
    Arena* arena;
    Arena* temp_arena;
    PooledArray<IR_Register> registers;
    PooledArray<IR_Object> objects;
    PooledArray<IR_LoopingScope> looping_scopes;
    i32 scope;
    
    Array<VariableType*> params;
    Array<VariableType*> returns;
};

IR_Group ir_from_node(IR_Context* ir, OpNode* node0, ExpresionContext context, b32 new_scope);

IR ir_generate_from_function_definition(Interpreter* inter, FunctionDefinition* fn);
IR ir_generate_from_initializer(Interpreter* inter, OpNode* node, ExpresionContext context);
IR ir_generate_from_value(Value value);

b32 ct_value_from_node(Interpreter* inter, OpNode* node, VariableType* expected_vtype, Value* value);
b32 ct_string_from_node(Arena* arena, Interpreter* inter, OpNode* node, String* str);
b32 ct_bool_from_node(Interpreter* inter, OpNode* node, b32* b);

void ir_generate(Interpreter* inter, b32 require_args, b32 require_intrinsics);

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
    VariableType* vtype;
};

IR_Object* ir_find_object(IR_Context* ir, String identifier, b32 parent_scopes);
IR_Object* ir_find_object_from_value(IR_Context* ir, Value value);
IR_Object* ir_find_object_from_register(IR_Context* ir, i32 register_index);
IR_Object* ir_define_object(IR_Context* ir, String identifier, VariableType* vtype, i32 scope, i32 register_index);
IR_Object* ir_assume_object(IR_Context* ir, IR_Object* object, VariableType* vtype);
Symbol ir_find_symbol(IR_Context* ir, String identifier);

IR_LoopingScope* ir_looping_scope_push(IR_Context* ir, CodeLocation code);
void ir_looping_scope_pop(IR_Context* ir);
IR_LoopingScope* ir_get_looping_scope(IR_Context* ir);

void ir_scope_push(IR_Context* ir);
void ir_scope_pop(IR_Context* ir);

i32 ir_register_alloc_local(IR_Context* ir, VariableType* vtype);
i32 ir_register_get_global(IR_Context* ir, String identifier);
IR_Register* ir_register_get(IR_Context* ir, i32 index);

String string_from_register(Arena* arena, i32 index);
String string_from_unit_kind(Arena* arena, UnitKind unit);
String string_from_unit(Arena* arena, u32 index, u32 index_digits, u32 line_digits, Unit unit);
void print_units(Array<Unit> instructions);

void print_ir(String name, IR ir);

//- INTERPRETER

#define log_trace(code, text, ...) if (inter->mode == InterpreterMode_Execute && inter->settings.print_execution) { \
String log = string_format(scratch.arena, text, __VA_ARGS__);\
print_info("%S\n", log); \
} \
else do{}while(0)

#if DEV && 0
#define log_mem_trace(text, ...) print_info(STR(text), __VA_ARGS__)
#else
#define log_mem_trace(text, ...) do{}while(0)
#endif

Interpreter* interpreter_initialize();
b32 interpreter_run(Interpreter* inter, FunctionDefinition* fn, Array<Value> params);
void interpreter_run_main(Interpreter* inter);
void interpreter_shutdown(Interpreter* inter);

struct Scope {
    Array<Reference> registers;
    i32 pc; // Program Counter
    b32 return_requested;
    u32 scratch_arena_index;
    Scope* prev;
};

struct Interpreter {
    u32 object_id_counter;
    
    struct {
        Object* object_list;
        i32 object_count;
        i32 allocation_count;
    } gc;
    
    OpNode* empty_op;
    Scope* global_scope;
    Scope* current_scope;
    
    PooledArray<Reference> globals;
    
    struct {
        Reference yov;
        Reference os;
        Reference context;
        Reference calls;
    } common_globals;
};

void interpreter_exit(Interpreter* inter, i64 exit_code);
void interpreter_report_runtime_error(Interpreter* inter, CodeLocation code, Result result);
Result user_assertion(Interpreter* inter, String message);

Reference get_cd(Interpreter* inter);
String get_cd_value(Interpreter* inter);
String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path);
RedirectStdout get_calls_redirect_stdout(Interpreter* inter);

void    global_init(Interpreter* inter, ObjectDefinition def, CodeLocation code);
void    global_save(Interpreter* inter, String identifier, Reference ref);
void    global_save_by_index(Interpreter* inter, i32 index, Reference ref);
Reference global_get(Interpreter* inter, String identifier);
Reference global_get_by_index(Interpreter* inter, i32 index);

// Once defined the IR, this will be the functions used to execute/analyze

Reference ref_from_value(Interpreter* inter, Scope* scope, Value value);
void execute_ir(Interpreter* inter, IR ir, Array<Reference> output, Array<Value> params, CodeLocation code);
Reference execute_ir_single_return(Interpreter* inter, IR ir, Array<Value> params, CodeLocation code);

void run_instruction(Interpreter* inter, Unit unit);
void run_store(Interpreter* inter, i32 dst_index, Reference src, CodeLocation code);
void run_copy(Interpreter* inter, i32 dst_index, Reference src, CodeLocation code);
void run_return(Interpreter* inter, CodeLocation code);
void run_jump(Interpreter* inter, Reference ref, i32 condition, i32 offset, CodeLocation code);
void run_function_call(Interpreter* inter, i32 dst_index, FunctionDefinition* fn, Array<Value> parameters, CodeLocation code);
Reference run_binary_operation(Interpreter* inter, Reference dst, Reference left, Reference right, BinaryOperator op, CodeLocation code);
Reference run_sign_operation(Interpreter* inter, Reference value, BinaryOperator op, CodeLocation code);

//- SCOPE

void scope_clear(Interpreter* inter, Scope* scope);

void register_save(Interpreter* inter, Scope* scope, i32 index, Reference ref);
Reference register_get(Scope* scope, i32 index);

//- OBJECT

String string_from_object(Arena* arena, Interpreter* inter, Object* object, b32 raw = true);
String string_from_ref(Arena* arena, Interpreter* inter, Reference ref, b32 raw = true);
String string_from_compiletime(Arena* arena, Interpreter* inter, Value value, b32 raw = true);
b32 bool_from_compiletime(Interpreter* inter, Value value);
VariableType* type_from_compiletime(Interpreter* inter, Value value);
Reference ref_from_object(Object* object);
Reference ref_from_address(Object* parent, VariableType* vtype, void* address);

void ref_set_member(Interpreter* inter, Reference ref, u32 index, Reference member);

Reference ref_get_child(Interpreter* inter, Reference ref, u32 index, b32 is_member);
Reference ref_get_member(Interpreter* inter, Reference ref, u32 index);
Reference ref_get_property(Interpreter* inter, Reference ref, u32 index);

u32 ref_get_child_count(Reference ref, b32 is_member);
u32 ref_get_property_count(Reference ref);
u32 ref_get_member_count(Reference ref);

Reference alloc_int(Interpreter* inter, i64 value);
Reference alloc_bool(Interpreter* inter, b32 value);
Reference alloc_string(Interpreter* inter, String value);
Reference alloc_array(Interpreter* inter, VariableType* element_vtype, i64 count, b32 null_elements);
Reference alloc_array_multidimensional(Interpreter* inter, VariableType* base_vtype, Array<i64> dimensions);
Reference alloc_array_from_enum(Interpreter* inter, VariableType* enum_vtype);
Reference alloc_enum(Interpreter* inter, VariableType* vtype, i64 index);
Reference alloc_reference(Interpreter* inter, Reference ref);

b32 is_valid(Reference ref);
b32 is_unknown(Reference ref);
b32 is_const(Reference ref);
b32 is_null(Reference ref);
b32 is_int(Reference ref);
b32 is_bool(Reference ref);
b32 is_string(Reference ref);
b32 is_array(Reference ref);
b32 is_enum(Reference ref);
b32 is_reference(Reference ref);

i64 get_int(Reference ref);
b32 get_bool(Reference ref);
i64 get_enum_index(Reference ref);
String get_string(Reference ref);
ObjectData_Array* get_array(Reference ref);
Reference dereference(Reference ref);

i64 get_int_member(Interpreter* inter, Reference ref, String member);
b32 get_bool_member(Interpreter* inter, Reference ref, String member);
String get_string_member(Interpreter* inter, Reference ref, String member);

void set_int(Reference ref, i64 v);
void set_bool(Reference ref, b32 v);
void set_enum_index(Reference ref, i64 v);

ObjectData_String* ref_string_get_data(Interpreter* inter, Reference ref);
void ref_string_prepare(Interpreter* inter, Reference ref, u64 new_size, b32 can_discard);
void ref_string_clear(Interpreter* inter, Reference ref);
void ref_string_set(Interpreter* inter, Reference ref, String v);
void ref_string_append(Interpreter* inter, Reference ref, String v);

void set_reference(Interpreter* inter, Reference ref, Reference src);

void set_int_member(Interpreter* inter, Reference ref, String member, i64 v);
void set_bool_member(Interpreter* inter, Reference ref, String member, b32 v);
void set_enum_index_member(Interpreter* inter, Reference ref, String member, i64 v);
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
void ref_assign_YovParseOutput(Interpreter* inter, Reference ref, Yov* temp_yov);
void ref_assign_FunctionDefinition(Interpreter* inter, Reference ref, FunctionDefinition* fn);
void ref_assign_StructDefinition(Interpreter* inter, Reference ref, VariableType* vtype);
void ref_assign_EnumDefinition(Interpreter* inter, Reference ref, VariableType* vtype);
void ref_assign_ObjectDefinition(Interpreter* inter, Reference ref, ObjectDefinition def);

void ref_assign_Type(Interpreter* inter, Reference ref, VariableType* vtype);
VariableType* get_Type(Interpreter* inter, Reference ref);

Reference ref_from_Result(Interpreter* inter, Result res);
Result Result_from_ref(Interpreter* inter, Reference ref);

u32 object_generate_id(Interpreter* inter);
Reference object_alloc(Interpreter* inter, VariableType* vtype);
void object_free(Interpreter* inter, Object* obj);

void object_increment_ref(Object* obj);
void object_decrement_ref(Object* obj);

void ref_release_internal(Interpreter* inter, Reference ref);
void ref_init(Interpreter* inter, Reference ref, IR ir);
void ref_copy(Interpreter* inter, Reference dst, Reference src);
Reference ref_alloc_and_copy(Interpreter* inter, Reference src);

void* object_dynamic_allocate(Interpreter* inter, u64 size);
void object_dynamic_free(Interpreter* inter, void* ptr);
void object_free_unused_memory(Interpreter* inter);

void* gc_allocate(Interpreter* inter, u64 size);
void gc_free(Interpreter* inter, void* ptr);
void gc_free_unused(Interpreter* inter);

void print_memory_usage(Interpreter* inter);