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

#define assert(x) do { if ((x) == 0) assertion_failed(#x); } while (0)
#define inline_fn inline
#define internal_fn static
#define array_count(x) (sizeof(x) / sizeof(x[0]))
#define foreach(it, count) for (u32 (it) = 0; (it) < (count); (it)++)

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

struct OS {
    u32 page_size;
};

extern OS os;

void os_initialize();
void os_shutdown();

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

b32 os_read_entire_file(Arena* arena, String path, RawBuffer* result);
b32 os_copy_file(String dst_path, String src_path, b32 override);

b32 os_ask_yesno(String title, String content);
i32 os_call(String working_dir, String command);

String os_get_working_path(Arena* arena);

b32 os_path_is_absolute(String path);
b32 os_path_is_directory(String path);

#include "utils.h"

Array<String> os_get_args(Arena* arena);

void os_thread_sleep(u64 millis);
void os_console_wait();

//- PATH 

inline_fn Array<String> path_subdivide(Arena* arena, String path)
{
    SCRATCH(arena);
    PooledArray<String> list = pooled_array_make<String>(scratch.arena, 32);
    
    u64 last_element = 0;
    u64 cursor = 0;
    while (cursor < path.size)
    {
        if (path[cursor] == '/') {
            String element = string_substring(path, last_element, cursor - last_element);
            if (element.size > 0) array_add(&list, element);
            last_element = cursor + 1;
        }
        
        cursor++;
    }
    
    String element = string_substring(path, last_element, cursor - last_element);
    if (element.size > 0) array_add(&list, element);
    
    return array_from_pooled_array(arena, list);
}

inline_fn String path_resolve(Arena* arena, String path)
{
    SCRATCH(arena);
    String res = path;
    res = string_replace(scratch.arena, res, STR("\\"), STR("/"));
    
    Array<String> elements = path_subdivide(scratch.arena, res);
    
    {
        i32 remove_prev_element_count = 0;
        
        for (i32 i = (i32)elements.count - 1; i >= 0; --i) {
            if (string_equals(elements[i], STR(".."))) {
                array_erase(&elements, i);
                remove_prev_element_count++;
            }
            else if (string_equals(elements[i], STR(".")) || remove_prev_element_count) {
                array_erase(&elements, i);
                if (remove_prev_element_count) remove_prev_element_count--;
            }
        }
    }
    
    StringBuilder builder = string_builder_make(scratch.arena);
    
    foreach(i, elements.count) {
        String element = elements[i];
        append(&builder, element);
        if (i < elements.count - 1) append_char(&builder, '/');
    }
    
    res = string_from_builder(scratch.arena, &builder);
    if (os_path_is_directory(res)) res = string_format(scratch.arena, "%S/", res);
    
    return string_copy(arena, res);
}

inline_fn String path_append(Arena* arena, String str0, String str1)
{
    if (os_path_is_absolute(str1)) return string_copy(arena, str0);
    if (str0.size == 0) return string_copy(arena, str1);
    if (str1.size == 0) return string_copy(arena, str0);
    
    if (str0[str0.size - 1] != '/') return string_format(arena, "%S/%S", str0, str1);
    return string_format(arena, "%S%S", str0, str1);
}

inline_fn String path_get_last_element(String path)
{
    // TODO(Jose): Optimize
    SCRATCH();
    Array<String> array = path_subdivide(scratch.arena, path);
    if (array.count == 0) return {};
    return array[array.count - 1];
}

//- PROGRAM CONTEXT 

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

struct ProgramSettings {
};

struct ProgramContext {
    Arena* static_arena;
    Arena* temp_arena;
    
    ProgramSettings settings;
    
    String script_name;
    String script_dir;
    String script_path;
    
    String caller_dir;
    
    Array<ProgramArg> args;
    
    PooledArray<Report> reports;
    i32 error_count;
};

