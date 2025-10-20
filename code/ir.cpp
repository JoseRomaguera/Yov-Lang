
inline_fn IR_Unit* ir_unit_alloc(IR_Context* ir, UnitKind kind, CodeLocation code)
{
    IR_Unit* unit = arena_push_struct<IR_Unit>(ir->arena);
    unit->kind = kind;
    unit->code = code;
    unit->dst_index = -1;
    return unit;
}

inline_fn i32 ir_alloc_register(IR_Context* ir) {
    return ir->next_register_index++;
}

internal_fn IR_Unit* ir_alloc_empty(IR_Context* ir, CodeLocation code) {
    return ir_unit_alloc(ir, UnitKind_Empty, code);
}

internal_fn IR_Unit* ir_alloc_jump(IR_Context* ir, i32 condition, Value src, IR_Unit* jump_to_unit, CodeLocation code)
{
    assert(jump_to_unit != NULL);
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Jump, code);
    unit->jump.condition = condition;
    unit->jump.src = src;
    unit->jump.unit = jump_to_unit;
    return unit;
}

inline_fn IR_Group ir_failed()
{
    IR_Group out{};
    out.success = false;
    out.unit_count = 0;
    out.first = NULL;
    out.last = NULL;
    out.value = value_none();
    return out;
}

inline_fn IR_Group ir_from_single(IR_Unit* unit, Value value = value_none())
{
    assert(unit->next == NULL && unit->prev == NULL);
    IR_Group out{};
    out.success = true;
    out.value = value;
    out.unit_count = 1;
    out.first = unit;
    out.last = unit;
    return out;
}
inline_fn IR_Group ir_from_none(Value value = value_none())
{
    IR_Group out{};
    out.success = true;
    out.unit_count = 0;
    out.first = NULL;
    out.last = NULL;
    out.value = value;
    return out;
}

#if DEV
internal_fn void DEV_validate_IR_Group(IR_Group out)
{
    u32 count = 0;
    IR_Unit* unit = out.first;
    while (unit != NULL) {
        assert(unit->next != NULL || unit == out.last);
        unit = unit->next;
        count++;
    }
    assert(out.unit_count == count);
}
#endif

internal_fn IR_Group IR_append(IR_Group o0, IR_Group o1)
{
    IR_Group out = {};
    out.success = o0.success && o1.success;
    if (!out.success) return ir_failed();
    
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

inline_fn IR_Group IR_append_3(IR_Group o0, IR_Group o1, IR_Group o2) {
    IR_Group out = IR_append(o0, o1);
    return IR_append(out, o2);
}
inline_fn IR_Group IR_append_4(IR_Group o0, IR_Group o1, IR_Group o2, IR_Group o3) {
    IR_Group out = IR_append(o0, o1);
    out = IR_append(out, o2);
    return IR_append(out, o3);
}
inline_fn IR_Group IR_append_5(IR_Group o0, IR_Group o1, IR_Group o2, IR_Group o3, IR_Group o4) {
    IR_Group out = IR_append(o0, o1);
    out = IR_append(out, o2);
    out = IR_append(out, o3);
    return IR_append(out, o4);
}

internal_fn IR_Group ir_from_result_eval(IR_Context* ir, Value src, CodeLocation code)
{
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_ResultEval, code);
    unit->result_eval.src = src;
    return ir_from_single(unit);
}
internal_fn IR_Group ir_from_define_temporal(IR_Context* ir, VariableType* vtype, CodeLocation code)
{
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Store, code);
    unit->dst_index = ir_alloc_register(ir);
    unit->store.global_identifier = {};
    unit->store.vtype = vtype;
    
    return ir_from_single(unit, value_from_register(unit->dst_index, vtype, false));
}
internal_fn IR_Group ir_from_define_local(IR_Context* ir, String identifier, VariableType* vtype, CodeLocation code)
{
    IR_Object* object = ir_define_object(ir, identifier, vtype, ir->scope, IR_ObjectContext_Local);
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Store, code);
    unit->dst_index = object->register_index;
    unit->store.global_identifier = {};
    unit->store.vtype = vtype;
    
    return ir_from_single(unit, value_from_ir_object(object));
}

internal_fn IR_Group ir_from_reference(IR_Context* ir, b32 expects_lvalue, Value value, CodeLocation code)
{
    if (expects_lvalue && value.kind != ValueKind_LValue) {
        report_ref_expects_lvalue(code);
        return ir_failed();
    }
    
    return ir_from_none(value_from_reference(value));
}

internal_fn IR_Group ir_from_dereference(IR_Context* ir, Value value, CodeLocation code)
{
    if (value.vtype->kind != VariableKind_Reference) {
        invalid_codepath();
        return ir_failed();
    }
    
    return ir_from_none(value_from_dereference(value));
}

internal_fn IR_Group ir_from_symbol(IR_Context* ir, String identifier, CodeLocation code)
{
    Symbol symbol = ir_find_symbol(ir, identifier);
    
    // Define globals
    if (symbol.type == SymbolType_None)
    {
        ObjectDefinition* def = find_global(identifier);
        
        if (def != NULL)
        {
            Value global_value = def->ir.value;
            
            if (def->is_constant && value_is_compiletime(global_value)) {
                return ir_from_none(value_from_constant(ir->arena, def->vtype, def->name));
            }
            else {
                IR_Object* object = ir_define_object(ir, def->name, def->vtype, ir->scope, IR_ObjectContext_Global);
                
                IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Store, code);
                unit->dst_index = object->register_index;
                unit->store.global_identifier = def->name;
                unit->store.vtype = def->vtype;
                
                return ir_from_single(unit, value_from_ir_object(object));
            }
        }
    }
    
    if (symbol.type == SymbolType_Object)
    {
        IR_Object* object = symbol.object;
        return ir_from_none(value_from_register(object->register_index, object->vtype, true));
    }
    else if (symbol.type == SymbolType_Type) {
        return ir_from_none(value_from_type(symbol.vtype));
    }
    
    report_symbol_not_found(code, identifier);
    return ir_failed();
}

internal_fn IR_Group ir_from_binary_operator(IR_Context* ir, Value left, Value right, BinaryOperator op, b32 reuse_left, CodeLocation code)
{
    IR_Group out = ir_from_none();
    
    VariableType* vtype = vtype_from_binary_operation(left.vtype, right.vtype, op);
    
    if (vtype == nil_vtype)
    {
        b32 deref = false;
        deref |= left.vtype->kind == VariableKind_Reference && right.vtype->kind != VariableKind_Reference;
        deref |= right.vtype->kind == VariableKind_Reference && left.vtype->kind != VariableKind_Reference;
        
        if (deref)
        {
            reuse_left = false;
            
            if (left.vtype->kind == VariableKind_Reference) {
                out = IR_append(out, ir_from_dereference(ir, left, code));
                left = out.value;
            }
            else {
                out = IR_append(out, ir_from_dereference(ir, right, code));
                right = out.value;
            }
            
            vtype = vtype_from_binary_operation(left.vtype, right.vtype, op);
        }
    }
    
    if (vtype == nil_vtype) {
        report_invalid_binary_op(code, left.vtype->name, string_from_binary_operator(op), right.vtype->name);
        return ir_failed();
    }
    
    reuse_left = reuse_left && vtype->ID == left.vtype->ID && (left.kind == ValueKind_LValue || left.kind == ValueKind_Register);
    
    Value dst = reuse_left ? left : value_from_register(ir_alloc_register(ir), vtype, false);
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_BinaryOperation, code);
    unit->dst_index = dst.reg.index;
    unit->binary_op.src0 = left;
    unit->binary_op.src1 = right;
    unit->binary_op.op = op;
    
    return IR_append(out, ir_from_single(unit, dst));
}

internal_fn IR_Group ir_from_sign_operator(IR_Context* ir, Value src, BinaryOperator op, CodeLocation code)
{
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_SignOperation, code);
    unit->dst_index = ir_alloc_register(ir);
    unit->sign_op.src = src;
    unit->sign_op.op = op;
    
    VariableType* vtype = vtype_from_sign_operation(src.vtype, op);
    if (vtype->ID <= VTypeID_Any) {
        report_invalid_signed_op(code, string_from_binary_operator(op), src.vtype->name);
        return ir_failed();
    }
    
    return ir_from_single(unit, value_from_register(unit->dst_index, vtype, false));
}

internal_fn IR_Group ir_from_assignment(IR_Context* ir, b32 expects_lvalue, Value dst, Value src, BinaryOperator op, CodeLocation code)
{
    if (expects_lvalue && dst.kind != ValueKind_LValue) {
        report_expr_expects_lvalue(code);
        return ir_failed();
    }
    
    IR_Group out = ir_from_none();
    
    if (op != BinaryOperator_None)
    {
        out = IR_append(out, ir_from_binary_operator(ir, dst, src, op, true, code));
        if (!out.success) return ir_failed();
        src = out.value;
        
        if (value_equals(dst, src)) {
            return out;
        }
    }
    
    if (dst.vtype->kind == VariableKind_Reference && dst.vtype->child_next == src.vtype) {
        out = IR_append(out, ir_from_dereference(ir, dst, code));
        dst = out.value;
    }
    else if (src.vtype->kind == VariableKind_Reference && src.vtype->child_next == dst.vtype) {
        out = IR_append(out, ir_from_dereference(ir, src, code));
        src = out.value;
    }
    
    if (dst.vtype != src.vtype) {
        report_type_missmatch_assign(code, src.vtype->name, dst.vtype->name);
        return ir_failed();
    }
    
    IR_Object* obj = ir_find_object_from_value(ir, dst);
    if (obj != NULL) obj->assignment_count++;
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Assignment, code);
    unit->dst_index = dst.reg.index;
    unit->assignment.src = src;
    
    return IR_append(out, ir_from_single(unit));
}

