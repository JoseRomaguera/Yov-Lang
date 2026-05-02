#pragma once

#include "common.h"

enum OperatorKind {
    OperatorKind_Unknown,
    OperatorKind_None,
    
    OperatorKind_Addition,
    OperatorKind_Substraction,
    OperatorKind_Multiplication,
    OperatorKind_Division,
    OperatorKind_Modulo,
    
    OperatorKind_LogicalNot,
    OperatorKind_LogicalOr,
    OperatorKind_LogicalAnd,
    
    OperatorKind_Equals,
    OperatorKind_NotEquals,
    OperatorKind_LessThan,
    OperatorKind_LessEqualsThan,
    OperatorKind_GreaterThan,
    OperatorKind_GreaterEqualsThan,
    
    OperatorKind_Is,
};

String StringFromOperatorKind(OperatorKind op);
B32 OperatorKindIsArithmetic(OperatorKind op);
B32 OperatorKindIsComparison(OperatorKind op);

enum PrimitiveType {
    PrimitiveType_Int,
    PrimitiveType_UInt,
    PrimitiveType_Bool,
    PrimitiveType_Float,
    PrimitiveType_String,
};

String StringFromPrimitive(PrimitiveType type);

enum VKind {
    VKind_Nil,
    VKind_Void,
    VKind_Any,
    VKind_Primitive,
    VKind_Struct,
    VKind_Enum,
    VKind_Reference,
    VKind_Array,
    VKind_List,
};

struct StructDefinition;
struct EnumDefinition;
struct FunctionDefinition;

struct Type {
    String name;
    VKind kind;
    
    union {
        PrimitiveType primitive;
        StructDefinition* _struct;
        EnumDefinition* _enum;
        Type* reference_base;
        Type* element_type;
    };
};

struct Object {
    U32 ID;
    I32 ref_count;
    Type* type;
    Object* prev;
    Object* next;
};

struct ObjectData_Array {
    U32 count;
    U32 capacity;
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

#define Type_Type TypeFromName(program, "Type")
#define Type_Result TypeFromName(program, "Result")
#define Type_CopyMode TypeFromName(program, "CopyMode")
#define Type_YovInfo TypeFromName(program, "YovInfo")
#define Type_Context TypeFromName(program, "Context")
#define Type_CallsContext TypeFromName(program, "CallsContext")
#define Type_OS TypeFromName(program, "OS")
#define Type_CallOutput TypeFromName(program, "CallOutput")
#define Type_FileInfo TypeFromName(program, "FileInfo")
#define Type_YovParseOutput TypeFromName(program, "YovParseOutput")
#define Type_ObjectDefinition TypeFromName(program, "ObjectDefinition")
#define Type_FunctionDefinition TypeFromName(program, "FunctionDefinition")
#define Type_StructDefinition TypeFromName(program, "StructDefinition")
#define Type_EnumDefinition TypeFromName(program, "EnumDefinition")

global_var Type* nil_type;
global_var Type* void_type;
global_var Type* any_type;

global_var Type* int_type;
global_var Type* uint_type;
global_var Type* bool_type;
global_var Type* float_type;
global_var Type* string_type;

global_var Object* nil_obj;
global_var Object* null_obj;

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
    Type* type;
    ValueKind kind;
    union {
        struct {
            I32 index;
            I32 reference_op; // 0 -> None; 1 -> Take Reference; -1 -> Dereference
        } reg;
        
        I64 literal_sint;
        U64 literal_uint;
        B32 literal_bool;
        F64 literal_float;
        String literal_string;
        Type* literal_type;
        struct {
            Array<Value> values;
        } array;
        
        Array<Value> string_composition;
        Array<Value> multiple_return;
    };
};

struct Reference {
    Object* parent;
    Type* type;
    void* address;
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
    UnitKind_Child,
    UnitKind_ResultEval,
    
    UnitKind_Add, UnitKind_Sub, UnitKind_Mul,
    UnitKind_Div, UnitKind_Mod,
    
    UnitKind_Eql, UnitKind_Neq,
    UnitKind_Gtr, UnitKind_Lss,
    UnitKind_Geq, UnitKind_Leq,
    
    UnitKind_Or, UnitKind_And, UnitKind_Not,
    UnitKind_Neg,
    
