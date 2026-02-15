#include "front.h"

inline_fn IR_Unit* ir_unit_alloc(IR_Context* ir, UnitKind kind, Location location)
{
    IR_Unit* unit = ArenaPushStruct<IR_Unit>(ir->arena);
    unit->kind = kind;
    unit->location = location;
    unit->dst_index = -1;
    return unit;
}

internal_fn IR_Unit* ir_alloc_empty(IR_Context* ir, Location location) {
    return ir_unit_alloc(ir, UnitKind_Empty, location);
}

internal_fn IR_Unit* ir_alloc_jump(IR_Context* ir, I32 condition, Value src, IR_Unit* jump_to_unit, Location location)
{
    Assert(jump_to_unit != NULL);
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Jump, location);
    unit->src = src;
    unit->jump.condition = condition;
    unit->jump.unit = jump_to_unit;
    return unit;
}

Value ValueFromIrObject(IR_Object* object)
{
    if (object->register_index < 0) return ValueNone();
    return ValueFromRegister(object->register_index, object->vtype, true);
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

inline_fn IR_Group ir_append_3(IR_Group o0, IR_Group o1, IR_Group o2) {
    IR_Group out = IRAppend(o0, o1);
    return IRAppend(out, o2);
}
inline_fn IR_Group ir_append_4(IR_Group o0, IR_Group o1, IR_Group o2, IR_Group o3) {
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
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_ResultEval, location);
    unit->src = src;
    return IRFromSingle(unit);
}

IR_Group IRFromDefineObject(IR_Context* ir, RegisterKind register_kind, String identifier, VType vtype, B32 constant, Location location)
{
    Assert(TypeIsAny(vtype) || VTypeValid(vtype));
    
    I32 register_index = IRRegisterAlloc(ir, vtype, register_kind, constant);
    
    IR_Object* object = ir_define_object(ir, identifier, vtype, ir->scope, register_index);
    Value value = ValueFromIrObject(object);
    return IRFromNone(value);
}

IR_Group IRFromDefineTemporal(IR_Context* ir, VType vtype, Location location)
{
    Assert(VTypeValid(vtype));
    
    I32 register_index = IRRegisterAlloc(ir, vtype, RegisterKind_Local, false);
    return IRFromNone(ValueFromRegister(register_index, vtype, false));
}


IR_Group IRFromReference(IR_Context* ir, B32 expects_lvalue, Value value, Location location)
{
    Reporter* reporter = ir->reporter;
    
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
    
    return IRFromNone(ValueFromReference(value));
}

IR_Group IRFromDereference(IR_Context* ir, Value value, Location location)
{
    Program* program = ir->program;
    if (value.vtype.kind != VKind_Reference) {
        InvalidCodepath();
        return IRFailed();
    }
    
    return IRFromNone(ValueFromDereference(program, value));
}

IR_Group IRFromSymbol(IR_Context* ir, String identifier, Location location)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    Symbol symbol = ir_find_symbol(ir, identifier);
    
    // Define globals
    if (symbol.type == SymbolType_None)
    {
        I32 global_index = GlobalIndexFromIdentifier(program, identifier);
        
        if (global_index >= 0)
        {
            Assert(VTypeValid(GlobalFromIndex(program, global_index)->vtype));
            
            Value value = ValueFromGlobal(program, global_index);
            return IRFromNone(value);
        }
    }
    
    if (symbol.type == SymbolType_Object)
    {
        IR_Object* object = symbol.object;
        return IRFromNone(ValueFromIrObject(object));
    }
    else if (symbol.type == SymbolType_Type) {
        return IRFromNone(ValueFromType(program, symbol.vtype));
    }
    
    report_symbol_not_found(location, identifier);
    return IRFailed();
}

