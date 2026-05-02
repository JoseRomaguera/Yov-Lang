#include "front.h"

IR_Unit* IRUnitAlloc(IR_Context* ir, UnitKind kind, Location location)
{
    PROFILE_FUNCTION;
    IR_Unit* unit = ArenaPushStruct<IR_Unit>(ir->arena);
    unit->src0 = ValueNone();
    unit->src1 = ValueNone();
    unit->kind = kind;
    unit->location = location;
    unit->dst_index = -1;
    return unit;
}

IR_Unit* IRUnitAlloc_Empty(IR_Context* ir, Location location) {
    return IRUnitAlloc(ir, UnitKind_Empty, location);
}

IR_Unit* IRUnitAlloc_Jump(IR_Context* ir, I32 condition, Value src, IR_Unit* jump_to_unit, Location location)
{
    Assert(jump_to_unit != NULL);
    IR_Unit* unit = IRUnitAlloc(ir, UnitKind_Jump, location);
    unit->src0 = src;
    unit->jump.condition = condition;
    unit->jump.unit = jump_to_unit;
    return unit;
}

Value ValueFromIrObject(IR_Object* object)
{
    if (object->register_index < 0) return ValueNone();
    return ValueFromRegister(object->register_index, object->type, true);
}

IR_Group IRFailed()
{
    IR_Group out{};
    out.success = false;
    out.unit_count = 0;
    out.first = NULL;
    out.last = NULL;
    out.value = ValueNone();
    return out;
}

IR_Group IRFromNone(Value value)
{
    IR_Group out{};
    out.success = true;
    out.unit_count = 0;
    out.first = NULL;
    out.last = NULL;
    out.value = value;
    return out;
}

IR_Group IRFromSingle(IR_Unit* unit, Value value)
{
    Assert(unit->next == NULL && unit->prev == NULL);
    IR_Group out{};
    out.success = true;
    out.value = value;
    out.unit_count = 1;
    out.first = unit;
    out.last = unit;
    return out;
}
#if DEV
internal_fn void DEV_validate_IR_Group(IR_Group out)
{
    PROFILE_FUNCTION;
    U32 count = 0;
    IR_Unit* unit = out.first;
    while (unit != NULL) {
        Assert(unit->next != NULL || unit == out.last);
        unit = unit->next;
        count++;
    }
    Assert(out.unit_count == count);
}
#endif

IR_Group IRAppend(IR_Group o0, IR_Group o1)
{
    PROFILE_FUNCTION;
    
    IR_Group out = {};
    out.success = o0.success && o1.success;
    if (!out.success) return IRFailed();
    
#if DEV
    DEV_validate_IR_Group(o0);
    DEV_validate_IR_Group(o1);
#endif
    
    if (o0.unit_count > 0) {
        out.first = o0.first;
    }
    else if (o1.unit_count > 0) {
        out.first = o1.first;
    }
    
    if (o1.unit_count > 0) {
        out.last = o1.last;
    }
    else if (o0.unit_count > 0) {
        out.last = o0.last;
    }
    
    if (o0.first != NULL) o0.first->prev = NULL;
    if (o0.last != NULL) o0.last->next = o1.first;
    if (o1.first != NULL) o1.first->prev = o0.last;
    if (o1.last != NULL) o1.last->next = NULL;
    
    out.unit_count = o0.unit_count + o1.unit_count;
    out.value = o1.value;
    
#if DEV
    DEV_validate_IR_Group(out);
#endif
    return out;
}

IR_Group IRAppend3(IR_Group o0, IR_Group o1, IR_Group o2) {
    IR_Group out = IRAppend(o0, o1);
    return IRAppend(out, o2);
}
IR_Group IRAppend4(IR_Group o0, IR_Group o1, IR_Group o2, IR_Group o3) {
    IR_Group out = IRAppend(o0, o1);
    out = IRAppend(out, o2);
    return IRAppend(out, o3);
}
inline_fn IR_Group ir_append_5(IR_Group o0, IR_Group o1, IR_Group o2, IR_Group o3, IR_Group o4) {
    IR_Group out = IRAppend(o0, o1);
    out = IRAppend(out, o2);
    out = IRAppend(out, o3);
    return IRAppend(out, o4);
}

internal_fn IR_Group ir_from_result_eval(IR_Context* ir, Value src, Location location)
{
    PROFILE_FUNCTION;
    IR_Unit* unit = IRUnitAlloc(ir, UnitKind_ResultEval, location);
    unit->src0 = src;
    return IRFromSingle(unit);
}

IR_Group IRFromDefineObject(IR_Context* ir, RegisterKind register_kind, String identifier, Type* type, B32 constant, Location location)
{
    PROFILE_FUNCTION;
    Assert(type == any_type || TypeIsValid(type));
    
    I32 register_index = IRRegisterAlloc(ir, type, register_kind, constant);
    
    IR_Object* object = ir_define_object(ir, identifier, type, ir->scope, register_index);
    Value value = ValueFromIrObject(object);
    return IRFromNone(value);
}

IR_Group IRFromDefineTemporal(IR_Context* ir, Type* type, Location location)
{
    PROFILE_FUNCTION;
    Assert(TypeIsValid(type));
    
    I32 register_index = IRRegisterAlloc(ir, type, RegisterKind_Local, false);
    return IRFromNone(ValueFromRegister(register_index, type, false));
}


IR_Group IRFromReference(IR_Context* ir, B32 expects_lvalue, Value value, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    if (TypeIsReference(value.type)) {
        //ReportErrorFront(location, "Can't take a reference of a reference");
        //return IRFailed();
        return IRFromNone(value);
    }
    
    Register reg = IRRegisterFromValue(ir, value);
    B32 is_constant = reg.is_constant;
    
    if (is_constant) {
        ReportErrorFront(location, "Can't reference a constant: {line}");
        return IRFailed();
    }
    
    if (expects_lvalue && value.kind != ValueKind_LValue) {
        report_ref_expects_lvalue(location);
        return IRFailed();
    }
    
    return IRFromNone(ValueFromReference(program, value));
}

IR_Group IRFromDereference(IR_Context* ir, Value value, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    if (value.type->kind != VKind_Reference) {
        InvalidCodepath();
        return IRFailed();
    }
    
    return IRFromNone(ValueFromDereference(program, value));
}

internal_fn Value ValueFromSymbol(IR_Context* ir, String identifier, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Symbol symbol = ir_find_symbol(ir, identifier);
    
    // Define globals
    if (symbol.kind == SymbolKind_None)
    {
        I32 global_index = GlobalIndexFromIdentifier(program, identifier);
        
        if (global_index >= 0)
        {
            Assert(TypeIsValid(GlobalFromIndex(program, global_index)->type));
            return ValueFromGlobal(program, global_index);
        }
    }
    
    if (symbol.kind == SymbolKind_Object)
    {
        IR_Object* object = symbol.object;
        return ValueFromIrObject(object);
    }
    else if (symbol.kind == SymbolKind_Type) {
        return ValueFromType(program, symbol.type);
    }
    
    return ValueNone();
}

IR_Group IRFromSymbol(IR_Context* ir, String identifier, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    Value value = ValueFromSymbol(ir, identifier, location);
    
    if (value.kind != ValueKind_None)
    {
        IR_Group out = IRFromNone(value);
        
        //if (TypeIsReference(value.type)) {
        //out = IRAppend(out, IRFromDereference(ir, out.value, location));
        //}
        
        return out;
    }
    
    report_symbol_not_found(location, identifier);
    return IRFailed();
}

