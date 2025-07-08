#pragma once

#define YOV_MAJOR_VERSION 0
#define YOV_MINOR_VERSION 2
#define YOV_REVISION_VERSION 0
#define YOV_VERSION STR("v"MACRO_STR(YOV_MAJOR_VERSION)"."MACRO_STR(YOV_MINOR_VERSION)"."MACRO_STR(YOV_REVISION_VERSION))

//-

#include <stdint.h>
#include <stdarg.h>

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

#define inline_fn inline
#define internal_fn static
#define global_var extern

#define array_count(x) (sizeof(x) / sizeof(x[0]))
#define foreach(it, count) for (u32 (it) = 0; (it) < (count); (it)++)

#if DEV
#define assert(x) do { if ((x) == 0) assertion_failed(#x); } while (0)
#else
#define assert(x) do {} while (0)
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

void assertion_failed(const char* text);

void memory_copy(void* dst, const void* src, u64 size);
void memory_zero(void* dst, u64 size);

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


//- OS 

void os_setup_memory_info();
void os_initialize();
void os_shutdown();

struct Result {
    String message;
    i32 exit_code;
    b32 success;
};

inline_fn Result result_failed_make(String message, i32 error_code = -1) { return Result{ message, error_code, false }; }

#define RESULT_SUCCESS Result{ "", 0, true }

enum Severity {
    Severity_Info,
    Severity_Warning,
    Severity_Error,
};

void os_print(Severity severity, String text);

void* os_allocate_heap(u64 size);
void  os_free_heap(void* address);

void* os_reserve_virtual_memory(u32 pages, b32 commit);
void os_commit_virtual_memory(void* address, u32 page_offset, u32 page_count);
void os_release_virtual_memory(void* address);

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

b32 os_ask_yesno(String title, String content);

enum RedirectStdout {
    RedirectStdout_Console,
    RedirectStdout_Ignore,
    RedirectStdout_Script,
};

struct CallResult {
    Result result;
    String stdout;
};

CallResult os_call(Arena* arena, String working_dir, String command, RedirectStdout redirect_stdout);
CallResult os_call_exe(Arena* arena, String working_dir, String exe, String args, RedirectStdout redirect_stdout);
CallResult os_call_script(Arena* arena, String working_dir, String script, String args, RedirectStdout redirect_stdout);

String os_get_working_path(Arena* arena);
String os_get_executable_path(Arena* arena);

b32 os_path_is_absolute(String path);
b32 os_path_is_directory(String path);

Array<String> os_get_args(Arena* arena);

void os_thread_sleep(u64 millis);
void os_console_wait();

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
b32 u32_from_string(u32* dst, String str);
b32 u32_from_char(u32* dst, char c);
b32 i64_from_string(String str, i64* out);
b32 i32_from_string(String str, i32* out);
String string_from_codepoint(Arena* arena, u32 codepoint);
String string_from_memory(Arena* arena, u64 bytes);
String string_join(Arena* arena, LinkedList<String> ll);
Array<String> string_split(Arena* arena, String str, String separator);
String string_replace(Arena* arena, String str, String old_str, String new_str);
String string_format_with_args(Arena* arena, String string, va_list args);
String string_format_ex(Arena* arena, String string, ...);
u32 string_get_codepoint(String str, u64* cursor_ptr);
u32 string_calculate_char_count(String str);

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
};

String string_from_binary_operator(BinaryOperator op);

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
    
    TokenKind_IfKeyword,
    TokenKind_ElseKeyword,
    TokenKind_WhileKeyword,
    TokenKind_ForKeyword,
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
void log_tokens(Array<Token> tokens);

//- PROGRAM CONTEXT 

#include "templates.h"

void print_ex(Severity severity, String str, ...);
#define print(severity, str, ...) print_ex(severity, STR(str), __VA_ARGS__)
#define print_info(str, ...) print_ex(Severity_Info, STR(str), __VA_ARGS__)
#define print_warning(str, ...) print_ex(Severity_Warning, STR(str), __VA_ARGS__)
#define print_error(str, ...) print_ex(Severity_Error, STR(str), __VA_ARGS__)