IR_Group IRFromFunctionCall(IR_Context* ir, String identifier, Array<Value> parameters, ExpresionContext expr_context, Location location)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    FunctionDefinition* fn = FunctionFromIdentifier(program, identifier);
    
    if (fn != NULL)
    {
        IR_Group out = IRFromNone();
        
        Array<Value> params = array_make<Value>(context.arena, parameters.count);
        
        foreach(i, parameters.count)
        {
            VType expected_vtype = VType_Nil;
            if (i < fn->parameters.count) expected_vtype = fn->parameters[i].vtype;
            
            Value param = parameters[i];
            
            if (param.kind == ValueKind_LValue && TypeIsReference(param.vtype) && TypeEquals(program, VTypeNext(program, param.vtype), expected_vtype)) {
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
            if (!TypeIsAny(fn->parameters[i].vtype) && !TypeEquals(program, fn->parameters[i].vtype, params[i].vtype)) {
                report_function_wrong_parameter_type(location, fn->identifier, VTypeGetName(program, fn->parameters[i].vtype), i + 1);
                return IRFailed();
            }
        }
        
        //out = IRAppend(out, ir_from_function_call(ir, fn, params, node->location));
        {
            I32 first_register_index = -1;
            
            Array<Value> values = array_make<Value>(context.arena, fn->returns.count);
            foreach(i, values.count) {
                VType vtype = fn->returns[i].vtype;
                I32 register_index = IRRegisterAlloc(ir, vtype, RegisterKind_Local, false);
                if (i == 0) first_register_index = register_index;
                values[i] = ValueFromRegister(register_index, vtype, false);
            }
            
            IR_Unit* unit = ir_unit_alloc(ir, UnitKind_FunctionCall, location);
            unit->dst_index = first_register_index;
            unit->function_call.fn = fn;
            unit->function_call.parameters = array_copy(ir->arena, params);
            
            out = IRAppend(out, IRFromSingle(unit, value_from_return(ir->arena, values)));
        }
        
        Array<Value> returns = ValuesFromReturn(context.arena, out.value, false);
        
        for (U32 i = expr_context.assignment_count; i < returns.count; ++i) {
            if (TypeEquals(program, returns[i].vtype, VType_Result))
                out = IRAppend(out, ir_from_result_eval(ir, returns[i], location));
        }
        
        returns.count = Min(returns.count, expr_context.assignment_count);
        Value return_value = value_from_return(ir->arena, returns);
        
        out.value = return_value;
        
        return out;
    }
    
    VType call_vtype = TypeFromName(program, identifier);
    if (!TypeIsNil(call_vtype)) {
        return IRFromDefaultInitializer(ir, call_vtype, location);
    }
    
    report_symbol_not_found(location, identifier);
    return IRFailed();
}

IR_Group IRFromDefaultInitializer(IR_Context* ir, VType vtype, Location location) {
    Assert(VTypeValid(vtype));
    return IRFromNone(ValueFromZero(vtype));
}

IR_Group IRFromStore(IR_Context* ir, Value dst, Value src, Location location)
{
    Assert(dst.kind == ValueKind_LValue || dst.kind == ValueKind_Register);
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Store, location);
    unit->dst_index = dst.reg.index;
    unit->src = src;
    
    return IRFromSingle(unit, dst);
}

