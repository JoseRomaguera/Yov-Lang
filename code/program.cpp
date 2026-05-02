#include "program.h"

String StringFromOperatorKind(OperatorKind op) {
    if (op == OperatorKind_Addition) return "+";
    if (op == OperatorKind_Substraction) return "-";
    if (op == OperatorKind_Multiplication) return "*";
    if (op == OperatorKind_Division) return "/";
    if (op == OperatorKind_Modulo) return "%";
    if (op == OperatorKind_LogicalNot) return "!";
    if (op == OperatorKind_LogicalOr) return "||";
    if (op == OperatorKind_LogicalAnd) return "&&";
    if (op == OperatorKind_Equals) return "==";
    if (op == OperatorKind_NotEquals) return "!=";
    if (op == OperatorKind_LessThan) return "<";
    if (op == OperatorKind_LessEqualsThan) return "<=";
    if (op == OperatorKind_GreaterThan) return ">";
    if (op == OperatorKind_GreaterEqualsThan) return ">=";
    if (op == OperatorKind_Is) return "is";
    Assert(0);
    return "?";
}

B32 OperatorKindIsArithmetic(OperatorKind op) {
    if (op == OperatorKind_Addition) return true;
    if (op == OperatorKind_Substraction) return true;
    if (op == OperatorKind_Multiplication) return true;
    if (op == OperatorKind_Division) return true;
    if (op == OperatorKind_Modulo) return true;
    return false;
}

B32 OperatorKindIsComparison(OperatorKind op) {
    if (op == OperatorKind_Equals) return true;
    if (op == OperatorKind_NotEquals) return true;
    if (op == OperatorKind_LessThan) return true;
    if (op == OperatorKind_GreaterThan) return true;
    if (op == OperatorKind_LessEqualsThan) return true;
    if (op == OperatorKind_GreaterEqualsThan) return true;
    if (op == OperatorKind_LogicalAnd) return true;
    if (op == OperatorKind_LogicalOr) return true;
    if (op == OperatorKind_LogicalNot) return true;
    return false;
}

String StringFromPrimitive(PrimitiveType type)
{
    switch (type)
    {
        case PrimitiveType_Int: return "Int";
        case PrimitiveType_UInt: return "UInt";
        case PrimitiveType_Bool: return "Bool";
        case PrimitiveType_Float: return "Float";
        case PrimitiveType_String: return "String";
    }
    
    InvalidCodepath();
    return "?";
}

inline_fn Type _MakeSpecialType(const char* name, VKind kind) {
    Type type = {};
    type.name = name;
    type.kind = kind;
    return type;
}

inline_fn Type _MakePrimitive(const char* name, PrimitiveType primitive) {
    Type type = {};
    type.name = name;
    type.kind = VKind_Primitive;
    type.primitive = primitive;
    return type;
}

internal_fn Object _MakeObject(Type* type) {
    Object obj{};
    obj.type = type;
    return obj;
}

read_only Type _nil_type = _MakeSpecialType("Nil", VKind_Nil);
read_only Type _void_type = _MakeSpecialType("void", VKind_Void);
read_only Type _any_type = _MakeSpecialType("Any", VKind_Any);

read_only Type _int_type = _MakePrimitive("Int", PrimitiveType_Int);
read_only Type _uint_type = _MakePrimitive("UInt", PrimitiveType_UInt);
read_only Type _bool_type = _MakePrimitive("Bool", PrimitiveType_Bool);
read_only Type _float_type = _MakePrimitive("Float", PrimitiveType_Float);
read_only Type _string_type = _MakePrimitive("String", PrimitiveType_String);

Type* nil_type = &_nil_type;
Type* void_type = &_void_type;
Type* any_type = &_any_type;

Type* int_type = &_int_type;
Type* uint_type = &_uint_type;
Type* bool_type = &_bool_type;
Type* float_type = &_float_type;
Type* string_type = &_string_type;

read_only Object _nil_obj = _MakeObject(nil_type);
read_only Object _null_obj = _MakeObject(void_type);

Object* nil_obj = &_nil_obj;
Object* null_obj = &_null_obj;

ObjectDefinition ObjDefMake(String name, Type* type, Location location, B32 is_constant, Value value) {
    ObjectDefinition d{};
    d.name = name;
    d.type = type;
    d.is_constant = is_constant;
    d.value = value;
    d.location = location;
    return d;
}

ObjectDefinition ObjectDefinitionCopy(Arena* arena, ObjectDefinition src)
{
    ObjectDefinition dst = src;
    dst.name = StrCopy(arena, src.name);
    return dst;
}

Array<ObjectDefinition> ObjectDefinitionArrayCopy(Arena* arena, Array<ObjectDefinition> src)
{
    Array<ObjectDefinition> dst = ArrayAlloc<ObjectDefinition>(arena, src.count);
    foreach(i, src.count) {
        dst[i] = ObjectDefinitionCopy(arena, src[i]);
    }
    return dst;
}

String StringFromDefinitionType(DefinitionType type)
{
    if (type == DefinitionType_Function) return "Function";
    if (type == DefinitionType_Struct) return "Struct";
    if (type == DefinitionType_Enum) return "Enum";
    if (type == DefinitionType_Arg) return "Arg";
    InvalidCodepath();
    return "?";
}

Type* TypeFromArray(Program* program, Type* element, U32 dimension)
{
    MutexLockGuard(&program->types_mutex);
    
    Type* type = element;
    
    while (dimension > 0)
    {
        element = type;
        
        foreach_BArray(it, &program->types)
        {
            Type* t = it.value;
            
            if (t->kind == VKind_Array && t->element_type == element) {
                return t;
            }
        }
        
        type = BArrayAdd(&program->types);
        type->kind = VKind_Array;
        type->name = StrFormat(program->arena, "Array[%S]", element->name);
        type->element_type = element;
        
        dimension--;
    }
    
    return type;
}