internal_fn IR_Group ir_from_child(IR_Context* ir, Value src, Value index, b32 is_member, VariableType* vtype, CodeLocation code)
{
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Child, code);
    unit->dst_index = ir_alloc_register(ir);
    unit->child.src = src;
    unit->child.child_index = index;
    unit->child.child_is_member = is_member;
    
    Value child = value_from_register(unit->dst_index, vtype, src.kind == ValueKind_LValue);
    return ir_from_single(unit, child);
}

internal_fn IR_Group ir_from_multiple_assignment(IR_Context* ir, b32 expects_lvalue, Array<Value> dst, Value src, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    if (dst.count == 0) return ir_failed();
    if (src.kind == ValueKind_None) return ir_failed();
    
    IR_Group out = ir_from_none();
    
    Array<Value> sources = {};
    if (vtype_is_tuple(src.vtype)) {
        Array<VariableType*> vtypes = vtype_unpack(scratch.arena, src.vtype);
        sources = array_make<Value>(scratch.arena, vtypes.count);
        foreach(i, sources.count) {
            out = IR_append(out, ir_from_child(ir, src, value_from_int(i), true, vtypes[i], code));
            sources[i] = out.value;
        }
    }
    else {
        sources = array_make<Value>(scratch.arena, 1);
        sources[0] = src;
    }
    
    if (sources.count == 1)
    {
        foreach(i, dst.count) {
            out = IR_append(out, ir_from_assignment(ir, expects_lvalue, dst[i], sources[0], op, code));
        }
    }
    else
    {
        if (dst.count > sources.count) {
            report_error(code, "The number of destinations is greater than the number of sources");
            return ir_failed();
        }
        
        foreach(i, dst.count) {
            out = IR_append(out, ir_from_assignment(ir, expects_lvalue, dst[i], sources[i], op, code));
        }
        
        for (u32 i = dst.count; i < sources.count; ++i) {
            if (sources[i].vtype == VType_Result) {
                out = IR_append(out, ir_from_result_eval(ir, sources[i], code));
            }
        }
    }
    
    return out;
}

internal_fn IR_Group ir_from_function_call(IR_Context* ir, FunctionDefinition* fn, Array<Value> params, CodeLocation code)
{
    VariableType* return_vtype = fn->return_vtype;
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_FunctionCall, code);
    unit->dst_index = return_vtype == void_vtype ? -1 : ir_alloc_register(ir);
    unit->function_call.fn = fn;
    unit->function_call.parameters = array_copy(ir->arena, params);
    
    Value value = value_none();
    if (unit->dst_index >= 0) {
        value = value_from_register(unit->dst_index, return_vtype, false);
    }
    
    return ir_from_single(unit, value);
}

internal_fn IR_Group ir_from_default_initializer(IR_Context* ir, VariableType* vtype, CodeLocation code) {
    return ir_from_none(value_from_default(vtype));
}

internal_fn IR_Group ir_from_return(IR_Context* ir, IR_Group expression, CodeLocation code)
{
    SCRATCH();
    IR_Group out = expression;
    if (!out.success) return ir_failed();
    
    IR_Object* dst = ir_find_object(ir, "return", true);
    
    b32 expecting_return_value = dst != NULL && !vtype_is_tuple(dst->vtype);
    
    if (expecting_return_value)
    {
        if (out.value.kind == ValueKind_None) {
            report_error(code, "Function is expecting a '%S' as a return", dst->vtype->name);
            return ir_failed();
        }
        
        IR_Group assignment = ir_from_assignment(ir, true, value_from_ir_object(dst), out.value, BinaryOperator_None, code);
        out = IR_append(out, assignment);
    }
    else
    {
        if (out.value.kind != ValueKind_None) {
            report_error(code, "Function is expecting an empty return");
        }
        
        // Composition of tuple
        if (dst != NULL)
        {
            out = IR_append(out, ir_from_default_initializer(ir, dst->vtype, code));
            out = IR_append(out, ir_from_assignment(ir, true, value_from_ir_object(dst), out.value, BinaryOperator_None, code));
            
            Array<VariableType*> vtypes = vtype_unpack(scratch.arena, dst->vtype);
            
            foreach(i, vtypes.count)
            {
                IR_Object* obj = ir_find_object_from_context(ir, IR_ObjectContext_Return, i);
                assert(obj != NULL && obj->vtype == vtypes[i]);
                
                if (obj->assignment_count <= 0) {
                    report_error(code, "Return value '%S' is not specified", obj->identifier);
                }
                
                IR_Group child = ir_from_child(ir, value_from_ir_object(dst), value_from_int(i), true, vtypes[i], code);
                IR_Group assignment = ir_from_assignment(ir, true, child.value, value_from_ir_object(obj), BinaryOperator_None, code);
                out = IR_append_3(out, child, assignment);
            }
        }
    }
    
    IR_Unit* unit = ir_unit_alloc(ir, UnitKind_Return, code);
    return IR_append(out, ir_from_single(unit));
}

internal_fn IR_Group ir_from_loop(IR_Context* ir, IR_Group init, IR_Group condition, IR_Group content, IR_Group update, CodeLocation code)
{
    if (!init.success || !condition.success || !update.success || !content.success)
        return ir_failed();
    
    IR_LoopingScope* scope = ir_get_looping_scope(ir);
    
    if (scope == NULL) {
        invalid_codepath();
        return ir_failed();
    }
    
    // "update" merges at the end of the content
    update = IR_append(ir_from_single(scope->continue_unit), update);
    content = IR_append(content, update);
    
    // Condition
    {
        if (condition.value.vtype->ID != VTypeID_Bool) {
            report_error(code, "Loop condition expects a Bool");
            return ir_failed();
        }
        
        IR_Unit* jump = ir_alloc_jump(ir, -1, condition.value, scope->break_unit, code);
        condition = IR_append(condition, ir_from_single(jump));
    }
    
    // Loop
    {
        IR_Unit* jump = ir_alloc_jump(ir, 0, value_none(), condition.first, code);
        content = IR_append(content, ir_from_single(jump));
    }
    
    IR_Group loop = IR_append(condition, content);
    
    return IR_append_3(init, loop, ir_from_single(scope->break_unit));
}

internal_fn IR_Group ir_from_block(IR_Context* ir, Array<OpNode*> ops, b32 add_scope)
{
    if (add_scope) ir_scope_push(ir);
    
    IR_Group out = ir_from_none();
    
    foreach(i, ops.count) {
        IR_Group out0 = ir_from_node(ir, ops[i], ExpresionContext_from_void());
        out = IR_append(out, out0);
    }
    
    if (add_scope) ir_scope_pop(ir);
    
    return out;
}

internal_fn IR_Group ir_from_content_node(IR_Context* ir, OpNode* node0)
{
    if (node0 != NULL && node0->kind == OpKind_Block) {
        OpNode_Block* node = (OpNode_Block*)node0;
        return ir_from_block(ir, node->ops, false);
    }
    return ir_from_node(ir, node0, ExpresionContext_from_void());
}