#define print_separator() print_ex(Severity_Info, STR("=========================\n"))

struct OpNode;

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
    OpNode* ast;
};

struct Yov {
    Arena* static_arena;
    Arena* temp_arena;
    Arena* scratch_arenas[2];
    
    String main_script_path;
    String caller_dir;
    
    PooledArray<YovScript> scripts;
    
    Array<ScriptArg> args;
    
    PooledArray<Report> reports;
    i32 error_count;
    
    struct {
        b8 analyze_only;
        b8 trace;
        b8 user_assert;
        b8 wait_end;
        b8 no_user;
    } settings;
    
    struct {
        u64 page_size;
        void* internal;
    } os;
    
    i32 exit_code;
    b8 exit_code_is_set;
};

extern Yov* yov;

void yov_initialize();
void yov_shutdown();

void yov_set_exit_code(i32 exit_code);

void yov_run();

i32 yov_import_script(String path);

YovScript* yov_get_script(i32 script_id);
String yov_get_line_sample(Arena* arena, CodeLocation code);

b32 yov_read_args();
ScriptArg* yov_find_arg(String name);
String yov_get_inherited_args(Arena* arena);

b32 yov_ask_yesno(String title, String message);

void report_error_ex(CodeLocation code, String text, ...);

void yov_print_reports();

#define report_error(code, text, ...) report_error_ex(code, STR(text), __VA_ARGS__);

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
#define report_enum_value_expects_an_int(_code) report_error(_code, "Enum value expects an Int");
#define report_function_not_found(_code, _v) report_error(_code, "Function '%S' not found", _v);
#define report_function_expecting_parameters(_code, _v, _c) report_error(_code, "Function '%S' is expecting %u parameters", _v, _c);
#define report_function_wrong_parameter_type(_code, _f, _t, _c) report_error(_code, "Function '%S' is expecting a '%S' as a parameter %u", _f, _t, _c);
#define report_function_expects_ref_as_parameter(_code, _f, _c) report_error(_code, "Function '%S' is expecting a reference as a parameter %u", _f, _c);
#define report_function_expects_noref_as_parameter(_code, _f, _c) report_error(_code, "Function '%S' is not expecting a reference as a parameter %u", _f, _c);
#define report_function_wrong_return_type(_code, _t) report_error(_code, "Expected a '%S' as a return", _t);
#define report_function_expects_ref_as_return(_code) report_error(_code, "Expected a reference as a return");
#define report_function_expects_no_ref_as_return(_code) report_error(_code, "Can't return a reference");
#define report_function_no_return(_code, _f) report_error(_code, "Not all paths of '%S' have a return", _f);
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
#define report_reftype_invalid(_code) report_error(_code, "Invalid definition of a reference: {line}");
#define report_arg_invalid_name(_code, _v) report_error(_code, "Invalid arg name '%S'", _v);
#define report_arg_duplicated_name(_code, _v) report_error(_code, "Duplicated arg name '%S'", _v);
#define report_arg_is_required(_code, _v) report_error(_code, "Argument '%S' is required", _v);
#define report_arg_wrong_value(_code, _n, _v) report_error(_code, "Invalid argument assignment '%S = %S'", _n, _v);
#define report_arg_unknown(_code, _v) report_error(_code, "Unknown argument '%S'", _v);
#define report_break_inside_loop(_code) report_error(_code, "Break keyword must be used inside a loop");
#define report_continue_inside_loop(_code) report_error(_code, "Continue keyword must be used inside a loop");

//- LANG REPORTS