Type* TypeFromList(Program* program, Type* element, U32 dimension)
{
    MutexLockGuard(&program->types_mutex);
    
    Type* type = element;
    
    while (dimension > 0)
    {
        element = type;
        
        foreach_BArray(it, &program->types)
        {
            Type* t = it.value;
            
            if (t->kind == VKind_List && t->element_type == element) {
                return t;
            }
        }
        
        type = BArrayAdd(&program->types);
        type->kind = VKind_List;
        type->name = StrFormat(program->arena, "List[%S]", element->name);
        type->element_type = element;
        
        dimension--;
    }
    
    return type;
}

Type* TypeFromReference(Program* program, Type* base_type)
{
    MutexLockGuard(&program->types_mutex);
    
    foreach_BArray(it, &program->types)
    {
        Type* t = it.value;
        
        if (t->kind == VKind_Reference && t->reference_base == base_type) {
            return t;
        }
    }
    
    Type* type = BArrayAdd(&program->types);
    type->kind = VKind_Reference;
    type->name = StrFormat(program->arena, "%S&", base_type->name);
    type->reference_base = base_type;
    return type;
}

Type* TypeFromPrimitive(PrimitiveType primitive)
{
    switch (primitive) {
        case PrimitiveType_Int: return int_type;
        case PrimitiveType_UInt: return uint_type;
        case PrimitiveType_Bool: return bool_type;
        case PrimitiveType_Float: return float_type;
        case PrimitiveType_String: return string_type;
    }
    
    return nil_type;
}

Type* TypeFromStruct(Program* program, StructDefinition* def)
{
    MutexLockGuard(&program->types_mutex);
    
    foreach_BArray(it, &program->types)
    {
        Type* t = it.value;
        
        if (t->kind == VKind_Struct && t->_struct == def) {
            return t;
        }
    }
    
    Type* type = BArrayAdd(&program->types);
    type->kind = VKind_Struct;
    type->name = def->identifier;
    type->_struct = def;
    return type;
}

Type* TypeFromEnum(Program* program, EnumDefinition* def)
{
    MutexLockGuard(&program->types_mutex);
    
    foreach_BArray(it, &program->types)
    {
        Type* t = it.value;
        
        if (t->kind == VKind_Enum && t->_enum == def) {
            return t;
        }
    }
    
    Type* type = BArrayAdd(&program->types);
    type->kind = VKind_Enum;
    type->name = def->identifier;
    type->_enum = def;
    return type;
}

B32 TypeIsEnum(Type* type) { return type->kind == VKind_Enum; }
B32 TypeIsArray(Type* type) { return type->kind == VKind_Array; }
B32 TypeIsList(Type* type) { return type->kind == VKind_List; }
B32 TypeIsStruct(Type* type) { return type->kind == VKind_Struct; }
B32 TypeIsReference(Type* type) { return type->kind == VKind_Reference; }
B32 TypeIsAnyInt(Type* type) { return type == int_type || type == uint_type; }

B32 TypeIsValid(Type* type) {
    return type->kind > VKind_Any;
}

Type* TypeGetNext(Program* program, Type* type)
{
    PROFILE_FUNCTION;
    
    if (type->kind == VKind_Array || type->kind == VKind_List) {
        return type->element_type;
    }
    
    if (type->kind == VKind_Reference) {
        return type->reference_base;
    }
    
    return nil_type;
}

Type* TypeGetBase(Program* program, Type* type)
{
    PROFILE_FUNCTION;
    
    Type* next = type;
    while (next != nil_type) {
        type = next;
        next = TypeGetNext(program, next);
    }
    
    return type;
}

B32 TypeIsReady(Type* type)
{
    if (type->kind == VKind_Struct) {
        return type->_struct->stage == DefinitionStage_Ready;
    }
    if (type->kind == VKind_Enum) {
        return type->_enum->stage == DefinitionStage_Ready;
    }
    
    return true;
}

B32 TypeIsSizeReady(Type* type)
{
    if (type->kind == VKind_Struct) {
        return type->_struct->stage == DefinitionStage_Ready;
    }
    return true;
}

U32 TypeGetSize(Type* type)
{
    if (type->kind == VKind_Primitive) {
        switch(type->primitive) {
            case PrimitiveType_Int: return sizeof(I64);
            case PrimitiveType_UInt: return sizeof(U64);
            case PrimitiveType_Bool: return sizeof(B32);
            case PrimitiveType_Float: return sizeof(F64);
            case PrimitiveType_String: return sizeof(ObjectData_String);
        }
    }
    if (type->kind == VKind_Struct) {
        Assert(type->_struct->stage == DefinitionStage_Ready);
        return type->_struct->size;
    }
    if (type->kind == VKind_Enum) return sizeof(I64);
    if (type->kind == VKind_Array) return sizeof(ObjectData_Array);
    if (type->kind == VKind_Reference) return sizeof(ObjectData_Ref);
    if (type->kind == VKind_Void) return 0;
    if (type->kind == VKind_Nil) return 0;
    
    InvalidCodepath();
    return 0;
}

B32 VTypeNeedsInternalRelease(Program* program, Type* type)
{
    if (TypeIsArray(type)) return true;
    if (TypeIsReference(type)) return true;
    if (type == string_type) return true;
    
    if (TypeIsStruct(type)) {
        Assert(type->_struct->stage == DefinitionStage_Ready);
        return type->_struct->needs_internal_release;
    }
    
    return false;
}

Type* TypeChooseMostSignificantPrimitive(Type* t0, Type* t1)
{
    if (t0->kind != VKind_Primitive || t1->kind != VKind_Primitive) {
        InvalidCodepath();
        return t0;
    }
    
    Assert(t0 != string_type);
    Assert(t1 != string_type);
    
    if (t0 == float_type || t1 == float_type) return float_type;
    
    if (TypeIsAnyInt(t0) || TypeIsAnyInt(t1))
    {
        B32 t0_sign = t0 == int_type;
        B32 t1_sign = t1 == int_type;
        B32 sign = t0_sign || t1_sign;
        
        if (sign) return int_type;
        else return uint_type;
    }
    
    Assert(t0 == t1);
    return t0;
}