    UnitKind_Cast,
    UnitKind_BitCast,
    
    UnitKind_Is,
};

struct Unit {
    UnitKind kind;
    U32 line;
    I32 dst_index;
    Value src0;
    Value src1;
    
    PrimitiveType op_dst_type;
    
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
    Type* type;
};

inline_fn B32 RegisterIsValid(Register reg) { return reg.kind != RegisterKind_None; }

struct IR {
    B32 success;
    Value value;
    Array<Unit> instructions;
    Array<Register> local_registers;
    U32 parameter_count;
    
    String path;
};

struct Global {
    String identifier;
    Type* type;
    B32 is_constant;
};

struct ObjectDefinition {
    Type* type;
    B32 is_constant;
    String name;
    Value value;
    Location location;
};

ObjectDefinition ObjDefMake(String name, Type* type, Location location, B32 is_constant, Value value);
ObjectDefinition ObjectDefinitionCopy(Arena* arena, ObjectDefinition src);
Array<ObjectDefinition> ObjectDefinitionArrayCopy(Arena* arena, Array<ObjectDefinition> src);

struct VariableTypeChild {
    B32 is_member;
    String name;
    I32 index;
    Type* type;
};

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

struct DefinitionHeader {
    DefinitionType type;
    
    String identifier;
    Location location;
    volatile DefinitionStage stage;
};

struct Runtime;
typedef void IntrinsicFunction(Runtime* runtime, Array<Reference> params, Array<Reference> returns);

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
    Type* value_type;
    B32 required;
    Value default_value;
};

struct StructDefinition : DefinitionHeader {
    Array<String> names;
    Array<Type*> types;
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

struct Program
{
    Arena* arena;
    
    String caller_dir;
    String script_dir;
    
    Mutex types_mutex;
    BArray<Type> types;
    
    Array<Definition> definitions;
    U32 function_count;
    U32 struct_count;
    U32 enum_count;
    U32 arg_count;
    
