#pragma once

#include "program.h"

// PARSER / TOKENIZER

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

String StringFromTokens(Arena* arena, Array<Token> tokens);
String DebugInfoFromToken(Arena* arena, Token token);

BinaryOperator binary_operator_from_token(TokenKind token);
B32 token_is_sign_or_binary_op(TokenKind token);
B32 TokenIsFlowModifier(TokenKind token);
TokenKind TokenKindFromOpenScope(TokenKind open_token);

Location LocationFromTokens(Array<Token> tokens);

Token read_token(String text, U64 cursor, I32 script_id);
Token read_valid_token(String text, U64 cursor, U64 end_cursor, I32 script_id);
B32 TokenIsValid(TokenKind token);

B32 CheckTokensAreCouple(Array<Token> tokens, U32 open_index, U32 close_index, TokenKind open_token, TokenKind close_token);

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

struct YovScript {
    I32 id;
    String path;
    String name;
    String dir;
    String text;
    Array<U64> lines;
};

struct Parser {
    YovScript* script;
    I32 script_id;
    String text;
    RangeU64 range;
    U64 cursor;
    // TODO(Jose): Cache tokens
};

Parser* ParserAlloc(YovScript* script, RangeU64 range);
Parser* ParserSub(Parser* parser, Location location);
Location LocationFromParser(Parser* parser, U64 end = U64_MAX);

Token PeekToken(Parser* parser, I64 cursor_offset = 0);
void  SkipToken(Parser* parser, Token token);
void  AssumeToken(Parser* parser, TokenKind kind);
void  MoveCursor(Parser* parser, U64 cursor);
Token ConsumeToken(Parser* parser);
Array<Token> ConsumeAllTokens(Parser* parser);
void SkipInvalidTokens(Parser* parser);

Location FindUntil(Parser* parser, B32 include_match, TokenKind match0, TokenKind match1 = TokenKind_None);
U64 find_token_with_depth_check(Parser* parser, B32 parenthesis, B32 braces, B32 brackets, TokenKind match0, TokenKind match1 = TokenKind_None);
Location FindScope(Parser* parser, TokenKind open_token, B32 include_delimiters);
Location FindCode(Parser* parser);

Location FetchUntil(Parser* parser, B32 include_match, TokenKind match0, TokenKind match1 = TokenKind_None);
Location FetchScope(Parser* parser, TokenKind open_token, B32 include_delimiters);
Location FetchCode(Parser* parser);

SentenceKind GuessSentenceKind(Parser* parser);

// IR GENERATON

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

struct IR_Group {
    B32 success;
    Value value;
    U32 unit_count;
    IR_Unit* first;
    IR_Unit* last;
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
    
    Program* program;
    Reporter* reporter;
    
    PooledArray<Register> local_registers;
    PooledArray<IR_Object> objects;
    PooledArray<IR_LoopingScope> looping_scopes;
    I32 scope;
};

struct ExpresionContext {
    VType vtype;
    U32 assignment_count;
};

ExpresionContext ExpresionContext_from_void();
ExpresionContext ExpresionContext_from_inference(U32 assignment_count);
ExpresionContext ExpresionContext_from_vtype(VType vtype, U32 assignment_count);

IR_Group ReadExpression(IR_Context* ir, Parser* parser, ExpresionContext context);
IR_Group ReadCode(IR_Context* ir, Parser* parser);
IR_Group ReadSentence(IR_Context* ir, Parser* parser);

IR_Group ReadFunctionCall(IR_Context* ir, ExpresionContext context, Parser* parser);

struct ObjectDefinitionResult {
    Array<ObjectDefinition> objects;
    IR_Group out;
    B32 success;
};