#define lang_report_stack_is_broken() report_error({}, "[LANG_ERROR] The stack is broken");
#define lang_report_unfreed_objects() report_error({}, "[LANG_ERROR] Not all objects have been freed");
#define lang_report_unfreed_dynamic() report_error({}, "[LANG_ERROR] Not all dynamic allocations have been freed");

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
    OpKind_Error,
    OpKind_Block,
    OpKind_Assignment,
    OpKind_Symbol,
    OpKind_Indexing,
    OpKind_IfStatement,
    OpKind_WhileStatement,
    OpKind_ForStatement,
    OpKind_ForeachArrayStatement,
    OpKind_ObjectDefinition,
    OpKind_ObjectType,
    OpKind_FunctionCall,
    OpKind_ArrayExpresion,
    OpKind_Binary,
    OpKind_Sign,
    OpKind_Reference,
    OpKind_IntLiteral,
    OpKind_BoolLiteral,
    OpKind_StringLiteral,
    OpKind_CodepointLiteral,
    OpKind_MemberValue,
    OpKind_EnumDefinition,
    OpKind_StructDefinition,
    OpKind_ArgDefinition,
    OpKind_FunctionDefinition,
    OpKind_Return,
    OpKind_Continue,
    OpKind_Break,
    OpKind_Import,
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
    String object_name;
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
    OpNode* return_node;
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
OpNode* extract_block(Parser* parser);
OpNode* extract_op(Parser* parser);

OpNode* generate_ast(Array<Token> tokens, b32 is_block);

Parser* parser_alloc(Array<Token> tokens);

Array<OpNode_Import*> get_imports(Arena* arena, OpNode* ast);

void log_ast(OpNode* node, i32 depth);

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

struct Object {
    u32 ID;
    i32 vtype;
    i32 ref_count;
    Object* prev;
    Object* next;
};

struct Object_Int : Object {
    i64 value;
};
struct Object_Bool : Object {
    b32 value;
};
struct Object_Enum : Object {
    i64 index;
};
struct Object_String : Object{
    String value;
};
struct Object_Array : Object {
    Array<Object*> elements;
};
struct Object_Struct : Object {
    Array<Object*> members;
};

struct ObjectRef {
    String identifier;
    Object* object;
    i32 vtype;
    b32 constant;
};

enum ValueKind {
    ValueKind_LValue,
    ValueKind_RValue,
    ValueKind_Reference,// LValue requested to be referenced
};

struct LValue {
    ObjectRef* ref;
};

struct Value {
    Object* obj;// TODO(Jose): Rename to "object"
    Object* parent;
    u32 index;
    i32 vtype;
    ValueKind kind;
    LValue lvalue;
};

#define VType_Unknown -1
#define VType_Void 0
#define VType_Int 1
#define VType_Bool 2
#define VType_String 3

#define VType_CopyMode vtype_from_name(inter, "CopyMode")
#define VType_YovInfo vtype_from_name(inter, "YovInfo")
#define VType_Context vtype_from_name(inter, "Context")
#define VType_OS vtype_from_name(inter, "OS")
#define VType_CallResult vtype_from_name(inter, "CallResult")

#define VType_IntArray vtype_from_array_dimension(inter, VType_Int, 1)
#define VType_BoolArray vtype_from_array_dimension(inter, VType_Bool, 1)
#define VType_StringArray vtype_from_array_dimension(inter, VType_String, 1)

global_var Object* nil_obj;
global_var Object* null_obj;
global_var ObjectRef* nil_ref;

enum VariableKind {
    VariableKind_Unknown,
    VariableKind_Void,
    VariableKind_Primitive,
    VariableKind_Array,
    VariableKind_Enum,
    VariableKind_Struct,
};

struct VariableType {
    i32 vtype;
    String name;
    VariableKind kind;
    
    i32 array_of;
    
    Array<String> enum_names;
    Array<i64> enum_values;
    
    Array<String> struct_names;
    Array<i32> struct_vtypes;
    Array<OpNode*> struct_initialize_expresions;
};

struct FunctionReturn {
    Value return_value;
    Result error_result;
};

struct Interpreter;
typedef FunctionReturn IntrinsicFunction(Interpreter* inter, Array<Value> objs, CodeLocation code);

struct IntrinsicDefinition {
    String name;
    IntrinsicFunction* fn;
};

Array<IntrinsicDefinition> get_intrinsics_table(Arena* arena);