IR_Group IRFromFunctionCall(IR_Context* ir, FunctionDefinition* fn, Array<Value> parameters, ExpresionContext expr_context, Location location)
{
    PROFILE_FUNCTION;
    
    Reporter* reporter = ir->reporter;
    Program* program = ir->program;
    
    IR_Group out = IRFromNone();
    
    Array<Value> params = ArrayAlloc<Value>(context.arena, parameters.count);
    
    foreach(i, parameters.count)
    {
        Type* expected_type = nil_type;
        if (i < fn->parameters.count) expected_type = fn->parameters[i].type;
        
        Value param = parameters[i];
        
        if (param.kind == ValueKind_LValue && TypeIsReference(param.type) && TypeGetNext(program, param.type) == expected_type) {
            param = ValueFromDereference(program, param);
        }
        
        params[i] = param;
    }
    
    if (params.count != fn->parameters.count) {
        report_function_expecting_parameters(location, fn->identifier, fn->parameters.count);
        return IRFailed();
    }
    
    // Check parameters
    foreach(i, params.count) {
        if (fn->parameters[i].type != any_type && fn->parameters[i].type != params[i].type) {
            report_function_wrong_parameter_type(location, fn->identifier, fn->parameters[i].type->name, i + 1);
            return IRFailed();
        }
    }
    
    //out = IRAppend(out, ir_from_function_call(ir, fn, params, node->location));
    {
        I32 first_register_index = -1;
        
        Array<Value> values = ArrayAlloc<Value>(context.arena, fn->returns.count);
        foreach(i, values.count) {
            Type* type = fn->returns[i].type;
            I32 register_index = IRRegisterAlloc(ir, type, RegisterKind_Local, false);
            if (i == 0) first_register_index = register_index;
            values[i] = ValueFromRegister(register_index, type, false);
        }
        
        IR_Unit* unit = IRUnitAlloc(ir, UnitKind_FunctionCall, location);
        unit->dst_index = first_register_index;
        unit->function_call.fn = fn;
        unit->function_call.parameters = ArrayCopy(ir->arena, params);
        
        out = IRAppend(out, IRFromSingle(unit, ValueFromReturn(ir->arena, values)));
    }
    
    Array<Value> returns = ValuesFromReturn(context.arena, out.value, false);
    
    for (U32 i = expr_context.assignment_count; i < returns.count; ++i) {
        if (returns[i].type == Type_Result)
            out = IRAppend(out, ir_from_result_eval(ir, returns[i], location));
    }
    
    returns.count = Min(returns.count, expr_context.assignment_count);
    Value return_value = ValueFromReturn(ir->arena, returns);
    
    out.value = return_value;
    
    return out;
}

IR_Group IRFromFunctionCallName(IR_Context* ir, String name, Array<Value> parameters, ExpresionContext context, Location location)
{
    Reporter* reporter = ir->reporter;
    
    FunctionDefinition* fn = FunctionFromIdentifier(ir->program, name);
    if (fn == NULL) {
        report_symbol_not_found(location, name);
        return IRFailed();
    }
    return IRFromFunctionCall(ir, fn, parameters, context, location);
}

IR_Group IRFromDefaultInitializer(IR_Context* ir, Type* type, Location location) {
    Assert(TypeIsValid(type));
    return IRFromNone(ValueFromZero(type));
}

IR_Group IRFromEmptyArray(IR_Context* ir, Type* base_type, Array<Value> dimensions, Location location)
{
    Program* program = ir->program;
    
    for (U32 i = 0; i < dimensions.count; i++) {
        Assert(dimensions[i].type == uint_type);
    }
    
    Array<Value> params = ArrayAlloc<Value>(context.arena, 2);
    params[0] = ValueFromType(program, base_type);
    params[1] = ValueFromArray(ir->arena, TypeFromArray(program, uint_type, 1), dimensions);
    
    Type* expected_type = TypeFromArray(program, base_type, dimensions.count);
    IR_Group out = IRFromFunctionCallName(ir, "ArrayMakeEmpty", params, ExpresionContext_from_type(expected_type, 1), location);
    if (!out.success) return IRFailed();
    
    Value dst = out.value;
    IR_Object* obj = ir_find_object_from_value(ir, dst);
    if (obj == NULL) {
        InvalidCodepath();
        return IRFailed();
    }
    
    ir_assume_object(ir, obj, expected_type);
    out.value = ValueFromIrObject(obj);
    return out;
}

IR_Group IRFromEmptyList(IR_Context* ir, Type* base_type, Array<Value> dimensions, Location location)
{
    InvalidCodepath();
    return IRFailed();
}

IR_Group IRFromStore(IR_Context* ir, Value dst, Value src, Location location)
{
    PROFILE_FUNCTION;
    
    Assert(dst.kind == ValueKind_LValue || dst.kind == ValueKind_Register);
    
    IR_Unit* unit = IRUnitAlloc(ir, UnitKind_Store, location);
    unit->dst_index = dst.reg.index;
    unit->src0 = src;
    
    return IRFromSingle(unit, dst);
}

IR_Group IRFromCopy(IR_Context* ir, Value dst, Value src, Location location)
{
    PROFILE_FUNCTION;
    
    Assert(dst.kind == ValueKind_LValue || dst.kind == ValueKind_Register);
    
    IR_Unit* unit = IRUnitAlloc(ir, UnitKind_Copy, location);
    unit->dst_index = dst.reg.index;
    unit->src0 = src;
    
    return IRFromSingle(unit, dst);
}

IR_Group IRFromAssignment(IR_Context* ir, B32 expects_lvalue, Value dst, Value src, OperatorKind op, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    if (expects_lvalue && dst.kind != ValueKind_LValue) {
        report_expr_expects_lvalue(location);
        return IRFailed();
    }
    
    IR_Group out = IRFromNone();
    
    if (op != OperatorKind_None)
    {
        out = IRAppend(out, IRFromBinaryOperator(ir, dst, src, op, true, location));
        if (!out.success) return IRFailed();
        src = out.value;
        
        if (ValueEquals(dst, src)) {
            return out;
        }
    }
    
    I32 mode = 0; // 0 -> Invalid; 1 -> Copy; 2 -> Store
    
    if (dst.type == src.type) {
        mode = 1;
    }
    else if (TypeIsReference(dst.type) && TypeGetNext(program, dst.type) == src.type) {
        out = IRAppend(out, IRFromDereference(ir, dst, location));
        dst = out.value;
        mode = 1;
    }
    else if (TypeIsReference(src.type) && TypeGetNext(program, src.type) == dst.type) {
        out = IRAppend(out, IRFromDereference(ir, src, location));
        src = out.value;
        mode = 1;
    }
    else if (dst.type->kind == VKind_Reference && ValueIsNull(src)) {
        mode = 2;
    }
    else
    {
        IR_Object* dst_object = ir_find_object_from_value(ir, dst);
        
        if (dst_object != NULL)
        {
            Register reg = IRRegisterGet(ir, dst_object->register_index);
            
            if (RegisterIsValid(reg) && reg.type == any_type) {
                dst_object->type = src.type;
                dst = ValueFromIrObject(dst_object);
                mode = 2;
            }
        }
    }
    
    if (mode == 0)
    {
        report_type_missmatch_assign(location, src.type->name, dst.type->name);
        return IRFailed();
    }
    
    IR_Object* obj = ir_find_object_from_value(ir, dst);
    if (obj != NULL) obj->assignment_count++;
    
    if (dst.type == any_type) {
        mode = 2;
    }
    
    IR_Unit* unit;
    if (mode == 1)
    {
        unit = IRUnitAlloc(ir, UnitKind_Copy, location);
        unit->dst_index = dst.reg.index;
        unit->src0 = src;
    }
    else
    {
        unit = IRUnitAlloc(ir, UnitKind_Store, location);
        unit->dst_index = dst.reg.index;
        unit->src0 = src;
    }
    
    return IRAppend(out, IRFromSingle(unit));
}