ProgramContext* program_context_initialize(Arena* arena, ProgramSettings settings, String script_path);
void program_context_shutdown(ProgramContext* ctx);

b32 generate_program_args(ProgramContext* ctx, Array<String> raw_args);

void report_ex(ProgramContext* ctx, Severity severity, CodeLocation code, String text, ...);

#define report_info(ctx, code, text, ...) report_ex(ctx, Severity_Info, code, STR(text), __VA_ARGS__);
#define report_warning(ctx, code, text, ...) report_ex(ctx, Severity_Warning, code, STR(text), __VA_ARGS__);
#define report_error(ctx, code, text, ...) report_ex(ctx, Severity_Error, code, STR(text), __VA_ARGS__);

void print_report(Report report);
void print_reports(Array<Report> reports);

//- MISC

enum BinaryOperator {
    BinaryOperator_Unknown,
    BinaryOperator_None,
    
    BinaryOperator_Addition,
    BinaryOperator_Substraction,
    BinaryOperator_Multiplication,
    BinaryOperator_Division,
    
    BinaryOperator_Equals,
    BinaryOperator_NotEquals,
    BinaryOperator_LessThan,
    BinaryOperator_LessEqualsThan,
    BinaryOperator_GreaterThan,
    BinaryOperator_GreaterEqualsThan,
};

inline_fn String string_from_binary_operator(BinaryOperator op) {
    if (op == BinaryOperator_Addition) return STR("+");
    if (op == BinaryOperator_Substraction) return STR("-");
    if (op == BinaryOperator_Multiplication) return STR("*");
    if (op == BinaryOperator_Division) return STR("/");
    if (op == BinaryOperator_Equals) return STR("==");
    if (op == BinaryOperator_NotEquals) return STR("!=");
    if (op == BinaryOperator_LessThan) return STR("<");
    if (op == BinaryOperator_LessEqualsThan) return STR("<=");
    if (op == BinaryOperator_GreaterThan) return STR(">");
    if (op == BinaryOperator_GreaterEqualsThan) return STR(">=");
    return STR("?");
}

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

enum KeywordType {
    KeywordType_None,
    KeywordType_If,
    KeywordType_Else,
    KeywordType_While,
    KeywordType_For,
};

String string_from_keyword(KeywordType keyword);

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
    ProgramContext* ctx;
    
    PooledArray<Token> tokens;
    String text;
    
    u64 cursor;
    u32 code_column;
    u32 code_line;
    
    b8 discard_tokens;
};

Array<Token> generate_tokens(ProgramContext* ctx, String text, b32 discard_tokens);

//- AST 

enum OpKind {
    OpKind_None,
    OpKind_Unknown,
    OpKind_Block,
    OpKind_IfStatement,
    OpKind_WhileStatement,
    OpKind_ForStatement,
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

struct OpNode_Assignment : OpNode {
    String identifier;
    OpNode* value;
    BinaryOperator binary_operator;
};

struct OpNode_VariableDefinition : OpNode {
    String type;
    String identifier;
    OpNode* assignment;
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
    b8 negative;
    OpNode* value;
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
    ProgramContext* ctx;
    Array<Token> tokens;
    u32 token_index;
    PooledArray<OpNode*> ops;
};

Array<Token> extract_sentence(Parser* parser, TokenKind separator, b32 require_separator);
void skip_tokens_before_op(Parser* parser);
OpNode* process_op(Parser* parser);

OpNode* generate_ast_from_block(ProgramContext* ctx, Array<Token> tokens);
OpNode* generate_ast_from_sentence(ProgramContext* ctx, Array<Token> tokens);
OpNode* generate_ast(ProgramContext* ctx, Array<Token> tokens);

OpNode* process_expresion(Parser* parser, Array<Token> tokens);

//- INTERPRETER 

#define VType_Unknown -1
#define VType_Void 0
#define VType_Int 1
#define VType_IntArray 2
#define VType_Bool 3
#define VType_BoolArray 4
#define VType_String 5
#define VType_StringArray 6

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

struct Variable {
    i32 vtype;
    void* data;
    u32 count;
};

struct Object {
    String identifier;
    Variable* var;
    i32 scope;
};

struct Interpreter;
typedef Variable* IntrinsicFunction(Interpreter* inter, OpNode* node, Array<Variable*> vars);

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

void interpret(ProgramContext* ctx, OpNode* block, InterpreterSettings settings);

struct Interpreter {
    ProgramContext* ctx;
    InterpreterSettings settings;
    