Type* TypeFromName(Program* program, String name)
{
    if (name == "Any") return any_type;
    if (name == "void") return void_type;
    if (name == "Int") return int_type;
    if (name == "UInt") return uint_type;
    if (name == "Bool") return bool_type;
    if (name == "Float") return float_type;
    if (name == "String") return string_type;
    
    MutexLockGuard(&program->types_mutex);
    
    foreach_BArray(it, &program->types) {
        Type* t = it.value;
        if (t->name == name) {
            return t;
        }
    }
    return nil_type;
}

Type* TypeGetChildAt(Program* program, Type* type, U32 index, B32 is_member)
{
    if (is_member)
    {
        if (type->kind == VKind_Array) {
            return TypeGetNext(program, type);
        }
        else if (type->kind == VKind_Struct) {
            Array<Type*> types = type->_struct->types;
            if (index >= types.count) return nil_type;
            return types[index];
        }
    }
    else
    {
        // TODO(Jose): 
        InvalidCodepath();
    }
    
    return nil_type;
}

VariableTypeChild TypeGetChild(Program* program, Type* type, String name)
{
    VariableTypeChild info = TypeGetMember(type, name);
    if (info.index >= 0) return info;
    return VTypeGetProperty(program, type, name);
}

VariableTypeChild TypeGetMember(Type* type, String member)
{
    if (type->kind == VKind_Struct) {
        Array<String> names = type->_struct->names;
        foreach(i, names.count) {
            if (names[i] == member) {
                return { true, names[i], (I32)i, type->_struct->types[i] };
            }
        }
    }
    
    return { true, "", -1, nil_type };
}

VariableTypeChild VTypeGetProperty(Program* program, Type* type, String property)
{
    Array<VariableTypeChild> props = VTypeGetProperties(program, type);
    foreach(i, props.count) {
        if (props[i].name == property) return props[i];
    }
    return { false, "", -1, nil_type };
}

VariableTypeChild string_properties[] = {
    VariableTypeChild{ false, "size", 0, uint_type }
};

VariableTypeChild array_properties[] = {
    VariableTypeChild{ false, "count", 0, uint_type }
};

VariableTypeChild enum_properties[] = {
    VariableTypeChild{ false, "index", 0, int_type },
    VariableTypeChild{ false, "value", 1, int_type },
    VariableTypeChild{ false, "name", 2, string_type }
};

Array<VariableTypeChild> VTypeGetProperties(Program* program, Type* type)
{
    if (type == string_type) {
        return arrayof(string_properties);
    }
    
    if (TypeIsArray(type)) {
        return arrayof(array_properties);
    }
    
    if (TypeIsEnum(type)) {
        return arrayof(enum_properties);
    }
    
    return {};
}

Array<Type*> TypesFromDefinitions(Arena* arena, Array<ObjectDefinition> defs)
{
    Array<Type*> types = ArrayAlloc<Type*>(arena, defs.count);
    foreach(i, types.count) types[i] = defs[i].type;
    return types;
}

void DefinitionIdentify(Program* program, U32 index, DefinitionType type, String identifier, Location location)
{
    PROFILE_FUNCTION;
    
    if (index >= program->definitions.count) {
        InvalidCodepath();
        return;
    }
    
    Definition* full_def = &program->definitions[index];
    DefinitionHeader* def = &full_def->header;
    
    if (def->stage != DefinitionStage_None) {
        InvalidCodepath();
        return;
    }
    
    def->type = type;
    def->identifier = StrCopy(program->arena, identifier);
    def->location = location;
    def->stage = DefinitionStage_Identified;
    
    if (type == DefinitionType_Enum)
    {
        TypeFromEnum(program, &full_def->_enum);
    }
    else if (type == DefinitionType_Struct)
    {
        TypeFromStruct(program, &full_def->_struct);
    }
    
    LogType("%S Identify: %S", StringFromDefinitionType(type), identifier);
}

void EnumDefine(Program* program, EnumDefinition* def, Array<String> names, Array<Location> expression_locations)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    def->names = StrArrayCopy(program->arena, names);
    def->expression_locations = ArrayCopy(program->arena, expression_locations);
    def->stage = DefinitionStage_Defined;
    
    foreach(i, names.count) {
        LogType("Enum Define: %S[%u] -> %S", def->identifier, i, names[i]);
    }
}

void EnumResolve(Program* program, EnumDefinition* def, Array<I64> values)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    if (values.count == 0) {
        values = ArrayAlloc<I64>(context.arena, def->names.count);
        foreach(i, values.count) values[i] = i;
    }
    
    def->values = ArrayCopy(program->arena, values);
    Assert(def->names.count == def->values.count);
    def->stage = DefinitionStage_Ready;
    
    foreach(i, def->names.count) {
        LogType("Enum Resolve: %S.%S = %i", def->identifier, def->names[i], (I32)def->values[i]);
    }
}

void StructDefine(Program* program, StructDefinition* def, Array<ObjectDefinition> members)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    Array<String> names = ArrayAlloc<String>(program->arena, members.count);
    Array<Type*> types = ArrayAlloc<Type*>(program->arena, members.count);
    
    foreach(i, members.count)
    {
        ObjectDefinition member = members[i];
        
        names[i] = StrCopy(program->arena, member.name);
        types[i] = member.type;
        
        if (!TypeIsValid(member.type)) {
            InvalidCodepath();
            return;
        }
    }
    
    Assert(names.count == types.count);
    
    def->names = names;
    def->types = types;
    def->stage = DefinitionStage_Defined;
    
    foreach(i, members.count) {
        LogType("Struct Define: %S -> %S: %S", def->identifier, def->names[i], VTypeGetName(program, def->types[i]));
    }
}