IR_Group IRFromMultipleAssignment(IR_Context* ir, B32 expects_lvalue, Array<Value> destinations, Value src, OperatorKind op, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    if (destinations.count == 0) return IRFailed();
    if (src.kind == ValueKind_None) return IRFailed();
    
    IR_Group out = IRFromNone();
    
    Array<Value> sources = ValuesFromReturn(context.arena, src, false);
    
    if (sources.count == 1)
    {
        foreach(i, destinations.count) {
            out = IRAppend(out, IRFromAssignment(ir, expects_lvalue, destinations[i], sources[0], op, location));
        }
    }
    else
    {
        if (destinations.count > sources.count) {
            ReportErrorFront(location, "The number of destinations is greater than the number of sources");
            return IRFailed();
        }
        
        foreach(i, destinations.count) {
            out = IRAppend(out, IRFromAssignment(ir, expects_lvalue, destinations[i], sources[i], op, location));
        }
        
        for (U32 i = destinations.count; i < sources.count; ++i) {
            if (sources[i].type == Type_Result) {
                out = IRAppend(out, ir_from_result_eval(ir, sources[i], location));
            }
        }
    }
    
    return out;
}

IR_Group IRFromOp(IR_Context* ir, UnitKind kind, Type* dst_type, Value src0, Value src1, Location location)
{
    PROFILE_FUNCTION;
    
    Assert(dst_type->kind == VKind_Primitive);
    Value dst = ValueFromRegister(IRRegisterAlloc(ir, dst_type, RegisterKind_Local, false), dst_type, false);
    
    IR_Unit* unit = IRUnitAlloc(ir, kind, location);
    unit->dst_index = dst.reg.index;
    unit->src0 = src0;
    unit->src1 = src1;
    unit->op_dst_type = dst_type->primitive;
    return IRFromSingle(unit, dst);
}

internal_fn IR_Group IRFromArrayAppend(IR_Context* ir, Value array, Value src, B32 reuse_array, B32 front, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Type* element_type = TypeGetNext(program, array.type);
    B32 src_is_element = !TypeIsArray(src.type) || element_type == src.type;
    
    Assert(TypeIsArray(array.type));
    
    ExpresionContext expr_ctx = ExpresionContext_from_inference(1);
    IR_Group out = IRFromNone();
    
    if (!reuse_array)
    {
        out = IRAppend(out, IRFromDefineTemporal(ir, array.type, location));
        Value dst = out.value;
        out = IRAppend(out, IRFromStore(ir, dst, ValueFromZero(dst.type), location));
        out = IRAppend(out, IRFromCopy(ir, dst, array, location));
        array = out.value;
    }
    
    out = IRAppend(out, IRFromReference(ir, false, array, location));
    array = out.value;
    
    String call = {};
    if (front && src_is_element) call = "ArrayAppendElementFront";
    else if (!front && src_is_element) call = "ArrayAppendElementBack";
    else if (front && !src_is_element) call = "ArrayAppendFront";
    else if (!front && !src_is_element) call = "ArrayAppendBack";
    else {
        InvalidCodepath();
        return IRFailed();
    }
    
    Array<Value> params = ArrayAlloc<Value>(context.arena, 2);
    params[0] = array;
    params[1] = src;
    out = IRAppend(out, IRFromFunctionCallName(ir, call, params, expr_ctx, location));
    out = IRAppend(out, IRFromDereference(ir, array, location));
    return out;
}