    Array<Global> globals;
    IR globals_initialize_ir;
    IR args_initialize_ir;
};

B32 TypeIsValid(Type* type);
Type* TypeGetNext(Program* program, Type* type);
Type* TypeGetBase(Program* program, Type* type);

B32 TypeIsReady(Type* type);
B32 TypeIsSizeReady(Type* type);
U32 TypeGetSize(Type* type);
B32 VTypeNeedsInternalRelease(Program* program, Type* type);

Type* TypeChooseMostSignificantPrimitive(Type* t0, Type* t1);

Type* TypeFromName(Program* program, String name);
Type* TypeFromArray(Program* program, Type* element, U32 dimension);
Type* TypeFromList(Program* program, Type* element, U32 dimension);
Type* TypeFromReference(Program* program, Type* base_type);
Type* TypeFromPrimitive(PrimitiveType primitive);
Type* TypeFromStruct(Program* program, StructDefinition* def);
Type* TypeFromEnum(Program* program, EnumDefinition* def);
B32 TypeIsEnum(Type* type);
B32 TypeIsArray(Type* type);
B32 TypeIsList(Type* type);
B32 TypeIsStruct(Type* type);
B32 TypeIsReference(Type* type);
B32 TypeIsAnyInt(Type* type);

Type* TypeGetChildAt(Program* program, Type* type, I32 index, B32 is_member);
VariableTypeChild TypeGetChild(Program* program, Type* type, String name);
VariableTypeChild TypeGetMember(Type* type, String member);
VariableTypeChild VTypeGetProperty(Program* program, Type* type, String property);
Array<VariableTypeChild> VTypeGetProperties(Program* program, Type* type);

Array<Type*> TypesFromDefinitions(Arena* arena, Array<ObjectDefinition> defs);

B32 ValueIsCompiletime(Value value);
I32 ValueGetRegister(Value value);
B32 ValueIsRValue(Value value);
B32 ValueIsNull(Value value);

B32 ValueEquals(Value v0, Value v1);
Value ValueCopy(Arena* arena, Value src);
Array<Value> ValueArrayCopy(Arena* arena, Array<Value> src);

Value ValueNone();
Value ValueNull();
Value ValueFromRegister(I32 index, Type* type, B32 is_lvalue);
Value ValueFromReference(Program* program, Value value);
Value ValueFromDereference(Program* program, Value value);
Value ValueFromInt(I64 value);
Value ValueFromUInt(U64 value);
Value ValueFromBool(B32 value);
Value ValueFromFloat(F64 value);
Value ValueFromEnum(Type* type, I64 value);
Value ValueFromString(Arena* arena, String value);
Value ValueFromStringArray(Arena* arena, Program* program, Array<Value> values);
Value ValueFromType(Program* program, Type* type);
Value ValueFromArray(Arena* arena, Type* array_type, Array<Value> elements);
Value ValueFromZero(Type* type);
Value ValueFromGlobal(Program* program, U32 global_index);
Value ValueFromStringExpression(Arena* arena, String str, Type* type);
Value ValueFromReturn(Arena* arena, Array<Value> values);

Array<Value> ValuesFromReturn(Arena* arena, Value value, B32 empty_on_void);

String StrFromValue(Arena* arena, Program* program, Value value, B32 raw = false);

String StringFromCompiletime(Arena* arena, Program* program, Value value);
B32 B32FromCompiletime(Value value);
Type* TypeFromCompiletime(Program* program, Value value);
B32 CompiletimeEquals(Program* program, Value v0, Value v1);

void DefinitionIdentify(Program* program, U32 index, DefinitionType type, String identifier, Location location);

void EnumDefine(Program* program, EnumDefinition* def, Array<String> names, Array<Location> expression_locations);
void EnumResolve(Program* program, EnumDefinition* def, Array<I64> values);

void StructDefine(Program* program, StructDefinition* def, Array<ObjectDefinition> members);
void StructResolve(Program* program, StructDefinition* def);

void FunctionDefine(Program* program, FunctionDefinition* def, Array<ObjectDefinition> parameters, Array<ObjectDefinition> returns);
void FunctionResolveIntrinsic(Program* program, FunctionDefinition* def, IntrinsicFunction* fn);
void FunctionResolve(Program* program, FunctionDefinition* def, IR ir);

void ArgDefine(Program* program, ArgDefinition* def, Type* type);
void ArgResolve(Program* program, ArgDefinition* def, String name, String description, B32 required, Value default_value);

Definition* DefinitionFromIdentifier(Program* program, String identifier);
Definition* DefinitionFromIndex(Program* program, U32 index);
B32 DefinitionExists(Program* program, String identifier);
StructDefinition* StructFromIdentifier(Program* program, String identifier);
StructDefinition* StructFromIndex(Program* program, U32 index);
EnumDefinition* EnumFromIdentifier(Program* program, String identifier);
EnumDefinition* EnumFromIndex(Program* program, U32 index);
FunctionDefinition* FunctionFromIdentifier(Program* program, String identifier);
FunctionDefinition* FunctionFromIndex(Program* program, U32 index);
ArgDefinition* ArgFromIndex(Program* program, U32 index);
ArgDefinition* ArgFromName(Program* program, String name);

void GlobalDefine(Program* program, U32 index, Type* type, B32 is_constant);
I32 GlobalIndexFromIdentifier(Program* program, String identifier);
Global* GlobalFromIdentifier(Program* program, String identifier);
Global* GlobalFromIndex(Program* program, U32 index);
Global* GlobalFromRegisterIndex(Program* program, I32 index);

I32 RegIndexFromGlobal(U32 global_index);
I32 RegIndexFromLocal(Program* program, U32 local_index);
I32 LocalFromRegIndex(Program* program, I32 register_index);

String StringFromRegister(Arena* arena, Program* program, I32 index);
String StringFromUnitKind(Arena* arena, UnitKind unit);

#if DEV

String StringFromUnit(Arena* arena, Program* program, U32 index, U32 index_digits, U32 line_digits, Unit unit);
void PrintUnits(Program* program, Array<Unit> instructions);
void PrintIr(Program* program, String name, IR ir);

#endif

//- HIGH LEVEL CALLS 

IntrinsicFunction* IntrinsicFromIdentifier(String identifier);

Program* ProgramFromInput(Arena* arena, Input* input, Reporter* reporter);

struct RuntimeSettings {
    B8 user_assert;
    B8 no_user;
};

void ExecuteProgram(Program* program, Reporter* reporter, RuntimeSettings settings);