struct ObjectDefinition {
    i32 vtype;
    b32 is_reference;
    String name;
    OpNode* default_value;
};

inline_fn ObjectDefinition obj_def_make(String name, i32 vtype, b32 is_reference = false) {
    ObjectDefinition d{};
    d.name = name;
    d.vtype = vtype;
    d.is_reference = is_reference;
    return d;
}

struct FunctionDefinition {
    String identifier;
    Array<ObjectDefinition> parameters;
    i32 return_vtype;
    b32 return_reference;
    
    CodeLocation code;
    IntrinsicFunction* intrinsic_fn;
    OpNode_Block* defined_fn;
    
    b8 is_intrinsic;
};

struct ArgDefinition {
    String identifier;
    String name;
    String description;
    i32 vtype;
    b32 required;
};

enum SymbolType {
    SymbolType_None,
    SymbolType_ObjectRef,
    SymbolType_Function,
    SymbolType_Type,
};

struct Symbol {
    SymbolType type;
    String identifier;
    
    ObjectRef* ref;
    FunctionDefinition* function;
    i32 vtype;
};

struct InterpreterSettings {
    b8 user_assertion; // Forces user assertion of every operation to the OS
    b8 print_execution;
};

enum InterpreterMode {
    InterpreterMode_Analysis, // Do semantic analysis of the entire AST
    InterpreterMode_Help,     // Validate arguments and show the descriptions
    InterpreterMode_Execute,  // Execute AST until an error ocurrs
};

void interpret(InterpreterSettings settings, InterpreterMode mode);

enum ScopeType {
    ScopeType_Global,
    ScopeType_Block,
    ScopeType_LoopIteration,
    ScopeType_Function,
};

struct Scope {
    ScopeType type;
    PooledArray<ObjectRef> object_refs;
    PooledArray<Object*> temp_objects;
    Value return_value;
    i32 expected_return_vtype;
    b32 expected_return_reference;
    b32 loop_iteration_continue_requested;
    b32 loop_iteration_break_requested;
    Scope* next;
    Scope* previous;
};

struct Interpreter {
    InterpreterMode mode;
    InterpreterSettings settings;
    
    OpNode* root;
    
    PooledArray<VariableType> vtype_table;
    PooledArray<FunctionDefinition> functions;
    PooledArray<ArgDefinition> arg_definitions;
    
    u32 object_id_counter;
    Object* object_list;
    i32 object_count;
    i32 allocation_count;
    
    OpNode* empty_op;
    
    Scope* global_scope;
    Scope* current_scope;
    Scope* free_scope;
    
    struct {
        ObjectRef* yov;
        ObjectRef* os;
        ObjectRef* context;
        ObjectRef* calls;
    } globals;
};

void interpreter_exit(Interpreter* inter, i32 exit_code);
void interpreter_report_runtime_error(Interpreter* inter, CodeLocation code, String resolved_line, Result result);
Result user_assertion(Interpreter* inter, String message);

struct ExpresionContext {
    i32 expected_vtype;
};

inline_fn ExpresionContext expresion_context_make(i32 expected_vtype) {
    ExpresionContext d{};
    d.expected_vtype = expected_vtype;
    return d;
}

Value interpret_expresion(Interpreter* inter, OpNode* node, ExpresionContext context);
Value interpret_assignment_for_object_definition(Interpreter* inter, OpNode_ObjectDefinition* node, b32 allow_reference);
void interpret_object_definition(Interpreter* inter, OpNode* node0);
void interpret_assignment(Interpreter* inter, OpNode* node0);
void interpret_if_statement(Interpreter* inter, OpNode* node0);
void interpret_while_statement(Interpreter* inter, OpNode* node0);
void interpret_for_statement(Interpreter* inter, OpNode* node0);
void interpret_foreach_array_statement(Interpreter* inter, OpNode* node0);
Value interpret_function_call(Interpreter* inter, OpNode* node0, b32 is_expresion);
i32 interpret_object_type(Interpreter* inter, OpNode* node0, b32 allow_reference);
void interpret_return(Interpreter* inter, OpNode* node0);
void interpret_continue(Interpreter* inter, OpNode* node0);
void interpret_break(Interpreter* inter, OpNode* node0);
void interpret_block(Interpreter* inter, OpNode* block0, b32 push_scope);
void interpret_op(Interpreter* inter, OpNode* parent, OpNode* node);