IR_Group IRFromBinaryOperator(IR_Context* ir, Value left, Value right, OperatorKind op, B32 reuse_left, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    IR_Group out = IRFromNone();
    
    if (left.type->kind == VKind_Reference)
    {
        reuse_left = false;
        out = IRAppend(out, IRFromDereference(ir, left, location));
        left = out.value;
    }
    
    if (right.type->kind == VKind_Reference)
    {
        out = IRAppend(out, IRFromDereference(ir, right, location));
        right = out.value;
    }
    
    if (left.type->kind == VKind_Primitive && right.type->kind == VKind_Primitive)
    {
        if (left.type->primitive == PrimitiveType_String || right.type->primitive == PrimitiveType_String)
        {
            // TODO(Jose): 
        }
        else
        {
            {
                Type* type = TypeChooseMostSignificantPrimitive(left.type, right.type);
                out = IRAppend(out, IRFromOptionalCasting(ir, left, type, location));
                left = out.value;
                out = IRAppend(out, IRFromOptionalCasting(ir, right, type, location));
                right = out.value;
            }
            
            if (TypeIsAnyInt(left.type) && TypeIsAnyInt(right.type))
            {
                if (OperatorKindIsArithmetic(op))
                {
                    Type* dst_type = left.type;
                    
                    UnitKind kind = UnitKind_Error;
                    if (op == OperatorKind_Addition)            kind = UnitKind_Add;
                    else if (op == OperatorKind_Substraction)   kind = UnitKind_Sub;
                    else if (op == OperatorKind_Multiplication) kind = UnitKind_Mul;
                    else if (op == OperatorKind_Division)       kind = UnitKind_Div;
                    else if (op == OperatorKind_Modulo)         kind = UnitKind_Mod;
                    
                    if (kind != UnitKind_Error) {
                        return IRAppend(out, IRFromOp(ir, kind, dst_type, left, right, location));
                    }
                }
                else if (OperatorKindIsComparison(op))
                {
                    UnitKind kind = UnitKind_Error;
                    if (op == OperatorKind_Equals)                 kind = UnitKind_Eql;
                    else if (op == OperatorKind_NotEquals)         kind = UnitKind_Neq;
                    else if (op == OperatorKind_LessThan)          kind = UnitKind_Lss;
                    else if (op == OperatorKind_LessEqualsThan)    kind = UnitKind_Leq;
                    else if (op == OperatorKind_GreaterThan)       kind = UnitKind_Gtr;
                    else if (op == OperatorKind_GreaterEqualsThan) kind = UnitKind_Geq;
                    else if (op == OperatorKind_LogicalOr)         kind = UnitKind_Or;
                    else if (op == OperatorKind_LogicalAnd)        kind = UnitKind_And;
                    
                    if (kind != UnitKind_Error) {
                        return IRAppend(out, IRFromOp(ir, kind, bool_type, left, right, location));
                    }
                }
            }
            else if (left.type == bool_type && right.type == bool_type)
            {
                if (OperatorKindIsComparison(op))
                {
                    UnitKind kind = UnitKind_Error;
                    if (op == OperatorKind_Equals)                 kind = UnitKind_Eql;
                    else if (op == OperatorKind_NotEquals)         kind = UnitKind_Neq;
                    else if (op == OperatorKind_LessThan)          kind = UnitKind_Lss;
                    else if (op == OperatorKind_LessEqualsThan)    kind = UnitKind_Leq;
                    else if (op == OperatorKind_GreaterThan)       kind = UnitKind_Gtr;
                    else if (op == OperatorKind_GreaterEqualsThan) kind = UnitKind_Geq;
                    else if (op == OperatorKind_LogicalOr)         kind = UnitKind_Or;
                    else if (op == OperatorKind_LogicalAnd)        kind = UnitKind_And;
                    
                    if (kind != UnitKind_Error) {
                        return IRAppend(out, IRFromOp(ir, kind, bool_type, left, right, location));
                    }
                }
            }
            else if (left.type == float_type && right.type == float_type)
            {
                if (OperatorKindIsArithmetic(op))
                {
                    Type* type = (TypeGetSize(left.type) > TypeGetSize(right.type)) ? left.type : right.type;
                    
                    UnitKind kind = UnitKind_Error;
                    if (op == OperatorKind_Addition)            kind = UnitKind_Add;
                    else if (op == OperatorKind_Substraction)   kind = UnitKind_Sub;
                    else if (op == OperatorKind_Multiplication) kind = UnitKind_Mul;
                    else if (op == OperatorKind_Division)       kind = UnitKind_Div;
                    
                    if (kind != UnitKind_Error) {
                        return IRAppend(out, IRFromOp(ir, kind, type, left, right, location));
                    }
                }
                else if (OperatorKindIsComparison(op))
                {
                    UnitKind kind = UnitKind_Error;
                    if (op == OperatorKind_Equals)                 kind = UnitKind_Eql;
                    else if (op == OperatorKind_NotEquals)         kind = UnitKind_Neq;
                    else if (op == OperatorKind_LessThan)          kind = UnitKind_Lss;
                    else if (op == OperatorKind_LessEqualsThan)    kind = UnitKind_Leq;
                    else if (op == OperatorKind_GreaterThan)       kind = UnitKind_Gtr;
                    else if (op == OperatorKind_GreaterEqualsThan) kind = UnitKind_Geq;
                    
                    if (kind != UnitKind_Error) {
                        return IRAppend(out, IRFromOp(ir, kind, bool_type, left, right, location));
                    }
                }
            }
            
        }
    }
    
    if (TypeIsEnum(left.type) && left.type == right.type)
    {
        VariableTypeChild info = VTypeGetProperty(program, left.type, "index");
        Value child_index = ValueFromUInt(info.index);
        
        out = IRAppend(out, IRFromChild(ir, left, child_index, false, info.type, location));
        left = out.value;
        
        out = IRAppend(out, IRFromChild(ir, right, child_index, false, info.type, location));
        right = out.value;
        
        out = IRAppend(out, IRFromBinaryOperator(ir, left, right, op, reuse_left, location));
        return out;
    }
    
    if (right.type == Type_Type) {
        if (op == OperatorKind_Is) return IRAppend(out, IRFromOp(ir, UnitKind_Is, bool_type, left, right, location));
    }
    
#if 0
    if (left.kind == VKind_Reference && TypeEquals(program, left, right))
    {
        if (op == OperatorKind_Equals)    return BinaryOperation_Eql_Ref_Type;
        if (op == OperatorKind_NotEquals) return BinaryOperation_Neq_Ref_Type;
    }
    
    if (TypeEquals(program, left, VType_Type) && TypeEquals(program, right, VType_Type))
    {
        if (op == OperatorKind_Equals)         return BinaryOperation_Eql_Type_Type;
        else if (op == OperatorKind_NotEquals) return BinaryOperation_Neq_Type_Type;
    }
#endif
    
    if (left.type == string_type && right.type == string_type)
    {
        ExpresionContext expr_ctx = ExpresionContext_from_inference(1);
        
        Array<Value> params = ArrayAlloc<Value>(context.arena, 2);
        params[0] = left;
        params[1] = right;
        
        if (op == OperatorKind_Addition) {
            out = IRAppend(out, IRFromFunctionCallName(ir, "StrAppend", params, expr_ctx, location));
            return out;
        }
        if (op == OperatorKind_Division) {
            out = IRAppend(out, IRFromFunctionCallName(ir, "PathAppend", params, expr_ctx, location));
            return out;
        }
        if (op == OperatorKind_Equals) {
            out = IRAppend(out, IRFromFunctionCallName(ir, "StrEquals", params, expr_ctx, location));
            return out;
        }
        if (op == OperatorKind_NotEquals) {
            out = IRAppend(out, IRFromFunctionCallName(ir, "StrEquals", params, expr_ctx, location));
            out = IRAppend(out, IRFromOp(ir, UnitKind_Not, out.value.type, out.value, ValueNone(), location));
            return out;
        }
    }
    
    if (op == OperatorKind_Addition && (left.type == string_type || right.type == string_type) && (TypeIsAnyInt(left.type) || TypeIsAnyInt(right.type)))
    {
        Value str, cp;
        B32 front;
        
        if (left.type == string_type) {
            str = left;
            cp = right;
            front = false;
        }
        else {
            str = right;
            cp = left;
            front = true;
        }
        
        // String from codepoint
        {
            if (cp.type == int_type) {
                out = IRAppend(out, IRFromCasting(ir, cp, uint_type, false, location));
                cp = out.value;
            }
            
            Array<Value> params = ArrayAlloc<Value>(context.arena, 1);
            params[0] = cp;
            
            ExpresionContext expr_ctx = ExpresionContext_from_type(string_type, 1);
            out = IRAppend(out, IRFromFunctionCallName(ir, "StrFromCodepoint", params, expr_ctx, location));
            if (!out.success) return IRFailed();
            
            cp = out.value;
        }
        
        Array<Value> params = ArrayAlloc<Value>(context.arena, 2);
        params[0] = front ? cp : str;
        params[1] = front ? str : cp;
        
        ExpresionContext expr_ctx = ExpresionContext_from_inference(1);
        out = IRAppend(out, IRFromFunctionCallName(ir, "StrAppend", params, expr_ctx, location));
        return out;
    }
    
    if (op == OperatorKind_Addition && (TypeIsArray(left.type) || TypeIsArray(right.type)))
    {
        Type* left_element = TypeIsArray(left.type) ? TypeGetNext(program, left.type) : left.type;
        Type* right_element = TypeIsArray(right.type) ? TypeGetNext(program, right.type) : right.type;
        
        if (left_element == right_element)
        {
            Value array, src;
            B32 reuse_array = false;
            B32 front = false;
            
            if (TypeIsArray(left.type)) {
                array = left;
                src = right;
                reuse_array = reuse_left;
            }
            else
            {
                array = right;
                src = left;
                front = true;
            }
            
            out = IRAppend(out, IRFromArrayAppend(ir, array, src, reuse_array, front, location));
            return out;
        }
    }
    
    report_invalid_binary_op(location, left.type->name, StringFromOperatorKind(op), right.type->name);
    return IRFailed();
}