IR_Group ir_from_node(IR_Context* ir, OpNode* node0, ExpresionContext context)
{
    SCRATCH();
    
    if (node0 == NULL || node0->kind == OpKind_None) {
        return ir_from_none();
    }
    
    if (node0->kind == OpKind_Block)
    {
        OpNode_Block* node = (OpNode_Block*)node0;
        return ir_from_block(ir, node->ops, true);
    }
    
    if (node0->kind == OpKind_Binary)
    {
        auto node = (OpNode_Binary*)node0;
        
        context.allow_tuple = false;
        IR_Group left = ir_from_node(ir, node->left, context);
        IR_Group right = ir_from_node(ir, node->right, ExpresionContext_from_vtype(left.value.vtype, false));
        
        if (!left.success || !right.success) return ir_failed();
        
        IR_Group op = ir_from_binary_operator(ir, left.value, right.value, node->op, false, node->code);
        return IR_append_3(left, right, op);
    }
    
    if (node0->kind == OpKind_Sign)
    {
        auto node = (OpNode_Sign*)node0;
        context.allow_tuple = false;
        IR_Group src = ir_from_node(ir, node->expresion, context);
        if (!src.success) return ir_failed();
        return IR_append(src, ir_from_sign_operator(ir, src.value, node->op, node->code));
    }
    
    if (node0->kind == OpKind_IntLiteral)
    {
        OpNode_NumericLiteral* node = (OpNode_NumericLiteral*)node0;
        return ir_from_none(value_from_int(node->int_literal));
    }
    if (node0->kind == OpKind_CodepointLiteral)
    {
        OpNode_NumericLiteral* node = (OpNode_NumericLiteral*)node0;
        return ir_from_none(value_from_int(node->codepoint_literal));
    }
    if (node0->kind == OpKind_BoolLiteral)
    {
        OpNode_NumericLiteral* node = (OpNode_NumericLiteral*)node0;
        return ir_from_none(value_from_bool(node->bool_literal));
    }
    if (node0->kind == OpKind_Symbol)
    {
        OpNode_Symbol* node = (OpNode_Symbol*)node0;
        return ir_from_symbol(ir, node->identifier, node->code);
    }
    
    if (node0->kind == OpKind_StringLiteral)
    {
        OpNode_StringLiteral* node = (OpNode_StringLiteral*)node0;
        
        if (node->expresions.count == 0) {
            return ir_from_none(value_from_string(ir->arena, node->value));
        }
        
        String value = node->value;
        IR_Group out = ir_from_none();
        PooledArray<Value> sources = pooled_array_make<Value>(scratch.arena, 8);
        
        u32 expresion_index = 0;
        StringBuilder builder = string_builder_make(scratch.arena);
        
        u64 cursor = 0;
        while (cursor < value.size)
        {
            u32 codepoint = string_get_codepoint(value, &cursor);
            
            if (codepoint == '\\')
            {
                codepoint = 0;
                if (cursor < value.size) {
                    codepoint = string_get_codepoint(value, &cursor);
                }
                
                if (codepoint == '\\') append(&builder, "\\");
                else if (codepoint == '%') append(&builder, "%");
                else { invalid_codepath(); }
                
                continue;
            }
            
            if (codepoint == '%')
            {
                if (expresion_index >= node->expresions.count) {
                    invalid_codepath();
                }
                else
                {
                    OpNode* expression_node = node->expresions[expresion_index++];
                    IR_Group expression = ir_from_node(ir, expression_node, ExpresionContext_from_void());
                    if (!expression.success) return ir_failed();
                    
                    Value value = expression.value;
                    
                    if (value_is_compiletime(value)) {
                        String ct_str = string_from_compiletime(scratch.arena, ir->inter, value);
                        append(&builder, ct_str);
                    }
                    else
                    {
                        String literal = string_from_builder(scratch.arena, &builder);
                        if (literal.size > 0) {
                            builder = string_builder_make(scratch.arena);
                            array_add(&sources, value_from_string(ir->arena, literal));
                        }
                        
                        out = IR_append(out, expression);
                        array_add(&sources, expression.value);
                    }
                }
                
                continue;
            }
            
            append_codepoint(&builder, codepoint);
        }
        
        String literal = string_from_builder(scratch.arena, &builder);
        if (literal.size > 0) {
            array_add(&sources, value_from_string(ir->arena, literal));
        }
        
        out.value = value_from_string_array(ir->inter, ir->arena, array_from_pooled_array(scratch.arena, sources));
        return out;
    }
    
    if (node0->kind == OpKind_Reference)
    {
        OpNode_Reference* node = (OpNode_Reference*)node0;
        
        IR_Group out = ir_from_node(ir, node->expresion, context);
        if (!out.success) return ir_failed();
        return IR_append(out, ir_from_reference(ir, true, out.value, node->code));
    }
    
    if (node0->kind == OpKind_MemberValue)
    {
        OpNode_MemberValue* node = (OpNode_MemberValue*)node0;
        
        IR_Group src_out = ir_from_node(ir, node->expresion, context);
        if (!src_out.success) return ir_failed();
        
        Value src = src_out.value;
        if (src.vtype == void_vtype && context.vtype->ID > VTypeID_Any) {
            src = value_from_type(context.vtype);
        }
        
        if (src.vtype->kind == VariableKind_Reference) {
            src_out = IR_append(src_out, ir_from_dereference(ir, src, node->code));
            src = src_out.value;
        }
        
        String member = node->member;
        
        IR_Group mem = ir_failed();
        
        VariableTypeChild info = vtype_get_child(src.vtype, member);
        
        if (info.index >= 0)
        {
            mem = ir_from_child(ir, src, value_from_int(info.index), info.is_member, info.vtype, node->code);
        }
        else if (src.vtype == VType_Type && value_is_compiletime(src))
        {
            VariableType* vtype = type_from_compiletime(ir->inter, src);
            
            if (vtype->kind == VariableKind_Enum)
            {
                if (string_equals(member, "count")) {
                    mem = ir_from_none(value_from_int(vtype->_enum.values.count));
                }
                else if (string_equals(member, "array"))
                {
                    Array<Value> values = array_make<Value>(scratch.arena, vtype->_enum.names.count);
                    foreach(i, values.count) {
                        values[i] = value_from_enum(vtype, vtype->_enum.values[i]);
                    }
                    
                    mem = ir_from_none(value_from_array(ir->arena, vtype_from_dimension(vtype, 1), values));
                }
                else
                {
                    i64 value_index = -1;
                    foreach(i, vtype->_enum.names.count) {
                        if (string_equals(vtype->_enum.names[i], member)) {
                            value_index = i;
                            break;
                        }
                    }
                    
                    if (value_index >= 0) {
                        mem = ir_from_none(value_from_enum(vtype, value_index));
                    }
                }
            }
        }
        
        if (!mem.success) {
            report_error(node->code, "Member '%S' not found in '%S'", member, src.vtype->name);
            return ir_failed();
        }
        
        return IR_append(src_out, mem);
    }
    
    if (node0->kind == OpKind_Indexing)
    {
        OpNode_Indexing* node = (OpNode_Indexing*)node0;
        
        IR_Group src = ir_from_node(ir, node->value, ExpresionContext_from_void());
        IR_Group index = ir_from_node(ir, node->index, ExpresionContext_from_void());
        if (!src.success || !index.success) return ir_failed();
        
        if (index.value.vtype->ID != VTypeID_Int) {
            report_indexing_expects_an_int(node->code);
            return ir_failed();
        }
        
        VariableType* type = src.value.vtype;
        
        IR_Group out = IR_append(src, index);
        
        if (type->kind == VariableKind_Array)
        {
            VariableType* element_vtype = type->child_next;
            out = IR_append(out, ir_from_child(ir, src.value, index.value, true, element_vtype, node->code));
        }
        else
        {
            report_indexing_not_allowed(node->code, type->name);
            return ir_failed();
        }
        
        return out;
    }
    
    if (node0->kind == OpKind_ObjectDefinition)
    {
        OpNode_ObjectDefinition* node = (OpNode_ObjectDefinition*)node0;
        
        Array<String> identifiers = node->names;
        
        // Validation for duplicated symbols
        foreach(i, identifiers.count) {
            String identifier = identifiers[i];
            if (ir_find_object(ir, identifier, false) != NULL) {
                report_symbol_duplicated(node->code, identifier);
                return ir_failed();
            }
        }
        
        // Explicit type
        VariableType* definition_vtype = vtype_from_node(node->type);
        if (definition_vtype->ID == VTypeID_Unknown) return ir_failed();
        b32 inference_type = definition_vtype == void_vtype || definition_vtype->ID == VTypeID_Any;
        
        // Assignment expresion
        ExpresionContext context = inference_type ? ExpresionContext_from_inference() : ExpresionContext_from_vtype(definition_vtype, true);
        IR_Group src = ir_from_node(ir, node->assignment, context);
        if (!src.success) return ir_failed();
        
        // Inference
        VariableType* assignment_vtype = definition_vtype;
        if (inference_type) {
            assignment_vtype = src.value.vtype;
            if (definition_vtype == void_vtype) definition_vtype = src.value.vtype;
        }
        
        if (definition_vtype->ID <= VTypeID_Void || assignment_vtype->ID <= VTypeID_Any) {
            report_error(node->code, "Unresolved object type for definition");
            return ir_failed();
        }
        
        Array<VariableType*> vtypes = array_make<VariableType*>(scratch.arena, identifiers.count);
        foreach(i, vtypes.count) vtypes[i] = definition_vtype;
        
        if (vtype_is_tuple(definition_vtype))
        {
            Array<VariableType*> childs = vtype_unpack(scratch.arena, definition_vtype);
            vtypes = vtype_unpack(scratch.arena, definition_vtype);
            foreach(i, MIN(vtypes.count, childs.count)) {
                vtypes[i] = childs[i];
            }
        }
        
        IR_Group out = ir_from_none();
        
        Array<Value> values = array_make<Value>(scratch.arena, identifiers.count);
        foreach(i, values.count) {
            VariableType* vtype = vtypes[i];
            out = IR_append(out, ir_from_define_local(ir, identifiers[i], vtype, node->code));
            values[i] = out.value;
        }
        
        // Default src
        if (src.value.kind == ValueKind_None) {
            src = ir_from_default_initializer(ir, definition_vtype, node->code);
        }
        
        out = IR_append(out, src);
        
        IR_Group assignment = ir_from_multiple_assignment(ir, true, values, src.value, BinaryOperator_None, node->code);
        out = IR_append(out, assignment);
        return out;
    }
    
    if (node0->kind == OpKind_Assignment)
    {
        OpNode_Assignment* node = (OpNode_Assignment*)node0;
        
        IR_Group out = ir_from_none();
        Array<Value> values = {};
        
        if (node->destination->kind == OpKind_ParameterList)
        {
            OpNode_ParameterList* params_node = (OpNode_ParameterList*)node->destination;
            
            values = array_make<Value>(scratch.arena, params_node->nodes.count);
            foreach(i, values.count) {
                out = IR_append(out, ir_from_node(ir, params_node->nodes[i], ExpresionContext_from_void()));
                values[i] = out.value;
            }
        }
        else
        {
            out = IR_append(out, ir_from_node(ir, node->destination, ExpresionContext_from_void()));
            values = array_make<Value>(scratch.arena, 1);
            values[0] = out.value;
        }
        IR_Group src = ir_from_node(ir, node->source, ExpresionContext_from_vtype(values[0].vtype, true));
        out = IR_append(out, src);
        
        if (!out.success) return ir_failed();
        
        IR_Group assignment = ir_from_multiple_assignment(ir, true, values, src.value, node->binary_operator, node->code);
        return IR_append(out, assignment);
    }
    
    if (node0->kind == OpKind_FunctionCall)
    {
        OpNode_FunctionCall* node = (OpNode_FunctionCall*)node0;
        
        Array<Value> params = {};
        
        FunctionDefinition* fn = find_function(node->identifier);
        if (fn != NULL)
        {
            IR_Group out = ir_from_none();
            
            params = array_make<Value>(scratch.arena, node->parameters.count);
            
            foreach(i, node->parameters.count)
            {
                VariableType* expected_vtype = nil_vtype;
                if (i < fn->parameters.count) expected_vtype = fn->parameters[i].vtype;
                
                IR_Group param_IR = ir_from_node(ir, node->parameters[i], ExpresionContext_from_vtype(expected_vtype, false));
                out = IR_append(out, param_IR);
                
                Value param = out.value;
                
                if (param.kind == ValueKind_LValue && param.vtype->kind == VariableKind_Reference && param.vtype->child_next == expected_vtype) {
                    param = value_from_dereference(param);
                }
                
                params[i] = param;
            }
            
            if (params.count != fn->parameters.count) {
                report_function_expecting_parameters(node->code, fn->identifier, fn->parameters.count);
                return ir_failed();
            }
            
            // Check parameters
            foreach(i, params.count)
            {
                if (fn->parameters[i].vtype->ID != VTypeID_Any && fn->parameters[i].vtype != params[i].vtype) {
                    report_function_wrong_parameter_type(node->code, fn->identifier, fn->parameters[i].vtype->name, i + 1);
                    return ir_failed();
                }
            }
            
            out = IR_append(out, ir_from_function_call(ir, fn, params, node->code));
            
            Value return_value = out.value;
            
            if (vtype_is_tuple(return_value.vtype) && !context.allow_tuple) {
                VariableType* vtype = vtype_unpack(scratch.arena, return_value.vtype)[0];
                IR_Group child = ir_from_child(ir, return_value, value_from_int(0), true, vtype, node->code);
                out = IR_append(out, child);
                return_value = out.value;
            }
            else if (context.vtype == void_vtype && return_value.kind != ValueKind_None)
            {
                Array<Value> returns = {};
                if (vtype_is_tuple(return_value.vtype))
                {
                    Array<VariableType*> vtypes = vtype_unpack(scratch.arena, return_value.vtype);
                    returns = array_make<Value>(scratch.arena, vtypes.count);
                    foreach(i, returns.count) {
                        out = IR_append(out, ir_from_child(ir, return_value, value_from_int(i), true, vtypes[i], node->code));
                        returns[i] = out.value;
                    }
                }
                else {
                    returns = array_make<Value>(scratch.arena, 1);
                    returns[0] = return_value;
                }
                
                foreach(i, returns.count) {
                    if (returns[i].vtype == VType_Result)
                        out = IR_append(out, ir_from_result_eval(ir, returns[i], node->code));
                }
            }
            
            out.value = return_value;
            
            return out;
        }
        
        VariableType* call_vtype = vtype_from_name(node->identifier);
        if (call_vtype != nil_vtype) {
            return ir_from_default_initializer(ir, call_vtype, node->code);
        }
        
        report_symbol_not_found(node->code, node->identifier);
        return ir_failed();
    }
    
    if (node0->kind == OpKind_Return)
    {
        OpNode_Return* node = (OpNode_Return*)node0;
        
        IR_Group expression = ir_from_node(ir, node->expresion, context);
        if (!expression.success) return ir_failed();
        return ir_from_return(ir, expression, node->code);
    }
    
    if (node0->kind == OpKind_Continue || node0->kind == OpKind_Break)
    {
        OpNode* node = node0;
        IR_LoopingScope* scope = ir_get_looping_scope(ir);
        
        if (scope == NULL) {
            String op = (node->kind == OpKind_Continue) ? "continue" : "break";
            report_error(node->code, "Can't use '%S' outside of a loop", op);
            return ir_failed();
        }
        
        IR_Unit* unit = (node->kind == OpKind_Continue) ? scope->continue_unit : scope->break_unit;
        return ir_from_single(ir_alloc_jump(ir, 0, value_none(), unit, node->code));
    }
    
    if (node0->kind == OpKind_IfStatement)
    {
        OpNode_IfStatement* node = (OpNode_IfStatement*)node0;
        
        IR_Group expression = ir_from_node(ir, node->expresion, ExpresionContext_from_vtype(VType_Bool, false));
        IR_Group success = ir_from_node(ir, node->success, ExpresionContext_from_void());
        IR_Group failure = ir_from_node(ir, node->failure, ExpresionContext_from_void());
        
        if (!expression.success || !success.success || !failure.success) {
            return ir_failed();
        }
        
        IR_Unit* end_unit = ir_alloc_empty(ir, node->code);
        IR_Unit* failed_unit = end_unit;
        
        if (failure.unit_count > 0)
        {
            IR_Unit* jump = ir_alloc_jump(ir, 0, value_none(), end_unit, node->code);
            success = IR_append(success, ir_from_single(jump));
            
            failed_unit = failure.first;
        }
        
        IR_Unit* jump = ir_alloc_jump(ir, -1, expression.value, failed_unit, node->code);
        return IR_append_5(expression, ir_from_single(jump), success, failure, ir_from_single(end_unit));
    }
    
    if (node0->kind == OpKind_WhileStatement)
    {
        OpNode_WhileStatement* node = (OpNode_WhileStatement*)node0;
        
        ir_looping_scope_push(ir, node->code);
        IR_Group expression = ir_from_node(ir, node->expresion, ExpresionContext_from_vtype(VType_Bool, false));
        IR_Group content = ir_from_content_node(ir, node->content);
        
        IR_Group out = ir_from_loop(ir, ir_from_none(), expression, content, ir_from_none(), node->code);
        ir_looping_scope_pop(ir);
        
        return out;
    }
    
    if (node0->kind == OpKind_ForStatement)
    {
        OpNode_ForStatement* node = (OpNode_ForStatement*)node0;
        
        ir_looping_scope_push(ir, node->code);
        IR_Group init = ir_from_node(ir, node->initialize_sentence, ExpresionContext_from_void());
        IR_Group condition = ir_from_node(ir, node->condition_expresion, ExpresionContext_from_vtype(VType_Bool, false));
        IR_Group update = ir_from_node(ir, node->update_sentence, ExpresionContext_from_void());
        IR_Group content = ir_from_content_node(ir, node->content);
        
        IR_Group out = ir_from_loop(ir, init, condition, content, update, node->code);
        ir_looping_scope_pop(ir);
        
        return out;
    }
    
    if (node0->kind == OpKind_ForeachArrayStatement)
    {
        OpNode_ForeachArrayStatement* node = (OpNode_ForeachArrayStatement*)node0;
        
        ir_looping_scope_push(ir, node->code);
        DEFER(ir_looping_scope_pop(ir));
        
        IR_Group array_expression = ir_from_node(ir, node->expresion, ExpresionContext_from_void());
        
        if (!array_expression.success)
            return ir_failed();
        
        Value array = array_expression.value;
        
        if (array.vtype->kind != VariableKind_Array) {
            report_for_expects_an_array(node->code);
            return ir_failed();
        }
        
        // internal_index := 0
        IR_Group init = array_expression;
        init = IR_append(init, ir_from_define_temporal(ir, VType_Int, node->code));
        Value internal_index = init.value;
        init = IR_append(init, ir_from_assignment(ir, false, internal_index, value_from_int(0), BinaryOperator_None, node->code));
        
        if (!init.success)
            return ir_failed();
        
        // (internal_index < array.count)
        VariableTypeChild count_info = vtype_get_property(array.vtype, "count");
        IR_Group condition = ir_from_child(ir, array, value_from_int(count_info.index), count_info.is_member, count_info.vtype, node->code);
        condition = IR_append(condition, ir_from_binary_operator(ir, internal_index, condition.value, BinaryOperator_LessThan, false, node->code));
        
        if (!condition.success)
            return ir_failed();
        
        // "element" := array[internal_index]
        IR_Group content = ir_from_define_local(ir, node->element_name, vtype_from_reference(array.vtype->child_next), node->code);
        Value element = content.value;
        content = IR_append(content, ir_from_child(ir, array, internal_index, true, array.vtype->child_next, node->code));
        content = IR_append(content, ir_from_reference(ir, false, content.value, node->code));
        content = IR_append(content, ir_from_assignment(ir, true, element, content.value, BinaryOperator_None, node->code));
        
        // "index" := internal_index
        if (node->index_name.size > 0) {
            content = IR_append(content, ir_from_define_local(ir, node->index_name, VType_Int, node->code));
            Value index = content.value;
            content = IR_append(content, ir_from_assignment(ir, true, index, internal_index, BinaryOperator_None, node->code));
        }
        
        content = IR_append(content, ir_from_content_node(ir, node->content));
        
        // internal_index += 1
        IR_Group update = ir_from_assignment(ir, false, internal_index, value_from_int(1), BinaryOperator_Addition, node->code);
        
        if (!content.success || !update.success)
            return ir_failed();
        
        return ir_from_loop(ir, init, condition, content, update, node->code);
    }
    
    if (node0->kind == OpKind_ArrayExpresion)
    {
        OpNode_ArrayExpresion* node = (OpNode_ArrayExpresion*)node0;
        
        VariableType* array_vtype = void_vtype;
        
        if (node->type->kind == OpKind_ObjectType) {
            array_vtype = vtype_from_node((OpNode_ObjectType*)node->type);
            if (array_vtype == nil_vtype) return ir_failed();
        }
        
        if (array_vtype == void_vtype && context.vtype->kind == VariableKind_Array) {
            array_vtype = context.vtype;
        }
        
        IR_Group out = ir_from_none();
        
        if (node->is_empty)
        {
            if (array_vtype == void_vtype) {
                report_error(node->code, "Unresolved type for array expression");
                return ir_failed();
            }
            
            VariableType* base_vtype = array_vtype;
            u32 starting_dimensions = 0;
            
            if (array_vtype->kind == VariableKind_Array) {
                base_vtype = array_vtype->child_base;
                starting_dimensions = array_vtype->array_dimensions;
            }
            
            Array<Value> dimensions = array_make<Value>(scratch.arena, node->nodes.count + starting_dimensions);
            
            foreach(i, starting_dimensions) {
                dimensions[i] = value_from_int(0);
            }
            
            foreach(i, node->nodes.count)
            {
                out = IR_append(out, ir_from_node(ir, node->nodes[i], ExpresionContext_from_vtype(VType_Int, false)));
                if (!out.success) return ir_failed();
                
                Value dim = out.value;
                
                if (dim.vtype->ID != VTypeID_Int) {
                    report_dimensions_expects_an_int(node->code);
                    return ir_failed();
                }
                
                u32 index = starting_dimensions + i;
                dimensions[index] = dim;
            }
            
            out.value = value_from_empty_array(ir->arena, base_vtype, dimensions);
            return out;
        }
        else
        {
            Array<Value> elements = array_make<Value>(scratch.arena, node->nodes.count);
            
            foreach(i, node->nodes.count) {
                out = IR_append(out, ir_from_node(ir, node->nodes[i], ExpresionContext_from_inference()));
                elements[i] = out.value;
            }
            
            if (array_vtype == void_vtype && elements.count > 0)
            {
                VariableType* element_vtype = elements[0].vtype;
                
                for (u32 i = 1; i < elements.count; i++) {
                    if (elements[i].vtype != element_vtype) {
                        element_vtype = nil_vtype;
                        break;
                    }
                }
                
                if (element_vtype != nil_vtype) {
                    array_vtype = vtype_from_dimension(element_vtype, 1);
                }
            }
            
            if (array_vtype == void_vtype) {
                report_error(node->code, "Unresolved type for array expression");
                return ir_failed();
            }
            
            VariableType* element_vtype = array_vtype->child_next;
            
            // Assert same vtype
            foreach(i, elements.count) {
                if (elements[i].vtype->ID != element_vtype->ID) {
                    report_type_missmatch_array_expr(node->code, element_vtype->name, elements[i].vtype->name);
                    return ir_failed();
                }
            }
            
            out.value = value_from_array(ir->arena, array_vtype, elements);
        }
        
        
        return out;
    }
    
    report_expr_semantic_unknown(node0->code);
    return ir_failed();
}

