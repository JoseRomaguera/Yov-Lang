#include "program.h"

String StringFromBinaryOperator(BinaryOperator op) {
    if (op == BinaryOperator_Addition) return "+";
    if (op == BinaryOperator_Substraction) return "-";
    if (op == BinaryOperator_Multiplication) return "*";
    if (op == BinaryOperator_Division) return "/";
    if (op == BinaryOperator_Modulo) return "%";
    if (op == BinaryOperator_LogicalNot) return "!";
    if (op == BinaryOperator_LogicalOr) return "||";
    if (op == BinaryOperator_LogicalAnd) return "&&";
    if (op == BinaryOperator_Equals) return "==";
    if (op == BinaryOperator_NotEquals) return "!=";
    if (op == BinaryOperator_LessThan) return "<";
    if (op == BinaryOperator_LessEqualsThan) return "<=";
    if (op == BinaryOperator_GreaterThan) return ">";
    if (op == BinaryOperator_GreaterEqualsThan) return ">=";
    if (op == BinaryOperator_Is) return "is";
    Assert(0);
    return "?";
}

B32 BinaryOperatorIsArithmetic(BinaryOperator op) {
    if (op == BinaryOperator_Addition) return true;
    if (op == BinaryOperator_Substraction) return true;
    if (op == BinaryOperator_Multiplication) return true;
    if (op == BinaryOperator_Division) return true;
    if (op == BinaryOperator_Modulo) return true;
    return false;
}

internal_fn Object _make_object(VType vtype) {
    Object obj{};
    obj.vtype = vtype;
    return obj;
}

read_only Object _nil_obj = _make_object(VType_Nil);
read_only Object _null_obj = _make_object(VType_Void);

Object* nil_obj = &_nil_obj;
Object* null_obj = &_null_obj;