IR_Group IRFromSignOperator(IR_Context* ir, Value src, OperatorKind op, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    Assert(op != OperatorKind_None);
    
    if (TypeIsAnyInt(src.type))
    {
        if (op == OperatorKind_Addition) return IRFromNone(src);
        else if (op == OperatorKind_Substraction)
        {
            Type* type = int_type;
            
            if (ValueIsCompiletime(src))
            {
                if (src.type == int_type) {
                    return IRFromNone(ValueFromInt(-src.literal_sint));
                }
                else if (src.type == uint_type) {
                    return IRFromNone(ValueFromInt(-((I64)src.literal_uint)));
                }
            }
            
            return IRFromOp(ir, UnitKind_Neg, type, src, ValueNone(), location);
        }
        if (op == OperatorKind_LogicalNot) return IRFromOp(ir, UnitKind_Not, src.type, src, ValueNone(), location);
    }
    else if (src.type == float_type)
    {
        if (op == OperatorKind_Addition) return IRFromNone(src);
        else if (op == OperatorKind_Substraction)
        {
            Type* type = src.type;
            return IRFromOp(ir, UnitKind_Neg, type, src, ValueNone(), location);
        }
    }
    
    report_invalid_signed_op(location, StringFromOperatorKind(op), src.type->name);
    return IRFailed();
}

IR_Group IRFromCasting(IR_Context* ir, Value src, Type* type, B32 bitcast, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    if (src.type->kind != VKind_Primitive || type->kind != VKind_Primitive) {
        ReportErrorFront(location, "Can't cast '%S' to '%S', only primitive types are allowed", src.type->name, type->name);
        return IRFailed();
    }
    
    UnitKind kind = bitcast ? UnitKind_BitCast : UnitKind_Cast;
    return IRFromOp(ir, kind, type, src, ValueNone(), location);
}

IR_Group IRFromOptionalCasting(IR_Context* ir, Value src, Type* type, Location location)
{
    PROFILE_FUNCTION;
    
    if (src.type == type) return IRFromNone(src);
    return IRFromCasting(ir, src, type, false, location);
}

IR_Group IRFromChild(IR_Context* ir, Value src, Value index, B32 is_member, Type* type, Location location)
{
    PROFILE_FUNCTION;
    
    IR_Unit* unit = IRUnitAlloc(ir, UnitKind_Child, location);
    unit->dst_index = IRRegisterAlloc(ir, type, RegisterKind_Local, false);
    unit->src0 = src;
    unit->src1 = index;
    unit->child.child_is_member = is_member;
    
    Value child = ValueFromRegister(unit->dst_index, type, src.kind == ValueKind_LValue);
    return IRFromSingle(unit, child);
}

IR_Group IRFromChildAccess(IR_Context* ir, Value src, String child_name, ExpresionContext expr_context, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    if (src.type == void_type && TypeIsValid(expr_context.type)) {
        src = ValueFromType(program, expr_context.type);
    }
    
    IR_Group out = IRFromNone();
    
    if (src.type->kind == VKind_Reference) {
        out = IRAppend(out, IRFromDereference(ir, src, location));
        src = out.value;
    }
    
    VariableTypeChild info = TypeGetChild(program, src.type, child_name);
    
    IR_Group mem = IRFailed();
    
    if (info.index >= 0)
    {
        mem = IRFromChild(ir, src, ValueFromUInt(info.index), info.is_member, info.type, location);
    }
    else if (src.type == Type_Type && ValueIsCompiletime(src))
    {
        Type* type = TypeFromCompiletime(program, src);
        
        if (type->kind == VKind_Enum)
        {
            Assert(type->_enum->stage >= DefinitionStage_Defined);
            
            if (StrEquals(child_name, "count")) {
                mem = IRFromNone(ValueFromUInt(type->_enum->values.count));
            }
            else if (StrEquals(child_name, "array"))
            {
                Array<Value> values = ArrayAlloc<Value>(context.arena, type->_enum->names.count);
                foreach(i, values.count) {
                    values[i] = ValueFromEnum(type, i);
                }
                
                mem = IRFromNone(ValueFromArray(ir->arena, TypeFromArray(program, type, 1), values));
            }
            else
            {
                I64 value_index = -1;
                foreach(i, type->_enum->names.count) {
                    if (StrEquals(type->_enum->names[i], child_name)) {
                        value_index = i;
                        break;
                    }
                }
                
                if (value_index >= 0) {
                    mem = IRFromNone(ValueFromEnum(type, value_index));
                }
            }
        }
    }
    
    if (!mem.success) {
        ReportErrorFront(location, "Member '%S' not found in '%S'", child_name, src.type->name);
        return IRFailed();
    }
    
    return IRAppend(out, mem);
}

IR_Group IRFromIfStatement(IR_Context* ir, Value condition, IR_Group success, IR_Group failure, Location location)
{
    PROFILE_FUNCTION;
    
    Reporter* reporter = ir->reporter;
    
    if (condition.type != bool_type) {
        ReportErrorFront(location, "If statement expects a boolean expression");
        return IRFailed();
    }
    
    if (ValueIsCompiletime(condition))
    {
        B32 result = B32FromCompiletime(condition);
        if (result) return success;
        return failure;
    }
    
    IR_Unit* end_unit = IRUnitAlloc_Empty(ir, location);
    IR_Unit* failed_unit = end_unit;
    
    if (failure.unit_count > 0)
    {
        IR_Unit* jump = IRUnitAlloc_Jump(ir, 0, ValueNone(), end_unit, location);
        success = IRAppend(success, IRFromSingle(jump));
        
        failed_unit = failure.first;
    }
    
    IR_Unit* jump = IRUnitAlloc_Jump(ir, -1, condition, failed_unit, location);
    return IRAppend4(IRFromSingle(jump), success, failure, IRFromSingle(end_unit));
}

IR_Group IRFromLoop(IR_Context* ir, IR_Group init, IR_Group condition, IR_Group content, IR_Group update, Location location)
{
    PROFILE_FUNCTION;
    
    Reporter* reporter = ir->reporter;
    
    if (!init.success|| !update.success || !condition.success || !content.success)
        return IRFailed();
    
    if (condition.value.type != bool_type) {
        ReportErrorFront(location, "Loop condition expects a boolean expression");
        return IRFailed();
    }
    
    B32 fixed_condition = ValueIsCompiletime(condition.value);
    
    if (fixed_condition && !B32FromCompiletime(condition.value)) {
        return IRFromNone();
    }
    
    IR_LoopingScope* scope = ir_get_looping_scope(ir);
    
    if (scope == NULL) {
        InvalidCodepath();
        return IRFailed();
    }
    
    // "update" merges at the end of the content
    update = IRAppend(IRFromSingle(scope->continue_unit), update);
    content = IRAppend(content, update);
    
    // Condition
    if (!fixed_condition)
    {
        IR_Unit* jump = IRUnitAlloc_Jump(ir, -1, condition.value, scope->break_unit, location);
        condition = IRAppend(condition, IRFromSingle(jump));
    }
    
    // Loop
    {
        if (condition.unit_count == 0) {
            condition = IRAppend(condition, IRFromSingle(IRUnitAlloc_Empty(ir, location)));
        }
        
        IR_Unit* jump = IRUnitAlloc_Jump(ir, 0, ValueNone(), condition.first, location);
        content = IRAppend(content, IRFromSingle(jump));
    }
    
    IR_Group loop = IRAppend(condition, content);
    
    return IRAppend3(init, loop, IRFromSingle(scope->break_unit));
}