IR_Group IRFromAssignment(IR_Context* ir, B32 expects_lvalue, Value dst, Value src, BinaryOperator op, Location location)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    if (expects_lvalue && dst.kind != ValueKind_LValue) {
        report_expr_expects_lvalue(location);
        return IRFailed();
    }
    
    IR_Group out = IRFromNone();
    
    if (op != BinaryOperator_None)
    {
        out = IRAppend(out, IRFromBinaryOperator(ir, dst, src, op, true, location));
        if (!out.success) return IRFailed();
        src = out.value;
        
        if (ValueEquals(dst, src)) {
            return out;
        }
    }
    
    I32 mode = 0; // 0 -> Invalid; 1 -> Copy; 2 -> Store
    
    if (TypeEquals(program, dst.vtype, src.vtype)) {
        mode = 1;
    }
    else if (TypeIsReference(dst.vtype) && TypeEquals(program, VTypeNext(program, dst.vtype), src.vtype)) {
        out = IRAppend(out, IRFromDereference(ir, dst, location));
        dst = out.value;
        mode = 1;
    }
    else if (TypeIsReference(src.vtype) && TypeEquals(program, VTypeNext(program, src.vtype), dst.vtype)) {
        out = IRAppend(out, IRFromDereference(ir, src, location));
        src = out.value;
        mode = 1;
    }
    else if (dst.vtype.kind == VKind_Reference && ValueIsNull(src)) {
        mode = 2;
    }
    else
    {
        IR_Object* dst_object = ir_find_object_from_value(ir, dst);
        
        if (dst_object != NULL)
        {
            Register reg = IRRegisterGet(ir, dst_object->register_index);
            
            if (RegisterIsValid(reg) && TypeIsAny(reg.vtype)) {
                dst_object->vtype = src.vtype;
                dst = ValueFromIrObject(dst_object);
                mode = 2;
            }
        }
    }
    
    if (mode == 0)
    {
        report_type_missmatch_assign(location, VTypeGetName(program, src.vtype), VTypeGetName(program, dst.vtype));
        return IRFailed();
    }
    
    IR_Object* obj = ir_find_object_from_value(ir, dst);
    if (obj != NULL) obj->assignment_count++;
    
    if (TypeIsAny(dst.vtype)) {
        mode = 2;
    }
    
    IR_Unit* unit;
    if (mode == 1)
    {
        unit = ir_unit_alloc(ir, UnitKind_Copy, location);
        unit->dst_index = dst.reg.index;
        unit->src = src;
    }
    else
    {
        unit = ir_unit_alloc(ir, UnitKind_Store, location);
        unit->dst_index = dst.reg.index;
        unit->src = src;
    }
    
    return IRAppend(out, IRFromSingle(unit));
}

IR_Group IRFromMultipleAssignment(IR_Context* ir, B32 expects_lvalue, Array<Value> destinations, Value src, BinaryOperator op, Location location)
{
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
            if (TypeEquals(program, sources[i].vtype, VType_Result)) {
                out = IRAppend(out, ir_from_result_eval(ir, sources[i], location));
            }
        }
    }
    
    return out;
}

IR_Group IRFromBinaryOperator(IR_Context* ir, Value left, Value right, BinaryOperator op, B32 reuse_left, Location location)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    IR_Group out = IRFromNone();
    
    VType vtype = TypeFromBinaryOperation(program, left.vtype, right.vtype, op);
    
    if (TypeIsNil(vtype))
    {
        B32 deref = false;
        deref |= left.vtype.kind == VKind_Reference && right.vtype.kind != VKind_Reference;
        deref |= right.vtype.kind == VKind_Reference && left.vtype.kind != VKind_Reference;
        
        if (deref)
        {
            reuse_left = false;
            
            if (left.vtype.kind == VKind_Reference) {
                out = IRAppend(out, IRFromDereference(ir, left, location));
                left = out.value;
            }
            else {
                out = IRAppend(out, IRFromDereference(ir, right, location));
                right = out.value;
            }
            
            vtype = TypeFromBinaryOperation(program, left.vtype, right.vtype, op);
        }
    }
    
    if (TypeIsNil(vtype)) {
        report_invalid_binary_op(location, VTypeGetName(program, left.vtype), StringFromBinaryOperator(op), VTypeGetName(program, right.vtype));
        return IRFailed();
    }
    
    reuse_left = reuse_left && TypeEquals(program, vtype, left.vtype) && (left.kind == ValueKind_LValue || left.kind == ValueKind_Register);
    
    Value dst = reuse_left ? left : ValueFromRegister(IRRegisterAlloc(ir, vtype, RegisterKind_Local, false), vtype, false);
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_BinaryOperation, location);
    unit->dst_index = dst.reg.index;
    unit->src = left;
    unit->binary_op.src1 = right;
    unit->binary_op.op = op;
    
    return IRAppend(out, IRFromSingle(unit, dst));
}

