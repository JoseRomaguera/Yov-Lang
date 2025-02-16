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
#define YOV_MINOR_VERSION 0
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

b32 os_read_entire_file(Arena* arena, String path, RawBuffer* result);
b32 os_copy_file(String dst_path, String src_path, b32 override);

b32 os_folder_create(String path, b32 recursive);

b32 os_ask_yesno(String title, String content);
i32 os_call(String working_dir, String command);

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
String string_join(Arena* arena, LinkedList<String> ll);
Array<String> string_split(Arena* arena, String str, String separator);
String string_replace(Arena* arena, String str, String old_str, String new_str);
String string_format_with_args(Arena* arena, String string, va_list args);
String string_format_ex(Arena* arena, String string, ...);

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

enum KeywordType {
    KeywordType_None,
    KeywordType_If,
    KeywordType_Else,
    KeywordType_While,
    KeywordType_For,
};

String string_from_keyword(KeywordType keyword);

//- PROGRAM CONTEXT 

#include "templates.h"

void print_ex(Severity severity, String str, ...);
#define print(severity, str, ...) print_ex(severity, STR(str), __VA_ARGS__)
#define print_info(str, ...) print_ex(Severity_Info, STR(str), __VA_ARGS__)
#define print_warning(str, ...) print_ex(Severity_Warning, STR(str), __VA_ARGS__)
#define print_error(str, ...) print_ex(Severity_Error, STR(str), __VA_ARGS__)

#define print_separator() print_ex(Severity_Info, STR("=========================\n"))

struct CodeLocation {
    u64 offset;
    u32 line;
    u32 column;
};

inline_fn CodeLocation code_location_make(u64 offset, u32 line, u32 column) { return { offset, line, column }; }

struct Report {
    String text;
    CodeLocation code;
    Severity severity;
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
    
    String caller_dir;
    
    Array<ProgramArg> args;
    
    PooledArray<Report> reports;
    i32 error_count;
};

Yov* yov_initialize(Arena* arena, String script_path);
void yov_shutdown(Yov* ctx);

b32 generate_program_args(Yov* ctx, Array<String> raw_args);

void report_ex(Yov* ctx, Severity severity, CodeLocation code, String text, ...);

#define report_info(ctx, code, text, ...) report_ex(ctx, Severity_Info, code, STR(text), __VA_ARGS__);
#define report_warning(ctx, code, text, ...) report_ex(ctx, Severity_Warning, code, STR(text), __VA_ARGS__);
#define report_error(ctx, code, text, ...) report_ex(ctx, Severity_Error, code, STR(text), __VA_ARGS__);

void print_report(Report report);
void print_reports(Array<Report> reports);

//- LEXER

enum TokenKind {
    TokenKind_Unknown,
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
    
    TokenKind_Keyword,
    TokenKind_BoolLiteral,
};

struct Token {
    TokenKind kind;
    String value;
    CodeLocation code;
    
    union {
        KeywordType keyword;
        BinaryOperator binary_operator;
    };
};

struct Lexer {
    Yov* ctx;
    
    PooledArray<Token> tokens;
    String text;
    
    u64 cursor;
    u32 code_column;
    u32 code_line;
    
    b8 discard_tokens;
};

Array<Token> generate_tokens(Yov* ctx, String text, b32 discard_tokens);

//- AST 