IR_Group IRFromFlowModifier(IR_Context* ir, B32 is_break, Location location)
{
    PROFILE_FUNCTION;
    
    Reporter* reporter = ir->reporter;
    
    IR_LoopingScope* scope = ir_get_looping_scope(ir);
    
    if (scope == NULL) {
        String op = is_break ? "break" : "continue";
        ReportErrorFront(location, "Can't use '%S' outside of a loop", op);
        return IRFailed();
    }
    
    IR_Unit* unit = is_break ? scope->break_unit : scope->continue_unit;
    return IRFromSingle(IRUnitAlloc_Jump(ir, 0, ValueNone(), unit, location));
}

IR_Group IRFromReturn(IR_Context* ir, IR_Group expression, Location location)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    IR_Group out = expression;
    if (!out.success) return IRFailed();
    
    IR_Object* dst = ir_find_object(ir, "return", true);
    
    B32 expecting_return_value = dst != NULL;
    
    if (expecting_return_value)
    {
        if (out.value.kind == ValueKind_None) {
            ReportErrorFront(location, "Function is expecting a '%S' as a return", dst->type->name);
            return IRFailed();
        }
        
        IR_Group assignment = IRFromAssignment(ir, true, ValueFromIrObject(dst), out.value, OperatorKind_None, location);
        out = IRAppend(out, assignment);
    }
    else
    {
        if (out.value.kind != ValueKind_None) {
            ReportErrorFront(location, "Function is expecting an empty return");
        }
        
        // Composition of tuple
        if (dst != NULL)
        {
            out = IRAppend(out, IRFromDefaultInitializer(ir, dst->type, location));
            out = IRAppend(out, IRFromAssignment(ir, true, ValueFromIrObject(dst), out.value, OperatorKind_None, location));
            
            //Array<VType> types = ir->returns;
            
            InvalidCodepath();
            // TODO(Jose): 
#if 0
            
            U32 start_index = ir->params.count + 1;
            
            foreach(i, types.count)
            {
                IR_Object* obj = ir_find_object_from_register(ir, start_index + i);
                Assert(obj != NULL && obj->type == types[i]);
                
                if (obj->assignment_count <= 0) {
                    ReportErrorFront(location, "Return value '%S' is not specified", obj->identifier);
                }
                
                IR_Group child = ir_from_child(ir, ValueFromIrObject(dst), ValueFromInt(i), true, types[i], location);
                IR_Group assignment = ir_from_assignment(ir, true, child.value, ValueFromIrObject(obj), BinaryOperator_None, location);
                out = ir_append_3(out, child, assignment);
            }
#endif
        }
    }
    
    IR_Unit* unit = IRUnitAlloc(ir, UnitKind_Return, location);
    return IRAppend(out, IRFromSingle(unit));
}

#if 0

IR_Group ir_from_node(IR_Context* ir, OpNode* node0, ExpresionContext context, B32 new_scope)
{
    SCRATCH();
    
    if (node0->kind == OpKind_Reference)
    {
        OpNode_Reference* node = (OpNode_Reference*)node0;
        
        IR_Group out = ir_from_node(ir, node->expresion, context, false);
        if (!out.success) return IRFailed();
        return IRAppend(out, ir_from_reference(ir, true, out.value, node->location));
    }
    
    if (node0->kind == OpKind_Null)
    {
        return IRFromNone(value_null());
    }
    
    if (node0->kind == OpKind_ArrayExpresion)
    {
        OpNode_ArrayExpresion* node = (OpNode_ArrayExpresion*)node0;
        
        Type* array_type = VType_Void;
        
        if (node->type->kind == OpKind_ObjectType) {
            array_type = type_from_node((OpNode_ObjectType*)node->type);
            if (array_type == nil_type) return IRFailed();
        }
        
        if (array_type == VType_Void && context.type->kind == VKind_Array) {
            array_type = context.type;
        }
        
        IR_Group out = IRFromNone();
        
        if (node->is_empty)
        {
            if (array_type == VType_Void) {
                ReportErrorFront(node->location, "Unresolved type for array expression");
                return IRFailed();
            }
            
            Type* base_type = array_type;
            U32 starting_dimensions = 0;
            
            if (array_type->kind == VKind_Array) {
                base_type = array_type->child_base;
                starting_dimensions = array_type->array_dimensions;
            }
            
            Array<Value> dimensions = ArrayAlloc<Value>(context.arena, node->nodes.count + starting_dimensions);
            
            foreach(i, starting_dimensions) {
                dimensions[i] = ValueFromInt(0);
            }
            
            foreach(i, node->nodes.count)
            {
                out = IRAppend(out, ir_from_node(ir, node->nodes[i], ExpresionContext_from_type(int_type, 1), false));
                if (!out.success) return IRFailed();
                
                Value dim = out.value;
                
                if (dim.type != int_type) {
                    report_dimensions_expects_an_int(node->location);
                    return IRFailed();
                }
                
                U32 index = starting_dimensions + i;
                dimensions[index] = dim;
            }
            
            out.value = value_from_empty_array(ir->arena, base_type, dimensions);
            return out;
        }
        else
        {
            Array<Value> elements = ArrayAlloc<Value>(context.arena, node->nodes.count);
            
            foreach(i, node->nodes.count) {
                out = IRAppend(out, ir_from_node(ir, node->nodes[i], ExpresionContext_from_void(), false));
                elements[i] = out.value;
            }
            
            if (array_type == VType_Void && elements.count > 0)
            {
                Type* element_type = elements[0].type;
                
                for (U32 i = 1; i < elements.count; i++) {
                    if (elements[i].type != element_type) {
                        element_type = nil_type;
                        break;
                    }
                }
                
                if (element_type != nil_type) {
                    array_type = type_from_dimension(element_type, 1);
                }
            }
            
            if (array_type == VType_Void) {
                ReportErrorFront(node->location, "Unresolved type for array expression");
                return IRFailed();
            }
            
            Type* element_type = array_type->child_next;
            
            // Assert same type
            foreach(i, elements.count) {
                if (elements[i].type != element_type) {
                    report_type_missmatch_array_expr(node->location, element_type->name, elements[i].type->name);
                    return IRFailed();
                }
            }
            
            out.value = ValueFromArray(ir->arena, array_type, elements);
        }
        
        
        return out;
    }
    
    report_expr_semantic_unknown(node0->location);
    return IRFailed();
}

#endif

internal_fn Unit UnitMake(Arena* arena, IR_Unit* unit)
{
    if (unit->kind == UnitKind_Error || unit->kind == UnitKind_Empty) return {};
    
    UnitKind kind = unit->kind;
    
    Unit dst = {};
    dst.kind = kind;
    dst.dst_index = unit->dst_index;
    dst.src0 = ValueCopy(arena, unit->src0);
    dst.src1 = ValueCopy(arena, unit->src1);
    dst.op_dst_type = unit->op_dst_type;
    
    if (kind == UnitKind_FunctionCall) {
        dst.function_call.fn = unit->function_call.fn;
        dst.function_call.parameters = ValueArrayCopy(arena, unit->function_call.parameters);
    }
    else if (kind == UnitKind_Jump) {
        dst.jump.condition = unit->jump.condition;
        dst.jump.offset = I32_MIN; // Calculated later
    }
    else if (kind == UnitKind_Child) {
        dst.child.child_is_member = unit->child.child_is_member;
    }
    
    return dst;
}