IR_Group IRFromSignOperator(IR_Context* ir, Value src, BinaryOperator op, Location location)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    Assert(op != BinaryOperator_None);
    
    VType vtype = TypeFromSignOperation(program, src.vtype, op);
    
    if (!VTypeValid(vtype)) {
        report_invalid_signed_op(location, StringFromBinaryOperator(op), VTypeGetName(program, src.vtype));
        return IRFailed();
    }
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_SignOperation, location);
    unit->dst_index = IRRegisterAlloc(ir, vtype, RegisterKind_Local, false);
    unit->src = src;
    unit->sign_op.op = op;
    
    return IRFromSingle(unit, ValueFromRegister(unit->dst_index, vtype, false));
}

IR_Group IRFromChild(IR_Context* ir, Value src, Value index, B32 is_member, VType vtype, Location location)
{
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Child, location);
    unit->dst_index = IRRegisterAlloc(ir, vtype, RegisterKind_Local, false);
    unit->src = src;
    unit->child.child_index = index;
    unit->child.child_is_member = is_member;
    
    Value child = ValueFromRegister(unit->dst_index, vtype, src.kind == ValueKind_LValue);
    return IRFromSingle(unit, child);
}

IR_Group IRFromChildAccess(IR_Context* ir, Value src, String child_name, ExpresionContext expr_context, Location location)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    if (TypeIsVoid(src.vtype) && VTypeValid(expr_context.vtype)) {
        src = ValueFromType(program, expr_context.vtype);
    }
    
    IR_Group out = IRFromNone();
    
    if (src.vtype.kind == VKind_Reference) {
        out = IRAppend(out, IRFromDereference(ir, src, location));
        src = out.value;
    }
    
    VariableTypeChild info = TypeGetChild(program, src.vtype, child_name);
    
    IR_Group mem = IRFailed();
    
    if (info.index >= 0)
    {
        mem = IRFromChild(ir, src, ValueFromInt(info.index), info.is_member, info.vtype, location);
    }
    else if (TypeEquals(program, src.vtype, VType_Type) && ValueIsCompiletime(src))
    {
        VType vtype = TypeFromCompiletime(program, src);
        
        if (vtype.kind == VKind_Enum)
        {
            Assert(vtype._enum->stage >= DefinitionStage_Defined);
            
            if (StrEquals(child_name, "count")) {
                mem = IRFromNone(ValueFromInt(vtype._enum->values.count));
            }
            else if (StrEquals(child_name, "array"))
            {
                Array<Value> values = array_make<Value>(context.arena, vtype._enum->names.count);
                foreach(i, values.count) {
                    values[i] = ValueFromEnum(vtype, i);
                }
                
                mem = IRFromNone(ValueFromArray(ir->arena, vtype_from_dimension(vtype, 1), values));
            }
            else
            {
                I64 value_index = -1;
                foreach(i, vtype._enum->names.count) {
                    if (StrEquals(vtype._enum->names[i], child_name)) {
                        value_index = i;
                        break;
                    }
                }
                
                if (value_index >= 0) {
                    mem = IRFromNone(ValueFromEnum(vtype, value_index));
                }
            }
        }
    }
    
    if (!mem.success) {
        ReportErrorFront(location, "Member '%S' not found in '%S'", child_name, VTypeGetName(program, src.vtype));
        return IRFailed();
    }
    
    return IRAppend(out, mem);
}

IR_Group IRFromIfStatement(IR_Context* ir, Value condition, IR_Group success, IR_Group failure, Location location)
{
    Reporter* reporter = ir->reporter;
    
    if (!TypeIsBool(condition.vtype)) {
        ReportErrorFront(location, "If statement expects a Bool");
        return IRFailed();
    }
    
    if (ValueIsCompiletime(condition))
    {
        B32 result = B32FromCompiletime(condition);
        if (result) return success;
        return failure;
    }
    
    IR_Unit* end_unit = ir_alloc_empty(ir, location);
    IR_Unit* failed_unit = end_unit;
    
    if (failure.unit_count > 0)
    {
        IR_Unit* jump = ir_alloc_jump(ir, 0, ValueNone(), end_unit, location);
        success = IRAppend(success, IRFromSingle(jump));
        
        failed_unit = failure.first;
    }
    
    IR_Unit* jump = ir_alloc_jump(ir, -1, condition, failed_unit, location);
    return ir_append_4(IRFromSingle(jump), success, failure, IRFromSingle(end_unit));
}