void StructResolve(Program* program, StructDefinition* def)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    // TODO(Jose): Memory alignment
    
    Array<U32> offsets = ArrayAlloc<U32>(program->arena, def->types.count);
    
    U32 size = 0;
    B32 needs_internal_release = false;
    
    foreach(i, def->types.count)
    {
        Type* type = def->types[i];
        
        offsets[i] = size;
        
        if (!TypeIsValid(type)) {
            InvalidCodepath();
            return;
        }
        
        size += TypeGetSize(type);
        needs_internal_release |= VTypeNeedsInternalRelease(program, type);
    }
    
    def->needs_internal_release = needs_internal_release;
    def->offsets = offsets;
    def->size = size;
    def->stage = DefinitionStage_Ready;
    
    LogType("Struct Resolve: %S -> %u bytes", def->identifier, def->size);
}

void FunctionDefine(Program* program, FunctionDefinition* def, Array<ObjectDefinition> parameters, Array<ObjectDefinition> returns)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    def->parameters = ObjectDefinitionArrayCopy(program->arena, parameters);
    def->returns = ObjectDefinitionArrayCopy(program->arena, returns);
    def->stage = DefinitionStage_Defined;
    
    StringBuilder builder = string_builder_make(context.arena);
    appendf(&builder, "Function Define: %S (", def->identifier);
    
    foreach(i, parameters.count) {
        appendf(&builder, "%S: %S", parameters[i].name, parameters[i].type->name);
        if (i + 1 < parameters.count)
            append(&builder, ", ");
    }
    
    append(&builder, ") -> (");
    
    foreach(i, returns.count) {
        appendf(&builder, "%S: %S", returns[i].name, returns[i].type->name);
        if (i + 1 < returns.count)
            append(&builder, ", ");
    }
    
    append(&builder, ")");
    
    LogType(string_from_builder(context.arena, &builder));
}

void FunctionResolveIntrinsic(Program* program, FunctionDefinition* def, IntrinsicFunction* fn)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    def->is_intrinsic = true;
    def->intrinsic.fn = fn;
    def->stage = DefinitionStage_Ready;
    
    LogType("Function Resolve Instrinsic: %S", def->identifier);
}

void FunctionResolve(Program* program, FunctionDefinition* def, IR ir)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    def->defined.ir = ir;
    def->stage = DefinitionStage_Ready;
    
    LogType("Function Resolve: %S", def->identifier);
}

void ArgDefine(Program* program, ArgDefinition* def, Type* type)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    def->value_type = type;
    def->stage = DefinitionStage_Defined;
    
    LogType("Arg Define: %S", def->identifier);
}

void ArgResolve(Program* program, ArgDefinition* def, String name, String description, B32 required, Value default_value)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    def->name = StrCopy(program->arena, name);
    def->description = StrCopy(program->arena, description);
    def->required = required;
    def->default_value = default_value;
    def->stage = DefinitionStage_Ready;
    
    LogType("Arg Resolve: %S", def->identifier);
}

Definition* DefinitionFromIdentifier(Program* program, String identifier)
{
    foreach(i, program->definitions.count) {
        Definition* def = &program->definitions[i];
        if (StrEquals(def->header.identifier, identifier)) {
            return def;
        }
    }
    return NULL;
}

Definition* DefinitionFromIndex(Program* program, U32 index)
{
    if (index >= program->definitions.count) return NULL;
    return &program->definitions[index];
}

B32 DefinitionExists(Program* program, String identifier)
{
    return DefinitionFromIdentifier(program, identifier) != NULL;
}

StructDefinition* StructFromIdentifier(Program* program, String identifier)
{
    Definition* def = DefinitionFromIdentifier(program, identifier);
    if (def == NULL || def->header.type != DefinitionType_Struct) return NULL;
    return &def->_struct;
}

StructDefinition* StructFromIndex(Program* program, U32 index)
{
    Definition* def = DefinitionFromIndex(program, index);
    if (def == NULL || def->header.type != DefinitionType_Struct) return NULL;
    return &def->_struct;
}

EnumDefinition* EnumFromIdentifier(Program* program, String identifier)
{
    Definition* def = DefinitionFromIdentifier(program, identifier);
    if (def == NULL || def->header.type != DefinitionType_Enum) return NULL;
    return &def->_enum;
}

EnumDefinition* EnumFromIndex(Program* program, U32 index)
{
    Definition* def = DefinitionFromIndex(program, index);
    if (def == NULL || def->header.type != DefinitionType_Enum) return NULL;
    return &def->_enum;
}

FunctionDefinition* FunctionFromIdentifier(Program* program, String identifier)
{
    Definition* def = DefinitionFromIdentifier(program, identifier);
    if (def == NULL || def->header.type != DefinitionType_Function) return NULL;
    return &def->function;
}

FunctionDefinition* FunctionFromIndex(Program* program, U32 index)
{
    Definition* def = DefinitionFromIndex(program, index);
    if (def == NULL || def->header.type != DefinitionType_Function) return NULL;
    return &def->function;
}

ArgDefinition* ArgFromIndex(Program* program, U32 index)
{
    Definition* def = DefinitionFromIndex(program, index);
    if (def == NULL || def->header.type != DefinitionType_Arg) return NULL;
    return &def->arg;
}

ArgDefinition* ArgFromName(Program* program, String name)
{
    foreach(i, program->definitions.count) {
        ArgDefinition* def = &program->definitions[i].arg;
        if (def->type != DefinitionType_Arg) continue;
        if (StrEquals(def->name, name)) return def;
    }
    return NULL;
}

void GlobalDefine(Program* program, U32 index, Type* type, B32 is_constant)
{
    Global* global = GlobalFromIndex(program, index);
    Assert(global->type == void_type);
    Assert(TypeIsValid(type));
    global->type = type;
    global->is_constant = is_constant;
}

I32 GlobalIndexFromIdentifier(Program* program, String identifier)
{
    foreach(i, program->globals.count) {
        Global* global = &program->globals[i];
        if (global->identifier == identifier) return i;
    }
    return -1;
}

Global* GlobalFromIdentifier(Program* program, String identifier)
{
    I32 index = GlobalIndexFromIdentifier(program, identifier);
    if (index < 0) return NULL;
    return GlobalFromIndex(program, index);
}

Global* GlobalFromIndex(Program* program, U32 index) {
    if (index >= program->globals.count) {
        InvalidCodepath();
        return NULL;
    }
    return &program->globals[index];
}