enum OpKind {
    OpKind_None,
    OpKind_Unknown,
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

struct Parser {
    Yov* ctx;
    Array<Token> tokens;
    u32 token_index;
    PooledArray<OpNode*> ops;
};

Array<Token> extract_sentence(Parser* parser, TokenKind separator, b32 require_separator);
void skip_tokens_before_op(Parser* parser);
OpNode* process_op(Parser* parser);

OpNode* generate_ast_from_block(Yov* ctx, Array<Token> tokens);
OpNode* generate_ast_from_sentence(Yov* ctx, Array<Token> tokens);
OpNode* generate_ast(Yov* ctx, Array<Token> tokens);

OpNode* process_expresion(Parser* parser, Array<Token> tokens);

//- INTERPRETER 

#define VType_Unknown -1
#define VType_Void 0
#define VType_Int 1
#define VType_Bool 2
#define VType_String 3

#define VType_IntArray vtype_from_array_dimension(inter, VType_Int, 1)
#define VType_BoolArray vtype_from_array_dimension(inter, VType_Bool, 1)
#define VType_StringArray vtype_from_array_dimension(inter, VType_String, 1)

enum VariableKind {
    VariableKind_Unknown,
    VariableKind_Void,
    VariableKind_Primitive,
    VariableKind_Array,
};

struct VariableType {
    i32 vtype;
    String name;
    VariableKind kind;
    u32 size;
    
    i32 array_of;
};

// Int: {Object(Memory)}
// IntArray: {Object} [Memory, Memory]
// Int: {Object}

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

struct ObjectMemory {
    union {
        ObjectMemory_Int integer;
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
        ObjectMemory_Bool boolean;
        ObjectMemory_String string;
        ObjectMemory_Array array;
    };
};

struct Interpreter;
typedef Object* IntrinsicFunction(Interpreter* inter, OpNode* node, Array<Object*> objs);

struct FunctionDefinition {
    String identifier;
    i32 return_vtype;
    Array<i32> parameter_vtypes;
    
    IntrinsicFunction* intrinsic_fn;
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
    
    Array<VariableType> vtype_table;
    Array<FunctionDefinition> functions;
    Object* nil_obj;
    Object* void_obj;
    
    PooledArray<Object> objects;
    i32 scope;
    
    Object* cd_obj;
};

void interpreter_exit(Interpreter* inter);

VariableType vtype_get(Interpreter* inter, i32 vtype);
i32 vtype_from_name(Interpreter* inter, String name);
i32 vtype_from_array_dimension(Interpreter* inter, i32 vtype, u32 dimension);
i32 vtype_from_array_element(Interpreter* inter, i32 vtype);
i32 vtype_from_array_base(Interpreter* inter, i32 vtype);
u32 vtype_get_stride(i32 vtype);
i32 encode_vtype(u32 index, u32 dimensions);
void decode_vtype(i32 vtype, u32* _index, u32* _dimensions);

Object* obj_alloc_temp(Interpreter* inter, i32 vtype);

Object* obj_alloc_temp_int(Interpreter* inter, i64 value);
Object* obj_alloc_temp_bool(Interpreter* inter, b32 value);
Object* obj_alloc_temp_string(Interpreter* inter, String value);
Object* obj_alloc_temp_array(Interpreter* inter, i32 element_vtype, i64 count);
Object* obj_alloc_temp_array_multidimensional(Interpreter* inter, i32 element_vtype, Array<i64> dimensions);

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

//b32 var_assignment_is_valid(const ObjectMemory t0, const ObjectMemory t1);
//b32 var_assignment_is_valid(const ObjectMemory t0, i32 vtype);
String string_from_obj(Arena* arena, Interpreter* inter, Object* obj);
String string_from_vtype(Arena* arena, Interpreter* inter, i32 vtype);

i32 push_scope(Interpreter* inter);
void pop_scope(Interpreter* inter);
Object* find_object(Interpreter* inter, String identifier, b32 parent_scopes);
Object* define_object(Interpreter* inter, String identifier, i32 vtype);
void undefine_object(Interpreter* inter, Object* obj);
FunctionDefinition* find_function(Interpreter* inter, String identifier);

Object* interpret_function_call(Interpreter* inter, OpNode* node);
void interpret_op(Interpreter* inter, OpNode* node);

String solve_string_literal(Arena* arena, Interpreter* inter, String src, CodeLocation code);
String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path);
b32 user_assertion(Interpreter* inter, String message);
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

template<typename T>
inline_fn T& get_array_element(Object* obj) {
    
    String* v = (String*)obj->data;
    return *v;
}