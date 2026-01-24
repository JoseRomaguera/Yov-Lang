#include "inc.h"

Object _make_object(VType vtype) {
    Object obj{};
    obj.vtype = vtype;
    return obj;
}

read_only Object _nil_obj = _make_object(VType_Nil);
read_only Object _null_obj = _make_object(VType_Void);

Object* nil_obj = &_nil_obj;
Object* null_obj = &_null_obj;

void YovInitializeTypesTable()
{
    VType primitives[] = {
        VType_Int,
        VType_Bool,
        VType_String,
        
        MakePrimitive("I64", PrimitiveType_I64, -1),
    };
    
    U32 vtype_count = countof(primitives);
    vtype_count += yov->struct_count;
    vtype_count += yov->enum_count;
    
    yov->vtypes = array_make<VType>(yov->arena, vtype_count);
    
    U32 index = 0;
    foreach(i, countof(primitives)) {
        yov->vtypes[index] = primitives[i];
        yov->vtypes[index].base_index = index;
        index++;
    }
    
    foreach(i, yov->definitions.count)
    {
        DefinitionHeader* header = &yov->definitions[i].header;
        
        if (header->type == DefinitionType_Struct)
        {
            StructDefinition* def = &yov->definitions[i]._struct;
            
            VType vtype = {};
            vtype.base_name = def->identifier;
            vtype.kind = VKind_Struct;
            vtype._struct = def;
            vtype.base_index = index;
            
            yov->vtypes[index++] = vtype;
        }
        else if (header->type == DefinitionType_Enum)
        {
            EnumDefinition* def = &yov->definitions[i]._enum;
            
            VType vtype = {};
            vtype.base_name = def->identifier;
            vtype.kind = VKind_Enum;
            vtype._enum = def;
            vtype.base_index = index;
            
            yov->vtypes[index++] = vtype;
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

B32 vtype_is_enum(VType vtype) { return vtype.kind == VKind_Enum; }
B32 vtype_is_array(VType vtype) { return vtype.kind == VKind_Array; }
B32 vtype_is_struct(VType vtype) { return vtype.kind == VKind_Struct; }
B32 vtype_is_reference(VType vtype) { return vtype.kind == VKind_Reference; }

B32 vtype_equals(VType v0, VType v1) {
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
        return v0.array_dimensions == v1.array_dimensions && vtype_equals(vtype_from_index(v0.base_index), vtype_from_index(v1.base_index));
    }
    
    if (v0.kind == VKind_Reference) {
        return v0.array_dimensions == v1.array_dimensions && vtype_equals(vtype_from_index(v0.base_index), vtype_from_index(v1.base_index));;
    }
    
    InvalidCodepath();
    return false;
}

B32 VTypeValid(VType vtype) {
    return vtype.kind > VKind_Any;
}

VType VTypeNext(VType vtype)
{
    if (vtype.kind == VKind_Array) {
        if (vtype.array_dimensions == 1) return vtype_from_index(vtype.base_index);
        vtype.array_dimensions--;
        return vtype;
    }
    
    if (vtype.kind == VKind_Reference) {
        VType base_vtype = vtype_from_index(vtype.base_index);
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

B32 VTypeNeedsInternalRelease(VType vtype)
{
    if (vtype_is_array(vtype)) return true;
    if (vtype_is_reference(vtype)) return true;
    if (vtype == VType_String) return true;
    
    if (vtype_is_struct(vtype)) {
        Assert(vtype._struct->stage == DefinitionStage_Ready);
        return vtype._struct->needs_internal_release;
    }
    
    return false;
}

String VTypeGetName(VType vtype)
{
    if (vtype.kind == VKind_Array)
    {
        U64 needed_size = vtype.base_name.size + (vtype.array_dimensions * 2);
        String name = StringAlloc(context.arena, needed_size);
        MemoryCopy(name.data, vtype.base_name.data, vtype.base_name.size);
        
        foreach(i, vtype.array_dimensions) {
            U64 offset = vtype.base_name.size + i * 2;
            name[offset + 0] = '[';
            name[offset + 1] = ']';
        }
    }
    if (vtype.kind == VKind_Reference) {
        String next = VTypeGetName(VTypeNext(vtype));
        return StrFormat(context.arena, "%S&", next);
    }
    
    return vtype.base_name;
}

VType vtype_from_index(U32 index) {
    if (index >= yov->vtypes.count) return VType_Nil;
    return yov->vtypes[index];
}

VType vtype_from_name(String name)
{
    if (name == "Any") return VType_Any;
    if (name == "void") return VType_Void;
    
    U32 dimensions = 0;
    while (name.size > 2 && name[name.size - 1] == ']' && name[name.size - 2] == '[') {
        name = StrSub(name, 0, name.size - 2);
        dimensions++;
    }
    
    foreach(i, yov->vtypes.count) {
        if (yov->vtypes[i].base_name == name) {
            return vtype_from_dimension(yov->vtypes[i], dimensions);
        }
    }
    return VType_Nil;
}

VType vtype_get_child_at(VType vtype, U32 index, B32 is_member)
{
    if (is_member)
    {
        if (vtype.kind == VKind_Array) {
            return VTypeNext(vtype);
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

VariableTypeChild vtype_get_child(VType vtype, String name)
{
    VariableTypeChild info = vtype_get_member(vtype, name);
    if (info.index >= 0) return info;
    return VTypeGetProperty(vtype, name);
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

VariableTypeChild VTypeGetProperty(VType vtype, String property)
{
    Array<VariableTypeChild> props = VTypeGetProperties(vtype);
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

Array<VariableTypeChild> VTypeGetProperties(VType vtype)
{
    if (vtype == VType_String) {
        return arrayof(string_properties);
    }
    
    if (vtype_is_array(vtype)) {
        return arrayof(array_properties);
    }
    
    if (vtype_is_enum(vtype)) {
        return arrayof(enum_properties);
    }
    
    return {};
}

VType vtype_from_binary_operation(VType left, VType right, BinaryOperator op)
{
    if (left.kind == VKind_Reference && left == right)
    {
        if (op == BinaryOperator_Equals) return VType_Bool;
        if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (left == VType_Int && right == VType_Int) {
        if (binary_operator_is_arithmetic(op)) return VType_Int;
        
        if (op == BinaryOperator_Equals) return VType_Bool;
        if (op == BinaryOperator_NotEquals) return VType_Bool;
        if (op == BinaryOperator_LessThan) return VType_Bool;
        if (op == BinaryOperator_LessEqualsThan) return VType_Bool;
        if (op == BinaryOperator_GreaterThan) return VType_Bool;
        if (op == BinaryOperator_GreaterEqualsThan) return VType_Bool;
    }
    
    if (left == VType_Bool && right == VType_Bool) {
        if (op == BinaryOperator_LogicalOr) return VType_Bool;
        if (op == BinaryOperator_LogicalAnd) return VType_Bool;
        if (op == BinaryOperator_Equals) return VType_Bool;
        if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (left == VType_String && right == VType_String)
    {
        if (op == BinaryOperator_Addition) return VType_String;
        if (op == BinaryOperator_Division) return VType_String;
        if (op == BinaryOperator_Equals) return VType_Bool;
        if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (left == VType_Type && right == VType_Type)
    {
        if (op == BinaryOperator_Equals) return VType_Bool;
        else if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (right == VType_Type) {
        if (op == BinaryOperator_Is) return VType_Bool;
    }
    
    if ((left == VType_String && right == VType_Int) || (left == VType_Int && right == VType_String))
    {
        if (op == BinaryOperator_Addition) return VType_String;
    }
    
    if (left.kind == VKind_Enum && right.kind == VKind_Enum) {
        if (op == BinaryOperator_Equals) return VType_Bool;
        else if (op == BinaryOperator_NotEquals) return VType_Bool;
    }
    
    if (left.kind == VKind_Array && right.kind == VKind_Array && VTypeNext(left) == VTypeNext(right)) {
        if (op == BinaryOperator_Addition) return left;
    }
    
    if ((left.kind == VKind_Array && right.kind != VKind_Array) || (left.kind != VKind_Array && right.kind == VKind_Array))
    {
        VType array_type = (left.kind == VKind_Array) ? left : right;
        VType element_type = (left.kind == VKind_Array) ? right : left;
        
        if (VTypeNext(array_type) == element_type) {
            if (op == BinaryOperator_Addition) return array_type;
        }
    }
    
    return VType_Nil;
}

VType vtype_from_sign_operation(VType src, BinaryOperator op)
{
    if (src == VType_Int) {
        if (op == BinaryOperator_Addition) return VType_Int;
        if (op == BinaryOperator_Substraction) return VType_Int;
    }
    
    if (src == VType_Bool) {
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

void DefinitionIdentify(U32 index, DefinitionType type, String identifier, Location location)
{
    if (index >= yov->definitions.count) {
        InvalidCodepath();
        return;
    }
    
    DefinitionHeader* def = &yov->definitions[index].header;
    
    if (def->stage != DefinitionStage_None) {
        InvalidCodepath();
        return;
    }
    
    def->type = type;
    def->identifier = StrCopy(yov->arena, identifier);
    def->location = location;
    def->stage = DefinitionStage_Identified;
    
    LogType("%S Identify: %S", StringFromDefinitionType(type), identifier);
}

void EnumDefine(EnumDefinition* def, Array<String> names, Array<Location> expression_locations)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    def->names = array_copy(yov->arena, names);
    def->expression_locations = array_copy(yov->arena, expression_locations);
    def->stage = DefinitionStage_Defined;
    
    foreach(i, names.count) {
        LogType("Enum Define: %S[%u] -> %S", def->identifier, i, names[i]);
    }
}

void EnumResolve(EnumDefinition* def, Array<I64> values)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    if (values.count == 0) {
        values = array_make<I64>(context.arena, def->names.count);
        foreach(i, values.count) values[i] = i;
    }
    
    def->values = array_copy(yov->arena, values);
    Assert(def->names.count == def->values.count);
    def->stage = DefinitionStage_Ready;
    
    foreach(i, def->names.count) {
        LogType("Enum Resolve: %S.%S = %i", def->identifier, def->names[i], (I32)def->values[i]);
    }
}

void StructDefine(StructDefinition* def, Array<ObjectDefinition> members)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    Array<String> names = array_make<String>(yov->arena, members.count);
    Array<VType> vtypes = array_make<VType>(yov->arena, members.count);
    
    foreach(i, members.count)
    {
        ObjectDefinition member = members[i];
        
        names[i] = StrCopy(yov->arena, member.name);
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
        LogType("Struct Define: %S -> %S: %S", def->identifier, def->names[i], VTypeGetName(def->vtypes[i]));
    }
}

void StructResolve(StructDefinition* def)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    // TODO(Jose): Memory alignment
    
    Array<U32> offsets = array_make<U32>(yov->arena, def->vtypes.count);
    
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
        needs_internal_release |= VTypeNeedsInternalRelease(vtype);
    }
    
    def->needs_internal_release = needs_internal_release;
    def->offsets = offsets;
    def->size = size;
    def->stage = DefinitionStage_Ready;
    
    LogType("Struct Resolve: %S -> %u bytes", def->identifier, def->size);
}

void FunctionDefine(FunctionDefinition* def, Array<ObjectDefinition> parameters, Array<ObjectDefinition> returns)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    def->parameters = array_copy(yov->arena, parameters);
    def->returns = array_copy(yov->arena, returns);
    def->stage = DefinitionStage_Defined;
    
    StringBuilder builder = string_builder_make(context.arena);
    appendf(&builder, "Function Define: %S (", def->identifier);
    
    foreach(i, parameters.count) {
        appendf(&builder, "%S: %S", parameters[i].name, VTypeGetName(parameters[i].vtype));
        if (i + 1 < parameters.count)
            append(&builder, ", ");
    }
    
    append(&builder, ") -> (");
    
    foreach(i, returns.count) {
        appendf(&builder, "%S: %S", returns[i].name, VTypeGetName(returns[i].vtype));
        if (i + 1 < returns.count)
            append(&builder, ", ");
    }
    
    append(&builder, ")");
    
    LogType(string_from_builder(context.arena, &builder));
}

void FunctionResolveIntrinsic(FunctionDefinition* def, IntrinsicFunction* fn)
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

void FunctionResolve(FunctionDefinition* def, IR ir)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    def->defined.ir = ir;
    def->stage = DefinitionStage_Ready;
    
    LogType("Function Resolve: %S", def->identifier);
}

void ArgDefine(ArgDefinition* def, VType vtype)
{
    if (def->stage != DefinitionStage_Identified) {
        InvalidCodepath();
        return;
    }
    
    def->vtype = vtype;
    def->stage = DefinitionStage_Defined;
    
    LogType("Arg Define: %S", def->identifier);
}

void ArgResolve(ArgDefinition* def, String name, String description, B32 required, Value default_value)
{
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    def->name = StrCopy(yov->arena, name);
    def->description = StrCopy(yov->arena, description);
    def->required = required;
    def->default_value = default_value;
    def->stage = DefinitionStage_Ready;
    
    LogType("Arg Resolve: %S", def->identifier);
}

Definition* DefinitionFromIdentifier(String identifier)
{
    foreach(i, yov->definitions.count) {
        Definition* def = &yov->definitions[i];
        if (StrEquals(def->header.identifier, identifier)) {
            return def;
        }
    }
    return NULL;
}

Definition* DefinitionFromIndex(U32 index)
{
    if (index >= yov->definitions.count) return NULL;
    return &yov->definitions[index];
}

B32 DefinitionExists(String identifier)
{
    return DefinitionFromIdentifier(identifier) != NULL;
}

StructDefinition* StructFromIdentifier(String identifier)
{
    Definition* def = DefinitionFromIdentifier(identifier);
    if (def == NULL || def->header.type != DefinitionType_Struct) return NULL;
    return &def->_struct;
}

StructDefinition* StructFromIndex(U32 index)
{
    Definition* def = DefinitionFromIndex(index);
    if (def == NULL || def->header.type != DefinitionType_Struct) return NULL;
    return &def->_struct;
}

EnumDefinition* EnumFromIdentifier(String identifier)
{
    Definition* def = DefinitionFromIdentifier(identifier);
    if (def == NULL || def->header.type != DefinitionType_Enum) return NULL;
    return &def->_enum;
}

EnumDefinition* EnumFromIndex(U32 index)
{
    Definition* def = DefinitionFromIndex(index);
    if (def == NULL || def->header.type != DefinitionType_Enum) return NULL;
    return &def->_enum;
}

FunctionDefinition* FunctionFromIdentifier(String identifier)
{
    Definition* def = DefinitionFromIdentifier(identifier);
    if (def == NULL || def->header.type != DefinitionType_Function) return NULL;
    return &def->function;
}

FunctionDefinition* FunctionFromIndex(U32 index)
{
    Definition* def = DefinitionFromIndex(index);
    if (def == NULL || def->header.type != DefinitionType_Function) return NULL;
    return &def->function;
}

ArgDefinition* ArgFromIndex(U32 index)
{
    Definition* def = DefinitionFromIndex(index);
    if (def == NULL || def->header.type != DefinitionType_Arg) return NULL;
    return &def->arg;
}

ArgDefinition* ArgFromName(String name)
{
    foreach(i, yov->definitions.count) {
        ArgDefinition* def = &yov->definitions[i].arg;
        if (def->type != DefinitionType_Arg) continue;
        if (StrEquals(def->name, name)) return def;
    }
    return NULL;
}

void GlobalDefine(U32 index, VType vtype, B32 is_constant)
{
    Global* global = GlobalFromIndex(index);
    Assert(global->vtype == VType_Void);
    Assert(VTypeValid(vtype));
    global->vtype = vtype;
    global->is_constant = is_constant;
}

I32 GlobalIndexFromIdentifier(String identifier)
{
    foreach(i, yov->globals.count) {
        Global* global = &yov->globals[i];
        if (global->identifier == identifier) return i;
    }
    return -1;
}

Global* GlobalFromIdentifier(String identifier)
{
    I32 index = GlobalIndexFromIdentifier(identifier);
    if (index < 0) return NULL;
    return GlobalFromIndex(index);
}

Global* GlobalFromIndex(U32 index) {
    if (index >= yov->globals.count) {
        InvalidCodepath();
        return NULL;
    }
    return &yov->globals[index];
}

Global* GlobalFromRegisterIndex(I32 index) {
    U32 global_index = index;
    if (global_index >= yov->globals.count) {
        return NULL;
    }
    return &yov->globals[global_index];
}

ExpresionContext ExpresionContext_from_void() {
    ExpresionContext ctx{};
    ctx.vtype = VType_Void;
    ctx.assignment_count = 0;
    return ctx;
}

ExpresionContext ExpresionContext_from_inference(U32 assignment_count) {
    ExpresionContext ctx{};
    ctx.vtype = VType_Any;
    ctx.assignment_count = assignment_count;
    return ctx;
}

ExpresionContext ExpresionContext_from_vtype(VType vtype, U32 assignment_count) {
    ExpresionContext ctx{};
    ctx.vtype = vtype;
    ctx.assignment_count = assignment_count;
    return ctx;
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
    return value.kind == ValueKind_Literal && value.vtype == VType_Void;
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
    
    if (src.kind == ValueKind_Literal && src.vtype == VType_String) {
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

Value ValueFromIrObject(IR_Object* object)
{
    if (object->register_index < 0) return ValueNone();
    return ValueFromRegister(object->register_index, object->vtype, true);
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

Value ValueFromDereference(Value value)
{
    Assert(value.kind == ValueKind_LValue || value.kind == ValueKind_Register);
    Assert(value.vtype.kind == VKind_Reference);
    
    Value v{};
    v.vtype = VTypeNext(value.vtype);
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

Value ValueFromStringArray(Arena* arena, Array<Value> values)
{
    B32 is_compiletime = true;
    
    foreach(i, values.count) {
        if (values[i].vtype != VType_String || !ValueIsCompiletime(values[i])) {
            is_compiletime = false;
            break;
        }
    }
    
    if (is_compiletime)
    {
        StringBuilder builder = string_builder_make(context.arena);
        foreach(i, values.count) {
            String str = StringFromCompiletime(context.arena, values[i]);
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

Value ValueFromType(VType type) {
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
    if (vtype == VType_Int) {
        return ValueFromInt(0);
    }
    
    if (vtype == VType_Bool) {
        return ValueFromBool(false);
    }
    
    if (vtype == VType_Any) {
        return ValueNull();
    }
    
    Value v{};
    v.vtype = vtype;
    v.kind = ValueKind_ZeroInit;
    return v;
}

Value ValueFromGlobal(U32 global_index)
{
    Global* global = GlobalFromIndex(RegIndexFromGlobal(global_index));
    return ValueFromRegister(global_index, global->vtype, true);
}

Value ValueFromStringExpression(Arena* arena, String str, VType vtype)
{
    if (str.size <= 0) return ValueNone();
    // TODO(Jose): if (StrEquals(str, "null")) return value_null();
    
    if (vtype == VType_Int) {
        I64 value;
        if (!i64_from_string(str, &value)) return ValueNone();
        return ValueFromInt(value);
    }
    
    if (vtype == VType_Bool) {
        if (StrEquals(str, "true")) return ValueFromBool(true);
        if (StrEquals(str, "false")) return ValueFromBool(false);
        if (StrEquals(str, "1")) return ValueFromBool(true);
        if (StrEquals(str, "0")) return ValueFromBool(false);
        return ValueNone();
    }
    
    if (vtype == VType_String) {
        return ValueFromString(arena, str);
    }
    
    if (vtype_is_enum(vtype))
    {
        U64 start_name = 0;
        if (str[0] == '.') {
            start_name = 1;
        }
        else if (string_starts(str, StrFormat(context.arena, "%S.", vtype.base_name))) {
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
    
    if (value.vtype == VType_Void) {
        return {};
    }
    
    Array<Value> values = array_make<Value>(arena, 1);
    values[0] = value;
    return values;
}

String StrFromValue(Arena* arena, Value value, B32 raw)
{
    if (value.kind == ValueKind_None) return "E";
    
    if (value.kind == ValueKind_Literal) {
        if (value.vtype == VType_Int) return StrFormat(arena, "%l", value.literal_int);
        if (value.vtype == VType_Bool) return value.literal_bool ? "true" : "false";
        if (value.vtype == VType_String) {
            if (raw) return value.literal_string;
            String escape = escape_string_from_raw_string(context.arena, value.literal_string);
            return StrFormat(arena, "\"%S\"", escape);
        }
        if (value.vtype == VType_Void) return "null";
        if (value.vtype == VType_Type) return VTypeGetName(value.literal_type);
        if (vtype_is_enum(value.vtype)) {
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
                append(&builder, StrFromValue(context.arena, values[i], false));
                if (i < values.count - 1) append(&builder, ", ");
            }
            appendf(&builder, "]->%S", VTypeGetName(vtype_from_index(value.vtype.base_index)));
        }
        else
        {
            append(&builder, "{ ");
            foreach(i, values.count) {
                append(&builder, StrFromValue(context.arena, values[i], false));
                if (i < values.count - 1) append(&builder, ", ");
            }
            append(&builder, " }");
        }
        
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_StringComposition)
    {
        Assert(value.vtype == VType_String);
        Array<Value> values = value.string_composition;
        StringBuilder builder = string_builder_make(context.arena);
        foreach(i, values.count) {
            String src = StrFromValue(context.arena, values[i]);
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
            String src = StrFromValue(context.arena, values[i]);
            appendf(&builder, "%S", src);
            if (i < values.count - 1) append(&builder, ", ");
        }
        append(&builder, ")");
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return StrFormat(arena, "%S()", VTypeGetName(value.vtype));
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
        
        return StrFormat(arena, "%S%S", ref_op, StringFromRegister(context.arena, value.reg.index));
    }
    
    InvalidCodepath();
    return "";
}