Global* GlobalFromRegisterIndex(Program* program, I32 index) {
    U32 global_index = index;
    if (global_index >= program->globals.count) {
        return NULL;
    }
    return &program->globals[global_index];
}

B32 ValueIsCompiletime(Value value)
{
    if (value.kind == ValueKind_Array)
    {
        foreach(i, value.array.values.count) {
            if (!ValueIsCompiletime(value.array.values[i])) return false;
        }
        return true;
    }
    
    return value.kind == ValueKind_Literal || value.kind == ValueKind_ZeroInit || value.kind == ValueKind_None;
}

I32 ValueGetRegister(Value value) {
    if (value.kind != ValueKind_LValue && value.kind != ValueKind_Register) return -1;
    return value.reg.index;
}

B32 ValueIsRValue(Value value) { return value.kind != ValueKind_None && value.kind != ValueKind_LValue; }

B32 ValueIsNull(Value value) {
    return value.kind == ValueKind_Literal && value.type->kind == VKind_Void;
}

B32 ValueEquals(Value v0, Value v1)
{
    if (v0.kind != v1.kind) return false;
    
    if (v0.kind == ValueKind_LValue || v0.kind == ValueKind_Register) {
        return v0.reg.index == v1.reg.index && v0.reg.reference_op == v1.reg.reference_op;
    }
    
    return false;
}

Value ValueCopy(Arena* arena, Value src)
{
    Value dst = src;
    
    if (src.kind == ValueKind_Literal && src.type == string_type) {
        dst.literal_string = StrCopy(arena, src.literal_string);
    }
    else if (src.kind == ValueKind_Array) {
        dst.array.values = ValueArrayCopy(arena, src.array.values);
    }
    else if (src.kind == ValueKind_StringComposition) {
        dst.string_composition = ValueArrayCopy(arena, src.string_composition);
    }
    else if (src.kind == ValueKind_MultipleReturn) {
        dst.multiple_return = ValueArrayCopy(arena, src.multiple_return);
    }
    
    return dst;
}

Array<Value> ValueArrayCopy(Arena* arena, Array<Value> src)
{
    Array<Value> dst = ArrayAlloc<Value>(arena, src.count);
    foreach(i, dst.count) {
        dst[i] = ValueCopy(arena, src[i]);
    }
    return dst;
}

Value ValueNone() {
    Value v{};
    v.type = void_type;
    v.reg.index = -1;
    return v;
}

Value ValueNull() {
    Value v{};
    v.type = void_type;
    v.kind = ValueKind_Literal;
    return v;
}

Value ValueFromRegister(I32 index, Type* type, B32 is_lvalue) {
    Assert(index >= 0);
    Value v{};
    v.type = type;
    v.reg.index = index;
    v.reg.reference_op = 0;
    v.kind = is_lvalue ? ValueKind_LValue : ValueKind_Register;
    return v;
}

Value ValueFromReference(Program* program, Value value)
{
    Assert(value.kind == ValueKind_LValue || value.kind == ValueKind_Register);
    Assert(value.reg.reference_op <= 0);
    
    Value v{};
    v.type = TypeFromReference(program, value.type);
    v.reg.index = value.reg.index;
    v.reg.reference_op = value.reg.reference_op + 1;
    v.kind = value.kind;
    return v;
}

Value ValueFromDereference(Program* program, Value value)
{
    Assert(value.kind == ValueKind_LValue || value.kind == ValueKind_Register);
    Assert(value.type->kind == VKind_Reference);
    
    Value v{};
    v.type = TypeGetNext(program, value.type);
    v.reg.index = value.reg.index;
    v.reg.reference_op = value.reg.reference_op - 1;
    v.kind = value.kind;
    return v;
}

Value ValueFromInt(I64 value) {
    Value v{};
    v.type = int_type;
    v.kind = ValueKind_Literal;
    v.literal_sint = value;
    return v;
}

Value ValueFromUInt(U64 value) {
    Value v{};
    v.type = uint_type;
    v.kind = ValueKind_Literal;
    v.literal_uint = value;
    return v;
}

Value ValueFromBool(B32 value) {
    Value v{};
    v.type = bool_type;
    v.kind = ValueKind_Literal;
    v.literal_bool = value;
    return v;
}

Value ValueFromFloat(F64 value) {
    Value v{};
    v.type = float_type;
    v.kind = ValueKind_Literal;
    v.literal_float = value;
    return v;
}

Value ValueFromEnum(Type* type, I64 value) {
    Value v{};
    v.type = type;
    v.kind = ValueKind_Literal;
    v.literal_sint = value;
    return v;
}

Value ValueFromString(Arena* arena, String value) {
    Value v{};
    v.type = string_type;
    v.kind = ValueKind_Literal;
    v.literal_string = StrCopy(arena, value);
    return v;
}

Value ValueFromStringArray(Arena* arena, Program* program, Array<Value> values)
{
    B32 is_compiletime = true;
    
    foreach(i, values.count) {
        if (values[i].type != string_type || !ValueIsCompiletime(values[i])) {
            is_compiletime = false;
            break;
        }
    }
    
    if (is_compiletime)
    {
        StringBuilder builder = string_builder_make(context.arena);
        foreach(i, values.count) {
            String str = StringFromCompiletime(context.arena, program, values[i]);
            append(&builder, str);
        }
        
        return ValueFromString(arena, string_from_builder(context.arena, &builder));
    }
    else
    {
        Value v{};
        v.type = string_type;
        v.kind = ValueKind_StringComposition;
        v.string_composition = ArrayCopy(arena, values);
        return v;
    }
}

Value ValueFromType(Program* program, Type* type) {
    Value v{};
    v.type = Type_Type;
    v.kind = ValueKind_Literal;
    v.literal_type = type;
    return v;
}

Value ValueFromArray(Arena* arena, Type* array_type, Array<Value> elements)
{
    Assert(array_type->kind == VKind_Array);
    Value v{};
    v.type = array_type;
    v.kind = ValueKind_Array;
    v.array.values = ArrayCopy(arena, elements);
    return v;
}