IR_Group IRFromLoop(IR_Context* ir, IR_Group init, IR_Group condition, IR_Group content, IR_Group update, Location location)
{
    Reporter* reporter = ir->reporter;
    
    if (!init.success|| !update.success || !condition.success || !content.success)
        return IRFailed();
    
    if (!TypeIsBool(condition.value.vtype)) {
        ReportErrorFront(location, "Loop condition expects a Bool");
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
        IR_Unit* jump = ir_alloc_jump(ir, -1, condition.value, scope->break_unit, location);
        condition = IRAppend(condition, IRFromSingle(jump));
    }
    
    // Loop
    {
        if (condition.unit_count == 0) {
            condition = IRAppend(condition, IRFromSingle(ir_alloc_empty(ir, location)));
        }
        
        IR_Unit* jump = ir_alloc_jump(ir, 0, ValueNone(), condition.first, location);
        content = IRAppend(content, IRFromSingle(jump));
    }
    
    IR_Group loop = IRAppend(condition, content);
    
    return ir_append_3(init, loop, IRFromSingle(scope->break_unit));
}

IR_Group IRFromFlowModifier(IR_Context* ir, B32 is_break, Location location)
{
    Reporter* reporter = ir->reporter;
    
    IR_LoopingScope* scope = ir_get_looping_scope(ir);
    
    if (scope == NULL) {
        String op = is_break ? "break" : "continue";
        ReportErrorFront(location, "Can't use '%S' outside of a loop", op);
        return IRFailed();
    }
    
    IR_Unit* unit = is_break ? scope->break_unit : scope->continue_unit;
    return IRFromSingle(ir_alloc_jump(ir, 0, ValueNone(), unit, location));
}