internal_fn IR_Context* ir_context_alloc(Interpreter* inter, VariableType* return_vtype)
{
    Arena* arena = yov->static_arena;// TODO(Jose):
    
    IR_Context* ir = arena_push_struct<IR_Context>(arena);
    ir->inter = inter;
    ir->next_register_index = 0;
    ir->arena = arena;
    ir->temp_arena = yov->temp_arena;// TODO(Jose):
    ir->objects = pooled_array_make<IR_Object>(ir->temp_arena, 32);
    ir->looping_scopes = pooled_array_make<IR_LoopingScope>(ir->temp_arena, 8);
    ir->scope = 0;
    ir->return_vtype = return_vtype;
    return ir;
}

internal_fn Unit unit_make(IR_Unit* unit)
{
    if (unit->kind == UnitKind_Error || unit->kind == UnitKind_Empty) return {};
    
    UnitKind kind = unit->kind;
    
    Unit dst = {};
    dst.kind = kind;
    dst.code = unit->code;
    dst.dst_index = unit->dst_index;
    
    if (kind == UnitKind_Store) {
        dst.store.vtype = unit->store.vtype;
        dst.store.global_identifier = unit->store.global_identifier;
    }
    else if (kind == UnitKind_Assignment) {
        dst.assignment.src = unit->assignment.src;
    }
    else if (kind == UnitKind_FunctionCall) {
        dst.function_call.fn = unit->function_call.fn;
        dst.function_call.parameters = unit->function_call.parameters;
    }
    else if (kind == UnitKind_Return) {
    }
    else if (kind == UnitKind_Jump) {
        dst.jump.condition = unit->jump.condition;
        dst.jump.src = unit->jump.src;
        dst.jump.offset = i32_min; // Calculated later
    }
    else if (kind == UnitKind_BinaryOperation) {
        dst.binary_op.src0 = unit->binary_op.src0;
        dst.binary_op.src1 = unit->binary_op.src1;
        dst.binary_op.op = unit->binary_op.op;
    }
    else if (kind == UnitKind_SignOperation) {
        dst.sign_op.src = unit->sign_op.src;
        dst.sign_op.op = unit->sign_op.op;
    }
    else if (kind == UnitKind_Child) {
        dst.child.src = unit->child.src;
        dst.child.child_index = unit->child.child_index;
        dst.child.child_is_member = unit->child.child_is_member;
    }
    else if (kind == UnitKind_ResultEval) {
        dst.result_eval.src = unit->result_eval.src;
    }
    else {
        invalid_codepath();
    }
    
    return dst;
}