ObjectDefinition ObjDefMake(String name, VType vtype, Location location, B32 is_constant, Value value) {
    ObjectDefinition d{};
    d.name = name;
    d.vtype = vtype;
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
    Array<ObjectDefinition> dst = array_make<ObjectDefinition>(arena, src.count);
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

void ProgramInitializeTypesTable(Program* program)
{
    VType primitives[] = {
        VType_Int,
        VType_Bool,
        VType_String,
        
        MakePrimitive("I64", PrimitiveType_I64, -1),
    };
    
    U32 vtype_count = countof(primitives);
    vtype_count += program->struct_count;
    vtype_count += program->enum_count;
    
    program->vtypes = array_make<VType>(program->arena, vtype_count);
    
    U32 index = 0;
    foreach(i, countof(primitives)) {
        program->vtypes[index] = primitives[i];
        program->vtypes[index].base_index = index;
        index++;
    }
    
    foreach(i, program->definitions.count)
    {
        DefinitionHeader* header = &program->definitions[i].header;
        
        if (header->type == DefinitionType_Struct)
        {
            StructDefinition* def = &program->definitions[i]._struct;
            
            VType vtype = {};
            vtype.base_name = def->identifier;
            vtype.kind = VKind_Struct;
            vtype._struct = def;
            vtype.base_index = index;
            
            program->vtypes[index++] = vtype;
        }
        else if (header->type == DefinitionType_Enum)
        {
            EnumDefinition* def = &program->definitions[i]._enum;
            
            VType vtype = {};
            vtype.base_name = def->identifier;
            vtype.kind = VKind_Enum;
            vtype._enum = def;
            vtype.base_index = index;
            
            program->vtypes[index++] = vtype;
        }
    }
}

VType vtype_from_dimension(VType element, U32 dimension)
{
    if (dimension == 0) return element;
    VType vtype = {};
    vtype.kind = VKind_Array;
    vtype.array_dimensions = element.array_dimensions + dimension;
    vtype.base_name = element.base_name;
    vtype.base_index = element.base_index;
    return vtype;
}

VType vtype_from_reference(VType base_type)
{
    if (base_type.kind == VKind_Reference) {
        InvalidCodepath();
        return VType_Nil;
    }
    
    VType vtype = {};
    vtype.kind = VKind_Reference;
    vtype.array_dimensions = base_type.array_dimensions;
    vtype.base_name = base_type.base_name;
    vtype.base_index = base_type.base_index;
    return vtype;
}

B32 TypeIsEnum(VType vtype) { return vtype.kind == VKind_Enum; }
B32 TypeIsArray(VType vtype) { return vtype.kind == VKind_Array; }
B32 TypeIsStruct(VType vtype) { return vtype.kind == VKind_Struct; }
B32 TypeIsReference(VType vtype) { return vtype.kind == VKind_Reference; }
B32 TypeIsString(VType type) { return type.kind == VKind_Primitive && type.primitive_type == PrimitiveType_String; }
B32 TypeIsInt(VType type) { return type.kind == VKind_Primitive && type.primitive_type == PrimitiveType_I64; }
B32 TypeIsBool(VType type) { return type.kind == VKind_Primitive && type.primitive_type == PrimitiveType_B32; }
B32 TypeIsAny(VType type) { return type.kind == VKind_Any; }
B32 TypeIsVoid(VType type) { return type.kind == VKind_Void; }
B32 TypeIsNil(VType type) { return type.kind == VKind_Nil; }

B32 TypeEquals(Program* program, VType v0, VType v1) {
    if (v0.kind != v1.kind) return false;
    
    if (v0.kind <= VKind_Any) {
        return true;
    }
    
    if (v0.kind == VKind_Primitive) {
        return v0.primitive_type == v1.primitive_type;
    }
    
    if (v0.kind == VKind_Struct) {
        return v0._struct == v1._struct;
    }
    
    if (v0.kind == VKind_Enum) {
        return v0._enum == v1._enum;
    }
    
    if (v0.kind == VKind_Array) {
        return v0.array_dimensions == v1.array_dimensions && TypeEquals(program, TypeFromIndex(program, v0.base_index), TypeFromIndex(program, v1.base_index));
    }
    
    if (v0.kind == VKind_Reference) {
        return v0.array_dimensions == v1.array_dimensions && TypeEquals(program, TypeFromIndex(program, v0.base_index), TypeFromIndex(program, v1.base_index));;
    }
    
    InvalidCodepath();
    return false;
}

B32 VTypeValid(VType vtype) {
    return vtype.kind > VKind_Any;
}

VType VTypeNext(Program* program, VType vtype)
{
    if (vtype.kind == VKind_Array) {
        if (vtype.array_dimensions == 1) return TypeFromIndex(program, vtype.base_index);
        vtype.array_dimensions--;
        return vtype;
    }
    
    if (vtype.kind == VKind_Reference) {
        VType base_vtype = TypeFromIndex(program, vtype.base_index);
        return vtype_from_dimension(base_vtype, vtype.array_dimensions);
    }
    
    InvalidCodepath();
    return VType_Nil;
}

B32 VTypeIsReady(VType vtype)
{
    if (vtype.kind == VKind_Struct) {
        return vtype._struct->stage == DefinitionStage_Ready;
    }
    if (vtype.kind == VKind_Enum) {
        return vtype._enum->stage == DefinitionStage_Ready;
    }
    
    return true;
}

B32 VTypeIsSizeReady(VType vtype)
{
    if (vtype.kind == VKind_Struct) {
        return vtype._struct->stage == DefinitionStage_Ready;
    }
    return true;
}

U32 VTypeGetSize(VType vtype)
{
    if (vtype.kind == VKind_Primitive) {
        PrimitiveType type = vtype.primitive_type;
        if (type == PrimitiveType_I64) return sizeof(I64);
        if (type == PrimitiveType_B32) return sizeof(B32);
        if (type == PrimitiveType_String) return sizeof(ObjectData_String);
    }
    if (vtype.kind == VKind_Struct) {
        Assert(vtype._struct->stage == DefinitionStage_Ready);
        return vtype._struct->size;
    }
    if (vtype.kind == VKind_Enum) return sizeof(I64);
    if (vtype.kind == VKind_Array) return sizeof(ObjectData_Array);
    if (vtype.kind == VKind_Reference) return sizeof(ObjectData_Ref);
    if (vtype.kind == VKind_Void) return 0;
    if (vtype.kind == VKind_Nil) return 0;
    
    InvalidCodepath();
    return 0;
}

B32 VTypeNeedsInternalRelease(Program* program, VType vtype)
{
    if (TypeIsArray(vtype)) return true;
    if (TypeIsReference(vtype)) return true;
    if (TypeEquals(program, vtype, VType_String)) return true;
    
    if (TypeIsStruct(vtype)) {
        Assert(vtype._struct->stage == DefinitionStage_Ready);
        return vtype._struct->needs_internal_release;
    }
    
    return false;
}

String VTypeGetName(Program* program, VType vtype)
{
    if (vtype.kind == VKind_Array)
    {
        U64 needed_size = vtype.base_name.size + (vtype.array_dimensions * 2);
        String name = StrAlloc(context.arena, needed_size);
        MemoryCopy(name.data, vtype.base_name.data, vtype.base_name.size);
        
        foreach(i, vtype.array_dimensions) {
            U64 offset = vtype.base_name.size + i * 2;
            name[offset + 0] = '[';
            name[offset + 1] = ']';
        }
    }
    if (vtype.kind == VKind_Reference) {
        String next = VTypeGetName(program, VTypeNext(program, vtype));
        return StrFormat(context.arena, "%S&", next);
    }
    
    return vtype.base_name;
}

VType TypeFromIndex(Program* program, U32 index) {
    if (index >= program->vtypes.count) return VType_Nil;
    return program->vtypes[index];
}

VType TypeFromName(Program* program, String name)
{
    if (name == "Any") return VType_Any;
    if (name == "void") return VType_Void;
    
    U32 dimensions = 0;
    while (name.size > 2 && name[name.size - 1] == ']' && name[name.size - 2] == '[') {
        name = StrSub(name, 0, name.size - 2);
        dimensions++;
    }
    
    foreach(i, program->vtypes.count) {
        if (program->vtypes[i].base_name == name) {
            return vtype_from_dimension(program->vtypes[i], dimensions);
        }
    }
    return VType_Nil;
}

VType TypeGetChildAt(Program* program, VType vtype, U32 index, B32 is_member)
{
    if (is_member)
    {
        if (vtype.kind == VKind_Array) {
            return VTypeNext(program, vtype);
        }
        else if (vtype.kind == VKind_Struct) {
            Array<VType> vtypes = vtype._struct->vtypes;
            if (index >= vtypes.count) return VType_Nil;
            return vtypes[index];
        }
    }
    else
    {
        // TODO(Jose): 
        InvalidCodepath();
    }
    
    return VType_Nil;
}

VariableTypeChild TypeGetChild(Program* program, VType vtype, String name)
{
    VariableTypeChild info = vtype_get_member(vtype, name);
    if (info.index >= 0) return info;
    return VTypeGetProperty(program, vtype, name);
}

VariableTypeChild vtype_get_member(VType vtype, String member)
{
    if (vtype.kind == VKind_Struct) {
        Array<String> names = vtype._struct->names;
        foreach(i, names.count) {
            if (names[i] == member) {
                return { true, names[i], (I32)i, vtype._struct->vtypes[i] };
            }
        }
    }
    
    return { true, "", -1, VType_Nil };
}

VariableTypeChild VTypeGetProperty(Program* program, VType vtype, String property)
{
    Array<VariableTypeChild> props = VTypeGetProperties(program, vtype);
    foreach(i, props.count) {
        if (props[i].name == property) return props[i];
    }
    return { false, "", -1, VType_Nil };
}

VariableTypeChild string_properties[] = {
    VariableTypeChild{ false, "size", 0, VType_Int }
};

VariableTypeChild array_properties[] = {
    VariableTypeChild{ false, "count", 0, VType_Int }
};

VariableTypeChild enum_properties[] = {
    VariableTypeChild{ false, "index", 0, VType_Int },
    VariableTypeChild{ false, "value", 1, VType_Int },
    VariableTypeChild{ false, "name", 2, VType_String }
};

Array<VariableTypeChild> VTypeGetProperties(Program* program, VType vtype)
{
    if (TypeEquals(program, vtype, VType_String)) {
        return arrayof(string_properties);
    }
    
    if (TypeIsArray(vtype)) {
        return arrayof(array_properties);
    }
    
    if (TypeIsEnum(vtype)) {
        return arrayof(enum_properties);
    }
    
    return {};
}

VType TypeFromBinaryOperation(Program* program, VType left, VType right, BinaryOperator op)
{
    if (left.kind == VKind_Reference && TypeEquals(program, left, right))
    {
        if (op == BinaryOperator_Equals) return VType_Bool;
        if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (TypeEquals(program, left, VType_Int) && TypeEquals(program, right, VType_Int)) {
        if (BinaryOperatorIsArithmetic(op)) return VType_Int;
        
        if (op == BinaryOperator_Equals) return VType_Bool;
        if (op == BinaryOperator_NotEquals) return VType_Bool;
        if (op == BinaryOperator_LessThan) return VType_Bool;
        if (op == BinaryOperator_LessEqualsThan) return VType_Bool;
        if (op == BinaryOperator_GreaterThan) return VType_Bool;
        if (op == BinaryOperator_GreaterEqualsThan) return VType_Bool;
    }
    
    if (TypeEquals(program, left, VType_Bool) && TypeEquals(program, right, VType_Bool)) {
        if (op == BinaryOperator_LogicalOr) return VType_Bool;
        if (op == BinaryOperator_LogicalAnd) return VType_Bool;
        if (op == BinaryOperator_Equals) return VType_Bool;
        if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (TypeEquals(program, left, VType_String) && TypeEquals(program, right, VType_String))
    {
        if (op == BinaryOperator_Addition) return VType_String;
        if (op == BinaryOperator_Division) return VType_String;
        if (op == BinaryOperator_Equals) return VType_Bool;
        if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (TypeEquals(program, left, VType_Type) && TypeEquals(program, right, VType_Type))
    {
        if (op == BinaryOperator_Equals) return VType_Bool;
        else if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (TypeEquals(program, right, VType_Type)) {
        if (op == BinaryOperator_Is) return VType_Bool;
    }
    
    if ((TypeEquals(program, left, VType_String) && TypeEquals(program, right, VType_Int)) || (TypeEquals(program, left, VType_Int) && TypeEquals(program, right, VType_String)))
    {
        if (op == BinaryOperator_Addition) return VType_String;
    }
    
    if (left.kind == VKind_Enum && right.kind == VKind_Enum) {
        if (op == BinaryOperator_Equals) return VType_Bool;
        else if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (left.kind == VKind_Array && right.kind == VKind_Array && TypeEquals(program, VTypeNext(program, left), VTypeNext(program, right))) {
        if (op == BinaryOperator_Addition) return left;
    }
    
    if ((left.kind == VKind_Array && right.kind != VKind_Array) || (left.kind != VKind_Array && right.kind == VKind_Array))
    {
        VType array_type = (left.kind == VKind_Array) ? left : right;
        VType element_type = (left.kind == VKind_Array) ? right : left;
        
        if (TypeEquals(program, VTypeNext(program, array_type), element_type)) {
            if (op == BinaryOperator_Addition) return array_type;
        }
    }
    
    return VType_Nil;
}

VType TypeFromSignOperation(Program* program, VType src, BinaryOperator op)
{
    if (TypeEquals(program, src, VType_Int)) {
        if (op == BinaryOperator_Addition) return VType_Int;
        if (op == BinaryOperator_Substraction) return VType_Int;
    }
    
    if (TypeEquals(program, src, VType_Bool)) {
        if (op == BinaryOperator_LogicalNot) return VType_Bool;
    }
    
    return VType_Nil;
}

Array<VType> vtypes_from_definitions(Arena* arena, Array<ObjectDefinition> defs)
{
    Array<VType> types = array_make<VType>(arena, defs.count);
    foreach(i, types.count) types[i] = defs[i].vtype;
    return types;
}

void DefinitionIdentify(Program* program, U32 index, DefinitionType type, String identifier, Location location)
{
    if (index >= program->definitions.count) {
        InvalidCodepath();
        return;
    }
    
    DefinitionHeader* def = &program->definitions[index].header;
    
    if (def->stage != DefinitionStage_None) {
        InvalidCodepath();
        return;
    }
    
    def->type = type;
    def->identifier = StrCopy(program->arena, identifier);
    def->location = location;
    def->stage = DefinitionStage_Identified;
    
    LogType("%S Identify: %S", StringFromDefinitionType(type), identifier);
}

void EnumDefine(Program* program, EnumDefinition* def, Array<String> names, Array<Location> expression_locations)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    def->names = StrArrayCopy(program->arena, names);
    def->expression_locations = array_copy(program->arena, expression_locations);
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
        values = array_make<I64>(context.arena, def->names.count);
        foreach(i, values.count) values[i] = i;
    }
    
    def->values = array_copy(program->arena, values);
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
    
    Array<String> names = array_make<String>(program->arena, members.count);
    Array<VType> vtypes = array_make<VType>(program->arena, members.count);
    
    foreach(i, members.count)
    {
        ObjectDefinition member = members[i];
        
        names[i] = StrCopy(program->arena, member.name);
        vtypes[i] = member.vtype;
        
        if (!VTypeValid(member.vtype)) {
            InvalidCodepath();
            return;
        }
    }
    
    Assert(names.count == vtypes.count);
    
    def->names = names;
    def->vtypes = vtypes;
    def->stage = DefinitionStage_Defined;
    
    foreach(i, members.count) {
        LogType("Struct Define: %S -> %S: %S", def->identifier, def->names[i], VTypeGetName(program, def->vtypes[i]));
    }
}

void StructResolve(Program* program, StructDefinition* def)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    // TODO(Jose): Memory alignment
    
    Array<U32> offsets = array_make<U32>(program->arena, def->vtypes.count);
    
    U32 size = 0;
    B32 needs_internal_release = false;
    
    foreach(i, def->vtypes.count)
    {
        VType vtype = def->vtypes[i];
        
        offsets[i] = size;
        
        if (!VTypeValid(vtype)) {
            InvalidCodepath();
            return;
        }
        
        size += VTypeGetSize(vtype);
        needs_internal_release |= VTypeNeedsInternalRelease(program, vtype);
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
        appendf(&builder, "%S: %S", parameters[i].name, VTypeGetName(program, parameters[i].vtype));
        if (i + 1 < parameters.count)
            append(&builder, ", ");
    }
    
    append(&builder, ") -> (");
    
    foreach(i, returns.count) {
        appendf(&builder, "%S: %S", returns[i].name, VTypeGetName(program, returns[i].vtype));
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

void ArgDefine(Program* program, ArgDefinition* def, VType vtype)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    def->vtype = vtype;
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

void GlobalDefine(Program* program, U32 index, VType vtype, B32 is_constant)
{
    Global* global = GlobalFromIndex(program, index);
    Assert(TypeEquals(program, global->vtype, VType_Void));
    Assert(VTypeValid(vtype));
    global->vtype = vtype;
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
    
    return value.kind == ValueKind_Literal || value.kind == ValueKind_ZeroInit;
}

I32 ValueGetRegister(Value value) {
    if (value.kind != ValueKind_LValue && value.kind != ValueKind_Register) return -1;
    return value.reg.index;
}

B32 ValueIsRValue(Value value) { return value.kind != ValueKind_None && value.kind != ValueKind_LValue; }

B32 ValueIsNull(Value value) {
    return value.kind == ValueKind_Literal && value.vtype.kind == VKind_Void;
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
    
    if (src.kind == ValueKind_Literal && TypeIsString(src.vtype)) {
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
    Array<Value> dst = array_make<Value>(arena, src.count);
    foreach(i, dst.count) {
        dst[i] = ValueCopy(arena, src[i]);
    }
    return dst;
}

Value ValueNone() {
    Value v{};
    v.vtype = VType_Void;
    v.reg.index = -1;
    return v;
}

Value ValueNull() {
    Value v{};
    v.vtype = VType_Void;
    v.kind = ValueKind_Literal;
    return v;
}

Value ValueFromRegister(I32 index, VType vtype, B32 is_lvalue) {
    Assert(index >= 0);
    Value v{};
    v.vtype = vtype;
    v.reg.index = index;
    v.reg.reference_op = 0;
    v.kind = is_lvalue ? ValueKind_LValue : ValueKind_Register;
    return v;
}

Value ValueFromReference(Value value)
{
    Assert(value.kind == ValueKind_LValue || value.kind == ValueKind_Register);
    Assert(value.reg.reference_op <= 0);
    
    Value v{};
    v.vtype = vtype_from_reference(value.vtype);
    v.reg.index = value.reg.index;
    v.reg.reference_op = value.reg.reference_op + 1;
    v.kind = value.kind;
    return v;
}

Value ValueFromDereference(Program* program, Value value)
{
    Assert(value.kind == ValueKind_LValue || value.kind == ValueKind_Register);
    Assert(value.vtype.kind == VKind_Reference);
    
    Value v{};
    v.vtype = VTypeNext(program, value.vtype);
    v.reg.index = value.reg.index;
    v.reg.reference_op = value.reg.reference_op - 1;
    v.kind = value.kind;
    return v;
}

Value ValueFromInt(I64 value) {
    Value v{};
    v.vtype = VType_Int;
    v.kind = ValueKind_Literal;
    v.literal_int = value;
    return v;
}

Value ValueFromEnum(VType vtype, I64 value) {
    Value v{};
    v.vtype = vtype;
    v.kind = ValueKind_Literal;
    v.literal_int = value;
    return v;
}

Value ValueFromBool(B32 value) {
    Value v{};
    v.vtype = VType_Bool;
    v.kind = ValueKind_Literal;
    v.literal_bool = value;
    return v;
}

Value ValueFromString(Arena* arena, String value) {
    Value v{};
    v.vtype = VType_String;
    v.kind = ValueKind_Literal;
    v.literal_string = StrCopy(arena, value);
    return v;
}

Value ValueFromStringArray(Arena* arena, Program* program, Array<Value> values)
{
    B32 is_compiletime = true;
    
    foreach(i, values.count) {
        if (!TypeIsString(values[i].vtype) || !ValueIsCompiletime(values[i])) {
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
        v.vtype = VType_String;
        v.kind = ValueKind_StringComposition;
        v.string_composition = array_copy(arena, values);
        return v;
    }
}

Value ValueFromType(Program* program, VType type) {
    Value v{};
    v.vtype = VType_Type;
    v.kind = ValueKind_Literal;
    v.literal_type = type;
    return v;
}

Value ValueFromArray(Arena* arena, VType array_vtype, Array<Value> elements)
{
    Assert(array_vtype.kind == VKind_Array);
    Value v{};
    v.vtype = array_vtype;
    v.kind = ValueKind_Array;
    v.array.values = array_copy(arena, elements);
    v.array.is_empty = false;
    return v;
}

Value ValueFromEmptyArray(Arena* arena, VType base_vtype, Array<Value> dimensions)
{
    Assert(base_vtype.kind != VKind_Array);
    Value v{};
    v.vtype = vtype_from_dimension(base_vtype, dimensions.count);
    v.kind = ValueKind_Array;
    v.array.values = array_copy(arena, dimensions);
    v.array.is_empty = true;
    return v;
}

Value ValueFromZero(VType vtype)
{
    if (TypeIsInt(vtype)) {
        return ValueFromInt(0);
    }
    
    if (TypeIsBool(vtype)) {
        return ValueFromBool(false);
    }
    
    if (TypeIsAny(vtype)) {
        return ValueNull();
    }
    
    Value v{};
    v.vtype = vtype;
    v.kind = ValueKind_ZeroInit;
    return v;
}

Value ValueFromGlobal(Program* program, U32 global_index)
{
    Global* global = GlobalFromIndex(program, RegIndexFromGlobal(global_index));
    return ValueFromRegister(global_index, global->vtype, true);
}

Value ValueFromStringExpression(Arena* arena, String str, VType vtype)
{
    if (str.size <= 0) return ValueNone();
    // TODO(Jose): if (StrEquals(str, "null")) return value_null();
    
    if (TypeIsInt(vtype)) {
        I64 value;
        if (!I64FromString(str, &value)) return ValueNone();
        return ValueFromInt(value);
    }
    
    if (TypeIsBool(vtype)) {
        if (StrEquals(str, "true")) return ValueFromBool(true);
        if (StrEquals(str, "false")) return ValueFromBool(false);
        if (StrEquals(str, "1")) return ValueFromBool(true);
        if (StrEquals(str, "0")) return ValueFromBool(false);
        return ValueNone();
    }
    
    if (TypeIsString(vtype)) {
        return ValueFromString(arena, str);
    }
    
    if (TypeIsEnum(vtype))
    {
        U64 start_name = 0;
        if (str[0] == '.') {
            start_name = 1;
        }
        else if (StrStarts(str, StrFormat(context.arena, "%S.", vtype.base_name))) {
            start_name = vtype.base_name.size + 1;
        }
        
        String enum_name = StrSub(str, start_name, str.size - start_name);
        foreach(i, vtype._enum->names.count) {
            if (StrEquals(vtype._enum->names[i], enum_name)) return ValueFromEnum(vtype, i);
        }
        return ValueNone();
    }
    
    return ValueNone();
}

Value value_from_return(Arena* arena, Array<Value> values)
{
    if (values.count == 0) return ValueNone();
    if (values.count == 1) return values[0];
    
    Value v{};
    v.vtype = VType_Any;
    v.kind = ValueKind_MultipleReturn;
    v.multiple_return = array_copy(arena, values);
    return v;
}

Array<Value> ValuesFromReturn(Arena* arena, Value value, B32 empty_on_void)
{
    if (value.kind == ValueKind_MultipleReturn) return value.multiple_return;
    
    if (value.kind == ValueKind_None) {
        return {};
    }
    
    Array<Value> values = array_make<Value>(arena, 1);
    values[0] = value;
    return values;
}

String StrFromValue(Arena* arena, Program* program, Value value, B32 raw)
{
    if (value.kind == ValueKind_None) return "E";
    
    if (value.kind == ValueKind_Literal) {
        if (TypeIsInt(value.vtype)) return StrFormat(arena, "%l", value.literal_int);
        if (TypeIsBool(value.vtype)) return value.literal_bool ? "true" : "false";
        if (TypeIsString(value.vtype)) {
            if (raw) return value.literal_string;
            String escape = escape_string_from_raw_string(context.arena, value.literal_string);
            return StrFormat(arena, "\"%S\"", escape);
        }
        if (TypeIsVoid(value.vtype)) return "null";
        if (TypeEquals(program, value.vtype, VType_Type)) return VTypeGetName(program, value.literal_type);
        if (TypeIsEnum(value.vtype)) {
            I32 index = (I32)value.literal_int;
            if (index < 0 || index >= value.vtype._enum->names.count) return "?";
            return value.vtype._enum->names[index];
        }
        InvalidCodepath();
        return "";
    }
    
    if (value.kind == ValueKind_Array)
    {
        Array<Value> values = value.array.values;
        B32 is_empty = value.array.is_empty;
        if (values.count == 0) return "{ }";
        
        StringBuilder builder = string_builder_make(context.arena);
        
        if (is_empty)
        {
            append(&builder, "[");
            foreach(i, values.count) {
                append(&builder, StrFromValue(context.arena, program, values[i], false));
                if (i < values.count - 1) append(&builder, ", ");
            }
            appendf(&builder, "]->%S", VTypeGetName(program, TypeFromIndex(program, value.vtype.base_index)));
        }
        else
        {
            append(&builder, "{ ");
            foreach(i, values.count) {
                append(&builder, StrFromValue(context.arena, program, values[i], false));
                if (i < values.count - 1) append(&builder, ", ");
            }
            append(&builder, " }");
        }
        
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_StringComposition)
    {
        Assert(TypeIsString(value.vtype));
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
        return StrFormat(arena, "%S()", VTypeGetName(program, value.vtype));
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
    
    if (TypeIsString(value.vtype)) {
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
    
    if (!TypeIsBool(value.vtype)) {
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

VType TypeFromCompiletime(Program* program, Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return VType_Void;
    }
    
    if (!TypeEquals(program, value.vtype, VType_Type)) {
        InvalidCodepath();
        return VType_Void;
    }
    
    if (value.kind == ValueKind_Literal) {
        return value.literal_type;
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return VType_Void;
    }
    
    InvalidCodepath();
    return VType_Void;
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

internal_fn String StringFromUnitInfo(Arena* arena, Program* program, Unit unit)
{
    String dst = StringFromRegister(context.arena, program, unit.dst_index);
    
    if (unit.kind == UnitKind_Error) return {};
    
    if (unit.kind == UnitKind_Copy)
    {
        String src = StrFromValue(context.arena, program, unit.src);
        return StrFormat(arena, "%S = %S", dst, src);
    }
    
    if (unit.kind == UnitKind_Store)
    {
        String src = StrFromValue(context.arena, program, unit.src);
        return StrFormat(arena, "%S = %S", dst, src);
    }
    
    if (unit.kind == UnitKind_FunctionCall)
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
    
    if (unit.kind == UnitKind_Return) return "";
    
    if (unit.kind == UnitKind_Jump)
    {
        StringBuilder builder = string_builder_make(context.arena);
        String condition = StrFromValue(context.arena, program, unit.src);
        if (unit.jump.condition > 0) appendf(&builder, "%S ", condition);
        else if (unit.jump.condition < 0) appendf(&builder, "!%S ", condition);
        appendf(&builder, "%i", unit.jump.offset);
        return string_from_builder(arena, &builder);
    }
    
    if (unit.kind == UnitKind_BinaryOperation)
    {
        String op = StringFromBinaryOperator(unit.binary_op.op);
        String src0 = StrFromValue(context.arena, program, unit.src);
        String src1 = StrFromValue(context.arena, program, unit.binary_op.src1);
        return StrFormat(arena, "%S = %S %S %S", dst, src0, op, src1);
    }
    
    if (unit.kind == UnitKind_SignOperation)
    {
        String op = StringFromBinaryOperator(unit.sign_op.op);
        String src = StrFromValue(context.arena, program, unit.src);
        return StrFormat(arena, "%S = %S%S", dst, op, src);
    }
    
    if (unit.kind == UnitKind_Child)
    {
        Value src = unit.src;
        Value index = unit.child.child_index;
        B32 is_member = unit.child.child_is_member;
        B32 is_literal_int = index.kind == ValueKind_Literal && TypeIsInt(index.vtype);
        
        String op = {};
        
        if (is_member && src.vtype.kind == VKind_Struct && is_literal_int) {
            op = src.vtype._struct->names[index.literal_int];
            op = StrFormat(context.arena, ".%S", op);
        }
        else if (!is_member && is_literal_int) {
            Array<VariableTypeChild> props = VTypeGetProperties(program, src.vtype);
            op = StrFormat(context.arena, ".%S", props[index.literal_int].name);
        }
        else {
            String index = StrFromValue(context.arena, program, unit.child.child_index);
            op = StrFormat(context.arena, "[%S]", index);
        }
        
        String src_str = StrFromValue(context.arena, program, unit.src);
        return StrFormat(arena, "%S = %S%S", dst, src_str, op);
    }
    
    if (unit.kind == UnitKind_ResultEval) {
        return StrFromValue(arena, program, unit.src);
    }
    
    InvalidCodepath();
    return {};
}

String StringFromUnitKind(Arena* arena, UnitKind unit)
{
    if (unit == UnitKind_Error) return "error";
    if (unit == UnitKind_Copy) return "copy";
    if (unit == UnitKind_Store) return "store";
    if (unit == UnitKind_FunctionCall) return "call";
    if (unit == UnitKind_Return) return "return";
    if (unit == UnitKind_Jump) return "jump";
    if (unit == UnitKind_BinaryOperation) return "bin_op";
    if (unit == UnitKind_SignOperation) return "sgn_op";
    if (unit == UnitKind_Child) return "child";
    if (unit == UnitKind_ResultEval) return "res_ev";
    if (unit == UnitKind_Empty) return "empty";
    
    InvalidCodepath();
    return "?";
}

#if DEV

String StringFromUnit(Arena* arena, Program* program, U32 index, U32 index_digits, U32 line_digits, Unit unit)
{
    StringBuilder builder = string_builder_make(context.arena);
    
    String index_str = StrFormat(context.arena, "%u", index);
#if DEV_LOCATION_INFO
    String line_str = StrFormat(context.arena, "%u", unit.location.info.line);
#else
    String line_str = "";
#endif
    
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
        
        PrintEx(PrintLevel_DevLog, "%S: %S", StringFromRegister(context.arena, program, RegIndexFromLocal(program, i)), VTypeGetName(program, reg.vtype));
        PrintEx(PrintLevel_DevLog, "\n");
    }
    
    PrintEx(PrintLevel_DevLog, SEPARATOR_STRING "\n");
}

#endif