    Array<VariableType> vtype_table;
    Array<FunctionDefinition> functions;
    Variable* nil_var;
    Variable* void_var;
    
    PooledArray<Object> objects;
    i32 scope;
    
    Object* cd_obj;
};

VariableType vtype_get(Interpreter* inter, i32 vtype);
i32 vtype_from_array_element(Interpreter* inter, i32 vtype);

Variable* var_alloc_generic(Interpreter* inter, i32 vtype);

Variable* var_alloc_primitive(Interpreter* inter, i32 vtype);
Variable* var_alloc_array(Interpreter* inter, i32 vtype, u32 length);
Variable* var_copy(Interpreter* inter, const Variable* src);

Variable* var_alloc_int(Interpreter* inter, i64 v);
Variable* var_alloc_bool(Interpreter* inter, b8 v);
Variable* var_alloc_string(Interpreter* inter, String v);

Variable* var_alloc_from_array(Interpreter* inter, Variable* array, i64 index);
void var_assign_array_element(Interpreter* inter, Variable* array, i64 index, Variable* src);

b32 var_assignment_is_valid(const Variable* t0, const Variable* t1);
b32 var_assignment_is_valid(const Variable* t0, i32 vtype);
String string_from_var(Arena* arena, Interpreter* inter, Variable* var);
String string_from_vtype(Interpreter* inter, i32 vtype);

b32 obj_assign(Interpreter* inter, Object* obj, const Variable* src);

i32 push_scope(Interpreter* inter);
void pop_scope(Interpreter* inter);
Object* find_object(Interpreter* inter, String identifier, b32 parent_scopes);
Object* define_object(Interpreter* inter, String identifier, Variable* var);
void undefine_object(Interpreter* inter, Object* obj);
FunctionDefinition* find_function(Interpreter* inter, String identifier);

Variable* interpret_function_call(Interpreter* inter, OpNode* node);
void interpret_op(Interpreter* inter, OpNode* node);

String solve_string_literal(Arena* arena, Interpreter* inter, String src, CodeLocation code);
String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path);
b32 user_assertion(Interpreter* inter, String message);
b32 interpretion_failed(Interpreter* inter);

inline_fn b32 is_valid(const Variable* var) {
    if (var == NULL) return false;
    return var->vtype > 0;
}
inline_fn b32 is_unknown(const Variable* var) {
    if (var == NULL) return true;
    return var->vtype < 0;
}

inline_fn b32 is_int(const Variable* var) {
    if (var == NULL) return false;
    return var->vtype == VType_Int;
}
inline_fn b32 is_bool(const Variable* var) {
    if (var == NULL) return false;
    return var->vtype == VType_Bool;
}
inline_fn b32 is_string(const Variable* var) {
    if (var == NULL) return false;
    return var->vtype == VType_String;
}

inline_fn i64& get_int(Variable* var) {
    assert(is_int(var));
    i64* v = (i64*)var->data;
    return *v;
}
inline_fn b8& get_bool(Variable* var) {
    assert(is_bool(var));
    b8* v = (b8*)var->data;
    return *v;
}
inline_fn String& get_string(Variable* var) {
    assert(is_string(var));
    String* v = (String*)var->data;
    return *v;
}

template<typename T>
inline_fn T& get_array_element(Variable* var) {
    
    String* v = (String*)var->data;
    return *v;
}