internal_fn IR ir_make(Arena* arena, u32 register_count, IR_Group out)
{
    SCRATCH(arena);
    
    Array<IR_Unit*> units = array_make<IR_Unit*>(scratch.arena, out.unit_count);
    {
        IR_Unit* unit = out.first;
        foreach(i, units.count) {
            if (unit == NULL) {
                invalid_codepath();
                units.count = i;
                break;
            }
            units[i] = unit;
            unit = unit->next;
        }
    }
    
    PooledArray<Unit> instructions = pooled_array_make<Unit>(scratch.arena, 64);
    PooledArray<u32> mapping = pooled_array_make<u32>(scratch.arena, 64);
    
    foreach(i, units.count) {
        Unit instr = unit_make(units[i]);
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
        
        i32 ir_index = -1;
        foreach(i, units.count) {
            if (units[i] == ir->jump.unit) {
                ir_index = i;
                break;
            }
        }
        
        i32 jump_index = -1;
        for (auto it = pooled_array_make_iterator(&mapping); it.valid; ++it)
        {
            jump_index = it.index;
            u32 v = *it.value;
            if (v >= ir_index) break;
        }
        
        if (ir_index < 0 || jump_index < 0) {
            invalid_codepath();
            *rt = {};
            continue;
        }
        
        rt->jump.offset = jump_index - (i32)it.index - 1;
    }
    
    IR ir = {};
    ir.register_count = register_count;
    ir.value = out.value;
    ir.success = out.success;
    ir.instructions = array_from_pooled_array(arena, instructions);
    
    return ir;
}

internal_fn b32 validate_return_path(Array<Unit> units)
{
    i32 next_jump_index = -1;
    foreach(i, units.count) {
        if (units[i].kind == UnitKind_Jump) {
            next_jump_index = i;
            break;
        }
    }
    
    if (next_jump_index >= 0)
    {
        Unit jump = units[next_jump_index];
        b32 has_condition = jump.jump.condition != 0;
        b32 is_backwards = jump.jump.offset < 0;
        
        Array<Unit> path_prev = array_subarray(units, 0, next_jump_index);
        i32 index = next_jump_index + 1 + jump.jump.offset;
        Array<Unit> path_fail = array_subarray(units, next_jump_index + 1, units.count - next_jump_index - 1);
        Array<Unit> path_jump = array_subarray(units, index, units.count - index);
        
        if (validate_return_path(path_prev)) return true;
        
        b32 jump_res = is_backwards || validate_return_path(path_jump);
        b32 fail_res = has_condition || validate_return_path(path_fail);
        return jump_res && fail_res;
    }
    else {
        foreach(i, units.count) {
            if (units[i].kind == UnitKind_Return) return true;
        }
        return false;
    }
}

IR ir_generate_from_function_definition(Interpreter* inter, FunctionDefinition* fn)
{
    SCRATCH();
    
    OpNode* node = fn->defined.block;
    VariableType* return_vtype = fn->return_vtype;
    
    IR_Context* ir = ir_context_alloc(inter, return_vtype);
    
    IR_Group out = ir_from_none();
    
    foreach(i, fn->parameters.count) {
        // Automatically assigned on runtime
        ir_define_object(ir, fn->parameters[i].name, fn->parameters[i].vtype, ir->scope, IR_ObjectContext_Parameter);
    }
    
    IR_Object* return_obj = NULL;
    if (return_vtype != void_vtype) {
        return_obj = ir_define_object(ir, "return", return_vtype, ir->scope, IR_ObjectContext_ReturnRoot);
    }
    
    if (vtype_is_tuple(return_vtype)) {
        foreach(i, fn->returns.count) {
            ir_define_object(ir, fn->returns[i].name, fn->returns[i].vtype, ir->scope, IR_ObjectContext_Return);
        }
    }
    if (return_vtype != void_vtype) {
        
        // TODO(Jose): Default initialization here
    }
    
    out = IR_append(out, ir_from_node(ir, node, ExpresionContext_from_void()));
    
    // Ensure composition of tuple before any return
    if (vtype_is_tuple(return_vtype) && out.last != NULL && out.last->kind != UnitKind_Return) {
        out = IR_append(out, ir_from_return(ir, ir_from_none(), node->code));
    }
    
    out.value = value_none();
    if (return_obj != NULL) {
        out.value = value_from_ir_object(return_obj);
    }
    
    IR res = ir_make(ir->arena, ir->next_register_index, out);
    
    // Validation
    if (res.success)
    {
        Array<Unit> units = res.instructions;
        
        // TODO(Jose): Check for infinite loops
        
        // Check all paths have a return
        b32 expects_return = !vtype_is_tuple(return_vtype) && return_vtype != void_vtype;
        if (expects_return && !validate_return_path(units)) {
            report_function_no_return(fn->defined.block->code, fn->identifier);
        }
    }
    
    return res;
}

IR ir_generate_from_initializer(Interpreter* inter, OpNode* node, ExpresionContext context)
{
    IR_Context* ir = ir_context_alloc(inter, void_vtype);
    IR_Group out = ir_from_node(ir, node, context);
    if (out.value.vtype == void_vtype && context.vtype->ID > VTypeID_Any) {
        out = IR_append(out, ir_from_default_initializer(ir, context.vtype, node->code));
    }
    return ir_make(ir->arena, ir->next_register_index, out);
}