IR_Group IRFromReturn(IR_Context* ir, IR_Group expression, Location location)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    IR_Group out = expression;
    if (!out.success) return IRFailed();
    
    IR_Object* dst = ir_find_object(ir, "return", true);
    
    B32 expecting_return_value = dst != NULL;
    
    if (expecting_return_value)
    {
        if (out.value.kind == ValueKind_None) {
            ReportErrorFront(location, "Function is expecting a '%S' as a return", VTypeGetName(program, dst->vtype));
            return IRFailed();
        }
        
        IR_Group assignment = IRFromAssignment(ir, true, ValueFromIrObject(dst), out.value, BinaryOperator_None, location);
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
            out = IRAppend(out, IRFromDefaultInitializer(ir, dst->vtype, location));
            out = IRAppend(out, IRFromAssignment(ir, true, ValueFromIrObject(dst), out.value, BinaryOperator_None, location));
            
            //Array<VType> vtypes = ir->returns;
            
            InvalidCodepath();
            // TODO(Jose): 
#if 0
            
            U32 start_index = ir->params.count + 1;
            
            foreach(i, vtypes.count)
            {
                IR_Object* obj = ir_find_object_from_register(ir, start_index + i);
                Assert(obj != NULL && obj->vtype == vtypes[i]);
                
                if (obj->assignment_count <= 0) {
                    ReportErrorFront(location, "Return value '%S' is not specified", obj->identifier);
                }
                
                IR_Group child = ir_from_child(ir, ValueFromIrObject(dst), ValueFromInt(i), true, vtypes[i], location);
                IR_Group assignment = ir_from_assignment(ir, true, child.value, ValueFromIrObject(obj), BinaryOperator_None, location);
                out = ir_append_3(out, child, assignment);
            }
#endif
        }
    }
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Return, location);
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
        
        VType array_vtype = VType_Void;
        
        if (node->type->kind == OpKind_ObjectType) {
            array_vtype = vtype_from_node((OpNode_ObjectType*)node->type);
            if (array_vtype == VType_Nil) return IRFailed();
        }
        
        if (array_vtype == VType_Void && context.vtype.kind == VKind_Array) {
            array_vtype = context.vtype;
        }
        
        IR_Group out = IRFromNone();
        
        if (node->is_empty)
        {
            if (array_vtype == VType_Void) {
                ReportErrorFront(node->location, "Unresolved type for array expression");
                return IRFailed();
            }
            
            VType base_vtype = array_vtype;
            U32 starting_dimensions = 0;
            
            if (array_vtype.kind == VKind_Array) {
                base_vtype = array_vtype.child_base;
                starting_dimensions = array_vtype.array_dimensions;
            }
            
            Array<Value> dimensions = array_make<Value>(context.arena, node->nodes.count + starting_dimensions);
            
            foreach(i, starting_dimensions) {
                dimensions[i] = ValueFromInt(0);
            }
            
            foreach(i, node->nodes.count)
            {
                out = IRAppend(out, ir_from_node(ir, node->nodes[i], ExpresionContext_from_vtype(VType_Int, 1), false));
                if (!out.success) return IRFailed();
                
                Value dim = out.value;
                
                if (dim.vtype != VType_Int) {
                    report_dimensions_expects_an_int(node->location);
                    return IRFailed();
                }
                
                U32 index = starting_dimensions + i;
                dimensions[index] = dim;
            }
            
            out.value = value_from_empty_array(ir->arena, base_vtype, dimensions);
            return out;
        }
        else
        {
            Array<Value> elements = array_make<Value>(context.arena, node->nodes.count);
            
            foreach(i, node->nodes.count) {
                out = IRAppend(out, ir_from_node(ir, node->nodes[i], ExpresionContext_from_void(), false));
                elements[i] = out.value;
            }
            
            if (array_vtype == VType_Void && elements.count > 0)
            {
                VType element_vtype = elements[0].vtype;
                
                for (U32 i = 1; i < elements.count; i++) {
                    if (elements[i].vtype != element_vtype) {
                        element_vtype = VType_Nil;
                        break;
                    }
                }
                
                if (element_vtype != VType_Nil) {
                    array_vtype = vtype_from_dimension(element_vtype, 1);
                }
            }
            
            if (array_vtype == VType_Void) {
                ReportErrorFront(node->location, "Unresolved type for array expression");
                return IRFailed();
            }
            
            VType element_vtype = array_vtype.child_next;
            
            // Assert same vtype
            foreach(i, elements.count) {
                if (elements[i].vtype != element_vtype) {
                    report_type_missmatch_array_expr(node->location, element_vtype.name, elements[i].vtype.name);
                    return IRFailed();
                }
            }
            
            out.value = ValueFromArray(ir->arena, array_vtype, elements);
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
    dst.src = ValueCopy(arena, unit->src);
    
    if (kind == UnitKind_FunctionCall) {
        dst.function_call.fn = unit->function_call.fn;
        dst.function_call.parameters = ValueArrayCopy(arena, unit->function_call.parameters);
    }
    else if (kind == UnitKind_Jump) {
        dst.jump.condition = unit->jump.condition;
        dst.jump.offset = I32_MIN; // Calculated later
    }
    else if (kind == UnitKind_BinaryOperation) {
        dst.binary_op.src1 = ValueCopy(arena, unit->binary_op.src1);
        dst.binary_op.op = unit->binary_op.op;
    }
    else if (kind == UnitKind_SignOperation) {
        dst.sign_op.op = unit->sign_op.op;
    }
    else if (kind == UnitKind_Child) {
        dst.child.child_index = unit->child.child_index;
        dst.child.child_is_member = unit->child.child_is_member;
    }
    
    return dst;
}