Value ValueFromZero(Type* type)
{
    if (type == int_type) {
        return ValueFromInt(0);
    }
    if (type == uint_type) {
        return ValueFromUInt(0);
    }
    if (type == bool_type) {
        return ValueFromBool(false);
    }
    if (type == float_type) {
        return ValueFromFloat(0.0);
    }
    
    if (type == any_type) {
        return ValueNull();
    }
    
    Value v{};
    v.type = type;
    v.kind = ValueKind_ZeroInit;
    return v;
}

Value ValueFromGlobal(Program* program, U32 global_index)
{
    Global* global = GlobalFromIndex(program, RegIndexFromGlobal(global_index));
    return ValueFromRegister(global_index, global->type, true);
}

Value ValueFromStringExpression(Arena* arena, String str, Type* type)
{
    if (str.size <= 0) return ValueNone();
    // TODO(Jose): if (StrEquals(str, "null")) return value_null();
    
    if (type == bool_type) {
        if (str == "true" || str == "1") return ValueFromBool(true);
        if (str == "false" || str == "0") return ValueFromBool(false);
    }
    
    if (type == int_type) {
        I64 value;
        if (!I64FromString(&value, str)) return ValueNone();
        return ValueFromInt(value);
    }
    
    if (type == uint_type) {
        U64 value;
        if (!U64FromString(&value, str)) return ValueNone();
        return ValueFromUInt(value);
    }
    
    if (type == float_type) {
        F64 value;
        if (!F64FromString(&value, str)) return ValueNone();
        return ValueFromFloat(value);
    }
    
    if (type == string_type) {
        return ValueFromString(arena, str);
    }
    
    if (TypeIsEnum(type))
    {
        U64 start_name = 0;
        if (str[0] == '.') {
            start_name = 1;
        }
        else if (StrStarts(str, StrFormat(context.arena, "%S.", type->name))) {
            start_name = type->name.size + 1;
        }
        
        String enum_name = StrSub(str, start_name, str.size - start_name);
        foreach(i, type->_enum->names.count) {
            if (StrEquals(type->_enum->names[i], enum_name)) return ValueFromEnum(type, i);
        }
        return ValueNone();
    }
    
    return ValueNone();
}

Value ValueFromReturn(Arena* arena, Array<Value> values)
{
    if (values.count == 0) return ValueNone();
    if (values.count == 1) return values[0];
    
    Value v{};
    v.type = any_type;
    v.kind = ValueKind_MultipleReturn;
    v.multiple_return = ArrayCopy(arena, values);
    return v;
}

Array<Value> ValuesFromReturn(Arena* arena, Value value, B32 empty_on_void)
{
    if (value.kind == ValueKind_MultipleReturn) return value.multiple_return;
    
    if (value.kind == ValueKind_None) {
        return {};
    }
    
    Array<Value> values = ArrayAlloc<Value>(arena, 1);
    values[0] = value;
    return values;
}

String StrFromValue(Arena* arena, Program* program, Value value, B32 raw)
{
    if (value.kind == ValueKind_None) return "E";
    
    if (value.kind == ValueKind_Literal) {
        if (value.type == int_type) return StrFromI64(arena, value.literal_sint);
        if (value.type == uint_type) return StrFromU64(arena, value.literal_uint);
        if (value.type == bool_type) return value.literal_bool ? "true" : "false";
        if (value.type == float_type) return StrFromF64(arena, value.literal_float, 4);
        if (value.type == string_type) {
            if (raw) return value.literal_string;
            String escape = escape_string_from_raw_string(context.arena, value.literal_string);
            return StrFormat(arena, "\"%S\"", escape);
        }
        if (value.type == void_type) return "null";
        if (value.type == Type_Type) return value.literal_type->name;
        if (TypeIsEnum(value.type)) {
            I32 index = (I32)value.literal_sint;
            if (index < 0 || index >= value.type->_enum->names.count) return "?";
            return value.type->_enum->names[index];
        }
        InvalidCodepath();
        return "";
    }
    
    if (value.kind == ValueKind_Array)
    {
        Array<Value> values = value.array.values;
        if (values.count == 0) return "{ }";
        
        StringBuilder builder = string_builder_make(context.arena);
        
        append(&builder, "[ ");
        foreach(i, values.count) {
            append(&builder, StrFromValue(context.arena, program, values[i], false));
            if (i < values.count - 1) append(&builder, ", ");
        }
        append(&builder, " ]");
        
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_StringComposition)
    {
        Assert(value.type == string_type);
        Array<Value> values = value.string_composition;
        StringBuilder builder = string_builder_make(context.arena);
        foreach(i, values.count) {
            String src = StrFromValue(context.arena, program, values[i]);
            appendf(&builder, "%S", src);
            if (i < values.count - 1) append(&builder, " + ");
        }
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_MultipleReturn)
    {
        Array<Value> values = value.multiple_return;
        StringBuilder builder = string_builder_make(context.arena);
        append(&builder, "(");
        foreach(i, values.count) {
            String src = StrFromValue(context.arena, program, values[i]);
            appendf(&builder, "%S", src);
            if (i < values.count - 1) append(&builder, ", ");
        }
        append(&builder, ")");
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return StrFormat(arena, "%S()", value.type->name);
    }
    
    if (value.kind == ValueKind_Register || value.kind == ValueKind_LValue)
    {
        String ref_op = "";
        
        if (value.reg.reference_op != 0)
        {
            I32 op = value.reg.reference_op;
            
            while (op > 0) {
                ref_op = StrFormat(context.arena, "&%S", ref_op);
                op--;
            }
            
            while (op < 0) {
                ref_op = StrFormat(context.arena, "*%S", ref_op);
                op++;
            }
        }
        
        return StrFormat(arena, "%S%S", ref_op, StringFromRegister(context.arena, program, value.reg.index));
    }
    
    InvalidCodepath();
    return "";
}