ObjectDefinitionResult ReadObjectDefinition(Arena* arena, Parser* parser, Reporter* reporter, Program* program, B32 require_single, RegisterKind register_kind);
ObjectDefinitionResult ReadObjectDefinitionWithIr(Arena* arena, Parser* parser, IR_Context* ir, B32 require_single, RegisterKind register_kind);
ObjectDefinitionResult ReadDefinitionList(Arena* arena, Parser* parser, Reporter* reporter, Program* program, RegisterKind register_kind);
ObjectDefinitionResult ReadDefinitionListWithIr(Arena* arena, Parser* parser, IR_Context* ir, RegisterKind register_kind);

IR_Group ReadExpressionList(Arena* arena, IR_Context* ir, VType vtype, Array<VType> expected_vtypes, Parser* parser);
VType ReadObjectType(Parser* parser, Reporter* reporter, Program* program);

Value ValueFromIrObject(IR_Object* object);

IR_Group IRFailed();
IR_Group IRFromNone(Value value = ValueNone());
IR_Group IRFromSingle(IR_Unit* unit, Value value = ValueNone());
IR_Group IRAppend(IR_Group o0, IR_Group o1);
IR_Group IRFromDefineObject(IR_Context* ir, RegisterKind register_kind, String identifier, VType vtype, B32 constant, Location location);
IR_Group IRFromDefineTemporal(IR_Context* ir, VType vtype, Location location);
IR_Group IRFromReference(IR_Context* ir, B32 expects_lvalue, Value value, Location location);
IR_Group IRFromDereference(IR_Context* ir, Value value, Location location);
IR_Group IRFromSymbol(IR_Context* ir, String identifier, Location location);
IR_Group IRFromFunctionCall(IR_Context* ir, String identifier, Array<Value> parameters, ExpresionContext context, Location location);
IR_Group IRFromDefaultInitializer(IR_Context* ir, VType vtype, Location location);
IR_Group IRFromStore(IR_Context* ir, Value dst, Value src, Location location);
IR_Group IRFromAssignment(IR_Context* ir, B32 expects_lvalue, Value dst, Value src, BinaryOperator op, Location location);
IR_Group IRFromMultipleAssignment(IR_Context* ir, B32 expects_lvalue, Array<Value> destinations, Value src, BinaryOperator op, Location location);
IR_Group IRFromBinaryOperator(IR_Context* ir, Value left, Value right, BinaryOperator op, B32 reuse_left, Location location);
IR_Group IRFromSignOperator(IR_Context* ir, Value src, BinaryOperator op, Location location);
IR_Group IRFromChild(IR_Context* ir, Value src, Value index, B32 is_member, VType vtype, Location location);
IR_Group IRFromChildAccess(IR_Context* ir, Value src, String child_name, ExpresionContext context, Location location);
IR_Group IRFromIfStatement(IR_Context* ir, Value condition, IR_Group success, IR_Group failure, Location location);
IR_Group IRFromLoop(IR_Context* ir, IR_Group init, IR_Group condition, IR_Group content, IR_Group update, Location location);
IR_Group IRFromFlowModifier(IR_Context* ir, B32 is_break, Location location);
IR_Group IRFromReturn(IR_Context* ir, IR_Group expression, Location location);

IR MakeIR(Arena* arena, Program* program, Array<Register> local_registers, IR_Group group, YovScript* script);
IR_Context* IrContextAlloc(Program* program, Reporter* reporter);
Array<VType> ReturnsFromRegisters(Arena* arena, Array<Register> registers);

IR IrFromValue(Arena* arena, Program* program, Value value);

B32 IRValidateReturnPath(Array<Unit> units);

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

// FRONT

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

struct FrontContext {
    Arena* arena;
    
    Reporter* reporter;
    Input* input;
    Program* program;
    