Value get_cd(Interpreter* inter);
String get_cd_value(Interpreter* inter);
String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path);
RedirectStdout get_calls_redirect_stdout(Interpreter* inter);
b32 interpretion_failed(Interpreter* inter);
b32 skip_ops(Interpreter* inter);

//- SCOPE

Scope* scope_alloc(Interpreter* inter, ScopeType type);
void   scope_clear(Interpreter* inter, Scope* scope);
Scope* scope_push(Interpreter* inter, ScopeType type, i32 expected_return_vtype, b32 expected_return_reference);
void   scope_pop(Interpreter* inter);

void scope_add_temporal(Interpreter* inter, Object* object);
void scope_add_temporal(Interpreter* inter, Scope* scope, Object* object);
Scope* scope_find_returnable(Interpreter* inter);
Scope* scope_find_looping(Interpreter* inter);
ObjectRef* scope_define_object_ref(Interpreter* inter, String identifier, Value value, b32 constant = false);
ObjectRef* scope_find_object_ref(Interpreter* inter, String identifier, b32 parent_scopes);

//- DEFINITIONS

i32 define_enum(Interpreter* inter, String name, Array<String> names, Array<i64> values);
void define_struct(Interpreter* inter, String name, Array<ObjectDefinition> members);
void define_arg(Interpreter* inter, String identifier, String name, i32 vtype, b32 required, String description);
void define_function(Interpreter* inter, CodeLocation code, String identifier, Array<ObjectDefinition> parameters, i32 return_vtype, b32 return_reference, OpNode_Block* block);
void define_intrinsic_function(Interpreter* inter, CodeLocation code, String identifier, Array<ObjectDefinition> parameters, i32 return_vtype);

Symbol find_symbol(Interpreter* inter, String identifier);
FunctionDefinition* find_function(Interpreter* inter, String identifier);
ArgDefinition* find_arg_definition_by_name(Interpreter* inter, String name);
Value call_function(Interpreter* inter, FunctionDefinition* fn, Array<Value> parameters, OpNode* parent_node, b32 is_expresion);

VariableType vtype_get(Interpreter* inter, i32 vtype);
b32 vtype_is_enum(Interpreter* inter, i32 vtype);
b32 vtype_is_array(i32 vtype);
b32 vtype_is_struct(Interpreter* inter, i32 vtype);
i32 vtype_from_name(Interpreter* inter, String name);
i32 vtype_from_array_dimension(Interpreter* inter, i32 vtype, u32 dimension);
i32 vtype_from_array_element(Interpreter* inter, i32 vtype);
i32 vtype_from_array_base(Interpreter* inter, i32 vtype);
u32 vtype_get_size(Interpreter* inter, i32 vtype);
Value vtype_get_member(Interpreter* inter, i32 vtype, String member);
i32 vtype_get_element(Interpreter* inter, i32 vtype, u32 index);
i32 encode_vtype(u32 index, u32 dimensions);
void decode_vtype(i32 vtype, u32* _index, u32* _dimensions);

String string_from_vtype(Arena* arena, Interpreter* inter, i32 vtype);

//- VALUE OPS 

Value value_null(i32 vtype);
Value value_void();
Value value_nil();
Value value_def(Interpreter* inter, i32 vtype);
Value rvalue_make(Object* obj, Object* parent, u32 index, i32 vtype);
Value rvalue_from_obj(Object* obj);
Value lvalue_make(Object* obj, ObjectRef* ref, Object* parent, u32 index, i32 vtype);
Value lvalue_from_ref(ObjectRef* ref);
Value value_from_child(Value parent_value, Object* object, u32 index, i32 vtype);
Value value_from_string(Interpreter* inter, String str, i32 vtype);