IR ir_generate_from_value(Value value) {
    return ir_make(yov->static_arena, 0, ir_from_none(value));
}

b32 ct_value_from_node(Interpreter* inter, OpNode* node, VariableType* expected_vtype, Value* value)
{
    IR ir = ir_generate_from_initializer(inter, node, ExpresionContext_from_vtype(expected_vtype, false));
    
    *value = value_none();
    
    if (!ir.success) return false;
    
    Value v = ir.value;
    
    if (expected_vtype->ID > VTypeID_Any && v.vtype != expected_vtype) {
        report_type_missmatch_assign(node->code, v.vtype->name, expected_vtype->name);
        return false;
    }
    
    if (!value_is_compiletime(v)) {
        report_error(node->code, "Expecting a compile time resolved value");
        return false;
    }
    
    *value = v;
    return true;
}

b32 ct_string_from_node(Arena* arena, Interpreter* inter, OpNode* node, String* str)
{
    *str = {};
    Value v;
    if (!ct_value_from_node(inter, node, VType_String, &v)) return false;
    
    *str = string_from_compiletime(arena, inter, v);
    return true;
}

b32 ct_bool_from_node(Interpreter* inter, OpNode* node, b32* b)
{
    *b = {};
    Value v;
    if (!ct_value_from_node(inter, node, VType_Bool, &v)) return false;
    
    *b = bool_from_compiletime(inter, v);
    return true;
}