IR MakeIR(Arena* arena, Program* program, Array<Register> local_registers, IR_Group group, YovScript* script)
{
    PROFILE_FUNCTION;
    
    Array<IR_Unit*> units = ArrayAlloc<IR_Unit*>(context.arena, group.unit_count);
    {
        IR_Unit* unit = group.first;
        foreach(i, units.count) {
            if (unit == NULL) {
                InvalidCodepath();
                units.count = i;
                break;
            }
            units[i] = unit;
            unit = unit->next;
        }
    }
    
    BArray<Unit> instructions = BArrayMake<Unit>(context.arena, 64);
    BArray<U32> mapping = BArrayMake<U32>(context.arena, 64);
    
    foreach(i, units.count) {
        Unit instr = UnitMake(arena, units[i]);
        if (instr.kind == UnitKind_Error) continue;
        BArrayAdd(&instructions, instr);
        BArrayAdd(&mapping, i);
    }
    BArrayAdd(&mapping, units.count);
    
    // Resolve jump offsets
    foreach_BArray(it, &instructions)
    {
        Unit* rt = it.value;
        if (rt->kind != UnitKind_Jump) continue;
        
        IR_Unit* ir = units[mapping[it.index]];
        
        I32 ir_index = -1;
        foreach(i, units.count) {
            if (units[i] == ir->jump.unit) {
                ir_index = i;
                break;
            }
        }
        
        I32 jump_index = -1;
        foreach_BArray(it, &mapping)
        {
            jump_index = it.index;
            U32 v = *it.value;
            if (v >= ir_index) break;
        }
        
        if (ir_index < 0 || jump_index < 0) {
            InvalidCodepath();
            *rt = {};
            continue;
        }
        
        rt->jump.offset = jump_index - (I32)it.index - 1;
    }
    
    String ir_debug_path = {};
    
    // DEBUG INFO
    if (script != NULL)
    {
        ir_debug_path = StrCopy(arena, script->path);
        
        // Calculate lines
        foreach(i, instructions.count)
        {
            IR_Unit* unit0 = units[mapping[i]];
            Unit* unit = &instructions[i];
            
            unit->line = LineFromLocation(unit0->location, script);
        }
    }
    
    // Add last return
    if (instructions.count == 0 || instructions[instructions.count - 1].kind != UnitKind_Return)
    {
        Unit ret = {};
        ret.kind = UnitKind_Return;
        
        if (instructions.count > 0) {
            ret.line = instructions[instructions.count - 1].line + 1;
        }
        
        BArrayAdd(&instructions, ret);
    }
    
    IR ir = {};
    ir.success = group.success;
    ir.value = group.value;
    ir.local_registers = ArrayCopy(arena, local_registers);
    ir.instructions = ArrayFromBArray(arena, instructions);
    
    ir.path = ir_debug_path;
    
    // Count params
    foreach(i, ir.local_registers.count) {
        if (ir.local_registers[i].kind == RegisterKind_Parameter) {
            ir.parameter_count++;
        }
    }
    
    // Take return registers or last value as a return
    {
        BArray<Value> returns = BArrayMake<Value>(context.arena, 8);
        
        for (I32 i = 0; i < ir.local_registers.count; i++)
        {
            Register reg = ir.local_registers[i];
            
            if (reg.kind == RegisterKind_Return) {
                Value value = ValueFromRegister(RegIndexFromLocal(program, i), reg.type, true);
                BArrayAdd(&returns, value);
            }
        }
        
        Array<Value> values = ArrayFromBArray(context.arena, returns);
        
        if (values.count == 0) {
            ir.value = group.value;
        }
        else {
            ir.value = ValueFromReturn(arena, values);
        }
    }
    
    return ir;
}

IR_Context* IrContextAlloc(Program* program, Reporter* reporter)
{
    IR_Context* ir = ArenaPushStruct<IR_Context>(context.arena);
    ir->arena = context.arena;
    ir->reporter = reporter;
    ir->program = program;
    ir->local_registers = BArrayMake<Register>(ir->arena, 16);
    ir->objects = BArrayMake<IR_Object>(ir->arena, 32);
    ir->looping_scopes = BArrayMake<IR_LoopingScope>(ir->arena, 8);
    ir->scope = 0;
    return ir;
}

Array<Type*> ReturnsFromRegisters(Arena* arena, Array<Register> registers)
{
    U32 count = 0;
    foreach(i, registers.count) {
        if (registers[i].kind == RegisterKind_Return) count++;
    }
    
    Array<Type*> returns = ArrayAlloc<Type*>(arena, count);
    U32 index = 0;
    foreach(i, registers.count) {
        if (registers[i].kind == RegisterKind_Return) {
            returns[index++] = registers[i].type;
        }
    }
    
    return returns;
}

IR IrFromValue(Arena* arena, Program* program, Value value) {
    return MakeIR(arena, program, {}, IRFromNone(value), NULL);
}

#if 0

B32 ct_value_from_node(OpNode* node, Type* expected_type, Value* value)
{
    IR ir = ir_generate_from_initializer(node, ExpresionContext_from_type(expected_type, 1));
    
    *value = ValueNone();
    
    if (!ir.success) return false;
    
    Value v = ir.value;
    
    if (expected_type > VType_Void && v.type != expected_type) {
        report_type_missmatch_assign(node->location, v.type->name, expected_type->name);
        return false;
    }
    
    if (!ValueIsCompiletime(v)) {
        ReportErrorFront(node->location, "Expecting a compile time resolved value");
        return false;
    }
    
    *value = v;
    return true;
}

B32 ct_string_from_node(Arena* arena, OpNode* node, String* str)
{
    *str = {};
    Value v;
    if (!ct_value_from_node(node, string_type, &v)) return false;
    
    *str = string_from_compiletime(arena, yov->inter, v);
    return true;
}

B32 ct_bool_from_node(OpNode* node, B32* b)
{
    *b = {};
    Value v;
    if (!ct_value_from_node(node, bool_type, &v)) return false;
    
    *b = bool_from_compiletime(yov->inter, v);
    return true;
}

internal_fn Array<OpNode_StructDefinition*> get_struct_definitions(Arena* arena, Array<OpNode*> nodes)
{
    SCRATCH(arena);
    BArray<OpNode_StructDefinition*> result = BArrayMake<OpNode_StructDefinition*>(context.arena, 8);
    
    foreach(i, nodes.count) {
        OpNode* node0 = nodes[i];
        if (node0->kind == OpKind_StructDefinition)
        {
            OpNode_StructDefinition* node = (OpNode_StructDefinition*)node0;
            
            B32 duplicated = false;
            
            foreach(i, result.count) {
                if (StrEquals(result[i]->identifier, node->identifier)) {
                    duplicated = true;
                    break;
                }
            }
            
            if (duplicated) {
                report_symbol_duplicated(node->location, node->identifier);
                continue;
            }
            
            BArrayAdd(&result, node);
        }
    }
    
    return array_from_pooled_array(arena, result);
}