String StringFromCompiletime(Arena* arena, Program* program, Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return false;
    }
    
    if (value.type == string_type) {
        if (value.kind == ValueKind_Literal) {
            return value.literal_string;
        }
        
        if (value.kind == ValueKind_ZeroInit) {
            return {};
        }
    }
    else {
        return StrFromValue(arena, program, value, true);
    }
    
    InvalidCodepath();
    return false;
}

B32 B32FromCompiletime(Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return false;
    }
    
    if (value.type != bool_type) {
        InvalidCodepath();
        return false;
    }
    
    if (value.kind == ValueKind_Literal) {
        return value.literal_bool;
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return false;
    }
    
    InvalidCodepath();
    return false;
}

Type* TypeFromCompiletime(Program* program, Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return void_type;
    }
    
    if (value.type != Type_Type) {
        InvalidCodepath();
        return void_type;
    }
    
    if (value.kind == ValueKind_Literal) {
        return value.literal_type;
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return void_type;
    }
    
    InvalidCodepath();
    return void_type;
}

B32 CompiletimeEquals(Program* program, Value v0, Value v1)
{
    if (!ValueIsCompiletime(v0) || !ValueIsCompiletime(v1)) {
        InvalidCodepath();
        return false;
    }
    
    if (v0.type != v1.type) {
        InvalidCodepath();
        return false;
    }
    
    Type* type = v0.type;
    
    if (type == int_type) {
        return v0.literal_sint == v1.literal_sint;
    }
    if (type == uint_type) {
        return v0.literal_uint == v1.literal_uint;
    }
    if (type == bool_type) {
        return v0.literal_bool == v1.literal_bool;
    }
    if (type == float_type) {
        return v0.literal_float == v1.literal_float;
    }
    if (type == string_type) {
        return v0.literal_string == v1.literal_string;
    }
    if (TypeIsEnum(type)) {
        return v0.literal_sint == v1.literal_sint;
    }
    if (type == Type_Type) {
        return v0.literal_type == v1.literal_type;
    }
    
    InvalidCodepath();
    return false;
}

I32 RegIndexFromGlobal(U32 global_index)
{
    return global_index;
}

I32 RegIndexFromLocal(Program* program, U32 local_index)
{
    return local_index + program->globals.count;
}

I32 LocalFromRegIndex(Program* program, I32 register_index)
{
    if (register_index < program->globals.count) return -1;
    return register_index - program->globals.count;
}

String StringFromRegister(Arena* arena, Program* program, I32 index)
{
    Global* global = GlobalFromRegisterIndex(program, index);
    if (global != NULL) return global->identifier;
    
    I32 local_index = LocalFromRegIndex(program, index);
    
    if (local_index < 0) return "rE";
    return StrFormat(arena, "r%i", local_index);
}

internal_fn String StringFromBinaryOperation(Arena* arena, String dst, String left, String right, OperatorKind op_kind)
{
    String op = StringFromOperatorKind(op_kind);
    return StrFormat(arena, "%S = %S %S %S", dst, left, op, right);
}

internal_fn String StringFromUnaryOperation(Arena* arena, String dst, String src, OperatorKind op_kind)
{
    String op = StringFromOperatorKind(op_kind);
    return StrFormat(arena, "%S = %S%S", dst, op, src);
}

internal_fn String StringFromCast(Arena* arena, String dst, String src, PrimitiveType type)
{
    String type_str = StringFromPrimitive(type);
    return StrFormat(arena, "%S = (%S)%S", dst, type_str, src);
}

internal_fn String StringFromUnitInfo(Arena* arena, Program* program, Unit unit)
{
    String dst = StringFromRegister(context.arena, program, unit.dst_index);
    String src0 = StrFromValue(context.arena, program, unit.src0);
    String src1 = StrFromValue(context.arena, program, unit.src1);
    
    switch(unit.kind)
    {
        case UnitKind_Error: return {};
        
        case UnitKind_Copy:
        case UnitKind_Store:
        return StrFormat(arena, "%S = %S", dst, src0);
        
        case UnitKind_FunctionCall:
        {
            FunctionDefinition* fn = unit.function_call.fn;
            StringBuilder builder = string_builder_make(context.arena);
            
            if (unit.dst_index >= 0)
            {
                foreach(i, fn->returns.count)
                {
                    appendf(&builder, StringFromRegister(context.arena, program, unit.dst_index + i));
                    if (i + 1 < fn->returns.count) {
                        appendf(&builder, ", ");
                    }
                }
                
                appendf(&builder, " = ");
            }
            
            String identifier = fn->identifier;
            Array<Value> params = unit.function_call.parameters;
            
            appendf(&builder, "%S(", identifier);
            
            foreach(i, params.count) {
                String param = StrFromValue(context.arena, program, params[i]);
                appendf(&builder, "%S", param);
                if (i < params.count - 1) append(&builder, ", ");
            }
            append(&builder, ")");
            return string_from_builder(arena, &builder);
        }
        
        case UnitKind_Return: return "";
        
        case UnitKind_Jump:
        {
            StringBuilder builder = string_builder_make(context.arena);
            String condition = src0;
            if (unit.jump.condition > 0) appendf(&builder, "%S ", condition);
            else if (unit.jump.condition < 0) appendf(&builder, "!%S ", condition);
            appendf(&builder, "%i", unit.jump.offset);
            return string_from_builder(arena, &builder);
        }
        
        case UnitKind_Child:
        {
            Value src = unit.src0;
            Value index = unit.src1;
            
            B32 is_member = unit.child.child_is_member;
            B32 is_literal_int = index.kind == ValueKind_Literal && index.type == uint_type;
            
            String op = {};
            
            if (is_literal_int)
            {
                U64 i = index.literal_uint;
                
                if (is_member && src.type->kind == VKind_Struct) {
                    if (i < src.type->_struct->names.count) op = src.type->_struct->names[i];
                    else op = "?";
                    op = StrFormat(context.arena, ".%S", op);
                }
                else if (!is_member) {
                    Array<VariableTypeChild> props = VTypeGetProperties(program, src.type);
                    if (i < props.count) op = props[i].name;
                    else op = "?";
                    op = StrFormat(context.arena, ".%S", op);
                }
            }
            
            
            if (op.size == 0) {
                op = StrFormat(context.arena, "[%S]", src1);
            }
            
            return StrFormat(arena, "%S = %S%S", dst, src0, op);
        }
        
        case UnitKind_ResultEval: return src0;
        
        case UnitKind_Add: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Addition);
        case UnitKind_Sub: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Substraction);
        case UnitKind_Mul: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Multiplication);
        case UnitKind_Div: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Division);
        case UnitKind_Mod: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Modulo);
        
        case UnitKind_Eql: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Equals);
        case UnitKind_Neq: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_NotEquals);
        case UnitKind_Gtr: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_GreaterThan);
        case UnitKind_Lss: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_LessThan);
        case UnitKind_Geq: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_GreaterEqualsThan);
        case UnitKind_Leq: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_LessEqualsThan);
        
        case UnitKind_Or: return StringFromUnaryOperation(arena, dst, src0, OperatorKind_LogicalOr);
        case UnitKind_And: return StringFromUnaryOperation(arena, dst, src0, OperatorKind_LogicalAnd);
        case UnitKind_Not: return StringFromUnaryOperation(arena, dst, src0, OperatorKind_LogicalNot);
        
        case UnitKind_Neg: return StringFromUnaryOperation(arena, dst, src0, OperatorKind_Substraction);
        
        case UnitKind_Cast:
        case UnitKind_BitCast:
        return StringFromCast(arena, dst, src0, unit.op_dst_type);
        
        case UnitKind_Is: return StrFormat(arena, "%S = %S is %S", dst, src0, src1);
        
        case UnitKind_Empty: return {};
    }
    
    
    InvalidCodepath();
    return {};
}