internal_fn Array<OpNode_StructDefinition*> get_struct_definitions(Arena* arena, Array<OpNode*> nodes)
{
    SCRATCH(arena);
    PooledArray<OpNode_StructDefinition*> result = pooled_array_make<OpNode_StructDefinition*>(scratch.arena, 8);
    
    foreach(i, nodes.count) {
        OpNode* node0 = nodes[i];
        if (node0->kind == OpKind_StructDefinition)
        {
            OpNode_StructDefinition* node = (OpNode_StructDefinition*)node0;
            
            b32 duplicated = false;
            
            foreach(i, result.count) {
                if (string_equals(result[i]->identifier, node->identifier)) {
                    duplicated = true;
                    break;
                }
            }
            
            if (duplicated) {
                report_symbol_duplicated(node->code, node->identifier);
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
    PooledArray<String> names = pooled_array_make<String>(scratch.arena, 8);
    
    foreach(i, node->members.count) {
        OpNode_ObjectDefinition* member = node->members[i];
        VariableType* member_vtype = vtype_from_name(member->type->name);
        if (member_vtype->ID != VTypeID_Unknown) continue;
        
        foreach(j, struct_nodes.count) {
            OpNode_StructDefinition* struct0 = struct_nodes[j];
            if (struct0 == node) continue;
            if (string_equals(member->type->name, struct0->identifier)) {
                array_add(&names, member->type->name);
                break;
            }
        }
    }
    
    return array_from_pooled_array(arena, names);
}

internal_fn i32 OpNode_StructDefinition_compare_dependency_index(const void* _0, const void* _1)
{
    auto node0 = *(const OpNode_StructDefinition**)_0;
    auto node1 = *(const OpNode_StructDefinition**)_1;
    
    if (node0->dependency_index == node1->dependency_index) return 0;
    return (node0->dependency_index < node1->dependency_index) ? -1 : 1;
}

internal_fn b32 validate_arg_name(String name, CodeLocation code)
{
    b32 valid_chars = true;
    
    u64 cursor = 0;
    while (cursor < name.size) {
        u32 codepoint = string_get_codepoint(name, &cursor);
        
        b32 valid = false;
        if (codepoint_is_text(codepoint)) valid = true;
        if (codepoint_is_number(codepoint)) valid = true;
        if (codepoint == '-') valid = true;
        if (codepoint == '_') valid = true;
        
        if (!valid) {
            valid_chars = false;
            break;
        }
    }
    
    if (!valid_chars || name.size == 0) {
        report_arg_invalid_name(code, name);
        return false;
    }
    
    if (find_arg_definition_by_name(name) != NULL) {
        report_arg_duplicated_name(code, name);
        return false;
    }
    
    return true;
}

internal_fn void extract_definitions(Interpreter* inter, OpNode_Block* block, b32 is_main_script, b32 require_args, b32 require_intrinsics)
{
    SCRATCH();
    
    // Assert only definitions in root block
    foreach(i, block->ops.count)
    {
        OpNode* node = block->ops[i];
        
        b32 is_definition = false;
        if (node->kind == OpKind_EnumDefinition) is_definition = true;
        if (node->kind == OpKind_ArgDefinition) is_definition = true;
        if (node->kind == OpKind_StructDefinition) is_definition = true;
        if (node->kind == OpKind_FunctionDefinition) is_definition = true;
        if (node->kind == OpKind_Import) is_definition = true;
        if (node->kind == OpKind_ObjectDefinition) is_definition = true;
        
        if (!is_definition) {
            report_unsupported_operations(node->code);
            return;
        }
    }
    
    // Enums
    foreach(i, block->ops.count)
    {
        OpNode* node0 = block->ops[i];
        if (node0->kind != OpKind_EnumDefinition) continue;
        
        OpNode_EnumDefinition* node = (OpNode_EnumDefinition*)node0;
        
        assert(node->values.count == node->names.count);
        
        b32 valid = true;
        
        String name = node->identifier;
        
        if (definition_exists(name)) {
            report_symbol_duplicated(node->code, name);
            valid = false;
        }
        
        Array<i64> values = array_make<i64>(scratch.arena, node->values.count);
        foreach(i, values.count)
        {
            OpNode* value_node = node->values[i];
            
            if (value_node == NULL || value_node->kind == OpKind_None) {
                values[i] = i;
                continue;
            }
            
            IR ir = ir_generate_from_initializer(inter, value_node, ExpresionContext_from_vtype(VType_Int, false));
            if (!ir.success) continue;
            
            if (ir.value.kind != ValueKind_Literal || ir.value.vtype->ID != VTypeID_Int) {
                report_error(node->code, "Enum value expects an Int literal");
                valid = false;
                continue;
            }
            
            values[i] = ir.value.literal_int;
        }
        
        if (!valid) continue;
        
        define_enum(name, node->names, values);
    }
    
    // Structs
    {
        Array<OpNode_StructDefinition*> struct_nodes = get_struct_definitions(scratch.arena, block->ops);
        
        // Solve struct dependencies
        {
            foreach(i, struct_nodes.count) struct_nodes[i]->dependency_index = 0;
            
            foreach(i, struct_nodes.count)
            {
                OpNode_StructDefinition* node = struct_nodes[i];
                b32 valid = true;
                
                Array<String> deps0 = get_struct_dependencies(scratch.arena, node, struct_nodes);
                
                foreach(j, deps0.count)
                {
                    String dep_name = deps0[j];
                    
                    OpNode_StructDefinition* node1 = NULL;
                    foreach(i, struct_nodes.count) {
                        if (string_equals(struct_nodes[i]->identifier, dep_name)) {
                            node1 = struct_nodes[i];
                            break;
                        }
                    }
                    assert(node1 != NULL);
                    
                    Array<String> deps1 = get_struct_dependencies(scratch.arena, node1, struct_nodes);
                    
                    foreach(i, deps1.count) {
                        if (string_equals(deps1[i], node->identifier)) {
                            report_struct_circular_dependency(node->code);
                            valid = false;
                            break;
                        }
                    }
                    
                    if (!valid) break;
                    
                    node->dependency_index = MAX(node1->dependency_index + 1, node->dependency_index);
                }
            }
            
            array_sort(struct_nodes, OpNode_StructDefinition_compare_dependency_index);
        }
        
        foreach(i, struct_nodes.count)
        {
            OpNode_StructDefinition* node = struct_nodes[i];
            
            b32 valid = true;
            String name = node->identifier;
            
            if (definition_exists(name)) {
                report_symbol_duplicated(node->code, name);
                valid = false;
            }
            
            VariableType* struct_vtype = vtype_define_struct(name, node, false);
            
            PooledArray<ObjectDefinition> members = pooled_array_make<ObjectDefinition>(scratch.arena, 8);
            
            foreach(i, node->members.count) {
                OpNode_ObjectDefinition* member_node = node->members[i];
                
                if (member_node->type->name.size == 0) {
                    report_struct_implicit_member_type(node->code);
                    valid = false;
                    continue;
                }
                
                VariableType* vtype = vtype_from_node(member_node->type);
                if (vtype->ID == VTypeID_Unknown) {
                    valid = false;
                    continue;
                }
                
                if (vtype == struct_vtype) {
                    report_struct_recursive(node->code);
                    valid = false;
                    continue;
                }
                
                foreach(j, member_node->names.count)
                {
                    ObjectDefinition def = {};
                    def.name = member_node->names[j];
                    def.vtype = vtype;
                    def.ir = {};
                    array_add(&members, def);
                }
            }
            
            if (!valid) continue;
            
            vtype_init_struct(struct_vtype, array_from_pooled_array(scratch.arena, members));
        }
    }
    
    // Functions
    foreach(i, block->ops.count)
    {
        OpNode* node0 = block->ops[i];
        if (node0->kind != OpKind_FunctionDefinition) continue;
        
        OpNode_FunctionDefinition* node = (OpNode_FunctionDefinition*)node0;
        
        b32 valid = true;
        
        if (definition_exists(node->identifier)) {
            report_symbol_duplicated(node->code, node->identifier);
            valid = false;
        }
        
        Array<ObjectDefinition> parameters = array_make<ObjectDefinition>(scratch.arena, node->parameters.count);
        
        foreach(i, parameters.count)
        {
            OpNode_ObjectDefinition* param_node = node->parameters[i];
            ObjectDefinition* def = &parameters[i];
            
            VariableType* vtype = vtype_from_node(param_node->type);
            
            if (vtype == void_vtype || vtype == nil_vtype) {
                valid = false;
                continue;
            }
            
            if (param_node->names.count != 1) {
                report_error(param_node->code, "Parameters requires a single name per definition");
                valid = false;
                continue;
            }
            
            def->vtype = vtype;
            def->name = param_node->names[0];
            def->ir = ir_generate_from_initializer(inter, param_node->assignment, ExpresionContext_from_vtype(vtype, false));
        }
        
        PooledArray<ObjectDefinition> returns_list = pooled_array_make<ObjectDefinition>(scratch.arena, 8);
        
        foreach(i, node->returns.count)
        {
            OpNode* return_node0 = node->returns[i];
            
            if (return_node0->kind == OpKind_ObjectType)
            {
                OpNode_ObjectType* return_node = (OpNode_ObjectType*)return_node0;
                
                ObjectDefinition def = {};
                def.vtype = vtype_from_node(return_node);
                def.name = "return";
                def.ir = {};
                array_add(&returns_list, def);
            }
            else if (return_node0->kind == OpKind_ObjectDefinition)
            {
                OpNode_ObjectDefinition* return_node = (OpNode_ObjectDefinition*)return_node0;
                
                VariableType* vtype = vtype_from_node(return_node->type);
                
                if (vtype == void_vtype) {
                    IR out = ir_generate_from_initializer(inter, return_node->assignment, ExpresionContext_from_inference());
                    vtype = out.value.vtype;
                }
                
                if (vtype == void_vtype || vtype == nil_vtype) {
                    report_error(return_node->code, "Unresolved return type");
                    valid = false;
                    continue;
                }
                
                ObjectDefinition def = {};
                def.vtype = vtype;
                def.ir = ir_generate_from_initializer(inter, return_node->assignment, ExpresionContext_from_vtype(vtype, false));
                foreach(i, return_node->names.count) {
                    def.name = return_node->names[i];
                    array_add(&returns_list, def);
                }
            }
            else {
                invalid_codepath();
            }
        }
        
        Array<ObjectDefinition> returns = array_from_pooled_array(scratch.arena, returns_list);
        
        b32 is_intrinsic = true;
        OpNode_Block* block = NULL;
        
        if (node->block->kind == OpKind_Block) {
            is_intrinsic = false;
            block = (OpNode_Block*)node->block;
        }
        else if (node->block->kind == OpKind_None) { }
        else {
            valid = false;
            invalid_codepath();
        }
        
        if (!valid) continue;
        
        if (is_intrinsic)
        {
            b32 exists = find_function(node->identifier) != NULL;
            
            if (!exists) {
                if (require_intrinsics) {
                    report_intrinsic_not_resolved(node->code, node->identifier);
                }
                else {
                    define_intrinsic_function(node->code, NULL, node->identifier, parameters, returns);
                }
            }
        }
        else define_function(node->code, node->identifier, parameters, returns, block);
    }
    
    // Args
    if (is_main_script)
    {
        foreach(i, block->ops.count)
        {
            OpNode* node0 = block->ops[i];
            if (node0->kind != OpKind_ArgDefinition) continue;
            OpNode_ArgDefinition* node = (OpNode_ArgDefinition*)node0;
            
            b32 valid = true;
            b32 required = false;
            String name = string_format(yov->static_arena, "-%S", node->identifier);
            Value default_value = value_none();
            b32 has_explicit_vtype = false;
            String description = {};
            
            VariableType* vtype = VType_Bool;
            
            if (node->type->kind == OpKind_ObjectType) {
                vtype = vtype_from_node((OpNode_ObjectType*)node->type);
                if (vtype->ID == VTypeID_Unknown) continue;
                has_explicit_vtype = true;
            }
            
            if (node->name->kind == OpKind_Assignment)
            {
                OpNode_Assignment* assignment = (OpNode_Assignment*)node->name;
                
                if (!ct_string_from_node(scratch.arena, inter, assignment->source, &name)) {
                    valid = false;
                }
                else if (!validate_arg_name(name, assignment->code)) {
                    valid = false;
                }
            }
            
            if (node->description->kind == OpKind_Assignment)
            {
                OpNode_Assignment* assignment = (OpNode_Assignment*)node->description;
                
                if (!ct_string_from_node(scratch.arena, inter, assignment->source, &description)) {
                    valid = false;
                }
            }
            
            if (node->required->kind == OpKind_Assignment)
            {
                OpNode_Assignment* assignment = (OpNode_Assignment*)node->required;
                
                if (!ct_bool_from_node(inter, assignment->source, &required)) {
                    valid = false;
                }
            }
            
            if (node->default_value->kind == OpKind_Assignment)
            {
                OpNode_Assignment* assignment = (OpNode_Assignment*)node->default_value;
                
                if (!ct_value_from_node(inter, assignment->source, vtype, &default_value)) continue;
                
                if (has_explicit_vtype && vtype != default_value.vtype) {
                    report_type_missmatch_assign(assignment->code, default_value.vtype->name, vtype->name);
                    continue;
                }
                
                vtype = default_value.vtype;
            }
            
            if (definition_exists(node->identifier)) {
                report_symbol_duplicated(node->code, node->identifier);
                valid = false;
            }
            
            Value value = value_none();
            
            if (!valid || require_args)
            {
                required = false;
            }
            else
            {
                // From arg
                if (value.kind == ValueKind_None)
                {
                    ScriptArg* script_arg = yov_find_arg(name);
                    
                    if (script_arg == NULL) {
                        if (required) {
                            report_arg_is_required(node->code, name);
                            continue;
                        }
                    }
                    else
                    {
                        if (script_arg->value.size <= 0)
                        {
                            if (vtype->ID == VTypeID_Bool) {
                                value = value_from_bool(true);
                            }
                        }
                        else
                        {
                            value = value_from_string_expression(yov->static_arena, script_arg->value, vtype);
                        }
                        
                        if (value.kind == ValueKind_None) {
                            report_arg_wrong_value(NO_CODE, name, script_arg->value);
                            continue;
                        }
                    }
                }
                
                // From default
                if (value.kind == ValueKind_None && default_value.kind != ValueKind_None) {
                    value = default_value;
                }
            }
            
            // Default vtype
            if (value.kind == ValueKind_None) {
                value = value_from_default(vtype);
            }
            
            //print_info("Arg %S: name=%S, required=%s\n", node->identifier, name, required ? "true" : "false");
            ObjectDefinition def = define_arg(node->identifier, name, value, required, description);
            global_init(inter, def, node->code);
        }
    }
    
    // Global Objects
    foreach(i, block->ops.count)
    {
        OpNode* node0 = block->ops[i];
        if (node0->kind != OpKind_ObjectDefinition) continue;
        OpNode_ObjectDefinition* node = (OpNode_ObjectDefinition*)node0;
        
        VariableType* vtype = vtype_from_node(node->type);
        
        ExpresionContext context = {};
        if (vtype == void_vtype) context = ExpresionContext_from_inference();
        else context = ExpresionContext_from_vtype(vtype, false);
        
        IR ir = ir_generate_from_initializer(inter, node->assignment, context);
        
        if (vtype == void_vtype) {
            vtype = ir.value.vtype;
        }
        else if (ir.value.vtype->ID > VTypeID_Any && vtype != ir.value.vtype) {
            report_error(node->code, "Type missmatch");
            continue;
        }
        
        if (vtype->ID < VTypeID_Any) {
            report_error(node->code, "Unresolved object type for definition");
            continue;
        }
        
        if (vtype == void_vtype && ir.value.kind != ValueKind_None) vtype = ir.value.vtype;
        
        foreach(i, node->names.count) {
            ObjectDefinition def = obj_def_make(node->names[i], vtype, node->is_constant, ir);
            define_global(def);
            global_init(inter, def, node->code);
        }
    }
}

void ir_generate(Interpreter* inter, b32 require_args, b32 require_intrinsics)
{
    SCRATCH();
    
    // Script Definitions
    for (auto it = pooled_array_make_iterator(&yov->scripts); it.valid; ++it)
    {
        OpNode_Block* ast = it.value->ast;
        
        if (ast->kind != OpKind_Block) {
            invalid_codepath();
            continue;
        }
        
        extract_definitions(inter, ast, it.index == 0, require_args, require_intrinsics);
    }
    
    // Assign IR
    {
        for (auto it = pooled_array_make_iterator(&yov->functions); it.valid; ++it)
        {
            FunctionDefinition* fn = it.value;
            
            if (fn->is_intrinsic) continue;
            
            fn->defined.ir = ir_generate_from_function_definition(inter, fn);
#if DEV_PRINT_AST
            print_units(fn->identifier, fn->defined.ir.instructions);
#endif
        }
        
        for (auto it = pooled_array_make_iterator(&yov->vtype_table); it.valid; ++it)
        {
            VariableType* vtype = it.value;
            
            if (vtype->kind == VariableKind_Struct)
            {
                foreach(i, vtype->_struct.irs.count) {
                    VariableType* member_vtype = vtype->_struct.vtypes[i];
                    OpNode_StructDefinition* node = vtype->_struct.node;
                    if (node == NULL) {
                        vtype->_struct.irs[i] = ir_generate_from_value(value_from_default(member_vtype));
                    }
                    else {
                        OpNode* assignment = vtype->_struct.node->members[i]->assignment;
                        vtype->_struct.irs[i] = ir_generate_from_initializer(inter, assignment, ExpresionContext_from_vtype(member_vtype, false));
                    }
                }
            }
        }
    }
    
    // Validate args
    foreach(i, yov->args.count)
    {
        String name = yov->args[i].name;
        if (string_equals(name, "-help")) continue;
        
        ArgDefinition* def = find_arg_definition_by_name(name);
        if (def == NULL) {
            report_arg_unknown(NO_CODE, name);
        }
    }
}

IR_Object* ir_find_object(IR_Context* ir, String identifier, b32 parent_scopes)
{
    IR_Object* res = NULL;
    
    for (auto it = pooled_array_make_iterator(&ir->objects); it.valid; ++it) {
        IR_Object* obj = it.value;
        if (!parent_scopes && obj->scope != ir->scope) continue;
        if (!string_equals(obj->identifier, identifier)) continue;
        if (res != NULL && res->scope >= obj->scope) continue;
        res = obj;
    }
    
    return res;
}

IR_Object* ir_find_object_from_value(IR_Context* ir, Value value)
{
    if (value.kind == ValueKind_LValue || value.kind == ValueKind_Register) {
        return ir_find_object_from_register(ir, value.reg.index);
    }
    return NULL;
}

IR_Object* ir_find_object_from_register(IR_Context* ir, i32 register_index)
{
    for (auto it = pooled_array_make_iterator(&ir->objects); it.valid; ++it) {
        IR_Object* obj = it.value;
        if (obj->register_index != register_index) continue;
        return obj;
    }
    return NULL;
}

IR_Object* ir_find_object_from_context(IR_Context* ir, IR_ObjectContext context, u32 index)
{
    u32 current = 0;
    for (auto it = pooled_array_make_iterator(&ir->objects); it.valid; ++it) {
        IR_Object* obj = it.value;
        if (obj->context != context) continue;
        if (current != index) {
            current++;
            continue;
        }
        return obj;
    }
    return NULL;
}

IR_Object* ir_define_object(IR_Context* ir, String identifier, VariableType* vtype, i32 scope, IR_ObjectContext context)
{
    assert(scope != ir->scope || ir_find_object(ir, identifier, false) == NULL);
    
    IR_Object* def = array_add(&ir->objects);
    def->identifier = string_copy(ir->arena, identifier);
    def->vtype = vtype;
    def->register_index = ir_alloc_register(ir);
    def->scope = scope;
    def->context = context;
    return def;
}

Symbol ir_find_symbol(IR_Context* ir, String identifier)
{
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
        FunctionDefinition* fn = find_function(identifier);
        
        if (fn != NULL) {
            symbol.type = SymbolType_Function;
            symbol.function = fn;
            return symbol;
        }
    }
    
    {
        VariableType* vtype = vtype_from_name(identifier);
        
        if (vtype->ID != VTypeID_Unknown) {
            symbol.type = SymbolType_Type;
            symbol.vtype = vtype;
            return symbol;
        }
    }
    
    return {};
}

IR_LoopingScope* ir_looping_scope_push(IR_Context* ir, CodeLocation code)
{
    IR_Unit* continue_unit = ir_alloc_empty(ir, code);
    IR_Unit* break_unit = ir_alloc_empty(ir, code);
    
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
    assert(ir->scope >= 0);
    
    for (i32 i = (i32)ir->objects.count - 1; i >= 0; --i)
    {
        if (ir->objects[i].scope > ir->scope) {
            array_erase(&ir->objects, i);
        }
    }
}

String string_from_register(Arena* arena, i32 index)
{
    if (index < 0) return "rE";
    return string_format(arena, "r%i", index);
}

internal_fn String string_from_unit_info(Arena* arena, Unit unit)
{
    SCRATCH(arena);
    
    String dst = string_from_register(scratch.arena, unit.dst_index);
    
    if (unit.kind == UnitKind_Error) return {};
    
    if (unit.kind == UnitKind_Store) {
        StringBuilder builder = string_builder_make(scratch.arena);
        appendf(&builder, "%S :: %S", dst, unit.store.vtype->name);
        if (unit.store.global_identifier.size > 0) {
            appendf(&builder, " (global '%S')", unit.store.global_identifier);
        }
        return string_from_builder(arena, &builder);
    }
    
    if (unit.kind == UnitKind_Assignment)
    {
        String src = string_from_value(scratch.arena, unit.assignment.src);
        return string_format(arena, "%S = %S", dst, src);
    }
    
    if (unit.kind == UnitKind_FunctionCall)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        if (unit.dst_index >= 0) {
            appendf(&builder, "%S = ", dst);
        }
        
        String identifier = unit.function_call.fn->identifier;
        Array<Value> params = unit.function_call.parameters;
        
        appendf(&builder, "%S(", identifier);
        
        foreach(i, params.count) {
            String param = string_from_value(scratch.arena, params[i]);
            appendf(&builder, "%S", param);
            if (i < params.count - 1) append(&builder, ", ");
        }
        append(&builder, ")");
        return string_from_builder(arena, &builder);
    }
    
    if (unit.kind == UnitKind_Return) return "";
    
    if (unit.kind == UnitKind_Jump)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        String condition = string_from_value(scratch.arena, unit.jump.src);
        if (unit.jump.condition > 0) appendf(&builder, "%S ", condition);
        else if (unit.jump.condition < 0) appendf(&builder, "!%S ", condition);
        appendf(&builder, "%i", unit.jump.offset);
        return string_from_builder(arena, &builder);
    }
    
    if (unit.kind == UnitKind_BinaryOperation)
    {
        String op = string_from_binary_operator(unit.binary_op.op);
        String src0 = string_from_value(scratch.arena, unit.binary_op.src0);
        String src1 = string_from_value(scratch.arena, unit.binary_op.src1);
        return string_format(arena, "%S = %S %S %S", dst, src0, op, src1);
    }
    
    if (unit.kind == UnitKind_SignOperation)
    {
        String op = string_from_binary_operator(unit.sign_op.op);
        String src = string_from_value(scratch.arena, unit.sign_op.src);
        return string_format(arena, "%S = %S%S", dst, op, src);
    }
    
    if (unit.kind == UnitKind_Child)
    {
        Value src = unit.child.src;
        Value index = unit.child.child_index;
        b32 is_member = unit.child.child_is_member;
        b32 is_literal_int = index.kind == ValueKind_Literal && index.vtype->ID == VTypeID_Int;
        
        String op = {};
        
        if (is_member && src.vtype->kind == VariableKind_Struct && is_literal_int) {
            op = src.vtype->_struct.names[index.literal_int];
            op = string_format(scratch.arena, ".%S", op);
        }
        else if (!is_member && is_literal_int) {
            Array<VariableTypeChild> props = vtype_get_properties(src.vtype);
            op = string_format(scratch.arena, ".%S", props[index.literal_int].name);
        }
        else {
            String index = string_from_value(scratch.arena, unit.child.child_index);
            op = string_format(scratch.arena, "[%S]", index);
        }
        
        String src_str = string_from_value(scratch.arena, unit.child.src);
        return string_format(arena, "%S = %S%S", dst, src_str, op);
    }
    
    if (unit.kind == UnitKind_ResultEval) {
        return string_from_value(arena, unit.result_eval.src);
    }
    
    invalid_codepath();
    return {};
}