String string_from_value(Arena* arena, Interpreter* inter, Value value, b32 raw = true);
String string_from_obj(Arena* arena, Interpreter* inter, Object* obj, b32 raw = true);

// Assignment Rules

// L = L   -> Copy
// L = R   -> Copy
// L = Ref -> Ref
// R = L   -> Copy
// R = R   -> Copy
// R = Ref -> Error

// new L = L   -> Alloc and Copy
// new L = R   -> Ref
// new L = Ref -> Ref
// new R = L   -> Alloc and Copy
// new R = R   -> Ref
// new R = Ref -> Error

b32 value_assign(Interpreter* inter, Value dst, Value src);
b32 value_assign_as_new(Interpreter* inter, Value dst, Value src);
b32 value_assign_ref(Interpreter* inter, Value* dst, Value src);
b32 value_assign_null(Interpreter* inter, Value* dst);
b32 value_copy(Interpreter* inter, Value dst, Value src);

Array<Object*> object_get_childs(Interpreter* inter, Object* obj);
b32 object_set_child(Interpreter* inter, Object* obj, u32 index, Object* child);

Value value_get_element(Interpreter* inter, Value value, u32 index);
Value value_get_member(Interpreter* inter, Value value, String member_name);

Value alloc_int(Interpreter* inter, i64 value);
Value alloc_bool(Interpreter* inter, b32 value);
Value alloc_string(Interpreter* inter, String value);
Value alloc_array(Interpreter* inter, i32 element_vtype, i64 count, b32 null_elements);
Value alloc_array_multidimensional(Interpreter* inter, i32 base_vtype, Array<i64> dimensions);
Value alloc_array_from_enum(Interpreter* inter, i32 enum_vtype);
Value alloc_enum(Interpreter* inter, i32 vtype, i64 index);

b32 value_is_vtype(Value value, i32 vtype);
b32 is_valid(Object* obj);
b32 is_valid(Value value);
b32 is_unknown(Object* obj);
b32 is_unknown(Value value);

b32 is_const(Value value);
b32 is_void(Value value);
b32 is_null(const Object* obj);
b32 is_null(Value value);
b32 is_int(Value value);
b32 is_bool(Value value);
b32 is_string(Value value);
b32 is_array(Value value);
b32 is_enum(Interpreter* inter, Value value);
b32 is_struct(Interpreter* inter, i32 vtype);

i64 get_int(Value value);
b32 get_bool(Value value);
i64 get_enum_index(Interpreter* inter, Value value);
String get_string(Value value);
Array<Object*> get_array(Value value);

void set_int(Value value, i64 v);
void set_bool(Value value, b32 v);
void set_enum_index(Interpreter* inter, Value value, i64 v);
void set_string(Interpreter* inter, Value value, String v);

enum CopyMode {
    CopyMode_NoOverride,
    CopyMode_Override,
};
inline_fn CopyMode get_enum_CopyMode(Interpreter* inter, Value value) {
    return (CopyMode)get_enum_index(inter, value);
}

//- GARBAGE COLLECTOR

u32 object_generate_id(Interpreter* inter);
Value object_alloc(Interpreter* inter, i32 vtype);
void object_free(Interpreter* inter, Object* obj);
void object_free_unused(Interpreter* inter);

void object_increment_ref(Object* obj);
void object_decrement_ref(Object* obj);

void object_release_internal(Interpreter* inter, Object* obj);
void object_copy(Interpreter* inter, Object* dst, Object* src);
Object* object_alloc_and_copy(Interpreter* inter, Object* src);

void* gc_dynamic_allocate(Interpreter* inter, u64 size);
void gc_dynamic_free(Interpreter* inter, void* ptr);

void print_memory_usage(Interpreter* inter);

//- TRANSPILER 

void yov_transpile_core_definitions();

String transpile_definitions(Arena* arena, OpNode_Block* ast);

String transpile_definition_for_object_type(Arena* arena, OpNode_ObjectType* node);
String transpile_definition_for_object_definition(Arena* arena, OpNode_ObjectDefinition* node);