internal_fn Array<String> get_struct_dependencies(Arena* arena, OpNode_StructDefinition* node, Array<OpNode_StructDefinition*> struct_nodes)
{
    SCRATCH(arena);
    BArray<String> names = BArrayMake<String>(context.arena, 8);
    
    foreach(i, node->members.count) {
        OpNode_ObjectDefinition* member = node->members[i];
        Type* member_type = type_from_name(member->type->name);
        if (member_type != VType_Unknown) continue;
        
        foreach(j, struct_nodes.count) {
            OpNode_StructDefinition* struct0 = struct_nodes[j];
            if (struct0 == node) continue;
            if (StrEquals(member->type->name, struct0->identifier)) {
                BArrayAdd(&names, member->type->name);
                break;
            }
        }
    }
    
    return array_from_pooled_array(arena, names);
}

internal_fn I32 OpNode_StructDefinition_compare_dependency_index(const void* _0, const void* _1)
{
    auto node0 = *(const OpNode_StructDefinition**)_0;
    auto node1 = *(const OpNode_StructDefinition**)_1;
    
    if (node0->dependency_index == node1->dependency_index) return 0;
    return (node0->dependency_index < node1->dependency_index) ? -1 : 1;
}

#endif

B32 IRValidateReturnPath(Array<Unit> units)
{
    I32 next_jump_index = -1;
    foreach(i, units.count) {
        if (units[i].kind == UnitKind_Jump) {
            next_jump_index = i;
            break;
        }
    }
    
    if (next_jump_index >= 0)
    {
        Unit jump = units[next_jump_index];
        B32 has_condition = jump.jump.condition != 0;
        B32 is_backwards = jump.jump.offset < 0;
        
        Array<Unit> path_prev = ArraySub(units, 0, next_jump_index);
        I32 index = next_jump_index + 1 + jump.jump.offset;
        Array<Unit> path_fail = ArraySub(units, next_jump_index + 1, units.count - next_jump_index - 1);
        Array<Unit> path_jump = ArraySub(units, index, units.count - index);
        
        if (IRValidateReturnPath(path_prev)) return true;
        
        B32 jump_res = is_backwards || IRValidateReturnPath(path_jump);
        B32 fail_res = has_condition || IRValidateReturnPath(path_fail);
        return jump_res && fail_res;
    }
    else {
        foreach(i, units.count) {
            if (units[i].kind == UnitKind_Return) return true;
        }
        return false;
    }
}

IR_Object* ir_find_object(IR_Context* ir, String identifier, B32 parent_scopes)
{
    IR_Object* res = NULL;
    
    foreach_BArray(it, &ir->objects) {
        IR_Object* obj = it.value;
        if (!parent_scopes && obj->scope != ir->scope) continue;
        if (!StrEquals(obj->identifier, identifier)) continue;
        if (res != NULL && res->scope >= obj->scope) continue;
        res = obj;
    }
    
    return res;
}

IR_Object* ir_find_object_from_value(IR_Context* ir, Value value)
{
    I32 register_index = ValueGetRegister(value);
    if (register_index >= 0) {
        return ir_find_object_from_register(ir, value.reg.index);
    }
    return NULL;
}

IR_Object* ir_find_object_from_register(IR_Context* ir, I32 register_index)
{
    foreach_BArray(it, &ir->objects) {
        IR_Object* obj = it.value;
        if (obj->register_index != register_index) continue;
        return obj;
    }
    return NULL;
}

IR_Object* ir_define_object(IR_Context* ir, String identifier, Type* type, I32 scope, I32 register_index)
{
    Assert(scope != ir->scope || ir_find_object(ir, identifier, false) == NULL);
    
    IR_Object* def = BArrayAdd(&ir->objects);
    def->identifier = StrCopy(ir->arena, identifier);
    def->type = type;
    def->register_index = register_index;
    def->scope = scope;
    return def;
}

IR_Object* ir_assume_object(IR_Context* ir, IR_Object* object, Type* type)
{
    Assert(IRRegisterGet(ir, object->register_index).type == any_type);
    
    IR_Object* def = BArrayAdd(&ir->objects);
    def->identifier = object->identifier;
    def->type = type;
    def->register_index = object->register_index;
    def->scope = ir->scope;
    return def;
}

Symbol ir_find_symbol(IR_Context* ir, String identifier)
{
    Program* program = ir->program;
    
    Symbol symbol{};
    symbol.identifier = identifier;
    
    {
        IR_Object* obj = ir_find_object(ir, identifier, true);
        
        if (obj != NULL) {
            symbol.kind = SymbolKind_Object;
            symbol.object = obj;
            return symbol;
        }
    }
    
    {
        FunctionDefinition* fn = FunctionFromIdentifier(program, identifier);
        
        if (fn != NULL) {
            symbol.kind = SymbolKind_Function;
            symbol.function = fn;
            return symbol;
        }
    }
    
    {
        Type* type = TypeFromName(program, identifier);
        
        if (type != nil_type) {
            symbol.kind = SymbolKind_Type;
            symbol.type = type;
            return symbol;
        }
    }
    
    return {};
}

IR_LoopingScope* ir_looping_scope_push(IR_Context* ir, Location location)
{
    IR_Unit* continue_unit = IRUnitAlloc_Empty(ir, location);
    IR_Unit* break_unit = IRUnitAlloc_Empty(ir, location);
    
    IR_LoopingScope* scope = BArrayAdd(&ir->looping_scopes);
    scope->continue_unit = continue_unit;
    scope->break_unit = break_unit;
    
    ir_scope_push(ir);
    return scope;
}

void ir_looping_scope_pop(IR_Context* ir)
{
    BArrayErase(&ir->looping_scopes, ir->looping_scopes.count - 1);
    ir_scope_pop(ir);
}

IR_LoopingScope* ir_get_looping_scope(IR_Context* ir) {
    if (ir->looping_scopes.count == 0) return NULL;
    return &ir->looping_scopes[ir->looping_scopes.count - 1];
}

void ir_scope_push(IR_Context* ir)
{
    ir->scope++;
}

void ir_scope_pop(IR_Context* ir)
{
    ir->scope--;
    Assert(ir->scope >= 0);
    
    for (I32 i = (I32)ir->objects.count - 1; i >= 0; --i)
    {
        if (ir->objects[i].scope > ir->scope) {
            BArrayErase(&ir->objects, i);
        }
    }
}

I32 IRRegisterAlloc(IR_Context* ir, Type* type, RegisterKind kind, B32 constant) {
    Assert(type != void_type);
    Assert(kind != RegisterKind_Global);
    Assert(kind != RegisterKind_None);
    U32 local_index = ir->local_registers.count;
    Register* reg = BArrayAdd(&ir->local_registers);
    reg->type = type;
    reg->kind = kind;
    reg->is_constant = constant;
    return RegIndexFromLocal(ir->program, local_index);
}

Register IRRegisterGet(IR_Context* ir, I32 register_index)
{
    Program* program = ir->program;
    
    I32 local_index = LocalFromRegIndex(ir->program, register_index);
    if (local_index >= 0) {
        if (local_index >= ir->local_registers.count) return {};
        return ir->local_registers[local_index];
    }
    
    Global* global = GlobalFromRegisterIndex(program, register_index);
    if (global != NULL) {
        Register reg = {};
        reg.kind = RegisterKind_Global;
        reg.is_constant = global->is_constant;
        reg.type = global->type;
        return reg;
    }
    
    return {};
}

Register IRRegisterFromValue(IR_Context* ir, Value value)
{
    IR_Object* obj = ir_find_object_from_value(ir, value);
    if (obj == NULL) return {};
    return IRRegisterGet(ir, obj->register_index);
}