String string_from_unit_kind(Arena* arena, UnitKind unit)
{
    SCRATCH(arena);
    
    if (unit == UnitKind_Error) return "error";
    if (unit == UnitKind_Store) return "store";
    if (unit == UnitKind_Assignment) return "assign";
    if (unit == UnitKind_FunctionCall) return "call";
    if (unit == UnitKind_Return) return "return";
    if (unit == UnitKind_Jump) return "jump";
    if (unit == UnitKind_BinaryOperation) return "bin_op";
    if (unit == UnitKind_SignOperation) return "sgn_op";
    if (unit == UnitKind_Child) return "child";
    if (unit == UnitKind_ResultEval) return "res_ev";
    if (unit == UnitKind_Empty) return "empty";
    
    invalid_codepath();
    return "?";
}

String string_from_unit(Arena* arena, u32 index, u32 index_digits, u32 line_digits, Unit unit)
{
    SCRATCH(arena);
    
    StringBuilder builder = string_builder_make(scratch.arena);
    
    String index_str = string_format(scratch.arena, "%u", index);
    String line_str = string_format(scratch.arena, "%u", unit.code.line);
    
    for (u32 i = (u32)index_str.size; i < index_digits; ++i) append(&builder, "0");
    append(&builder, index_str);
    append(&builder, " (");
    for (u32 i = (u32)line_str.size; i < line_digits; ++i) append(&builder, "0");
    append(&builder, line_str);
    append(&builder, ") ");
    
    String name = string_from_unit_kind(scratch.arena, unit.kind);
    
    append(&builder, name);
    for (u32 i = (u32)name.size; i < 7; ++i) append(&builder, " ");
    
    String info = string_from_unit_info(scratch.arena, unit);
    append(&builder, info);
    
    return string_from_builder(arena, &builder);
}

void print_units(String name, Array<Unit> units)
{
    SCRATCH();
    
    u32 max_index = units.count;
    u32 max_line = 0;
    foreach(i, units.count) {
        max_line = MAX(units[i].code.line, max_line);
    }
    
    // TODO(Jose): Use log
    u32 index_digits = 0;
    u32 aux = max_index;
    while (aux != 0) {
        aux /= 10;
        index_digits++;
    }
    
    u32 line_digits = 0;
    aux = max_line;
    while (aux != 0) {
        aux /= 10;
        line_digits++;
    }
    
    print_info("%S:\n", name);
    foreach(i, units.count) {
        print_info("%S\n", string_from_unit(scratch.arena, i, index_digits, line_digits, units[i]));
    }
    print_info("\n");
}