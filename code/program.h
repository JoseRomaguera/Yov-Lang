#pragma once

#include "common.h"

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

String StringFromBinaryOperator(BinaryOperator op);
B32 BinaryOperatorIsArithmetic(BinaryOperator op);

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

struct StructDefinition;
struct EnumDefinition;
struct FunctionDefinition;

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

#define VType_Type TypeFromName(program, "Type")
#define VType_Result TypeFromName(program, "Result")
#define VType_CopyMode TypeFromName(program, "CopyMode")
#define VType_YovInfo TypeFromName(program, "YovInfo")
#define VType_Context TypeFromName(program, "Context")
#define VType_CallsContext TypeFromName(program, "CallsContext")
#define VType_OS TypeFromName(program, "OS")
#define VType_CallOutput TypeFromName(program, "CallOutput")
#define VType_FileInfo TypeFromName(program, "FileInfo")
#define VType_YovParseOutput TypeFromName(program, "YovParseOutput")
#define VType_ObjectDefinition TypeFromName(program, "ObjectDefinition")
#define VType_FunctionDefinition TypeFromName(program, "FunctionDefinition")
#define VType_StructDefinition TypeFromName(program, "StructDefinition")
#define VType_EnumDefinition TypeFromName(program, "EnumDefinition")

#define VType_IntArray vtype_from_dimension(VType_Int, 1)
#define VType_BoolArray vtype_from_dimension(VType_Bool, 1)
#define VType_StringArray vtype_from_dimension(VType_String, 1)

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

struct Reference {
    Object* parent;
    VType vtype;
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
    U32 line;
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
    
    String path;
};

struct Global {
    String identifier;
    VType vtype;
    B32 is_constant;
};

struct ObjectDefinition {
    VType vtype;
    B32 is_constant;
    String name;
    Value value;
    Location location;
};

ObjectDefinition ObjDefMake(String name, VType vtype, Location location, B32 is_constant, Value value);
ObjectDefinition ObjectDefinitionCopy(Arena* arena, ObjectDefinition src);
Array<ObjectDefinition> ObjectDefinitionArrayCopy(Arena* arena, Array<ObjectDefinition> src);

struct VariableTypeChild {
    B32 is_member;
    String name;
    I32 index;
    VType vtype;
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

struct Program
{
    Arena* arena;
    
    String caller_dir;
    String script_dir;
    
    Array<VType> vtypes;
    Array<Definition> definitions;
    U32 function_count;
    U32 struct_count;
    U32 enum_count;
    U32 arg_count;
    
    Array<Global> globals;
    IR globals_initialize_ir;
    IR args_initialize_ir;
};

void ProgramInitializeTypesTable(Program* program);

B32 TypeEquals(Program* program, VType v0, VType v1);

B32 VTypeValid(VType vtype);
VType VTypeNext(Program* program, VType vtype);

B32 VTypeIsReady(VType vtype);
B32 VTypeIsSizeReady(VType vtype);
U32 VTypeGetSize(VType vtype);
B32 VTypeNeedsInternalRelease(Program* program, VType vtype);
String VTypeGetName(Program* program, VType vtype);

VType TypeFromIndex(Program* program, U32 index);
VType TypeFromName(Program* program, String name);
VType vtype_from_dimension(VType element, U32 dimension);
VType vtype_from_reference(VType base_type);
B32 TypeIsEnum(VType vtype);
B32 TypeIsArray(VType vtype);
B32 TypeIsStruct(VType vtype);
B32 TypeIsReference(VType vtype);
B32 TypeIsString(VType type);
B32 TypeIsInt(VType type);
B32 TypeIsBool(VType type);
B32 TypeIsAny(VType type);
B32 TypeIsVoid(VType type);
B32 TypeIsNil(VType type);
VType TypeGetChildAt(Program* program, VType vtype, I32 index, B32 is_member);
VariableTypeChild TypeGetChild(Program* program, VType vtype, String name);
VariableTypeChild vtype_get_member(VType vtype, String member);
VariableTypeChild VTypeGetProperty(Program* program, VType vtype, String property);
Array<VariableTypeChild> VTypeGetProperties(Program* program, VType vtype);
VType TypeFromBinaryOperation(Program* program, VType left, VType right, BinaryOperator op);
VType TypeFromSignOperation(Program* program, VType src, BinaryOperator op);
Array<VType> vtypes_from_definitions(Arena* arena, Array<ObjectDefinition> defs);

B32 ValueIsCompiletime(Value value);
I32 ValueGetRegister(Value value);
B32 ValueIsRValue(Value value);
B32 ValueIsNull(Value value);

B32 ValueEquals(Value v0, Value v1);
Value ValueCopy(Arena* arena, Value src);
Array<Value> ValueArrayCopy(Arena* arena, Array<Value> src);

Value ValueNone();
Value ValueNull();
Value ValueFromRegister(I32 index, VType vtype, B32 is_lvalue);
Value ValueFromReference(Value value);
Value ValueFromDereference(Program* program, Value value);
Value ValueFromInt(I64 value);
Value ValueFromEnum(VType vtype, I64 value);
Value ValueFromBool(B32 value);
Value ValueFromString(Arena* arena, String value);
Value ValueFromStringArray(Arena* arena, Program* program, Array<Value> values);
Value ValueFromType(Program* program, VType type);
Value ValueFromArray(Arena* arena, VType array_vtype, Array<Value> elements);
Value ValueFromEmptyArray(Arena* arena, VType base_vtype, Array<Value> dimensions);
Value ValueFromZero(VType vtype);
Value ValueFromGlobal(Program* program, U32 global_index);
Value ValueFromStringExpression(Arena* arena, String str, VType vtype);
Value value_from_return(Arena* arena, Array<Value> values);

Array<Value> ValuesFromReturn(Arena* arena, Value value, B32 empty_on_void);

String StrFromValue(Arena* arena, Program* program, Value value, B32 raw = false);

String StringFromCompiletime(Arena* arena, Program* program, Value value);
B32 B32FromCompiletime(Value value);
VType TypeFromCompiletime(Program* program, Value value);

void DefinitionIdentify(Program* program, U32 index, DefinitionType type, String identifier, Location location);

void EnumDefine(Program* program, EnumDefinition* def, Array<String> names, Array<Location> expression_locations);
void EnumResolve(Program* program, EnumDefinition* def, Array<I64> values);

void StructDefine(Program* program, StructDefinition* def, Array<ObjectDefinition> members);
void StructResolve(Program* program, StructDefinition* def);

void FunctionDefine(Program* program, FunctionDefinition* def, Array<ObjectDefinition> parameters, Array<ObjectDefinition> returns);
void FunctionResolveIntrinsic(Program* program, FunctionDefinition* def, IntrinsicFunction* fn);
void FunctionResolve(Program* program, FunctionDefinition* def, IR ir);

void ArgDefine(Program* program, ArgDefinition* def, VType vtype);
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

void GlobalDefine(Program* program, U32 index, VType vtype, B32 is_constant);
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