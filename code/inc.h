#pragma once

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

#define _JOIN(x, y) x##y
#define JOIN(x, y) _JOIN(x, y)

#define inline_fn inline
#define internal_fn static
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

#if _WIN32
#define OS_WINDOWS 1
#endif

#define STR(x) string_make(x)

#define _MACRO_STR(x) #x
#define MACRO_STR(x) _MACRO_STR(x) 

#define YOV_MAJOR_VERSION 0
#define YOV_MINOR_VERSION 1
#define YOV_REVISION_VERSION 0
#define YOV_VERSION STR("v"MACRO_STR(YOV_MAJOR_VERSION)"."MACRO_STR(YOV_MINOR_VERSION)"."MACRO_STR(YOV_REVISION_VERSION))

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
};

template<typename T>
struct Array {
    T* data;
    u32 count;
    
    inline T& operator[](u32 index) {
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

void os_initialize();
void os_shutdown();

struct Result {
    b32 success;
    String message;
};

#define RESULT_SUCCESS Result{ true }

enum Severity {
    Severity_Info,
    Severity_Warning,
    Severity_Error,
};

void os_print(Severity severity, String text);

u64 os_get_page_size();
u32 os_pages_from_bytes(u64 bytes);

void* os_allocate_heap(u64 size);
void  os_free_heap(void* address);

void* os_reserve_virtual_memory(u32 pages, b32 commit);
void os_commit_virtual_memory(void* address, u32 page_offset, u32 page_count);
void os_release_virtual_memory(void* address);

b32 os_exists(String path);
b32 os_read_entire_file(Arena* arena, String path, RawBuffer* result);
Result os_copy_file(String dst_path, String src_path, b32 override);
Result os_move_file(String dst_path, String src_path);
Result os_delete_file(String path);
Result os_create_directory(String path, b32 recursive);
Result os_delete_directory(String path);
Result os_copy_directory(String dst_path, String src_path);
Result os_move_directory(String dst_path, String src_path);

b32 os_ask_yesno(String title, String content);

i32 os_call(String working_dir, String command);
i32 os_call_exe(String working_dir, String exe, String params);

String os_get_working_path(Arena* arena);

b32 os_path_is_absolute(String path);
b32 os_path_is_directory(String path);

Array<String> os_get_args(Arena* arena);

void os_thread_sleep(u64 millis);
void os_console_wait();

//- MATH 

u64 u64_divide_high(u64 n0, u64 n1);

//- C STRING 

u32 cstring_size(const char* str);
u32 cstring_set(char* dst, const char* src, u32 src_size, u32 buff_size);
u32 cstring_copy(char* dst, const char* src, u32 buff_size);
u32 cstring_append(char* dst, const char* src, u32 buff_size);
void cstring_from_u64(char* dst, u64 value, u32 base = 10);
void cstring_from_i64(char* dst, i64 value, u32 base = 10);
void cstring_from_f64(char* dst, f64 value, u32 decimals);

//- STRING 

String string_make(const char* cstr, u64 size);
String string_make(const char* cstr);
String string_make(String str);
String string_make(RawBuffer buffer);
String string_copy(Arena* arena, String src);
String string_substring(String str, u64 offset, u64 size);
b32 string_equals(String s0, String s1);
b32 u32_from_string(u32* dst, String str);
b32 u32_from_char(u32* dst, char c);
b32 i64_from_string(String str, i64* out);
b32 i32_from_string(String str, i32* out);
String string_from_memory(Arena* arena, u64 bytes);
String string_join(Arena* arena, LinkedList<String> ll);
Array<String> string_split(Arena* arena, String str, String separator);
String string_replace(Arena* arena, String str, String old_str, String new_str);
String string_format_with_args(Arena* arena, String string, va_list args);
String string_format_ex(Arena* arena, String string, ...);
u32 string_get_codepoint(String str, u64* cursor_ptr);

#define string_format(arena, str, ...) string_format_ex(arena, STR(str), __VA_ARGS__)

//- PATH 

Array<String> path_subdivide(Arena* arena, String path);
String path_resolve(Arena* arena, String path);
String path_append(Arena* arena, String str0, String str1);
String path_get_last_element(String path);

//- STRING BUILDER 

StringBuilder string_builder_make(Arena* arena);
void append(StringBuilder* builder, String str);
void append(StringBuilder* builder, const char* cstr);
void append_i64(StringBuilder* builder, i64 v, u32 base = 10);
void append_i32(StringBuilder* builder, i32 v, u32 base = 10);
void append_u64(StringBuilder* builder, u64 v, u32 base = 10);
void append_u32(StringBuilder* builder, u32 v, u32 base = 10);
void append_f64(StringBuilder* builder, f64 v, u32 decimals);
void append_char(StringBuilder* builder, char c);
String string_from_builder(Arena* arena, StringBuilder* builder);

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

inline_fn CodeLocation code_location_make(u64 offset, u64 start_line_offset, u32 line, u32 column, i32 script_id) { return { offset, start_line_offset, line, column, script_id }; }

enum TokenKind {
    TokenKind_None,
    TokenKind_Error,
    TokenKind_Separator,
    TokenKind_Comment,
    TokenKind_Identifier,
    TokenKind_IntLiteral,
    TokenKind_StringLiteral,
    TokenKind_Dot,
    TokenKind_Comma,
    TokenKind_Colon,
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
    TokenKind_BinaryOperator,
    
    TokenKind_IfKeyword,
    TokenKind_ElseKeyword,
    TokenKind_WhileKeyword,
    TokenKind_ForKeyword,
    TokenKind_EnumKeyword,
    
    TokenKind_BoolLiteral,
};

struct Token {
    TokenKind kind;
    String value;
    CodeLocation code;
    
    union {
        BinaryOperator binary_operator;
    };
};

String string_from_tokens(Arena* arena, Array<Token> tokens);

//- PROGRAM CONTEXT 

#include "templates.h"

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

struct ProgramArg {
    String name;
    String value;
};

struct Yov {
    Arena* static_arena;
    Arena* temp_arena;
    
    String script_name;
    String script_dir;
    String script_path;
    String script_text;
    
    String caller_dir;
    
    Array<ProgramArg> args;
    
    PooledArray<Report> reports;
    i32 error_count;
};

Yov* yov_initialize(Arena* arena, String script_path);
void yov_shutdown(Yov* ctx);

String yov_get_script_path(Yov* ctx, i32 script_id);
String yov_get_line_sample(Arena* arena, Yov* ctx, CodeLocation code);

b32 generate_program_args(Yov* ctx, Array<String> raw_args);

void report_error_ex(Yov* ctx, CodeLocation code, String text, ...);

void yov_print_reports(Yov* ctx);

#define report_error(ctx, code, text, ...) report_error_ex(ctx, code, STR(text), __VA_ARGS__);
#define log_trace(ctx, code, text, ...) print_info(STR(text), __VA_ARGS__);

void print_report(Yov* ctx, Report report);

//- SYNTACTIC REPORTS 

#define report_common_missing_closing_bracket(_code) report_error(parser->ctx, _code, "Missing closing bracket");
#define report_common_missing_opening_bracket(_code) report_error(parser->ctx, _code, "Missing opening bracket");
#define report_common_missing_closing_parenthesis(_code) report_error(parser->ctx, _code, "Missing closing parenthesis");
#define report_common_missing_opening_parenthesis(_code) report_error(parser->ctx, _code, "Missing opening parenthesis");
#define report_common_missing_closing_brace(_code) report_error(parser->ctx, _code, "Missing closing brace");
#define report_common_missing_opening_brace(_code) report_error(parser->ctx, _code, "Missing opening brace");
#define report_common_expecting_valid_expresion(_code) report_error(parser->ctx, _code, "Expecting a valid expresion: {line}");
#define report_common_expecting_parenthesis(_code, _for_what) report_error(parser->ctx, _code, "Expecting parenthesis for %S", STR(_for_what));
#define report_syntactic_unknown_op(_code) report_error(parser->ctx, _code, "Unknown operation: {line}");
#define report_expr_invalid_binary_operation(_code, _op_str) report_error(parser->ctx, _code, "Invalid binary operation: '%S'", _op_str);
#define report_expr_syntactic_unknown(_code, _expr_str) report_error(parser->ctx, _code, "Unknown expresion: '%S'", _expr_str);
#define report_expr_is_empty(_code) report_error(parser->ctx, _code, "Empty expresion: {line}");
#define report_expr_empty_member(_code) report_error(parser->ctx, _code, "Member is not specified");
#define report_array_indexing_expects_expresion(_code) report_error(parser->ctx, _code, "Array indexing expects an expresion");
#define report_objdef_expecting_type_identifier(_code) report_error(parser->ctx, _code, "Expecting type identifier in object definition");
#define report_objdef_expecting_assignment(_code) report_error(parser->ctx, _code, "Expecting an assignment for object definition");
#define report_objdef_redundant_array_dimensions(_code) report_error(parser->ctx, _code, "Redundant array dimensions definition before assignment");
#define report_else_not_found_if(_code) report_error(parser->ctx, _code, "'else' is not valid without the corresponding 'if'");
#define report_for_unknown(_code) report_error(parser->ctx, _code, "Unknown for-statement: {line}");
#define report_foreach_expecting_identifier(_code) report_error(parser->ctx, _code, "Expecting an identifier for the itarator");
#define report_foreach_expecting_expresion(_code) report_error(parser->ctx, _code, "Expecting an array expresion");
#define report_foreach_expecting_colon(_code) report_error(parser->ctx, _code, "Expecting a colon separating identifiers and array expresion");
#define report_foreach_expecting_comma_separated_identifiers(_code) report_error(parser->ctx, _code, "Foreach-Statement expects comma separated identifiers");
#define report_assign_operator_not_found(_code) report_error(parser->ctx, _code, "Operator not found for an assignment");
#define report_enumdef_expecting_comma_separated_identifier(_code) report_error(parser->ctx, _code, "Expecting comma separated identifier for enum values");

//- SEMANTIC REPORTS

#define report_zero_division(_code) report_error(inter->ctx, _code, "Divided by zero");
#define report_right_path_cant_be_absolute(_code) report_error(inter->ctx, _code, "Right path can't be absolute");
#define report_nested_definition(_code) report_error(inter->ctx, _code, "This definition can't be nested");
#define report_type_missmatch_append(_code, _v0, _v1) report_error(inter->ctx, _code, "Type missmatch, can't append a '%S' into '%S'", _v0, _v1);
#define report_type_missmatch_array_expr(_code, _v0, _v1) report_error(inter->ctx, _code, "Type missmatch in array expresion, expecting '%S' but found '%S'", _v0, _v1);
#define report_type_missmatch_assign(_code, _v0, _v1) report_error(inter->ctx, _code, "Type missmatch, can't assign '%S' to '%S'", _v0, _v1);
#define report_type_undefined(_code, _t) report_error(inter->ctx, _code, "Undefined type '%S'", _t);
#define report_invalid_binary_op(_code, _v0, _op, _v1) report_error(inter->ctx, _code, "Invalid binary operation: '%S' %S '%S'", _v0, _op, _v1);
#define report_invalid_signed_op(_code, _op, _v) report_error(inter->ctx, _code, "Invalid signed operation '%S %S'", _op, _v);
#define report_symbol_not_found(_code, _v) report_error(inter->ctx, _code, "Symbol '%S' not found", _v);
#define report_object_not_found(_code, _v) report_error(inter->ctx, _code, "Object '%S' not found", _v);
#define report_object_type_not_found(_code, _t) report_error(inter->ctx, _code, "Object Type '%S' not found", _t);
#define report_object_duplicated(_code, _v) report_error(inter->ctx, _code, "Duplicated object '%S'", _v);
#define report_member_not_found_in_object(_code, _v, _t) report_error(inter->ctx, _code, "Member '%S' not found in a '%S'", _v, _t);
#define report_member_not_found_in_type(_code, _v, _t) report_error(inter->ctx, _code, "Member '%S' not found in '%S'", _v, _t);
#define report_member_invalid_symbol(_code, _v) report_error(inter->ctx, _code, "'%S' does not have members to access", _v);
#define report_enum_value_expects_an_int(_code) report_error(inter->ctx, _code, "Enum value expects an Int");
#define report_function_not_found(_code, _v) report_error(inter->ctx, _code, "Function '%S' not found", _v);
#define report_function_expecting_parameters(_code, _v, _c) report_error(inter->ctx, _code, "Function '%S' is expecting %u parameters", _v, _c);
#define report_function_wrong_parameter_type(_code, _f, _t, _c) report_error(inter->ctx, _code, "Function '%S' is expecting a '%S' as a parameter %u", _f, _t, _c);
#define report_indexing_expects_an_int(_code) report_error(inter->ctx, _code, "Indexing expects an Int");
#define report_indexing_not_allowed(_code, _t) report_error(inter->ctx, _code, "Indexing not allowed for a '%S'", _t);
#define report_indexing_out_of_bounds(_code) report_error(inter->ctx, _code, "Index out of bounds");
#define report_dimensions_expects_an_int(_code) report_error(inter->ctx, _code, "Expecting an integer for the dimensions of the array");
#define report_dimensions_must_be_positive(_code) report_error(inter->ctx, _code, "Expecting a positive integer for the dimensions of the array");
#define report_expr_expects_bool(_code, _what)  report_error(inter->ctx, _code, "%S expects a Bool", STR(_what));
#define report_expr_semantic_unknown(_code)  report_error(inter->ctx, _code, "Unknown expresion: {line}");
#define report_for_expects_an_array(_code)  report_error(inter->ctx, _code, "Foreach-Statement expects an array");
#define report_semantic_unknown_op(_code) report_error(inter->ctx, _code, "Unknown operation: {line}");

//- LANG REPORTS

#define report_stack_is_broken() report_error(inter->ctx, {}, "The stack is broken");

//- LEXER

struct Lexer {
    Yov* ctx;
    
    PooledArray<Token> tokens;
    String text;
    i32 script_id;
    
    u64 cursor;
    u64 code_start_line_offset;
    u32 code_column;
    u32 code_line;
};

Array<Token> generate_tokens(Yov* ctx, String text, b32 discard_tokens);

//- AST 

enum OpKind {
    OpKind_None,
    OpKind_Error,
    OpKind_Block,
    OpKind_IfStatement,
    OpKind_WhileStatement,
    OpKind_ForStatement,
    OpKind_ForeachArrayStatement,
    OpKind_VariableAssignment,
    OpKind_VariableDefinition,
    OpKind_FunctionCall,
    OpKind_ArrayExpresion,
    OpKind_ArrayElementValue,
    OpKind_ArrayElementAssignment,
    OpKind_Binary,
    OpKind_Sign,
    OpKind_IntLiteral,
    OpKind_StringLiteral,
    OpKind_BoolLiteral,
    OpKind_IdentifierValue,
    OpKind_MemberValue,
    OpKind_EnumDefinition,
};

struct OpNode {
    OpKind kind;
    CodeLocation code;
};

struct OpNode_Block : OpNode {
    Array<OpNode*> ops;
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

struct OpNode_Assignment : OpNode {
    String identifier;
    OpNode* value;
    BinaryOperator binary_operator;
};

struct OpNode_VariableDefinition : OpNode {
    String type;
    String identifier;
    OpNode* assignment;
    Array<OpNode*> array_dimensions;
    b8 is_array;
};

struct OpNode_FunctionCall : OpNode {
    String identifier;
    Array<OpNode*> parameters;
};

struct OpNode_ArrayExpresion : OpNode {
    Array<OpNode*> nodes;
};

struct OpNode_ArrayElementValue : OpNode {
    String identifier;
    OpNode* expresion;
};

struct OpNode_ArrayElementAssignment : OpNode {
    String identifier;
    OpNode* value;
    OpNode* indexing_expresion;
    BinaryOperator binary_operator;
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

struct OpNode_Literal : OpNode {
    union {
        u32 int_literal;
        b8 bool_literal;
        String string_literal;
    };
};

struct OpNode_IdentifierValue : OpNode {
    String identifier;
};

struct OpNode_MemberValue : OpNode {
    String identifier;
    String member;
};

struct OpNode_EnumDefinition : OpNode {
    String identifier;
    Array<String> names;
    Array<OpNode*> values;
};

u32 get_node_size(OpKind kind);
Array<OpNode*> get_node_childs(Arena* arena, OpNode* node);

struct Parser {
    Yov* ctx;
    Array<Token> tokens;
    u32 token_index;
    PooledArray<OpNode*> ops;
};

b32 check_tokens_are_couple(Array<Token> tokens, u32 open_index, u32 close_index, TokenKind open_token, TokenKind close_token);

Token peek_token(Parser* parser, i32 offset = 0);
Array<Token> peek_tokens(Parser* parser, i32 offset, u32 count);
void skip_tokens(Parser* parser, u32 count);
void skip_tokens_before_op(Parser* parser);
Token extract_token(Parser* parser);
Array<Token> extract_tokens(Parser* parser, u32 count);
Array<Token> extract_tokens_until(Parser* parser, b32 require_separator, TokenKind sep0, TokenKind sep1 = TokenKind_None);
Array<Token> extract_tokens_with_depth(Parser* parser, TokenKind open_token, TokenKind close_token, b32 require_separator);

OpNode* extract_op(Parser* parser);

OpNode* generate_ast_from_block(Yov* ctx, Array<Token> tokens);
OpNode* generate_ast_from_sentence(Yov* ctx, Array<Token> tokens);
OpNode* generate_ast(Yov* ctx, Array<Token> tokens);

OpNode* process_function_call(Parser* parser, Array<Token> tokens);
OpNode* process_expresion(Parser* parser, Array<Token> tokens);

//- INTERPRETER 

struct ObjectMemory_Int {
    i64 value;
};
struct ObjectMemory_Bool {
    b32 value;
};
struct ObjectMemory_String {
    char* data;
    u64 size;
};
struct ObjectMemory_Array {
    void* data;
    i64 count;
};
struct ObjectMemory_Enum {
    i64 index;
};

struct ObjectMemory {
    union {
        ObjectMemory_Int integer;
        ObjectMemory_Enum enum_;
        ObjectMemory_Bool boolean;
        ObjectMemory_String string;
        ObjectMemory_Array array;
    };
};

struct Object {
    String identifier;
    i32 vtype;
    i32 scope;
    
    union {
        ObjectMemory_Int integer;
        ObjectMemory_Enum enum_;
        ObjectMemory_Bool boolean;
        ObjectMemory_String string;
        ObjectMemory_Array array;
    };
};

#define VType_Unknown -1
#define VType_Void 0
#define VType_Int 1
#define VType_Bool 2
#define VType_String 3
#define VType_Enum_CopyMode 4

#define VType_IntArray vtype_from_array_dimension(inter, VType_Int, 1)
#define VType_BoolArray vtype_from_array_dimension(inter, VType_Bool, 1)
#define VType_StringArray vtype_from_array_dimension(inter, VType_String, 1)

enum VariableKind {
    VariableKind_Unknown,
    VariableKind_Void,
    VariableKind_Primitive,
    VariableKind_Array,
    VariableKind_Enum,
};

struct VariableType {
    i32 vtype;
    String name;
    VariableKind kind;
    
    i32 array_of;
    
    Array<String> enum_names;
    Array<i64> enum_values;
};

struct IntrinsicFunctionResult {
    Object* return_obj;
    Result error_result;
};

struct Interpreter;
typedef IntrinsicFunctionResult IntrinsicFunction(Interpreter* inter, Array<Object*> objs, CodeLocation code);

struct FunctionDefinition {
    String identifier;
    i32 return_vtype;
    Array<i32> parameter_vtypes;
    
    IntrinsicFunction* intrinsic_fn;
};

enum SymbolType {
    SymbolType_None,
    SymbolType_Object,
    SymbolType_Function,
    SymbolType_Type,
};

struct Symbol {
    SymbolType type;
    String identifier;
    
    Object* object;
    FunctionDefinition* function;
    i32 vtype;
};

struct InterpreterSettings {
    b8 execute; // Execute AST until an error ocurrs otherwise do semantic analysis of all the AST
    b8 user_assertion; // Forces user assertion of every operation to the OS
    b8 print_execution;
    
    // TODO(Jose): b8 ignore_errors; // Ignore errors on execution and keep running
};

void interpret(Yov* ctx, OpNode* block, InterpreterSettings settings);

struct Interpreter {
    Yov* ctx;
    InterpreterSettings settings;
    
    OpNode* root;
    
    PooledArray<VariableType> vtype_table;
    Array<FunctionDefinition> functions;
    Object* nil_obj;
    Object* void_obj;
    
    PooledArray<Object> objects;
    i32 scope;
    
    Object* cd_obj;
};

void interpreter_exit(Interpreter* inter);
void interpreter_report_runtime_error(Interpreter* inter, CodeLocation code, String resolved_line, String message_error);
Result user_assertion(Interpreter* inter, String message);

VariableType vtype_get(Interpreter* inter, i32 vtype);
i32 vtype_from_name(Interpreter* inter, String name);
i32 vtype_from_array_dimension(Interpreter* inter, i32 vtype, u32 dimension);
i32 vtype_from_array_element(Interpreter* inter, i32 vtype);
i32 vtype_from_array_base(Interpreter* inter, i32 vtype);
u32 vtype_get_stride(Interpreter* inter, i32 vtype);
i32 encode_vtype(u32 index, u32 dimensions);
void decode_vtype(i32 vtype, u32* _index, u32* _dimensions);

Object* obj_alloc_temp(Interpreter* inter, i32 vtype);

Object* obj_alloc_temp_int(Interpreter* inter, i64 value);
Object* obj_alloc_temp_bool(Interpreter* inter, b32 value);
Object* obj_alloc_temp_string(Interpreter* inter, String value);
Object* obj_alloc_temp_array(Interpreter* inter, i32 element_vtype, i64 count);
Object* obj_alloc_temp_array_multidimensional(Interpreter* inter, i32 element_vtype, Array<i64> dimensions);
Object* obj_alloc_temp_enum(Interpreter* inter, i32 vtype, i64 index);
Object* obj_alloc_temp_array_from_enum(Interpreter* inter, i32 enum_vtype);

ObjectMemory* obj_get_data(Object* obj);
ObjectMemory obj_copy_data(Interpreter* inter, Object* obj);
ObjectMemory obj_copy_data(Interpreter* inter, ObjectMemory src, i32 vtype);

void obj_copy_element_from_element(Interpreter* inter, Object* dst_array, i64 dst_index, Object* src_array, i64 src_index);
b32 obj_copy_element_from_object(Interpreter* inter, Object* dst_array, i64 dst_index, Object* src);
void obj_copy_from_element(Interpreter* inter, Object* dst, Object* src_array, i64 src_index);
b32 obj_copy(Interpreter* inter, Object* dst, Object* src);
void obj_set_int(Object* dst, i64 value);
void obj_set_bool(Object* dst, b32 value);
void obj_set_string(Interpreter* inter, Object* dst, String value);

String string_from_obj(Arena* arena, Interpreter* inter, Object* obj, b32 raw = true);
String string_from_vtype(Arena* arena, Interpreter* inter, i32 vtype);

i32 push_scope(Interpreter* inter);
void pop_scope(Interpreter* inter);
Symbol find_symbol(Interpreter* inter, String identifier);
Object* find_object(Interpreter* inter, String identifier, b32 parent_scopes);
Object* define_object(Interpreter* inter, String identifier, i32 vtype);
void undefine_object(Interpreter* inter, Object* obj);
FunctionDefinition* find_function(Interpreter* inter, String identifier);
Object* call_function(Interpreter* inter, FunctionDefinition* fn, Array<Object*> parameters, CodeLocation code, b32 is_expresion);

Object* member_from_object(Interpreter* inter, Object* obj, String member);
Object* member_from_type(Interpreter* inter, i32 vtype, String member);

struct ExpresionContext {
    i32 expected_vtype;
};

inline_fn ExpresionContext expresion_context_make(i32 expected_vtype) {
    ExpresionContext d{};
    d.expected_vtype = expected_vtype;
    return d;
}

Object* interpret_expresion(Interpreter* inter, OpNode* node, ExpresionContext context);
Object* interpret_function_call(Interpreter* inter, OpNode* node0, b32 is_expresion);
void interpret_op(Interpreter* inter, OpNode* parent, OpNode* node);

String solve_string_literal(Arena* arena, Interpreter* inter, String src, CodeLocation code);
String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path);
b32 interpretion_failed(Interpreter* inter);

inline_fn b32 is_valid(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype > 0;
}
inline_fn b32 is_unknown(const Object* obj) {
    if (obj == NULL) return true;
    return obj->vtype < 0;
}

inline_fn b32 is_void(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype == VType_Void;
}
inline_fn b32 is_int(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype == VType_Int;
}
inline_fn b32 is_bool(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype == VType_Bool;
}
inline_fn b32 is_string(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype == VType_String;
}
inline_fn b32 is_array(i32 vtype) {
    u32 dims;
    decode_vtype(vtype, NULL, &dims);
    return dims > 0;
}
inline_fn b32 is_array(Object* obj) {
    if (obj == NULL) return false;
    return is_array(obj->vtype);
}
inline_fn b32 is_enum(Interpreter* inter, i32 vtype) {
    return vtype_get(inter, vtype).kind == VariableKind_Enum;
}

inline_fn i64 get_int(Object* obj) {
    assert(is_int(obj));
    return obj->integer.value;
}
inline_fn b32 get_bool(Object* obj) {
    assert(is_bool(obj));
    return obj->boolean.value;
}
inline_fn String get_string(Object* obj) {
    assert(is_string(obj));
    String str;
    str.data = obj->string.data;
    str.size = obj->string.size;
    return str;
}

enum CopyMode {
    CopyMode_NoOverride,
    CopyMode_Override,
};
inline_fn CopyMode get_enum_CopyMode(Interpreter* inter, Object* obj) {
    assert(is_enum(inter, obj->vtype));
    return (CopyMode)obj->enum_.index;
}

template<typename T>
inline_fn T& get_array_element(Object* obj) {
    
    String* v = (String*)obj->data;
    return *v;
}