IR MakeIR(Arena* arena, Program* program, Array<Register> local_registers, IR_Group group, YovScript* script)
{
    Array<IR_Unit*> units = array_make<IR_Unit*>(context.arena, group.unit_count);
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
    
    PooledArray<Unit> instructions = pooled_array_make<Unit>(context.arena, 64);
    PooledArray<U32> mapping = pooled_array_make<U32>(context.arena, 64);
    
    foreach(i, units.count) {
        Unit instr = UnitMake(arena, units[i]);
        if (instr.kind == UnitKind_Error) continue;
        array_add(&instructions, instr);
        array_add(&mapping, i);
    }
    array_add(&mapping, units.count);
    
    // Resolve jump offsets
    for (auto it = pooled_array_make_iterator(&instructions); it.valid; ++it)
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
        for (auto it = pooled_array_make_iterator(&mapping); it.valid; ++it)
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
        
        array_add(&instructions, ret);
    }
    
    IR ir = {};
    ir.success = group.success;
    ir.value = group.value;
    ir.local_registers = array_copy(arena, local_registers);
    ir.instructions = array_from_pooled_array(arena, instructions);
    
    ir.path = ir_debug_path;
    
    // Count params
    foreach(i, ir.local_registers.count) {
        if (ir.local_registers[i].kind == RegisterKind_Parameter) {
            ir.parameter_count++;
        }
    }
    
    // Take return registers or last value as a return
    {
        PooledArray<Value> returns = pooled_array_make<Value>(context.arena, 8);
        
        for (I32 i = 0; i < ir.local_registers.count; i++)
        {
            Register reg = ir.local_registers[i];
            
            if (reg.kind == RegisterKind_Return) {
                Value value = ValueFromRegister(RegIndexFromLocal(program, i), reg.vtype, true);
                array_add(&returns, value);
            }
        }
        
        Array<Value> values = array_from_pooled_array(context.arena, returns);
        
        if (values.count == 0) {
            ir.value = group.value;
        }
        else {
            ir.value = value_from_return(arena, values);
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
    ir->local_registers = pooled_array_make<Register>(ir->arena, 16);
    ir->objects = pooled_array_make<IR_Object>(ir->arena, 32);
    ir->looping_scopes = pooled_array_make<IR_LoopingScope>(ir->arena, 8);
    ir->scope = 0;
    return ir;
}

Array<VType> ReturnsFromRegisters(Arena* arena, Array<Register> registers)
{
    U32 count = 0;
    foreach(i, registers.count) {
        if (registers[i].kind == RegisterKind_Return) count++;
    }
    
    Array<VType> returns = array_make<VType>(arena, count);
    U32 index = 0;
    foreach(i, registers.count) {
        if (registers[i].kind == RegisterKind_Return) {
            returns[index++] = registers[i].vtype;
        }
    }
    
    return returns;
}

IR IrFromValue(Arena* arena, Program* program, Value value) {
    return MakeIR(arena, program, {}, IRFromNone(value), NULL);
}

#if 0

B32 ct_value_from_node(OpNode* node, VType expected_vtype, Value* value)
{
    IR ir = ir_generate_from_initializer(node, ExpresionContext_from_vtype(expected_vtype, 1));
    
    *value = ValueNone();
    
    if (!ir.success) return false;
    
    Value v = ir.value;
    
    if (expected_vtype > VType_Void && v.vtype != expected_vtype) {
        report_type_missmatch_assign(node->location, v.vtype.name, expected_vtype.name);
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
    if (!ct_value_from_node(node, VType_String, &v)) return false;
    
    *str = string_from_compiletime(arena, yov->inter, v);
    return true;
}

B32 ct_bool_from_node(OpNode* node, B32* b)
{
    *b = {};
    Value v;
    if (!ct_value_from_node(node, VType_Bool, &v)) return false;
    
    *b = bool_from_compiletime(yov->inter, v);
    return true;
}

internal_fn Array<OpNode_StructDefinition*> get_struct_definitions(Arena* arena, Array<OpNode*> nodes)
{
    SCRATCH(arena);
    PooledArray<OpNode_StructDefinition*> result = pooled_array_make<OpNode_StructDefinition*>(context.arena, 8);
    
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
            
            array_add(&result, node);
        }
    }
    
    return array_from_pooled_array(arena, result);
}