    Mutex mutex;
    PooledArray<YovScript> scripts;
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

YovScript* FrontAddScript(FrontContext* front, String path);
YovScript* FrontAddCoreScript(FrontContext* front);
YovScript* FrontGetScript(FrontContext* front, I32 script_id);
U32 LineFromLocation(Location location, YovScript* script);

Parser* ParserFromLocation(FrontContext* front, Location location);

void FrontReadLocationsAndImports(FrontContext* front, YovScript* script, LaneGroup* lane_group);
void FrontReadAllScripts(LaneContext* lane, FrontContext* front)
;
void FrontIdentifyDefinitions(LaneContext* lane, FrontContext* front);
void FrontDefineDefinitions(LaneContext* lane, FrontContext* front);
void FrontDefineGlobals(LaneContext* lane, FrontContext* front);
void FrontResolveGlobals(LaneContext* lane, FrontContext* front);
void FrontResolveDefinitions(LaneContext* lane, FrontContext* front);

void FrontDefineEnum(FrontContext* front, CodeDefinition* code);
void FrontDefineStruct(FrontContext* front, CodeDefinition* code);
void FrontDefineFunction(FrontContext* front, CodeDefinition* code);
void FrontDefineArg(FrontContext* front, CodeDefinition* code);

void FrontResolveEnum(FrontContext* front, EnumDefinition* def);
B32  FrontResolveStruct(FrontContext* front, StructDefinition* def);
void FrontResolveFunction(FrontContext* front, FunctionDefinition* def, CodeDefinition* code);
void FrontResolveArg(FrontContext* front, CodeDefinition* code);

//- REPORTS 

#define ReportErrorFront(_location, text, ...) ReportErrorEx(reporter, _location, 0, {}, text, __VA_ARGS__);


#define report_common_missing_closing_bracket(_code) ReportErrorFront(_code, "Missing closing bracket");
#define report_common_missing_opening_bracket(_code) ReportErrorFront(_code, "Missing opening bracket");
#define report_common_missing_closing_parenthesis(_code) ReportErrorFront(_code, "Missing closing parenthesis");
#define report_common_missing_opening_parenthesis(_code) ReportErrorFront(_code, "Missing opening parenthesis");
#define report_common_missing_closing_brace(_code) ReportErrorFront(_code, "Missing closing brace");
#define report_common_missing_opening_brace(_code) ReportErrorFront(_code, "Missing opening brace");
#define report_common_missing_block(_code) ReportErrorFront(_code, "Missing block");
#define report_common_expecting_valid_expresion(_code) ReportErrorFront(_code, "Expecting a valid expresion: {line}");
#define report_common_expecting_parenthesis(_code, _for_what) ReportErrorFront(_code, "Expecting parenthesis for %S", STR(_for_what));
#define report_syntactic_unknown_op(_code) ReportErrorFront(_code, "Unknown operation: {line}");
#define report_expr_invalid_binary_operation(_code, _op_str) ReportErrorFront(_code, "Invalid binary operation: '%S'", _op_str);
#define report_expr_syntactic_unknown(_code, _expr_str) ReportErrorFront(_code, "Unknown expresion: '%S'", _expr_str);
#define report_expr_is_empty(_code) ReportErrorFront(_code, "Empty expresion: {line}");
#define report_expr_empty_member(_code) ReportErrorFront(_code, "Member is not specified");
#define report_array_expr_expects_an_arrow(_code) ReportErrorFront(_code, "Array expresion expects an arrow");
#define report_array_indexing_expects_expresion(_code) ReportErrorFront(_code, "Array indexing expects an expresion");
#define report_objdef_expecting_colon(_code) ReportErrorFront(_code, "Expecting colon in object definition");
#define report_objdef_expecting_type_identifier(_code) ReportErrorFront(_code, "Expecting type identifier");
#define report_objdef_expecting_assignment(_code) ReportErrorFront(_code, "Expecting an assignment for object definition");
#define report_else_not_found_if(_code) ReportErrorFront(_code, "'else' is not valid without the corresponding 'if'");
#define report_for_unknown(_code) ReportErrorFront(_code, "Unknown for-statement: {line}");
#define report_foreach_expecting_identifier(_code) ReportErrorFront(_code, "Expecting an identifier for the itarator");
#define report_foreach_expecting_expresion(_code) ReportErrorFront(_code, "Expecting an array expresion");
#define report_foreach_expecting_colon(_code) ReportErrorFront(_code, "Expecting a colon separating identifiers and array expresion");
#define report_foreach_expecting_comma_separated_identifiers(_code) ReportErrorFront(_code, "Foreach-Statement expects comma separated identifiers");
#define report_assign_operator_not_found(_code) ReportErrorFront(_code, "Operator not found for an assignment");
#define report_enumdef_expecting_comma_separated_identifier(_code) ReportErrorFront(_code, "Expecting comma separated identifier for enum values");
#define report_expecting_object_definition(_code) ReportErrorFront(_code, "Expecting an object definition");
#define report_expecting_string_literal(_code) ReportErrorFront(_code, "Expecting string literal");
#define report_expecting_semicolon(_code) ReportErrorFront(_code, "Expecting semicolon");
#define report_expecting_assignment(_code) ReportErrorFront(_code, "Expecting assignment");
#define report_unknown_parameter(_code, _v) ReportErrorFront(_code, "Unknown parameter '%S'", _v);
#define report_invalid_escape_sequence(_code, _v) ReportErrorFront(_code, "Invalid escape sequence '\\%S'", _v);
#define report_invalid_codepoint_literal(_code, _v) ReportErrorFront(_code, "Invalid codepoint literal: %S", _v);


#define report_nested_definition(_code) ReportErrorFront(_code, "This definition can't be nested");
#define report_unsupported_operations(_code) ReportErrorFront(_code, "Unsupported operations outside main script");
#define report_type_missmatch_append(_code, _v0, _v1) ReportErrorFront(_code, "Type missmatch, can't append a '%S' into '%S'", _v0, _v1);
#define report_type_missmatch_array_expr(_code, _v0, _v1) ReportErrorFront(_code, "Type missmatch in array expresion, expecting '%S' but found '%S'", _v0, _v1);
#define report_type_missmatch_assign(_code, _v0, _v1) ReportErrorFront(_code, "Type missmatch, can't assign '%S' to '%S'", _v0, _v1);
#define report_assignment_is_constant(_code, _v0) ReportErrorFront(_code, "Can't assign a value to '%S', is constant", _v0);
#define report_unknown_array_definition(_code) ReportErrorFront(_code, "Unknown type for array definition");
#define report_invalid_binary_op(_code, _v0, _op, _v1) ReportErrorFront(_code, "Invalid binary operation: '%S' %S '%S'", _v0, _op, _v1);
#define report_invalid_signed_op(_code, _op, _v) ReportErrorFront(_code, "Invalid signed operation '%S %S'", _op, _v);
#define report_symbol_not_found(_code, _v) ReportErrorFront(_code, "Symbol '%S' not found", _v);
#define report_symbol_duplicated(_code, _v) ReportErrorFront(_code, "Duplicated symbol '%S'", _v);
#define report_object_not_found(_code, _v) ReportErrorFront(_code, "Object '%S' not found", _v);
#define report_object_type_not_found(_code, _t) ReportErrorFront(_code, "Object Type '%S' not found", _t);
#define report_object_duplicated(_code, _v) ReportErrorFront(_code, "Duplicated object '%S'", _v);
#define report_object_invalid_type(_code, _t) ReportErrorFront(_code, "Invalid Type '%S'", STR(_t));
#define report_member_not_found_in_object(_code, _v, _t) ReportErrorFront(_code, "Member '%S' not found in a '%S'", _v, _t);
#define report_member_not_found_in_type(_code, _v, _t) ReportErrorFront(_code, "Member '%S' not found in '%S'", _v, _t);
#define report_member_invalid_symbol(_code, _v) ReportErrorFront(_code, "'%S' does not have members to access", _v);
#define report_function_not_found(_code, _v) ReportErrorFront(_code, "Function '%S' not found", _v);
#define report_function_expecting_parameters(_code, _v, _c) ReportErrorFront(_code, "Function '%S' is expecting %u parameters", _v, _c);
#define report_function_wrong_parameter_type(_code, _f, _t, _c) ReportErrorFront(_code, "Function '%S' is expecting a '%S' as a parameter %u", _f, _t, _c);
#define report_function_expects_ref_as_parameter(_code, _f, _c) ReportErrorFront(_code, "Function '%S' is expecting a reference as a parameter %u", _f, _c);
#define report_function_expects_noref_as_parameter(_code, _f, _c) ReportErrorFront(_code, "Function '%S' is not expecting a reference as a parameter %u", _f, _c);
#define report_function_wrong_return_type(_code, _t) ReportErrorFront(_code, "Expected a '%S' as a return", _t);
#define report_function_expects_ref_as_return(_code) ReportErrorFront(_code, "Expected a reference as a return");
#define report_function_expects_no_ref_as_return(_code) ReportErrorFront(_code, "Can't return a reference");
#define report_function_no_return(_code, _f) ReportErrorFront(_code, "Not all paths of '%S' have a return", _f);
#define report_function_no_return_named(_code, _f, _n) ReportErrorFront(_code, "Not all paths of '%S' have a return for '%S'", _f, _n);
#define report_symbol_not_invokable(_code, _v) ReportErrorFront(_code, "Not invokable symbol '%S'", _v);
#define report_indexing_expects_an_int(_code) ReportErrorFront(_code, "Indexing expects an Int");
#define report_indexing_not_allowed(_code, _t) ReportErrorFront(_code, "Indexing not allowed for a '%S'", _t);
#define report_indexing_out_of_bounds(_code) ReportErrorFront(_code, "Index out of bounds");
#define report_dimensions_expects_an_int(_code) ReportErrorFront(_code, "Expecting an integer for the dimensions of the array");
#define report_dimensions_must_be_positive(_code) ReportErrorFront(_code, "Expecting a positive integer for the dimensions of the array");
#define report_expr_expects_bool(_code, _what)  ReportErrorFront(_code, "%S expects a Bool", _what);
#define report_expr_expects_lvalue(_code) ReportErrorFront(_code, "Expresion expects a lvalue: {line}");
#define report_expr_semantic_unknown(_code)  ReportErrorFront(_code, "Unknown expresion: {line}");
#define report_for_expects_an_array(_code)  ReportErrorFront(_code, "Foreach-Statement expects an array");
#define report_semantic_unknown_op(_code) ReportErrorFront(_code, "Unknown operation: {line}");
#define report_struct_recursive(_code) ReportErrorFront(_code, "Recursive struct definition");
#define report_struct_circular_dependency(_code) ReportErrorFront(_code, "Struct has circular dependency");
#define report_struct_implicit_member_type(_code) ReportErrorFront(_code, "Implicit member type is not allowed in structs");
#define report_intrinsic_not_match(_code, _n) ReportErrorFront(_code, "Intrinsic '%S' does not match", STR(_n));
#define report_ref_expects_lvalue(_code) ReportErrorFront(_code, "Can't get a reference of a rvalue");
#define report_ref_expects_non_constant(_code) ReportErrorFront(_code, "Can't get a reference of a constant");
#define report_arg_invalid_name(_code, _v) ReportErrorFront(_code, "Invalid arg name '%S'", _v);
#define report_arg_duplicated_name(_code, _v) ReportErrorFront(_code, "Duplicated arg name '%S'", _v);
#define report_arg_is_required(_code, _v) ReportErrorFront(_code, "Argument '%S' is required", _v);
#define report_break_inside_loop(_code) ReportErrorFront(_code, "Break keyword must be used inside a loop");
#define report_continue_inside_loop(_code) ReportErrorFront(_code, "Continue keyword must be used inside a loop");