String StringFromUnitKind(Arena* arena, UnitKind unit)
{
    switch (unit)
    {
        case UnitKind_Error: return "error";
        case UnitKind_Copy: return "copy";
        case UnitKind_Store: return "store";
        case UnitKind_FunctionCall: return "call";
        case UnitKind_Return: return "return";
        case UnitKind_Jump: return "jump";
        case UnitKind_Child: return "child";
        case UnitKind_ResultEval: return "eval";
        case UnitKind_Empty: return "empty";
        case UnitKind_Add: return "add";
        case UnitKind_Sub: return "sub";
        case UnitKind_Mul: return "mul";
        case UnitKind_Div: return "div";
        case UnitKind_Mod: return "mod";
        
        case UnitKind_Eql: return "eql";
        
        case UnitKind_Neq: return "neq";
        case UnitKind_Gtr: return "gtr";
        case UnitKind_Lss: return "lss";
        case UnitKind_Geq: return "geq";
        case UnitKind_Leq: return "leq";
        
        case UnitKind_Or: return "or";
        case UnitKind_And: return "and";
        case UnitKind_Not: return "not";
        case UnitKind_Neg: return "neg";
        
        case UnitKind_Cast: return "cast";
        case UnitKind_BitCast: return "bcast";
        
        case UnitKind_Is: return "is";
    }
    
    InvalidCodepath();
    return "?";
}

#if DEV

String StringFromUnit(Arena* arena, Program* program, U32 index, U32 index_digits, U32 line_digits, Unit unit)
{
    PROFILE_FUNCTION;
    
    StringBuilder builder = string_builder_make(context.arena);
    
    String index_str = StrFormat(context.arena, "%u", index);
    String line_str = StrFormat(context.arena, "%u", unit.line);
    
    for (U32 i = (U32)index_str.size; i < index_digits; ++i) append(&builder, "0");
    append(&builder, index_str);
    append(&builder, " (");
    for (U32 i = (U32)line_str.size; i < line_digits; ++i) append(&builder, "0");
    append(&builder, line_str);
    append(&builder, ") ");
    
    String name = StringFromUnitKind(context.arena, unit.kind);
    
    append(&builder, name);
    for (U32 i = (U32)name.size; i < 7; ++i) append(&builder, " ");
    
    String info = StringFromUnitInfo(context.arena, program, unit);
    append(&builder, info);
    
    return string_from_builder(arena, &builder);
}

void PrintUnits(Program* program, Array<Unit> units)
{
    U32 max_index = units.count;
    U32 max_line = 0;
    
#if DEV_LOCATION_INFO
    foreach(i, units.count) {
        max_line = Max(units[i].location.info.line, max_line);
    }
#else
    max_line = 10000;
#endif
    
    // TODO(Jose): Use log
    U32 index_digits = 0;
    U32 aux = max_index;
    while (aux != 0) {
        aux /= 10;
        index_digits++;
    }
    
    U32 line_digits = 0;
    aux = max_line;
    while (aux != 0) {
        aux /= 10;
        line_digits++;
    }
    
    foreach(i, units.count) {
        PrintEx(PrintLevel_DevLog, "%S\n", StringFromUnit(context.arena, program, i, index_digits, line_digits, units[i]));
    }
}

void PrintIr(Program* program, String name, IR ir)
{
    PrintEx(PrintLevel_DevLog, "[IR] %S:\n", name);
    PrintUnits(program, ir.instructions);
    
    if (ir.local_registers.count > 0)
        PrintEx(PrintLevel_DevLog, "----- REGISTERS -----\n");
    
    foreach(i, ir.local_registers.count)
    {
        Register reg = ir.local_registers[i];
        
        B32 is_param = reg.kind == RegisterKind_Parameter;
        B32 is_return = reg.kind == RegisterKind_Return;
        
        if (is_param) PrintEx(PrintLevel_DevLog, "[param] ");
        else if (is_return) PrintEx(PrintLevel_DevLog, "[return] ");
        Assert(is_param + is_return <= 1);
        
        PrintEx(PrintLevel_DevLog, "%S: %S", StringFromRegister(context.arena, program, RegIndexFromLocal(program, i)), reg.type->name);
        PrintEx(PrintLevel_DevLog, "\n");
    }
    
    PrintEx(PrintLevel_DevLog, SEPARATOR_STRING "\n");
}

#endif