internal_fn Array<String> get_struct_dependencies(Arena* arena, OpNode_StructDefinition* node, Array<OpNode_StructDefinition*> struct_nodes)
{
    SCRATCH(arena);
    PooledArray<String> names = pooled_array_make<String>(context.arena, 8);
    
    foreach(i, node->members.count) {
        OpNode_ObjectDefinition* member = node->members[i];
        VType member_vtype = vtype_from_name(member->type->name);
        if (member_vtype != VType_Unknown) continue;
        
        foreach(j, struct_nodes.count) {
            OpNode_StructDefinition* struct0 = struct_nodes[j];
            if (struct0 == node) continue;
            if (StrEquals(member->type->name, struct0->identifier)) {
                array_add(&names, member->type->name);
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
        
        Array<Unit> path_prev = array_subarray(units, 0, next_jump_index);
        I32 index = next_jump_index + 1 + jump.jump.offset;
        Array<Unit> path_fail = array_subarray(units, next_jump_index + 1, units.count - next_jump_index - 1);
        Array<Unit> path_jump = array_subarray(units, index, units.count - index);
        
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
    
    for (auto it = pooled_array_make_iterator(&ir->objects); it.valid; ++it) {
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
    for (auto it = pooled_array_make_iterator(&ir->objects); it.valid; ++it) {
        IR_Object* obj = it.value;
        if (obj->register_index != register_index) continue;
        return obj;
    }
    return NULL;
}

IR_Object* ir_define_object(IR_Context* ir, String identifier, VType vtype, I32 scope, I32 register_index)
{
    Assert(scope != ir->scope || ir_find_object(ir, identifier, false) == NULL);
    
    IR_Object* def = array_add(&ir->objects);
    def->identifier = StrCopy(ir->arena, identifier);
    def->vtype = vtype;
    def->register_index = register_index;
    def->scope = scope;
    return def;
}

IR_Object* ir_assume_object(IR_Context* ir, IR_Object* object, VType vtype)
{
    Assert(TypeIsAny(IRRegisterGet(ir, object->register_index).vtype));
    
    IR_Object* def = array_add(&ir->objects);
    def->identifier = object->identifier;
    def->vtype = vtype;
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
            symbol.type = SymbolType_Object;
            symbol.object = obj;
            return symbol;
        }
    }
    
    {
        FunctionDefinition* fn = FunctionFromIdentifier(program, identifier);
        
        if (fn != NULL) {
            symbol.type = SymbolType_Function;
            symbol.function = fn;
            return symbol;
        }
    }
    
    {
        VType vtype = TypeFromName(program, identifier);
        
        if (!TypeIsNil(vtype)) {
            symbol.type = SymbolType_Type;
            symbol.vtype = vtype;
            return symbol;
        }
    }
    
    return {};
}

IR_LoopingScope* ir_looping_scope_push(IR_Context* ir, Location location)
{
    IR_Unit* continue_unit = ir_alloc_empty(ir, location);
    IR_Unit* break_unit = ir_alloc_empty(ir, location);
    
    IR_LoopingScope* scope = array_add(&ir->looping_scopes);
    scope->continue_unit = continue_unit;
    scope->break_unit = break_unit;
    
    ir_scope_push(ir);
    return scope;
}

void ir_looping_scope_pop(IR_Context* ir)
{
    array_erase(&ir->looping_scopes, ir->looping_scopes.count - 1);
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
            array_erase(&ir->objects, i);
        }
    }
}

I32 IRRegisterAlloc(IR_Context* ir, VType vtype, RegisterKind kind, B32 constant) {
    Assert(!TypeIsVoid(vtype));
    Assert(kind != RegisterKind_Global);
    Assert(kind != RegisterKind_None);
    U32 local_index = ir->local_registers.count;
    Register* reg = array_add(&ir->local_registers);
    reg->vtype = vtype;
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
        reg.vtype = global->vtype;
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

