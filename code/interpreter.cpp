#include "inc.h"

constexpr Object _make_object(i32 vtype) {
    Object obj{};
    obj.vtype = vtype;
    return obj;
}
constexpr ObjectRef _make_object_ref(i32 vtype, Object* obj) {
    ObjectRef ref{};
    ref.vtype = vtype;
    ref.object = obj;
    return ref;
}

read_only Object _nil_obj = _make_object(VType_Unknown);
read_only Object _null_obj = _make_object(VType_Void);
read_only ObjectRef _nil_ref = _make_object_ref(VType_Unknown, nil_obj);

Object* nil_obj = &_nil_obj;
Object* null_obj = &_null_obj;
ObjectRef* nil_ref = &_nil_ref;

b32 validate_assignment(Interpreter* inter, i32 dst_vtype, Value src)
{
    SCRATCH();
    if (dst_vtype == VType_Any) {
        return true;
    }
    
    i32 src_vtype = value_get_expected_vtype(src);
    
    if (inter->mode != InterpreterMode_Execute)
    {
        if (src.vtype == VType_Any) {
            src_vtype = dst_vtype;
        }
    }
    
    if (dst_vtype != src_vtype) return false;
    
    if (is_null(src)) {
        if (dst_vtype <= 0) return false;
        if (dst_vtype == VType_Any) return false;
    }
    
    return true;
}

internal_fn Value solve_binary_operation(Interpreter* inter, Value left, Value right, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    
    i32 left_vtype = value_get_expected_vtype(left);
    i32 right_vtype = value_get_expected_vtype(right);
    
    VariableType left_type = vtype_get(inter, left_vtype);
    VariableType right_type = vtype_get(inter, right_vtype);
    
    if (is_int(left) && is_int(right)) {
        if (op == BinaryOperator_Addition) return alloc_int(inter, get_int(left) + get_int(right));
        if (op == BinaryOperator_Substraction) return alloc_int(inter, get_int(left) - get_int(right));
        if (op == BinaryOperator_Multiplication) return alloc_int(inter, get_int(left) * get_int(right));
        if (op == BinaryOperator_Division || op == BinaryOperator_Modulo) {
            i64 divisor = get_int(right);
            if (divisor == 0) {
                report_zero_division(code);
                return alloc_int(inter, i64_max);
            }
            if (op == BinaryOperator_Modulo) return alloc_int(inter, get_int(left) % divisor);
            return alloc_int(inter, get_int(left) / divisor);
        }
        if (op == BinaryOperator_Equals) return alloc_bool(inter, get_int(left) == get_int(right));
        if (op == BinaryOperator_NotEquals) return alloc_bool(inter, get_int(left) != get_int(right));
        if (op == BinaryOperator_LessThan) return alloc_bool(inter, get_int(left) < get_int(right));
        if (op == BinaryOperator_LessEqualsThan) return alloc_bool(inter, get_int(left) <= get_int(right));
        if (op == BinaryOperator_GreaterThan) return alloc_bool(inter, get_int(left) > get_int(right));
        if (op == BinaryOperator_GreaterEqualsThan) return alloc_bool(inter, get_int(left) >= get_int(right));
    }
    
    if (is_bool(left) && is_bool(right)) {
        if (op == BinaryOperator_LogicalOr) return alloc_bool(inter, get_bool(left) || get_bool(right));
        if (op == BinaryOperator_LogicalAnd) return alloc_bool(inter, get_bool(left) && get_bool(right));
    }
    
    if (is_string(left) && is_string(right))
    {
        if (op == BinaryOperator_Addition) {
            String str = string_format(scratch.arena, "%S%S", get_string(left), get_string(right));
            return alloc_string(inter, str);
        }
        else if (op == BinaryOperator_Division)
        {
            if (os_path_is_absolute(get_string(right))) {
                report_right_path_cant_be_absolute(code);
                return left;
            }
            
            String str = path_append(scratch.arena, get_string(left), get_string(right));
            str = path_resolve(scratch.arena, str);
            
            return alloc_string(inter, str);
        }
        else if (op == BinaryOperator_Equals) {
            return alloc_bool(inter, (b8)string_equals(get_string(left), get_string(right)));
        }
        else if (op == BinaryOperator_NotEquals) {
            return alloc_bool(inter, !(b8)string_equals(get_string(left), get_string(right)));
        }
    }
    
    if (left_vtype == VType_Type && right_vtype == VType_Type)
    {
        i64 left_id = get_int(value_get_member(inter, left, "ID"));
        i64 right_id = get_int(value_get_member(inter, right, "ID"));
        if (op == BinaryOperator_Equals) {
            return alloc_bool(inter, left_id == right_id);
        }
        else if (op == BinaryOperator_NotEquals) {
            return alloc_bool(inter, left_id == right_id);
        }
    }
    
    if ((is_string(left) && is_int(right)) || (is_int(left) && is_string(right)))
    {
        if (op == BinaryOperator_Addition)
        {
            Value string_value = is_string(left) ? left : right;
            Value codepoint_value = is_int(left) ? left : right;
            
            String codepoint_str = string_from_codepoint(scratch.arena, (u32)get_int(codepoint_value));
            
            String left_str = is_string(left) ? get_string(left) : codepoint_str;
            String right_str = is_string(right) ? get_string(right) : codepoint_str;
            
            String str = string_format(scratch.arena, "%S%S", left_str, right_str);
            return alloc_string(inter, str);
        }
    }
    
    if (is_enum(inter, left) && is_enum(inter, right)) {
        if (op == BinaryOperator_Equals) {
            return alloc_bool(inter, get_enum_index(inter, left) == get_enum_index(inter, right));
        }
        else if (op == BinaryOperator_NotEquals) {
            return alloc_bool(inter, get_enum_index(inter, left) != get_enum_index(inter, right));
        }
    }
    
    if (left_type.array_of == right_type.array_of && left_type.kind == VariableKind_Array && right_type.kind == VariableKind_Array)
    {
        i32 element_vtype = left_type.array_of;
        VariableType element_type = vtype_get(inter, element_vtype);
        
        i32 left_count = get_array(left).count;
        i32 right_count = get_array(right).count;
        
        if (op == BinaryOperator_Addition) {
            Value array = alloc_array(inter, element_vtype, left_count + right_count, true);
            for (u32 i = 0; i < left_count; ++i) {
                Value dst = value_get_element(inter, array, i);
                Value src = value_get_element(inter, left, i);
                value_assign_as_new(inter, &dst, src);
            }
            for (u32 i = 0; i < right_count; ++i) {
                Value dst = value_get_element(inter, array, left_count + i);
                Value src = value_get_element(inter, right, i);
                value_assign_as_new(inter, &dst, src);
            }
            return array;
        }
    }
    
    if ((left_type.kind == VariableKind_Array && right_type.kind != VariableKind_Array) || (left_type.kind != VariableKind_Array && right_type.kind == VariableKind_Array))
    {
        VariableType array_type = (left_type.kind == VariableKind_Array) ? left_type : right_type;
        VariableType element_type = (left_type.kind == VariableKind_Array) ? right_type : left_type;
        
        if (array_type.array_of != element_type.vtype) {
            report_type_missmatch_append(code, element_type.name, array_type.name);
            return value_nil();
        }
        
        Value array_src = (left_type.kind == VariableKind_Array) ? left : right;
        Value element = (left_type.kind == VariableKind_Array) ? right : left;
        
        i32 array_src_count = get_array(array_src).count;
        Value array = alloc_array(inter, element_type.vtype, array_src_count + 1, true);
        
        i32 array_offset = (left_type.kind == VariableKind_Array) ? 0 : 1;
        
        for (i32 i = 0; i < array_src_count; ++i) {
            Value dst = value_get_element(inter, array, i + array_offset);
            Value src = value_get_element(inter, array_src, i);
            value_assign_as_new(inter, &dst, src);
        }
        
        i32 element_offset = (left_type.kind == VariableKind_Array) ? array_src_count : 0;
        Value dst = value_get_element(inter, array, element_offset);
        value_assign_as_new(inter, &dst, element);
        
        return array;
    }
    
    report_invalid_binary_op(code, string_from_vtype(scratch.arena, inter, left_type.vtype), string_from_binary_operator(op), string_from_vtype(scratch.arena, inter, right_type.vtype));
    return value_nil();
}

internal_fn Value solve_signed_operation(Interpreter* inter, Value expresion, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    
    if (is_int(expresion)) {
        if (op == BinaryOperator_Addition) return expresion;
        if (op == BinaryOperator_Substraction) return alloc_int(inter, -get_int(expresion));
    }
    
    if (is_bool(expresion)) {
        if (op == BinaryOperator_LogicalNot) return alloc_bool(inter, !get_bool(expresion));
    }
    
    return value_nil();
}

Value interpret_expresion(Interpreter* inter, OpNode* node, ExpresionContext context)
{
    SCRATCH();
    
    if (node->kind == OpKind_Error) return value_nil();
    if (node->kind == OpKind_None) {
        if (context.expected_vtype <= VType_Any) return value_void();
        return value_null(context.expected_vtype);
    }
    
    if (node->kind == OpKind_Binary)
    {
        auto node0 = (OpNode_Binary*)node;
        Value left = interpret_expresion(inter, node0->left, context);
        Value right = interpret_expresion(inter, node0->right, expresion_context_make(left.vtype));
        
        if (is_unknown(left) || is_unknown(right)) return value_nil();
        
        BinaryOperator op = node0->op;
        
        Value result = solve_binary_operation(inter, left, right, op, node->code);
        
        if (is_unknown(result)) return value_nil();
        
        if (inter->mode == InterpreterMode_Execute && inter->settings.print_execution) {
            String left_string = string_from_value(scratch.arena, inter, left);
            String right_string = string_from_value(scratch.arena, inter, right);
            String result_string = string_from_value(scratch.arena, inter, result);
            log_trace(node->code, "%S %S %S = %S", left_string, string_from_binary_operator(op), right_string, result_string);
        }
        return result;
    }
    
    if (node->kind == OpKind_Sign)
    {
        auto node0 = (OpNode_Sign*)node;
        Value expresion = interpret_expresion(inter, node0->expresion, context);
        BinaryOperator op = node0->op;
        
        Value result = solve_signed_operation(inter, expresion, op, node0->code);
        
        if (is_unknown(result)) {
            report_invalid_signed_op(node->code, string_from_binary_operator(op), string_from_value(scratch.arena, inter, expresion));
            return value_nil();
        }
        
        if (inter->mode == InterpreterMode_Execute && inter->settings.print_execution) {
            String expresion_string = string_from_value(scratch.arena, inter, expresion);
            String result_string = string_from_value(scratch.arena, inter, result);
            log_trace(node->code, "%S %S = %S", string_from_binary_operator(op), expresion_string, result_string);
        }
        
        return result;
    }
    
    if (node->kind == OpKind_Reference)
    {
        auto node0 = (OpNode_Reference*)node;
        Value ret = interpret_expresion(inter, node0->expresion, context);
        
        if (ret.kind != ValueKind_LValue) {
            report_ref_expects_lvalue(node->code);
            return value_nil();
        }
        
        if (is_const(ret)) {
            report_ref_expects_non_constant(node->code);
            return value_nil();
        }
        
        ret.kind = ValueKind_Reference;
        return ret;
    }
    
    if (node->kind == OpKind_IntLiteral) {
        return alloc_int(inter, ((OpNode_NumericLiteral*)node)->int_literal);
    }
    if (node->kind == OpKind_CodepointLiteral) {
        return alloc_int(inter, ((OpNode_NumericLiteral*)node)->codepoint_literal);
    }
    if (node->kind == OpKind_BoolLiteral) {
        return alloc_bool(inter, ((OpNode_NumericLiteral*)node)->bool_literal);
    }
    if (node->kind == OpKind_StringLiteral)
    {
        auto node0 = (OpNode_StringLiteral*)node;
        
        String value = node0->value;
        
        if (node0->expresions.count > 0)
        {
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
                    else { assert(0); }
                    
                    continue;
                }
                
                if (codepoint == '%')
                {
                    if (expresion_index >= node0->expresions.count) {
                        assert(0);
                    }
                    else
                    {
                        OpNode* expresion_node = node0->expresions[expresion_index++];
                        Value expresion_result = interpret_expresion(inter, expresion_node, expresion_context_make(VType_Unknown));
                        if (is_unknown(expresion_result)) return value_nil();
                        if (is_void(expresion_result)) append(&builder, "");
                        else append(&builder, string_from_value(scratch.arena, inter, expresion_result));
                        
                    }
                    
                    continue;
                }
                
                append_codepoint(&builder, codepoint);
            }
            
            value = string_from_builder(yov->static_arena, &builder);
        }
        
        return alloc_string(inter, value);
    }
    if (node->kind == OpKind_Symbol)
    {
        auto node0 = (OpNode_Symbol*)node;
        
        Symbol symbol = find_symbol(inter, node0->identifier);
        
        if (symbol.type == SymbolType_ObjectRef) {
            return lvalue_from_ref(symbol.ref);
        }
        else if (symbol.type == SymbolType_Type) {
            i32 vtype = symbol.vtype;
            Value type = object_alloc(inter, VType_Type);
            value_assign_Type(inter, type, vtype);
            return type;
        }
        else {
            report_symbol_not_found(node->code, node0->identifier);
            return value_nil();
        }
    }
    
    if (node->kind == OpKind_MemberValue) {
        auto node0 = (OpNode_MemberValue*)node;
        
        OpNode* expresion_node = node0->expresion;
        String member_name = node0->member;
        assert(member_name.size);
        
        if (expresion_node->kind == OpKind_Symbol || expresion_node->kind == OpKind_None)
        {
            String identifier = {};
            
            if (expresion_node->kind == OpKind_Symbol)
                identifier = ((OpNode_Symbol*)expresion_node)->identifier;
            
            if (identifier.size == 0 && context.expected_vtype > 0) {
                identifier = string_from_vtype(scratch.arena, inter, context.expected_vtype);
            }
            
            Symbol symbol = find_symbol(inter, identifier);
            
            if (symbol.type == SymbolType_None) {
                report_symbol_not_found(node->code, identifier);
                return value_nil();
            }
            
            if (symbol.type == SymbolType_ObjectRef)
            {
                ObjectRef* ref = symbol.ref;
                Value member = value_get_member(inter, lvalue_from_ref(ref), member_name);
                
                if (is_unknown(member)) {
                    report_member_not_found_in_object(node->code, member_name, string_from_vtype(scratch.arena, inter, ref->vtype));
                }
                
                return member;
            }
            
            if (symbol.type == SymbolType_Type)
            {
                Value member = vtype_get_member(inter, symbol.vtype, member_name);
                
                if (is_unknown(member)) {
                    report_member_not_found_in_type(node->code, member_name, string_from_vtype(scratch.arena, inter, symbol.vtype));
                }
                
                return member;
            }
            
            report_member_invalid_symbol(node->code, symbol.identifier);
        }
        else
        {
            Value ret = interpret_expresion(inter, expresion_node, context);
            if (is_unknown(ret)) return value_nil();
            
            Value member = value_get_member(inter, ret, member_name);
            
            if (is_unknown(member)) {
                report_member_not_found_in_object(node->code, member_name, string_from_vtype(scratch.arena, inter, ret.vtype));
            }
            
            return member;
        }
        
        return value_nil();
    }
    
    if (node->kind == OpKind_FunctionCall) {
        return interpret_function_call(inter, node, &context);
    }
    
    if (node->kind == OpKind_ArrayExpresion)
    {
        auto node0 = (OpNode_ArrayExpresion*)node;
        Array<Value> objects{};
        
        i32 explicit_vtype = -1;
        
        if (node0->type->kind == OpKind_ObjectType) {
            explicit_vtype = interpret_object_type(inter, node0->type, false);
            if (explicit_vtype < 0) return value_nil();
        }
        
        i32 element_vtype = explicit_vtype;
        if (element_vtype < 0) element_vtype = vtype_from_array_element(inter, context.expected_vtype);
        
        if (!node0->is_empty)
        {
            objects = array_make<Value>(scratch.arena, node0->nodes.count);
            foreach(i, objects.count) {
                objects[i] = interpret_expresion(inter, node0->nodes[i], expresion_context_make(element_vtype));
                if (is_unknown(objects[i])) return value_nil();
            }
        }
        
        if (element_vtype < 0 && objects.count > 0) element_vtype = objects[0].vtype;
        
        if (element_vtype <= 0) {
            report_unknown_array_definition(node->code);
            return value_nil();
        }
        
        i32 base_vtype = vtype_from_array_base(inter, element_vtype);
        
        if (node0->is_empty)
        {
            u32 starting_dimensions = 0;
            
            if (explicit_vtype >= 0) {
                decode_vtype(explicit_vtype, NULL, &starting_dimensions);
            }
            
            Array<i64> dimensions = array_make<i64>(scratch.arena, node0->nodes.count + starting_dimensions);
            
            foreach(i, node0->nodes.count)
            {
                Value dim = interpret_expresion(inter, node0->nodes[i], expresion_context_make(VType_Int));
                if (is_unknown(dim)) return value_nil();
                
                if (!is_int(dim)) {
                    report_dimensions_expects_an_int(node->code);
                    return value_nil();
                }
                u32 index = starting_dimensions + i;
                dimensions[index] = get_int(dim);
                if (dimensions[index] < 0) {
                    report_dimensions_must_be_positive(node->code);
                    return value_nil();
                }
            }
            
            return alloc_array_multidimensional(inter, base_vtype, dimensions);
        }
        
        if (objects.count == 0) {
            return alloc_array(inter, element_vtype, 0, false);
        }
        
        // Assert same vtype
        for (i32 i = 1; i < objects.count; ++i) {
            if (objects[i].vtype != element_vtype) {
                report_type_missmatch_array_expr(node->code, string_from_vtype(scratch.arena, inter, element_vtype), string_from_vtype(scratch.arena, inter, objects[i].vtype));
                return value_nil();
            }
        }
        
        Value array = alloc_array(inter, element_vtype, objects.count, true);
        foreach(i, objects.count) {
            Value dst = value_get_element(inter, array, i);
            value_assign_as_new(inter, &dst, objects[i]);
        }
        
        return array;
    }
    
    if (node->kind == OpKind_Indexing)
    {
        auto node0 = (OpNode_Indexing*)node;
        Value value = interpret_expresion(inter, node0->value, context);
        Value index = interpret_expresion(inter, node0->index, context);
        
        if (is_unknown(value)) return value_nil();
        if (is_unknown(index)) return value_nil();
        
        if (!is_int(index)) {
            report_indexing_expects_an_int(node->code);
            return value_nil();
        }
        
        VariableType type = vtype_get(inter, value.vtype);
        
        if (type.kind == VariableKind_Array)
        {
            if (inter->mode == InterpreterMode_Execute)
            {
                i64 i = get_int(index);
                b32 out_of_bounds = i < 0 || i >= get_array(value).count;
                
                if (out_of_bounds) {
                    report_indexing_out_of_bounds(node->code);
                    return value_nil();
                }
                
                return value_get_element(inter, value, (u32)i);
            }
            else
            {
                return value_get_element(inter, value, 0);
            }
        }
        
        report_indexing_not_allowed(node->code, string_from_vtype(scratch.arena, inter, type.vtype));
        return value_nil();
    }
    
    report_expr_semantic_unknown(node->code);
    return value_nil();
}

Value interpret_assignment_for_object_definition(Interpreter* inter, OpNode_ObjectDefinition* node, b32 allow_reference)
{
    SCRATCH();
    
    i32 vtype = interpret_object_type(inter, node->type, allow_reference);
    if (vtype < 0) return value_nil();
    
    OpNode* assignment = node->assignment;
    Value value = interpret_expresion(inter, assignment, expresion_context_make(vtype));
    if (is_unknown(value)) return value_nil();
    
    if (vtype == VType_Void) vtype = value.vtype;
    
    if (!validate_assignment(inter, vtype, value)) {
        i32 value_vtype = value_get_expected_vtype(value);
        report_type_missmatch_assign(node->code, string_from_vtype(scratch.arena, inter, value_vtype), string_from_vtype(scratch.arena, inter, vtype));
        return value_nil();
    }
    
    if (is_null(value) && vtype > VType_Any) {
        value = object_alloc(inter, vtype);
    }
    
    if (vtype != value.vtype) {
        value.vtype = vtype;
    }
    
    return value;
}

void interpret_object_definition(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    
    auto node = (OpNode_ObjectDefinition*)node0;
    String identifier = node->object_name;
    
    if (find_symbol(inter, identifier).type != SymbolType_None) {
        report_symbol_duplicated(node->code, identifier);
        return;
    }
    
    Value value = interpret_assignment_for_object_definition(inter, node, false);
    if (is_unknown(value)) return;
    
    ObjectRef* ref = scope_define_object_ref(inter, identifier, value, node->is_constant);
    
    if (inter->mode == InterpreterMode_Execute)
    {
        if (inter->settings.print_execution) log_trace(node->code, "%S %S", string_from_vtype(scratch.arena, inter, ref->vtype), ref->identifier);
    }
}

void interpret_assignment(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_Assignment*)node0;
    
    Value dst = interpret_expresion(inter, node->destination, expresion_context_make(-1));
    if (is_unknown(dst)) return;
    
    if (dst.kind != ValueKind_LValue) {
        report_expr_expects_lvalue(node->code);
        return;
    }
    
    if (is_const(dst)) {
        report_assignment_is_constant(node->code, string_from_value(scratch.arena, inter, dst));
        return;
    }
    
    OpNode* assignment_node = node->source;
    BinaryOperator op = node->binary_operator;
    
    Value assignment = interpret_expresion(inter, assignment_node, expresion_context_make(dst.vtype));
    
    if (is_unknown(assignment)) return;
    
    if (op != BinaryOperator_None) {
        assignment = solve_binary_operation(inter, dst, assignment, op, assignment_node->code);
        if (is_unknown(assignment)) return;
    }
    
    if (!validate_assignment(inter, dst.vtype, assignment)) {
        i32 value_vtype = value_get_expected_vtype(assignment);
        report_type_missmatch_assign(assignment_node->code, string_from_vtype(scratch.arena, inter, value_vtype), string_from_vtype(scratch.arena, inter, dst.vtype));
        return;
    }
    
    if (is_unknown(assignment)) return;
    
    if (!value_assign(inter, &dst, assignment)) {
        assert(0);
    }
    
    if (inter->mode == InterpreterMode_Execute)
    {
        // TODO(Jose): log_trace(assignment_node->code, "%S = %S", dst->identifier, string_from_ref(scratch.arena, inter, ref));
    }
}

void interpret_if_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_IfStatement*)node0;
    
    Value expresion = interpret_expresion(inter, node->expresion, expresion_context_make(VType_Bool));
    
    if (is_unknown(expresion)) return;
    
    if (!is_bool(expresion)) {
        report_expr_expects_bool(node->code, "If-Statement");
        return;
    }
    
    if (inter->mode == InterpreterMode_Execute) {
        log_trace(node->code, "if (%S)", string_from_value(scratch.arena, inter, expresion));
        b32 result = get_bool(expresion);
        
        scope_push(inter, ScopeType_Block, VType_Void, false);
        if (result) interpret_op(inter, node, node->success);
        else interpret_op(inter, node, node->failure);
        scope_pop(inter);
    }
    else {
        
        Scope* scope = scope_find_returnable(inter);
        Value previous_return_value = scope->return_value;
        
        scope_push(inter, ScopeType_Block, VType_Void, false);
        interpret_op(inter, node, node->success);
        scope_pop(inter);
        Value success_return_value = scope->return_value;
        scope->return_value = previous_return_value;
        
        scope_push(inter, ScopeType_Block, VType_Void, false);
        interpret_op(inter, node, node->failure);
        scope_pop(inter);
        Value failed_return_value = scope->return_value;
        scope->return_value = previous_return_value;
        
        if (is_valid(success_return_value) && is_valid(failed_return_value)) {
            scope->return_value = success_return_value;
        }
    }
    
}

void interpret_while_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_WhileStatement*)node0;
    
    b32 break_requested = false;
    
    while (!break_requested)
    {
        Value expresion = interpret_expresion(inter, node->expresion, expresion_context_make(VType_Bool));
        if (skip_ops(inter)) return;
        
        if (!is_bool(expresion)) {
            report_expr_expects_bool(node->code, "While-Statement");
            return;
        }
        
        if (inter->mode == InterpreterMode_Execute)
        {
            log_trace(node->code, "while (%S)", string_from_value(scratch.arena, inter, expresion));
            
            if (!get_bool(expresion)) break;
            
            scope_push(inter, ScopeType_LoopIteration, VType_Void, false);
            interpret_op(inter, node, node->content);
            if (inter->current_scope->loop_iteration_break_requested)
                break_requested = true;
            scope_pop(inter);
        }
        else
        {
            Scope* scope = scope_find_returnable(inter);
            Value previous_return_value = scope->return_value;
            scope_push(inter, ScopeType_LoopIteration, VType_Void, false);
            interpret_op(inter, node, node->content);
            scope_pop(inter);
            scope->return_value = previous_return_value;
            break;
        }
        
        if (skip_ops(inter)) return;
    }
}

void interpret_for_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_ForStatement*)node0;
    
    scope_push(inter, ScopeType_Block, VType_Void, false);
    
    interpret_op(inter, node, node->initialize_sentence);
    
    b32 break_requested = false;
    
    while (!break_requested)
    {
        Value expresion = interpret_expresion(inter, node->condition_expresion, expresion_context_make(VType_Bool));
        if (skip_ops(inter)) return;
        
        if (!is_bool(expresion)) {
            report_expr_expects_bool(node->code, "For-Statement");
            return;
        }
        
        if (inter->mode == InterpreterMode_Execute)
        {
            log_trace(node->code, "for (%S)", string_from_value(scratch.arena, inter, expresion));
            
            if (!get_bool(expresion)) break;
            
            scope_push(inter, ScopeType_LoopIteration, VType_Void, false);
            interpret_op(inter, node, node->content);
            if (inter->current_scope->loop_iteration_break_requested)
                break_requested = true;
            scope_pop(inter);
            
            interpret_op(inter, node, node->update_sentence);
        }
        else
        {
            Scope* scope = scope_find_returnable(inter);
            Value previous_return_value = scope->return_value;
            scope_push(inter, ScopeType_LoopIteration, VType_Void, false);
            interpret_op(inter, node, node->content);
            scope_pop(inter);
            interpret_op(inter, node, node->update_sentence);
            scope->return_value = previous_return_value;
            break;
        }
        
        if (skip_ops(inter)) return;
    }
    
    scope_pop(inter);
}

void interpret_foreach_array_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_ForeachArrayStatement*)node0;
    
    scope_push(inter, ScopeType_Block, VType_Void, false);
    DEFER(scope_pop(inter));
    
    Value array = interpret_expresion(inter, node->expresion, expresion_context_make(-1));
    if (is_unknown(array)) return;
    
    scope_add_temporal(inter, array.obj);
    
    VariableType array_type = vtype_get(inter, array.vtype);
    
    if (array_type.kind != VariableKind_Array) {
        report_for_expects_an_array(node->code);
        return;
    }
    
    if (skip_ops(inter)) return;
    
    if (scope_find_object_ref(inter, node->element_name, true) != nil_ref) {
        report_object_duplicated(node->code, node->element_name);
        return;
    }
    Value element = lvalue_from_ref(scope_define_object_ref(inter, node->element_name, value_null(array_type.array_of)));
    
    Value index = value_nil();
    if (node->index_name.size > 0) {
        if (scope_find_object_ref(inter, node->index_name, true) != nil_ref) {
            report_object_duplicated(node->code, node->index_name);
            return;
        }
        
        index = lvalue_from_ref(scope_define_object_ref(inter, node->index_name, alloc_int(inter, 0)));
    }
    
    if (inter->mode == InterpreterMode_Execute)
    {
        b32 break_requested = false;
        
        for (u32 i = 0; i < get_array(array).count && !break_requested; ++i)
        {
            value_assign_ref(inter, &element, value_get_element(inter, array, i));
            
            if (is_valid(index)) {
                set_int(index, i);
            }
            
            if (inter->settings.print_execution) log_trace(node->code, "for each[%l] (%S)", i, string_from_value(scratch.arena, inter, element));
            
            scope_push(inter, ScopeType_Block, VType_Void, false);
            interpret_op(inter, node, node->content);
            if (inter->current_scope->loop_iteration_break_requested)
                break_requested = true;
            scope_pop(inter);
            
            if (skip_ops(inter)) return;
        }
    }
    else
    {
        value_assign_ref(inter, &element, object_alloc(inter, element.vtype));
        if (is_valid(index)) set_int(index, 0);
        
        Scope* scope = scope_find_returnable(inter);
        Value previous_return_value = scope->return_value;
        scope_push(inter, ScopeType_Block, VType_Void, false);
        interpret_op(inter, node, node->content);
        scope_pop(inter);
        scope->return_value = previous_return_value;
    }
}

Value interpret_function_call(Interpreter* inter, OpNode* node0, ExpresionContext* expresion_context)
{
    SCRATCH();
    auto node = (OpNode_FunctionCall*)node0;
    
    Symbol symbol = find_symbol(inter, node->identifier);
    
    if (symbol.type == SymbolType_Function)
    {
        FunctionDefinition* fn = symbol.function;
        
        if (fn == NULL) {
            report_function_not_found(node->code, node->identifier);
            return value_nil();
        }
        
        Array<Value> objects = array_make<Value>(scratch.arena, node->parameters.count);
        
        foreach(i, objects.count) {
            i32 expected_vtype = -1;
            if (i < fn->parameters.count) expected_vtype = fn->parameters[i].vtype;
            objects[i] = interpret_expresion(inter, node->parameters[i], expresion_context_make(expected_vtype));
            if (expected_vtype == VType_Any) {
                objects[i].vtype = VType_Any;
            }
            scope_add_temporal(inter, objects[i].obj);
        }
        
        if (skip_ops(inter)) return value_nil();
        
        return call_function(inter, fn, objects, node, expresion_context);
    }
    
    if (symbol.type == SymbolType_Type)
    {
        VariableType type = vtype_get(inter, symbol.vtype);
        
        return object_alloc(inter, symbol.vtype);
    }
    
    if (symbol.type == SymbolType_None) {
        report_symbol_not_found(node->code, node->identifier);
    }
    else {
        report_symbol_not_invokable(node->code, node->identifier);
    }
    return value_nil();
}

i32 interpret_object_type(Interpreter* inter, OpNode* node0, b32 allow_reference)
{
    OpNode_ObjectType* node = (OpNode_ObjectType*)node0;
    if (!allow_reference && node->is_reference) {
        report_reftype_invalid(node->code);
        return VType_Unknown;
    }
    if (node->name.size == 0) return VType_Void;
    i32 vtype = vtype_from_name(inter, node->name);
    vtype = vtype_from_array_dimension(inter, vtype, node->array_dimensions);
    if (vtype < 0) report_object_type_not_found(node->code, node->name);
    return vtype;
}

void interpret_return(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    OpNode_Return* node = (OpNode_Return*)node0;
    Scope* scope = scope_find_returnable(inter);
    
    Value return_value = interpret_expresion(inter, node->expresion, expresion_context_make(scope->expected_return_vtype));
    if (is_unknown(return_value)) {
        scope->return_value = value_nil();
        return;
    }
    
    if (scope->expected_return_vtype == VType_Any) {
        return_value.vtype = VType_Any;
    }
    
    scope_add_temporal(inter, scope, return_value.obj);
    
    if (!validate_assignment(inter, scope->expected_return_vtype, return_value)) {
        report_function_wrong_return_type(node->code, string_from_vtype(scratch.arena, inter, scope->expected_return_vtype));
        return;
    }
    
    b32 return_is_reference = return_value.kind == ValueKind_Reference;
    
    if (return_is_reference && !scope->expected_return_reference) {
        report_function_expects_no_ref_as_return(node->code);
        return;
    }
    
    if (!return_is_reference && scope->expected_return_reference) {
        report_function_expects_ref_as_return(node->code);
        return;
    }
    
    scope->return_value = return_value;
}

void interpret_continue(Interpreter* inter, OpNode* node0)
{
    Scope* scope = scope_find_looping(inter);
    if (scope == NULL) {
        report_continue_inside_loop(node0->code);
        return;
    }
    
    scope->loop_iteration_continue_requested = true;
}

void interpret_break(Interpreter* inter, OpNode* node0)
{
    Scope* scope = scope_find_looping(inter);
    if (scope == NULL) {
        report_break_inside_loop(node0->code);
        return;
    }
    
    scope->loop_iteration_break_requested = true;
}

void interpret_block(Interpreter* inter, OpNode* block0, b32 push_scope)
{
    auto block = (OpNode_Block*)block0;
    assert(block->kind == OpKind_Block);
    
    if (push_scope) scope_push(inter, ScopeType_Block, VType_Void, false);
    
    Array<OpNode*> ops = block->ops;
    
    foreach(i, ops.count)
    {
        if (skip_ops(inter)) break;
        
        OpNode* node = ops[i];
        interpret_op(inter, block, node);
    }
    
    if (push_scope) scope_pop(inter);
}

void interpret_op(Interpreter* inter, OpNode* parent, OpNode* node)
{
    if (skip_ops(inter)) return;
    if (node->kind == OpKind_None) return;
    
    if (node->kind == OpKind_Block) interpret_block(inter, node, true);
    else if (node->kind == OpKind_Assignment) interpret_assignment(inter, node);
    else if (node->kind == OpKind_IfStatement) interpret_if_statement(inter, node);
    else if (node->kind == OpKind_WhileStatement) interpret_while_statement(inter, node);
    else if (node->kind == OpKind_ForStatement) interpret_for_statement(inter, node);
    else if (node->kind == OpKind_ForeachArrayStatement) interpret_foreach_array_statement(inter, node);
    else if (node->kind == OpKind_FunctionCall) interpret_function_call(inter, node, NULL);
    else if (node->kind == OpKind_Return) interpret_return(inter, node);
    else if (node->kind == OpKind_Continue) interpret_continue(inter, node);
    else if (node->kind == OpKind_Break) interpret_break(inter, node);
    else if (node->kind == OpKind_Error) {}
    else if (node->kind == OpKind_ObjectDefinition) {
        OpNode_ObjectDefinition* node0 = (OpNode_ObjectDefinition*)node;
        if (inter->current_scope->type != ScopeType_Global || !node0->is_constant)
            interpret_object_definition(inter, node);
    }
    else if (node->kind == OpKind_EnumDefinition || node->kind == OpKind_StructDefinition || node->kind == OpKind_FunctionDefinition || node->kind == OpKind_ArgDefinition) {
        if (parent != inter->root) {
            report_nested_definition(node->code);
        }
    }
    else if (node->kind == OpKind_Import) {} // Ignore
    else {
        report_semantic_unknown_op(node->code);
    }
}

internal_fn Array<OpNode_StructDefinition*> get_struct_definitions(Arena* arena, Interpreter* inter, Array<OpNode*> nodes)
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

internal_fn Array<String> get_struct_dependencies(Arena* arena, Interpreter* inter, OpNode_StructDefinition* node, Array<OpNode_StructDefinition*> struct_nodes)
{
    SCRATCH(arena);
    PooledArray<String> names = pooled_array_make<String>(scratch.arena, 8);
    
    foreach(i, node->members.count) {
        OpNode_ObjectDefinition* member = node->members[i];
        i32 member_vtype = vtype_from_name(inter, member->type->name);
        if (member_vtype >= 0) continue;
        
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

internal_fn b32 validate_arg_name(Interpreter* inter, String name, CodeLocation code)
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
    
    if (find_arg_definition_by_name(inter, name) != NULL) {
        report_arg_duplicated_name(code, name);
        return false;
    }
    
    return true;
}

internal_fn void interpret_definitions(OpNode_Block* block, Interpreter* inter, b32 only_definitions)
{
    SCRATCH();
    
    if (only_definitions) {
        foreach(i, block->ops.count)
        {
            OpNode* node = block->ops[i];
            
            b32 is_definition = false;
            if (node->kind == OpKind_EnumDefinition) is_definition = true;
            if (node->kind == OpKind_StructDefinition) is_definition = true;
            if (node->kind == OpKind_FunctionDefinition) is_definition = true;
            if (node->kind == OpKind_Import) is_definition = true;
            if (node->kind == OpKind_ObjectDefinition) {
                OpNode_ObjectDefinition* node0 = (OpNode_ObjectDefinition*)node;
                is_definition = node0->is_constant;
            }
            
            if (!is_definition) {
                report_unsupported_operations(node->code);
                return;
            }
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
        
        if (find_symbol(inter, name).type != SymbolType_None) {
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
            
            Value value = interpret_expresion(inter, value_node, expresion_context_make(VType_Int));
            
            if (is_unknown(value)) continue;
            if (value.vtype != VType_Int) {
                report_enum_value_expects_an_int(node->code);
                valid = false;
                continue;
            }
            
            values[i] = get_int(value);
        }
        
        if (!valid) continue;
        
        define_enum(inter, name, node->names, values);
    }
    
    // Structs
    {
        Array<OpNode_StructDefinition*> struct_nodes = get_struct_definitions(scratch.arena, inter, block->ops);
        
        // Solve struct dependencies
        {
            foreach(i, struct_nodes.count) struct_nodes[i]->dependency_index = 0;
            
            foreach(i, struct_nodes.count)
            {
                OpNode_StructDefinition* node = struct_nodes[i];
                b32 valid = true;
                
                Array<String> deps0 = get_struct_dependencies(scratch.arena, inter, node, struct_nodes);
                
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
                    
                    Array<String> deps1 = get_struct_dependencies(scratch.arena, inter, node1, struct_nodes);
                    
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
            
            if (find_symbol(inter, name).type != SymbolType_None) {
                report_symbol_duplicated(node->code, name);
                valid = false;
            }
            
            Array<ObjectDefinition> members = array_make<ObjectDefinition>(yov->static_arena, node->members.count);
            
            foreach(i, members.count) {
                OpNode_ObjectDefinition* member = node->members[i];
                members[i].name = member->object_name;
                
                if (string_equals(member->type->name, name)) {
                    report_struct_recursive(node->code);
                    valid = false;
                    continue;
                }
                
                if (member->type->name.size == 0) {
                    report_struct_implicit_member_type(node->code);
                    valid = false;
                    continue;
                }
                
                members[i].vtype = interpret_object_type(inter, member->type, false);
                if (members[i].vtype < 0) {
                    valid = false;
                    continue;
                }
                
                members[i].default_value = member->assignment;
            }
            
            if (!valid) continue;
            
            define_struct(inter, name, members);
        }
    }
    
    // Functions
    foreach(i, block->ops.count)
    {
        OpNode* node0 = block->ops[i];
        if (node0->kind != OpKind_FunctionDefinition) continue;
        
        OpNode_FunctionDefinition* node = (OpNode_FunctionDefinition*)node0;
        
        b32 valid = true;
        
        if (find_symbol(inter, node->identifier).type != SymbolType_None) {
            report_symbol_duplicated(node->code, node->identifier);
            valid = false;
        }
        
        Array<ObjectDefinition> parameters = array_make<ObjectDefinition>(scratch.arena, node->parameters.count);
        
        foreach(i, parameters.count)
        {
            OpNode_ObjectDefinition* param_node = node->parameters[i];
            ObjectDefinition* def = &parameters[i];
            
            Value assignment_result = interpret_assignment_for_object_definition(inter, param_node, true);
            if (is_unknown(assignment_result)) {
                valid = false;
                continue;
            }
            
            def->vtype = assignment_result.vtype;
            def->name = param_node->object_name;
            def->default_value = param_node->assignment;
            def->is_reference = param_node->type->is_reference;
        }
        
        i32 return_vtype = VType_Void;
        b32 return_reference = false;
        
        if (node->return_node->kind == OpKind_ObjectType) {
            return_vtype = interpret_object_type(inter, node->return_node, true);
            if (return_vtype < 0) valid = false;
            return_reference = ((OpNode_ObjectType*)node->return_node)->is_reference;
        }
        
        OpNode_Block* defined_fn = NULL;
        b8 is_intrinsic = false;
        
        if (node->block->kind == OpKind_Block) {
            defined_fn = (OpNode_Block*)node->block;
        }
        else if (node->block->kind == OpKind_None) {
            is_intrinsic = true;
        }
        else {
            valid = false;
            assert(0);
        }
        
        if (!valid) continue;
        
        if (is_intrinsic) define_intrinsic_function(inter, node->code, node->identifier, parameters, return_vtype);
        else define_function(inter, node->code, node->identifier, parameters, return_vtype, return_reference, defined_fn);
    }
    
    // Assign intrinsic functions
    {
        Array<IntrinsicDefinition> table = get_intrinsics_table(scratch.arena);
        
        for (auto it = pooled_array_make_iterator(&inter->functions); it.valid; ++it)
        {
            FunctionDefinition* fn = it.value;
            if (!fn->is_intrinsic) continue;
            
            IntrinsicDefinition* intr = NULL;
            
            foreach(i, table.count) {
                IntrinsicDefinition* intr0 = &table[i];
                if (string_equals(intr0->name, fn->identifier)) {
                    intr = intr0;
                    break;
                }
            }
            
            if (intr == NULL) {
                report_intrinsic_not_resolved(fn->code, fn->identifier);
                continue;
            }
            
            fn->intrinsic.fn = intr->fn;
        }
    }
    
    // Args
    foreach(i, block->ops.count)
    {
        OpNode* node0 = block->ops[i];
        if (node0->kind != OpKind_ArgDefinition) continue;
        OpNode_ArgDefinition* node = (OpNode_ArgDefinition*)node0;
        
        b32 valid = true;
        b32 required = false;
        String name = string_format(yov->static_arena, "-%S", node->identifier);
        Value default_value = value_nil();
        b32 has_explicit_vtype = false;
        String description = {};
        
        i32 vtype = VType_Bool;
        
        if (node->type->kind == OpKind_ObjectType) {
            vtype = interpret_object_type(inter, node->type, false);
            if (vtype < 0) continue;
            has_explicit_vtype = true;
        }
        
        if (node->name->kind == OpKind_Assignment)
        {
            OpNode_Assignment* assignment = (OpNode_Assignment*)node->name;
            Value value = interpret_expresion(inter, assignment->source, expresion_context_make(VType_String));
            if (is_unknown(value)) {
                valid = false;
            }
            else if (!is_string(value)) {
                valid = false;
                report_type_missmatch_assign(assignment->code, string_from_vtype(scratch.arena, inter, value.vtype), string_from_vtype(scratch.arena, inter, VType_String));
            }
            else {
                name = get_string(value);
                
                if (!validate_arg_name(inter, name, assignment->code))
                    valid = false;
            }
        }
        
        if (node->description->kind == OpKind_Assignment)
        {
            OpNode_Assignment* assignment = (OpNode_Assignment*)node->description;
            Value value = interpret_expresion(inter, assignment->source, expresion_context_make(VType_String));
            if (is_unknown(value)) {
                valid = false;
            }
            else if (!is_string(value)) {
                valid = false;
                report_type_missmatch_assign(assignment->code, string_from_vtype(scratch.arena, inter, value.vtype), string_from_vtype(scratch.arena, inter, VType_String));
            }
            else {
                description = get_string(value);
            }
        }
        
        if (node->required->kind == OpKind_Assignment)
        {
            OpNode_Assignment* assignment = (OpNode_Assignment*)node->required;
            Value value = interpret_expresion(inter, assignment->source, expresion_context_make(VType_Bool));
            if (is_unknown(value)) {
                valid = false;
            }
            else if (!is_bool(value)) {
                valid = false;
                report_type_missmatch_assign(assignment->code, string_from_vtype(scratch.arena, inter, value.vtype), string_from_vtype(scratch.arena, inter, VType_Bool));
            }
            else {
                required = get_bool(value);
            }
        }
        
        if (node->default_value->kind == OpKind_Assignment)
        {
            OpNode_Assignment* assignment = (OpNode_Assignment*)node->default_value;
            default_value = interpret_expresion(inter, assignment->source, expresion_context_make(vtype));
            if (is_unknown(default_value)) continue;
            
            if (has_explicit_vtype && vtype != default_value.vtype) {
                report_type_missmatch_assign(assignment->code, string_from_vtype(scratch.arena, inter, default_value.vtype), string_from_vtype(scratch.arena, inter, vtype));
                continue;
            }
            
            vtype = default_value.vtype;
        }
        
        if (find_symbol(inter, node->identifier).type != SymbolType_None) {
            report_symbol_duplicated(node->code, node->identifier);
            valid = false;
        }
        
        if (!valid || inter->mode == InterpreterMode_Help)
        {
            define_arg(inter, node->identifier, name, vtype, false, description);
            continue;
        }
        
        Value value = value_nil();
        
        // From arg
        if (is_unknown(value))
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
                    if (vtype == VType_Bool) {
                        value = alloc_bool(inter, true);
                    }
                }
                else
                {
                    value = value_from_string(inter, script_arg->value, vtype);
                }
                
                if (is_unknown(value)) {
                    report_arg_wrong_value(NO_CODE, name, script_arg->value);
                    continue;
                }
            }
        }
        
        // From default
        if (is_unknown(value) && is_valid(default_value)) {
            value = default_value;
        }
        
        // Default vtype
        if (is_unknown(value)) {
            value = object_alloc(inter, vtype);
        }
        
        ObjectRef* ref = scope_define_object_ref(inter, node->identifier, value, true);
        
        
        //print_info("Arg %S: name=%S, required=%s\n", node->identifier, name, required ? "true" : "false");
        
        define_arg(inter, node->identifier, name, vtype, required, description);
    }
    
    // Global Objects
    foreach(i, block->ops.count)
    {
        OpNode* node0 = block->ops[i];
        if (node0->kind != OpKind_ObjectDefinition) continue;
        OpNode_ObjectDefinition* node = (OpNode_ObjectDefinition*)node0;
        if (!node->is_constant) continue;
        
        interpret_object_definition(inter, node);
    }
}

#include "autogenerated/core.h"

internal_fn void register_definitions(Interpreter* inter)
{
    SCRATCH();
    
    inter->vtype_table = pooled_array_make<VariableType>(yov->static_arena, 32);
    inter->functions = pooled_array_make<FunctionDefinition>(yov->static_arena, 32);
    inter->arg_definitions = pooled_array_make<ArgDefinition>(yov->static_arena, 32);
    
    PooledArray<VariableType>* list = &inter->vtype_table;
    
    VariableType t;
    
    // Void
    {
        t = {};
        t.name = "void";
        t.kind = VariableKind_Void;
        t.vtype = list->count;
        assert(VType_Void == t.vtype);
        array_add(list, t);
    }
    
    // Any
    {
        t = {};
        t.name = "Any";
        t.kind = VariableKind_Any;
        t.vtype = list->count;
        assert(VType_Any == t.vtype);
        array_add(list, t);
    }
    
    // Int
    {
        t = {};
        t.name = "Int";
        t.kind = VariableKind_Primitive;
        t.vtype = list->count;
        assert(VType_Int == t.vtype);
        array_add(list, t);
    }
    
    // Bool
    {
        t = {};
        t.name = "Bool";
        t.kind = VariableKind_Primitive;
        t.vtype = list->count;
        assert(VType_Bool == t.vtype);
        array_add(list, t);
    }
    
    // String
    {
        t = {};
        t.name = "String";
        t.kind = VariableKind_Primitive;
        t.vtype = list->count;
        assert(VType_String == t.vtype);
        array_add(list, t);
    }
    
    define_core(inter);
    
    // Script Definitions
    for (auto it = pooled_array_make_iterator(&yov->scripts); it.valid; ++it)
    {
        OpNode* ast = it.value->ast;
        
        if (ast->kind != OpKind_Block) {
            assert(0);
            continue;
        }
        
        b32 only_definitions = it.index != 0;
        
        OpNode_Block* block = (OpNode_Block*)ast;
        interpret_definitions(block, inter, only_definitions);
    }
    
    // Analyze initialize expresions
    if (inter->mode != InterpreterMode_Execute)
    {
        for (i32 vtype = VType_Any + 1; vtype < inter->vtype_table.count; vtype++) {
            object_alloc(inter, vtype);
        }
    }
    
    foreach(i, inter->vtype_table.count) {
        VariableType t = inter->vtype_table[i];
        assert(t.kind != VariableKind_Array);
    }
}

internal_fn void define_globals(Interpreter* inter)
{
    SCRATCH();
    Value ref;
    Value member;
    
    // Yov struct
    {
        inter->globals.yov = scope_find_object_ref(inter, "yov", false);
        ref = lvalue_from_ref(inter->globals.yov);
        
        member = value_get_member(inter, ref, "minor");
        set_int(member, YOV_MINOR_VERSION);
        
        member = value_get_member(inter, ref, "major");
        set_int(member, YOV_MAJOR_VERSION);
        
        member = value_get_member(inter, ref, "revision");
        set_int(member, YOV_REVISION_VERSION);
        
        member = value_get_member(inter, ref, "version");
        set_string(inter, member, YOV_VERSION);
        
        member = value_get_member(inter, ref, "path");
        set_string(inter, member, os_get_executable_path(scratch.arena));
    }
    
    // OS struct
    {
        inter->globals.os = scope_find_object_ref(inter, "os", false);
        ref = lvalue_from_ref(inter->globals.os);
        
        member = value_get_member(inter, ref, "kind");
        
#if OS_WINDOWS
        set_enum_index(inter, member, 0);
#else 
#error TODO
#endif
    }
    
    // Context struct
    {
        inter->globals.context = scope_find_object_ref(inter, "context", false);
        ref = lvalue_from_ref(inter->globals.context);
        
        member = value_get_member(inter, ref, "cd");
        set_string(inter, member, yov->scripts[0].dir);
        
        member = value_get_member(inter, ref, "script_dir");
        set_string(inter, member, yov->scripts[0].dir);
        
        member = value_get_member(inter, ref, "caller_dir");
        set_string(inter, member, yov->caller_dir);
        
        // Args
        {
            Array<ScriptArg> args = yov->args;
            Value array = alloc_array(inter, VType_String, args.count, false);
            foreach(i, args.count) {
                member = value_get_element(inter, array, i);
                set_string(inter, member, args[i].name);
            }
            
            member = value_get_member(inter, ref, "args");
            value_assign_ref(inter, &member, array);
        }
        
        // Types
        {
            Value array = alloc_array(inter, VType_Type, inter->vtype_table.count, false);
            
            for (auto it = pooled_array_make_iterator(&inter->vtype_table); it.valid; ++it) {
                u32 index = it.index;
                Value element = value_get_element(inter, array, index);
                i32 vtype = it.value->vtype;
                value_assign_Type(inter, element, vtype);
            }
            
            member = value_get_member(inter, ref, "types");
            value_assign_ref(inter, &member, array);
        }
    }
    
    // Calls struct
    {
        inter->globals.calls = scope_find_object_ref(inter, "calls", false);
    }
}

internal_fn void report_invalid_arguments(Interpreter* inter)
{
    foreach(i, yov->args.count)
    {
        String name = yov->args[i].name;
        if (string_equals(name, "-help")) continue;
        
        ArgDefinition* def = find_arg_definition_by_name(inter, name);
        if (def == NULL) {
            report_arg_unknown(NO_CODE, name);
        }
    }
}

internal_fn void print_script_help(Interpreter* inter)
{
    SCRATCH();
    StringBuilder builder = string_builder_make(scratch.arena);
    
    // Script description
    {
        ObjectRef* ref = scope_find_object_ref(inter, "script_description", true);
        
        if (ref != nil_ref) {
            String description = get_string(lvalue_from_ref(ref));
            append(&builder, description);
            append(&builder, "\n\n");
        }
    }
    
    Array<String> headers = array_make<String>(scratch.arena, inter->arg_definitions.count);
    
    u32 longest_header = 0;
    for (auto it = pooled_array_make_iterator(&inter->arg_definitions); it.valid; ++it)
    {
        ArgDefinition* arg = it.value;
        
        b32 show_type = arg->vtype != VType_Bool && arg->vtype > 0;
        
        String space = "    ";
        
        String header;
        if (show_type)
        {
            VariableType type = vtype_get(inter, arg->vtype);
            
            String type_str;
            if (type.kind == VariableKind_Enum) {
                type_str = "enum";
            }
            else {
                type_str = string_from_vtype(scratch.arena, inter, arg->vtype);
            }
            header = string_format(scratch.arena, "%S%S -> %S", space, arg->name, type_str);
        }
        else {
            header = string_format(scratch.arena, "%S%S", space, arg->name);
        }
        
        headers[it.index] = header;
        
        u32 char_count = string_calculate_char_count(header);
        longest_header = MAX(longest_header, char_count);
    }
    
    u32 chars_to_description = longest_header + 4;
    
    appendf(&builder, "Script Arguments:\n");
    for (auto it = pooled_array_make_iterator(&inter->arg_definitions); it.valid; ++it)
    {
        ArgDefinition* arg = it.value;
        String header = headers[it.index];
        
        append(&builder, header);
        
        if (arg->description.size != 0)
        {
            u32 char_count = string_calculate_char_count(header);
            for (u32 i = char_count; i < chars_to_description; ++i) {
                append(&builder, " ");
            }
            
            appendf(&builder, "%S", arg->description);
        }
        appendf(&builder, "\n");
    }
    
    String log = string_from_builder(scratch.arena, &builder);
    print_info(log);
}

void interpret(InterpreterSettings settings, InterpreterMode mode)
{
    YovScript* main_script = yov_get_script(0);
    
    Interpreter* inter = arena_push_struct<Interpreter>(yov->static_arena);
    inter->mode = mode;
    inter->settings = settings;
    inter->root = main_script->ast;
    
    inter->empty_op = arena_push_struct<OpNode>(yov->static_arena);
    
    inter->global_scope = scope_alloc(inter, ScopeType_Global);
    inter->current_scope = inter->global_scope;
    
    register_definitions(inter);
    report_invalid_arguments(inter);
    define_globals(inter);
    
    if (mode == InterpreterMode_Help)
    {
        if (yov->error_count == 0) print_script_help(inter);
        return;
    }
    
    interpret_block(inter, inter->root, false);
    
    scope_clear(inter, inter->global_scope);
    
    if (inter->object_count > 0) {
        lang_report_unfreed_objects();
    }
    else if (inter->allocation_count > 0) {
        lang_report_unfreed_dynamic();
    }
    
#if DEV && 0
    if (inter->mode == InterpreterMode_Analysis) print_memory_usage(inter);
#endif
    
    assert(yov->error_count != 0 || inter->global_scope == inter->current_scope);
}

void interpreter_exit(Interpreter* inter, i32 exit_code)
{
    yov_set_exit_code(exit_code);
    yov->error_count++;// TODO(Jose): Weird
}

void interpreter_report_runtime_error(Interpreter* inter, CodeLocation code, String resolved_line, Result result)
{
    String script_path = yov_get_script(code.script_id)->path;
    print_error("%S(%u): %S\n\t--> %S\n", script_path, (u32)code.line, resolved_line, result.message);
    interpreter_exit(inter, result.exit_code);
}

Result user_assertion(Interpreter* inter, String message)
{
    if (!inter->settings.user_assertion || yov_ask_yesno("User Assertion", message)) return RESULT_SUCCESS;
    return result_failed_make("Operation denied by user");
}

Value get_cd(Interpreter* inter) {
    return value_get_member(inter, lvalue_from_ref(inter->globals.context), "cd");
}

String get_cd_value(Interpreter* inter) {
    return get_string(get_cd(inter));
}

String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path)
{
    SCRATCH(arena);
    
    String cd = get_cd_value(inter);
    if (!os_path_is_absolute(path)) path = path_resolve(scratch.arena, path_append(scratch.arena, cd, path));
    return string_copy(arena, path);
}

RedirectStdout get_calls_redirect_stdout(Interpreter* inter)
{
    Value calls = lvalue_from_ref(inter->globals.calls);
    Value redirect_stdout = value_get_member(inter, calls, "redirect_stdout");
    return (RedirectStdout)get_enum_index(inter, redirect_stdout);
}

b32 interpretion_failed(Interpreter* inter)
{
    if (inter->mode != InterpreterMode_Execute) return false;
    return yov->error_count > 0;
}

b32 skip_ops(Interpreter* inter)
{
    if (interpretion_failed(inter)) return true;
    
    if (inter->mode == InterpreterMode_Execute)
    {
        Scope* returnable_scope = scope_find_returnable(inter);
        if (returnable_scope->expected_return_vtype > 0 && !is_null(returnable_scope->return_value)) return true;
        
        Scope* looping_scope = scope_find_looping(inter);
        if (looping_scope != NULL && looping_scope->loop_iteration_continue_requested) return true;
    }
    
    return false;
}

//- SCOPE 

Scope* scope_alloc(Interpreter* inter, ScopeType type)
{
    Scope* scope = arena_push_struct<Scope>(yov->static_arena);
    scope->object_refs = pooled_array_make<ObjectRef>(yov->static_arena, 32);
    scope->temp_objects = pooled_array_make<Object*>(yov->static_arena, 8);
    scope->type = type;
    scope->return_value = value_void();
    return scope;
}

void scope_clear(Interpreter* inter, Scope* scope)
{
    scope->next = NULL;
    scope->previous = NULL;
    
    for (auto it = pooled_array_make_iterator(&scope->object_refs); it.valid; ++it) {
        ObjectRef* ref = it.value;
        Value value = lvalue_from_ref(ref);
        value_assign_null(inter, &value);
        *ref = {};
    }
    array_reset(&scope->object_refs);
    
    for (auto it = pooled_array_make_iterator(&scope->temp_objects); it.valid; ++it) {
        Object* obj = *it.value;
        object_decrement_ref(obj);
    }
    array_reset(&scope->temp_objects);
    
    object_free_unused(inter);
    
    scope->return_value = value_void();
    scope->loop_iteration_continue_requested = false;
    scope->loop_iteration_break_requested = false;
}

Scope* scope_push(Interpreter* inter, ScopeType type, i32 expected_return_vtype, b32 expected_return_reference)
{
    Scope* scope = NULL;
    
    if (inter->free_scope != NULL) {
        scope = inter->free_scope;
        inter->free_scope = scope->next;
        scope->next = NULL;
        scope->previous = NULL;
        scope->type = type;
    }
    
    if (scope == NULL) scope = scope_alloc(inter, type);
    
    scope->return_value = value_void();
    
    scope->expected_return_vtype = expected_return_vtype;
    scope->expected_return_reference = expected_return_reference;
    
    scope->previous = inter->current_scope;
    inter->current_scope->next = scope->previous;
    inter->current_scope = scope;
    
    return scope;
}

void scope_pop(Interpreter* inter)
{
    if (inter->current_scope == inter->global_scope) {
        lang_report_stack_is_broken();
        return;
    }
    
    Scope* scope = inter->current_scope;
    inter->current_scope = scope->previous;
    inter->current_scope->next = NULL;
    
    scope_clear(inter, scope);
    
    scope->next = inter->free_scope;
    inter->free_scope = scope;
}

void scope_add_temporal(Interpreter* inter, Object* object)
{
    Scope* scope = inter->current_scope;
    scope_add_temporal(inter, scope, object);
}

void scope_add_temporal(Interpreter* inter, Scope* scope, Object* object) {
    object_increment_ref(object);
    array_add(&scope->temp_objects, object);
}

Scope* scope_find_returnable(Interpreter* inter)
{
    Scope* scope = inter->current_scope;
    
    while (scope != NULL) {
        if (scope->type == ScopeType_Function) return scope;
        if (scope->type == ScopeType_Global) return scope;
        scope = scope->previous;
    }
    assert(0);
    return inter->global_scope;
}

Scope* scope_find_looping(Interpreter* inter)
{
    Scope* scope = inter->current_scope;
    
    while (scope != NULL) {
        if (scope->type == ScopeType_LoopIteration) return scope;
        scope = scope->previous;
    }
    return NULL;
}

ObjectRef* scope_define_object_ref(Interpreter* inter, String identifier, Value value, b32 constant)
{
    assert(is_valid(value));
    assert(scope_find_object_ref(inter, identifier, false) == nil_ref);
    assert(identifier.size > 0);
    
    Scope* scope = inter->current_scope;
    
    ObjectRef* ref = array_add(&scope->object_refs);
    ref->identifier = identifier;
    
    ref->vtype = value.vtype;
    ref->object = null_obj;
    ref->constant = constant;
    
    Value lvalue = lvalue_from_ref(ref);
    value_assign_as_new(inter, &lvalue, value);
    
    return ref;
}

ObjectRef* scope_find_object_ref(Interpreter* inter, String identifier, b32 parent_scopes)
{
    if (identifier.size == 0) return nil_ref;
    
    Scope* scope = inter->current_scope;
    
    while (true)
    {
        for (auto it = pooled_array_make_iterator(&scope->object_refs); it.valid; ++it) {
            ObjectRef* ref = it.value;
            if (string_equals(ref->identifier, identifier)) {
                return ref;
            }
        }
        
        if (!parent_scopes) break;
        
        if (scope->type == ScopeType_Function) break;
        if (scope->type == ScopeType_Global) break;
        scope = scope->previous;
    }
    
    if (scope != inter->global_scope) {
        
        for (auto it = pooled_array_make_iterator(&inter->global_scope->object_refs); it.valid; ++it) {
            ObjectRef* ref = it.value;
            // TODO(Jose): if (ref->is_constant && string_equals(ref->identifier, identifier)) {
            if (string_equals(ref->identifier, identifier)) {
                return ref;
            }
        }
    }
    
    return nil_ref;
}

//- DEFINITIONS

i32 define_enum(Interpreter* inter, String name, Array<String> names, Array<i64> values)
{
    SCRATCH();
    
    if (values.count == 0) {
        values = array_make<i64>(scratch.arena, names.count);
        foreach(i, values.count) values[i] = i;
    }
    
    names = array_copy(yov->static_arena, names);
    values = array_copy(yov->static_arena, values);
    
    VariableType t = {};
    t.name = name;
    t.kind = VariableKind_Enum;
    t.vtype = inter->vtype_table.count;
    t.enum_names = names;
    t.enum_values = values;
    assert(names.count == values.count);
    array_add(&inter->vtype_table, t);
    
    return t.vtype;
}

void define_struct(Interpreter* inter, String name, Array<ObjectDefinition> members)
{
    Array<String> names = array_make<String>(yov->static_arena, members.count);
    Array<i32> vtypes = array_make<i32>(yov->static_arena, members.count);
    Array<OpNode*> initialize_expresions = array_make<OpNode*>(yov->static_arena, members.count);
    
    foreach(i, vtypes.count)
    {
        ObjectDefinition member = members[i];
        assert(!member.is_reference);
        names[i] = member.name;
        vtypes[i] = member.vtype;
        initialize_expresions[i] = member.default_value;
        
        if (vtypes[i] < 0) return;
    }
    
    VariableType t = {};
    t.name = name;
    t.kind = VariableKind_Struct;
    t.vtype = inter->vtype_table.count;
    t.struct_names = names;
    t.struct_vtypes = vtypes;
    t.struct_initialize_expresions = initialize_expresions;
    array_add(&inter->vtype_table, t);
}

void define_arg(Interpreter* inter, String identifier, String name, i32 vtype, b32 required, String description)
{
    ArgDefinition arg{};
    arg.identifier = identifier;
    arg.name = name;
    arg.vtype = vtype;
    arg.required = required;
    arg.description = description;
    
    array_add(&inter->arg_definitions, arg);
}

void define_function(Interpreter* inter, CodeLocation code, String identifier, Array<ObjectDefinition> parameters, i32 return_vtype, b32 return_reference, OpNode_Block* block)
{
    FunctionDefinition fn{};
    fn.identifier = identifier;
    fn.return_vtype = return_vtype;
    fn.return_reference = return_reference;
    fn.parameters = array_copy(yov->static_arena, parameters);
    fn.defined_fn = block;
    fn.code = code;
    fn.is_intrinsic = false;
    array_add(&inter->functions, fn);
}

void define_intrinsic_function(Interpreter* inter, CodeLocation code, String identifier, Array<ObjectDefinition> parameters, i32 return_vtype)
{
    FunctionDefinition fn{};
    fn.identifier = identifier;
    fn.return_vtype = return_vtype;
    fn.return_reference = false;
    fn.parameters = array_copy(yov->static_arena, parameters);
    fn.defined_fn = NULL;
    fn.code = code;
    fn.is_intrinsic = true;
    array_add(&inter->functions, fn);
}

Symbol find_symbol(Interpreter* inter, String identifier)
{
    Symbol symbol{};
    symbol.identifier = identifier;
    
    {
        ObjectRef* ref = scope_find_object_ref(inter, identifier, true);
        
        if (ref != nil_ref) {
            symbol.type = SymbolType_ObjectRef;
            symbol.ref = ref;
            return symbol;
        }
    }
    
    {
        FunctionDefinition* fn = find_function(inter, identifier);
        
        if (fn != NULL) {
            symbol.type = SymbolType_Function;
            symbol.function = fn;
            return symbol;
        }
    }
    
    {
        i32 vtype = vtype_from_name(inter, identifier);
        
        if (vtype >= 0) {
            symbol.type = SymbolType_Type;
            symbol.vtype = vtype;
            return symbol;
        }
    }
    
    return {};
}

FunctionDefinition* find_function(Interpreter* inter, String identifier)
{
    for (auto it = pooled_array_make_iterator(&inter->functions); it.valid; ++it) {
        FunctionDefinition* fn = it.value;
        if (string_equals(fn->identifier, identifier)) return fn;
    }
    return NULL;
}

ArgDefinition* find_arg_definition_by_name(Interpreter* inter, String name)
{
    for (auto it = pooled_array_make_iterator(&inter->arg_definitions); it.valid; ++it) {
        ArgDefinition* def = it.value;
        if (string_equals(def->name, name)) return def;
    }
    return NULL;
}

Value call_function(Interpreter* inter, FunctionDefinition* fn, Array<Value> parameters, OpNode* parent_node, ExpresionContext* expresion_context)
{
    SCRATCH();
    
    Scope* return_scope = inter->current_scope;
    CodeLocation code = parent_node->code;
    
    if (parameters.count != fn->parameters.count) {
        report_function_expecting_parameters(code, fn->identifier, fn->parameters.count);
        return value_nil();
    }
    
    // Ignore errors
    foreach(i, parameters.count) {
        if (is_unknown(parameters[i])) return value_nil();
    }
    
    foreach(i, parameters.count)
    {
        if (!validate_assignment(inter, fn->parameters[i].vtype, parameters[i])) {
            report_function_wrong_parameter_type(code, fn->identifier, string_from_vtype(scratch.arena, inter, fn->parameters[i].vtype), i + 1);
            return value_nil();
        }
        if (fn->parameters[i].is_reference && parameters[i].kind != ValueKind_Reference) {
            report_function_expects_ref_as_parameter(code, fn->identifier, i + 1);
            return value_nil();
        }
        if (!fn->parameters[i].is_reference && parameters[i].kind == ValueKind_Reference) {
            report_function_expects_noref_as_parameter(code, fn->identifier, i + 1);
            return value_nil();
        }
    }
    
    String resolved_line;
    
    // Generate resolved line
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        append(&builder, fn->identifier);
        append(&builder, "(");
        foreach(i, parameters.count) {
            append(&builder, string_from_value(scratch.arena, inter, parameters[i], false));
            if (i != parameters.count - 1) append(&builder, ", ");
        }
        append(&builder, ")");
        resolved_line = string_from_builder(scratch.arena, &builder);
    }
    
    b32 is_intrinsic = fn->is_intrinsic;
    
    if (is_intrinsic && inter->mode != InterpreterMode_Execute)
    {
        i32 vtype = fn->return_vtype;
        if (vtype == VType_Any)
        {
            if (expresion_context != NULL) {
                vtype = expresion_context->expected_vtype;
            }
            if (vtype <= VType_Any) vtype = VType_Int;
        }
        
        if (vtype == VType_Void) return value_void();
        Value value = object_alloc(inter, vtype);
        value.vtype = fn->return_vtype;
        return value;
    }
    
    FunctionReturn ret{};
    
    if (is_intrinsic) {
        ret = fn->intrinsic.fn(inter, parameters, code);
    }
    else {
        ret.return_value = value_void();
        ret.error_result = RESULT_SUCCESS;
        
        Scope* fn_scope = scope_push(inter, ScopeType_Function, fn->return_vtype, fn->return_reference);
        
        foreach(i, fn->parameters.count) {
            ObjectDefinition param_def = fn->parameters[i];
            scope_define_object_ref(inter, param_def.name, parameters[i]);
        }
        
        interpret_op(inter, parent_node, fn->defined_fn);
        ret = { fn_scope->return_value, RESULT_SUCCESS };
        scope_add_temporal(inter, return_scope, fn_scope->return_value.obj);
        
        scope_pop(inter);
    }
    
    if (is_unknown(ret.return_value)) return value_nil();
    
    if (fn->return_vtype != VType_Void && ret.return_value.vtype == VType_Void)
    {
        CodeLocation fn_code = (fn->defined_fn == NULL) ? code : fn->defined_fn->code;
        report_function_no_return(fn_code, fn->identifier);
        return value_nil();
    }
    
    if (!ret.error_result.success && expresion_context == NULL) {
        interpreter_report_runtime_error(inter, code, resolved_line, ret.error_result);
        return ret.return_value;
    }
    
    if (inter->settings.print_execution) {
        String return_string = string_from_value(scratch.arena, inter, ret.return_value);
        String parameters_string = "TODO";
        log_trace(node->code, "%S(%S) => %S", fn->identifier, parameters_string, return_string);
    }
    
    return ret.return_value;
}

VariableType vtype_get(Interpreter* inter, i32 vtype)
{
    u32 index, dim;
    decode_vtype(vtype, &index, &dim);
    
    if (vtype < 0 || index >= inter->vtype_table.count) {
        VariableType t{};
        t.name = STR("Unknown");
        t.kind = VariableKind_Unknown;
        return t;
    }
    
    VariableType t = inter->vtype_table[index];
    
    if (dim > 0) {
        t.vtype = vtype;
        t.kind = VariableKind_Array;
        t.array_of = vtype_from_array_element(inter, vtype);
    }
    
    return t;
}

b32 vtype_is_enum(Interpreter* inter, i32 vtype) {
    return vtype_get(inter, vtype).kind == VariableKind_Enum;
}
b32 vtype_is_array(i32 vtype) {
    u32 dims;
    decode_vtype(vtype, NULL, &dims);
    return dims > 0;
}
b32 vtype_is_struct(Interpreter* inter, i32 vtype) {
    return vtype_get(inter, vtype).kind == VariableKind_Struct;
}

i32 vtype_from_name(Interpreter* inter, String name)
{
    u32 dimensions = 0;
    while (name.size > 2 && name[name.size - 1] == ']' && name[name.size - 2] == '[') {
        name = string_substring(name, 0, name.size - 2);
        dimensions++;
    }
    
    foreach(i, inter->vtype_table.count) {
        if (string_equals(inter->vtype_table[i].name, name)) {
            return vtype_from_array_dimension(inter, i, dimensions);
        }
    }
    return -1;
}

i32 vtype_from_array_dimension(Interpreter* inter, i32 vtype, u32 dimension)
{
    u32 index, dim;
    decode_vtype(vtype, &index, &dim);
    return encode_vtype(index, dim + dimension);
}

i32 vtype_from_array_element(Interpreter* inter, i32 vtype)
{
    u32 index, dim;
    decode_vtype(vtype, &index, &dim);
    
    if (dim == 0) return -1;
    return encode_vtype(index, dim - 1);
}

i32 vtype_from_array_base(Interpreter* inter, i32 vtype)
{
    if (vtype < 0) return -1;
    
    u32 index, dim;
    decode_vtype(vtype, &index, &dim);
    
    return index;
}

u32 vtype_get_size(Interpreter* inter, i32 vtype) {
    if (vtype == VType_Void) return sizeof(Object);
    if (vtype == VType_Int) return sizeof(Object_Int);
    if (vtype == VType_Bool) return sizeof(Object_Bool);
    if (vtype == VType_String) return sizeof(Object_String);
    
    u32 dims;
    decode_vtype(vtype, NULL, &dims);
    if (dims > 0) return sizeof(Object_Array);
    
    VariableType type = vtype_get(inter, vtype);
    if (type.kind == VariableKind_Enum) return sizeof(Object_Enum);
    if (type.kind == VariableKind_Struct) return sizeof(Object_Struct);
    
    assert(0);
    return sizeof(Object);
}

Value vtype_get_member(Interpreter* inter, i32 vtype, String member)
{
    SCRATCH();
    VariableType type = vtype_get(inter, vtype);
    
    if (string_equals(member, "name")) {
        String name = string_from_vtype(scratch.arena, inter, vtype);
        return alloc_string(inter, name);
    }
    
    if (type.kind == VariableKind_Enum)
    {
        if (string_equals(member, "count")) {
            return alloc_int(inter, type.enum_values.count);
        }
        if (string_equals(member, "array")) {
            return alloc_array_from_enum(inter, vtype);
        }
        
        i64 value_index = -1;
        foreach(i, type.enum_names.count) {
            if (string_equals(type.enum_names[i], member)) {
                value_index = i;
                break;
            }
        }
        
        if (value_index < 0) return value_nil();
        return alloc_enum(inter, vtype, value_index);
    }
    
    return value_nil();
}

i32 vtype_get_element(Interpreter* inter, i32 vtype, u32 index)
{
    VariableType type = vtype_get(inter, vtype);
    
    if (type.kind == VariableKind_Array) {
        return type.array_of;
    }
    else if (type.kind == VariableKind_Struct) {
        Array<i32> vtypes = type.struct_vtypes;
        if (index >= vtypes.count) return VType_Unknown;
        return vtypes[index];
    }
    
    return VType_Unknown;
}

i32 encode_vtype(u32 index, u32 dimensions) {
    u32 vtype = index;
    vtype |= dimensions << 24;
    return vtype;
}

void decode_vtype(i32 vtype, u32* _index, u32* _dimensions) {
    if (vtype < 0) {
        if (_index != NULL) *_index = u32_max;
        if (_dimensions != NULL) *_dimensions = 0;
    }
    else {
        if (_index != NULL) *_index = vtype & 0x00FFFFFF;
        if (_dimensions != NULL) *_dimensions = vtype >> 24;
    }
}

String string_from_vtype(Arena* arena, Interpreter* inter, i32 vtype)
{
    String name = vtype_get(inter, vtype).name;
    assert(name.size);
    
    u32 dims;
    decode_vtype(vtype, NULL, &dims);
    
    if (dims > 0)
    {
        String array_name;
        array_name.size = name.size + dims * 2;
        array_name.data = (char*)arena_push(arena, array_name.size + 1);
        foreach(i, name.size) array_name[i] = name[i];
        foreach(i, dims) {
            u64 index = name.size + i * 2;
            array_name[index + 0] = '[';
            array_name[index + 1] = ']';
        }
        name = array_name;
    }
    
    return name;
}

//- VALUE OPS

Value value_null(i32 vtype) {
    assert(vtype > 0);
    Value v{};
    v.kind = ValueKind_RValue;
    v.obj = null_obj;
    v.vtype = vtype;
    return v;
}

Value value_void() {
    Value v{};
    v.kind = ValueKind_RValue;
    v.obj = null_obj;
    v.parent = null_obj;
    v.index = 0;
    v.vtype = VType_Void;
    return v;
}

Value value_nil() {
    Value v{};
    v.kind = ValueKind_RValue;
    v.obj = nil_obj;
    v.parent = nil_obj;
    v.index = 0;
    v.vtype = -1;
    return v;
}

Value value_def(Interpreter* inter, i32 vtype) {
    return object_alloc(inter, vtype);
}

Value rvalue_make(Object* obj, Object* parent, u32 index, i32 vtype) {
    assert(is_valid(obj));
    Value v{};
    v.kind = ValueKind_RValue;
    v.obj = obj;
    v.parent = parent;
    v.index = index;
    v.vtype = vtype;
    return v;
}

Value rvalue_from_obj(Object* obj) {
    assert(is_valid(obj) && !is_null(obj));
    return rvalue_make(obj, null_obj, 0, obj->vtype);
}

Value lvalue_make(Object* obj, ObjectRef* ref, Object* parent, u32 index, i32 vtype) {
    assert(is_valid(obj));
    Value v{};
    v.kind = ValueKind_LValue;
    v.obj = obj;
    v.vtype = vtype;
    v.parent = parent;
    v.index = index;
    v.lvalue.ref = ref;
    return v;
}

Value lvalue_from_ref(ObjectRef* ref) {
    return lvalue_make(ref->object, ref, null_obj, 0, ref->vtype);
}

Value value_from_child(Value parent_value, Object* object, u32 index, i32 vtype)
{
    if (parent_value.kind == ValueKind_RValue) {
        return rvalue_make(object, parent_value.obj, index, vtype);
    }
    
    if (parent_value.kind == ValueKind_LValue) {
        return lvalue_make(object, parent_value.lvalue.ref, parent_value.obj, index, vtype);
    }
    
    assert(0);
    return value_nil();
}

Value value_from_string(Interpreter* inter, String str, i32 vtype)
{
    SCRATCH();
    if (str.size <= 0) return value_nil();
    if (string_equals(str, "null")) return value_null(vtype);
    
    if (vtype == VType_Int) {
        i64 value;
        if (!i64_from_string(str, &value)) return value_nil();
        return alloc_int(inter, value);
    }
    
    if (vtype == VType_Bool) {
        if (string_equals(str, "true")) return alloc_bool(inter, true);
        if (string_equals(str, "false")) return alloc_bool(inter, false);
        if (string_equals(str, "1")) return alloc_bool(inter, true);
        if (string_equals(str, "0")) return alloc_bool(inter, false);
        return value_nil();
    }
    
    if (vtype == VType_String) {
        return alloc_string(inter, str);
    }
    
    VariableType type = vtype_get(inter, vtype);
    
    if (type.kind == VariableKind_Enum)
    {
        u64 start_name = 0;
        if (str[0] == '.') {
            start_name = 1;
        }
        else if (string_starts(str, string_format(scratch.arena, "%S.", type.name))) {
            start_name = type.name.size + 1;
        }
        
        String enum_name = string_substring(str, start_name, str.size - start_name);
        foreach(i, type.enum_names.count) {
            if (string_equals(type.enum_names[i], enum_name)) return alloc_enum(inter, vtype, i);
        }
        return value_nil();
    }
    
    return value_nil();
}

String string_from_value(Arena* arena, Interpreter* inter, Value value, b32 raw) {
    SCRATCH(arena);
    
    if (is_null(value)) {
        return "null";
    }
    
    if (value.vtype == VType_Any) {
        value.vtype = value.obj->vtype;
    }
    
    i32 vtype = value.vtype;
    
    if (vtype == VType_String) {
        if (raw) return get_string(value);
        return string_format(arena, "\"%S\"", get_string(value));
    }
    if (vtype == VType_Int) { return string_format(arena, "%l", get_int(value)); }
    if (vtype == VType_Bool) { return get_bool(value) ? "true" : "false"; }
    if (vtype == VType_Void) { return "void"; }
    if (vtype == VType_Unknown) { return "unknown"; }
    
    VariableType type = vtype_get(inter, vtype);
    
    if (type.kind == VariableKind_Array)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, "{ ");
        
        Array<Object*> array = get_array(value);
        
        foreach(i, array.count) {
            Object* element = array[i];
            append(&builder, string_from_obj(scratch.arena, inter, element, false));
            if (i < array.count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (type.kind == VariableKind_Enum)
    {
        i64 index = get_enum_index(inter, value);
        if (index < 0 || index >= type.enum_names.count) return "?";
        String name = type.enum_names[(u32)index];
        if (!raw) name = string_format(arena, "\"%S\"", name);
        return name;
    }
    
    if (type.kind == VariableKind_Struct)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, "{ ");
        
        Object_Struct* struct_obj = (Object_Struct*)value.obj;
        
        foreach(i, type.struct_vtypes.count)
        {
            String member_name = type.struct_names[i];
            i32 member_vtype = type.struct_vtypes[i];
            Object* member = struct_obj->members[i];
            appendf(&builder, "%S = %S", member_name, string_from_obj(scratch.arena, inter, member, false));
            if (i < type.struct_vtypes.count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    assert(0);
    return "?";
}

String string_from_obj(Arena* arena, Interpreter* inter, Object* obj, b32 raw) {
    return string_from_value(arena, inter, rvalue_from_obj(obj), raw);
}

i32 value_get_expected_vtype(Value value)
{
    if (is_unknown(value)) return VType_Unknown;
    if (is_void(value)) return VType_Void;
    if (value.vtype == VType_Any) {
        Object* obj = value.obj;
        if (is_unknown(obj) || is_null(obj)) return VType_Unknown;
        return obj->vtype;
    }
    return value.vtype;
}

b32 value_assign(Interpreter* inter, Value* dst, Value src)
{
    if (dst->kind == ValueKind_Reference) {
        assert(0);
        return false;
    }
    if (is_null(*dst)) {
        assert(0);
        return false;
    }
    if (is_const(*dst)) {
        assert(0);
        return false;
    }
    
    if (src.kind == ValueKind_RValue || src.kind == ValueKind_LValue)
    {
        if (dst->vtype == VType_Any) {
            Value new_value = object_alloc(inter, src.vtype);
            if (!value_assign_ref(inter, dst, new_value)) return false;
        }
        return value_copy(inter, *dst, src);
    }
    else if (src.kind == ValueKind_Reference) {
        return value_assign_ref(inter, dst, src);
    }
    assert(0);
    return false;
}

b32 value_assign_as_new(Interpreter* inter, Value* dst, Value src)
{
    if (!is_null(*dst)) {
        assert(0);
        return false;
    }
    
    if (src.kind == ValueKind_RValue || src.kind == ValueKind_Reference) {
        return value_assign_ref(inter, dst, src);
    }
    else if (src.kind == ValueKind_LValue)
    {
        Value new_object = object_alloc(inter, value_get_expected_vtype(src));
        if (!value_assign_ref(inter, dst, new_object)) return false;
        return value_copy(inter, *dst, src);
    }
    
    assert(0);
    return false;
}

b32 value_assign_ref(Interpreter* inter, Value* dst, Value src)
{
    if (dst->kind == ValueKind_Reference) {
        assert(0);
        return false;
    }
    if (dst->vtype != VType_Any && dst->vtype != src.vtype) {
        assert(0);
        return false;
    }
    // TODO(Jose): if (dst->is_constant) {
    if (0) {
        assert(0);
        return false;
    }
    
    if (is_null(dst->parent)) {
        if (dst->kind == ValueKind_LValue) {
            object_decrement_ref(dst->obj);
            dst->lvalue.ref->object = src.obj;
            object_increment_ref(src.obj);
        }
    }
    else {
        if (!object_set_child(inter, dst->parent, dst->index, src.obj)) {
            assert(0);
            return false;
        }
    }
    
    dst->obj = src.obj;
    
    return true;
}

b32 value_assign_null(Interpreter* inter, Value* dst)
{
    Value null = value_null(dst->vtype);
    return value_assign_ref(inter, dst, null);
}

b32 value_copy(Interpreter* inter, Value dst, Value src)
{
    if (dst.kind == ValueKind_Reference || src.kind == ValueKind_Reference) {
        assert(0);
        return false;
    }
    
    if (dst.vtype != VType_Any && dst.vtype != src.vtype) {
        assert(0);
        return false;
    }
    // TODO(Jose): if (dst->is_constant) {
    if (0) {
        assert(0);
        return false;
    }
    if (is_null(dst)) {
        assert(0);
        return false;
    }
    if (is_null(src)) {
        assert(0);
        return false;
    }
    
    object_copy(inter, dst.obj, src.obj);
    return true;
}

Array<Object*> object_get_childs(Interpreter* inter, Object* obj)
{
    VariableType type = vtype_get(inter, obj->vtype);
    
    if (type.kind == VariableKind_Array) {
        return ((Object_Array*)obj)->elements;
    }
    else if (type.kind == VariableKind_Struct) {
        return ((Object_Struct*)obj)->members;
    }
    
    return {};
}

b32 object_set_child(Interpreter* inter, Object* obj, u32 index, Object* child)
{
    VariableType type = vtype_get(inter, obj->vtype);
    
    if (type.kind == VariableKind_Array)
    {
        Array<Object*> array = ((Object_Array*)obj)->elements;
        if (index >= array.count) {
            return false;
        }
        object_decrement_ref(array[index]);
        array[index] = child;
        object_increment_ref(child);
        return true;
    }
    else if (type.kind == VariableKind_Struct)
    {
        Array<Object*> array = ((Object_Struct*)obj)->members;
        if (index >= array.count) {
            return false;
        }
        object_decrement_ref(array[index]);
        array[index] = child;
        object_increment_ref(child);
        return true;
    }
    
    return {};
}

Value value_get_element(Interpreter* inter, Value value, u32 index)
{
    if (is_unknown(value) || is_null(value)) {
        assert(0);
        return value_nil();
    }
    
    Array<Object*> array = object_get_childs(inter, value.obj);
    
    if (index < 0 || index >= array.count) {
        assert(0);
        return value_nil();
    }
    
    Object* element = array[index];
    return value_from_child(value, element, index, vtype_get_element(inter, value.vtype, index));
}

Value value_get_member(Interpreter* inter, Value value, String member_name)
{
    if (value.vtype == VType_String) {
        if (string_equals(member_name, "size")) {
            return alloc_int(inter, get_string(value).size);
        }
    }
    
    VariableType type = vtype_get(inter, value.vtype);
    
    if (type.kind == VariableKind_Array)
    {
        if (string_equals(member_name, "count")) {
            return alloc_int(inter, get_array(value).count);
        }
    }
    
    if (type.kind == VariableKind_Enum)
    {
        i64 index = get_enum_index(inter, value);
        if (string_equals(member_name, "index")) {
            return alloc_int(inter, index);
        }
        if (string_equals(member_name, "value")) {
            if (index < 0 || index >= type.enum_values.count) {
                return alloc_string(inter, "?");
            }
            return alloc_int(inter, type.enum_values[(u32)index]);
        }
        if (string_equals(member_name, "name")) {
            if (index < 0 || index >= type.enum_names.count) {
                return alloc_string(inter, "?");
            }
            return alloc_string(inter, type.enum_names[(u32)index]);
        }
    }
    
    if (type.kind == VariableKind_Struct)
    {
        foreach(i, type.struct_names.count)
        {
            if (string_equals(member_name, type.struct_names[i])) {
                return value_get_element(inter, value, i);
            }
        }
    }
    
    assert(0);
    return value_nil();
}

Value alloc_int(Interpreter* inter, i64 value)
{
    Value obj = object_alloc(inter, VType_Int);
    set_int(obj, value);
    return obj;
}

Value alloc_bool(Interpreter* inter, b32 value)
{
    Value obj = object_alloc(inter, VType_Bool);
    set_bool(obj, value);
    return obj;
}

Value alloc_string(Interpreter* inter, String value)
{
    Value obj = object_alloc(inter, VType_String);
    set_string(inter, obj, value);
    return obj;
}

Value alloc_array(Interpreter* inter, i32 element_vtype, i64 count, b32 null_elements)
{
    i32 vtype = vtype_from_array_dimension(inter, element_vtype, 1);
    VariableType type = vtype_get(inter, vtype);
    
    if (type.kind != VariableKind_Array) {
        assert(0);
        return value_nil();
    }
    
    Value value = object_alloc(inter, vtype);
    Object_Array* obj = (Object_Array*)value.obj;
    
    Object** element_memory = (Object**)gc_dynamic_allocate(inter, sizeof(Object*) * count);
    obj->elements = array_make(element_memory, (u32)count);
    
    if (null_elements) {
        foreach(i, obj->elements.count) {
            obj->elements[i] = null_obj;
        }
    }
    else {
        foreach(i, obj->elements.count) {
            obj->elements[i] = object_alloc(inter, element_vtype).obj;
            object_increment_ref(obj->elements[i]);
        }
    }
    
    return value;
}

Value alloc_array_multidimensional(Interpreter* inter, i32 base_vtype, Array<i64> dimensions)
{
    if (dimensions.count <= 0) {
        assert(0);
        return value_nil();
    }
    
    i32 vtype = vtype_from_array_dimension(inter, base_vtype, dimensions.count);
    i32 element_vtype = vtype_from_array_element(inter, vtype);
    
    if (dimensions.count == 1)
    {
        return alloc_array(inter, element_vtype, dimensions[0], false);
    }
    else
    {
        Value value = object_alloc(inter, vtype);
        Object_Array* obj = (Object_Array*)value.obj;
        
        u32 count = (u32)dimensions[0];
        Object** element_memory = (Object**)gc_dynamic_allocate(inter, sizeof(Object*) * count);
        obj->elements = array_make(element_memory, count);
        
        foreach(i, obj->elements.count) {
            obj->elements[i] = alloc_array_multidimensional(inter, base_vtype, array_subarray(dimensions, 1, dimensions.count - 1)).obj;
            object_increment_ref(obj->elements[i]);
        }
        
        return value;
    }
}

Value alloc_array_from_enum(Interpreter* inter, i32 enum_vtype)
{
    VariableType enum_type = vtype_get(inter, enum_vtype);
    
    if (enum_type.kind != VariableKind_Enum) {
        assert(0);
        return value_nil();
    }
    
    Value value = alloc_array(inter, enum_vtype, enum_type.enum_values.count, false);
    Object_Array* array = (Object_Array*)value.obj;
    foreach(i, array->elements.count) {
        Object* element = array->elements[i];
        set_enum_index(inter, rvalue_from_obj(element), i);
    }
    return value;
}

Value alloc_enum(Interpreter* inter, i32 vtype, i64 index)
{
    Value obj = object_alloc(inter, vtype);
    set_enum_index(inter, obj, index);
    return obj;
}

b32 value_is_vtype(Value value, i32 vtype) {
    if (value.vtype != vtype) return false;
    Object* obj = value.obj;
    if (is_unknown(obj)) return false;
    return is_null(obj) || obj->vtype == vtype;
}

b32 is_valid(Object* obj) {
    return !is_unknown(obj);
}
b32 is_valid(Value value) {
    return !is_unknown(value);
}
b32 is_unknown(Object* obj) {
    if (obj == NULL) return true;
    return obj->vtype < 0;
}
b32 is_unknown(Value value) {
    if (value.vtype < 0) return true;
    return is_unknown(value.obj);
}

b32 is_const(Value value) {
    return value.kind == ValueKind_LValue && value.lvalue.ref->constant;
}

b32 is_void(Value value) {
    return is_valid(value) && value.vtype == VType_Void;
}
b32 is_null(const Object* obj) {
    if (obj == NULL) return true;
    return obj->vtype == VType_Void;
}
b32 is_null(Value value) {
    if (is_unknown(value)) return false;
    return is_null(value.obj);
}

b32 is_int(Value value) { return value_is_vtype(value, VType_Int); }
b32 is_bool(Value value) { return value_is_vtype(value, VType_Bool); }
b32 is_string(Value value) { return value_is_vtype(value, VType_String); }

b32 is_array(Value value) {
    if (!vtype_is_array(value.vtype)) return false;
    Object* obj = value.obj;
    if (is_unknown(obj)) return false;
    return is_null(obj) || vtype_is_array(obj->vtype);
}

b32 is_enum(Interpreter* inter, Value value) {
    if (!vtype_is_enum(inter, value.vtype)) return false;
    Object* obj = value.obj;
    if (is_unknown(obj)) return false;
    return is_null(obj) || vtype_is_enum(inter, obj->vtype);
}
b32 is_struct(Interpreter* inter, i32 vtype) {
    return vtype_get(inter, vtype).kind == VariableKind_Struct;
}

i64 get_int(Value value)
{
    if (!is_int(value)) {
        assert(0);
        return 0;
    }
    
    Object* obj = value.obj;
    
    if (is_null(obj)) {
        assert(0);
        return 0;
    }
    
    Object_Int* obj0 = (Object_Int*)obj;
    return obj0->value;
}

b32 get_bool(Value value)
{
    if (!is_bool(value)) {
        assert(0);
        return 0;
    }
    
    Object* obj = value.obj;
    
    if (is_null(obj)) {
        assert(0);
        return 0;
    }
    
    Object_Bool* obj0 = (Object_Bool*)obj;
    return obj0->value;
}

i64 get_enum_index(Interpreter* inter, Value value) {
    if (!is_enum(inter, value)) {
        assert(0);
        return 0;
    }
    
    Object* obj = value.obj;
    
    if (is_null(obj)) {
        assert(0);
        return 0;
    }
    
    Object_Enum* obj0 = (Object_Enum*)obj;
    return obj0->index;
}

String get_string(Value value)
{
    if (!is_string(value)) {
        assert(0);
        return 0;
    }
    
    Object* obj = value.obj;
    
    if (is_null(obj)) {
        assert(0);
        return 0;
    }
    
    Object_String* obj0 = (Object_String*)obj;
    return obj0->value;
}

Array<Object*> get_array(Value value)
{
    if (!is_array(value)) {
        assert(0);
        return {};
    }
    
    Object* obj = value.obj;
    
    if (is_null(obj)) {
        assert(0);
        return {};
    }
    
    Object_Array* obj0 = (Object_Array*)obj;
    return obj0->elements;
}

void set_int(Value value, i64 v)
{
    if (!is_int(value)) {
        assert(0);
        return;
    }
    
    Object_Int* obj = (Object_Int*)value.obj;
    if (is_null(obj)) {
        assert(0);
        return;
    }
    
    obj->value = v;
}

void set_bool(Value value, b32 v)
{
    if (!is_bool(value)) {
        assert(0);
        return;
    }
    
    Object_Bool* obj = (Object_Bool*)value.obj;
    if (is_null(obj)) {
        assert(0);
        return;
    }
    
    obj->value = v;
}

void set_enum_index(Interpreter* inter, Value value, i64 v)
{
    if (!is_enum(inter, value)) {
        assert(0);
        return;
    }
    
    Object_Enum* obj = (Object_Enum*)value.obj;
    if (is_null(obj)) {
        assert(0);
        return;
    }
    
    obj->index = v;
}

void set_string(Interpreter* inter, Value value, String v)
{
    if (!is_string(value)) {
        assert(0);
        return;
    }
    
    Object_String* obj = (Object_String*)value.obj;
    if (is_null(obj)) {
        assert(0);
        return;
    }
    
    char* old_data = obj->value.data;
    char* new_data = (v.size > 0) ? (char*)gc_dynamic_allocate(inter, v.size) : NULL;
    
    memory_copy(new_data, v.data, v.size);
    if (old_data != NULL) gc_dynamic_free(inter, old_data);
    
    obj->value.data = new_data;
    obj->value.size = v.size;
}

void value_assign_FileInfo(Interpreter* inter, Value value, FileInfo info)
{
    Value mem;
    mem = value_get_member(inter, value, "path");
    set_string(inter, mem, info.path);
    mem = value_get_member(inter, value, "is_directory");
    set_bool(mem, info.is_directory);
}

void value_assign_Type(Interpreter* inter, Value value, i32 vtype)
{
    VariableType type = vtype_get(inter, vtype);
    Value mem;
    mem = value_get_member(inter, value, "ID");
    set_int(mem, vtype);
    mem = value_get_member(inter, value, "name");
    set_string(inter, mem, type.name);
}

//- GARBAGE COLLECTOR 

u32 object_generate_id(Interpreter* inter) {
    return ++inter->object_id_counter;
}

Value object_alloc(Interpreter* inter, i32 vtype)
{
    SCRATCH();
    
    assert(vtype > VType_Any);
    
    u32 ID = object_generate_id(inter);
    
    log_mem_trace("Alloc obj(%u): %S\n", ID, string_from_vtype(scratch.arena, inter, vtype));
    
    VariableType type = vtype_get(inter, vtype);
    u32 type_size = vtype_get_size(inter, vtype);
    assert(type_size > sizeof(Object));
    
    Object* obj = (Object*)gc_dynamic_allocate(inter, type_size);
    
    obj->next = inter->object_list;
    if (inter->object_list) inter->object_list->prev = obj;
    inter->object_list = obj;
    inter->object_count++;
    
    obj->ID = ID;
    obj->vtype = vtype;
    obj->ref_count = 0;
    
    if (type.kind == VariableKind_Struct)
    {
        Object** members_memory = (Object**)gc_dynamic_allocate(inter, sizeof(Object*) * type.struct_vtypes.count);
        Array<Object*> members = array_make(members_memory, type.struct_vtypes.count);
        
        foreach(i, type.struct_vtypes.count)
        {
            i32 member_vtype = type.struct_vtypes[i];
            OpNode* member_initialize_expresion = type.struct_initialize_expresions[i];
            u32 member_size = vtype_get_size(inter, member_vtype);
            
            Value member = value_nil();
            
            if (member_initialize_expresion != NULL && member_initialize_expresion->kind != OpKind_None)
            {
                member = interpret_expresion(inter, member_initialize_expresion, expresion_context_make(member_vtype));
                
                if (is_unknown(member)) {
                    member = object_alloc(inter, member_vtype);
                }
            }
            else {
                member = object_alloc(inter, member_vtype);
            }
            
            assert(member.kind == ValueKind_RValue);
            object_increment_ref(member.obj);
            members[i] = member.obj;
        }
        
        ((Object_Struct*)obj)->members = members;
    }
    else if (type.kind == VariableKind_Array)
    {
    }
    
    return rvalue_from_obj(obj);
}

void object_free(Interpreter* inter, Object* obj)
{
    SCRATCH();
    assert(obj->ref_count == 0);
    
    log_mem_trace("Free obj(%u): %S\n", obj->ID, string_from_vtype(scratch.arena, inter, obj->vtype));
    
    if (obj == inter->object_list)
    {
        assert(obj->prev == NULL);
        inter->object_list = obj->next;
        if (inter->object_list != NULL) inter->object_list->prev = NULL;
    }
    else
    {
        if (obj->next != NULL) obj->next->prev = obj->prev;
        obj->prev->next = obj->next;
    }
    inter->object_count--;
    
    object_release_internal(inter, obj);
    
    gc_dynamic_free(inter, obj);
}

void object_free_unused(Interpreter* inter)
{
    u32 free_count;
    do {
        Object* obj = inter->object_list;
        free_count = 0;
        
        while (obj != NULL)
        {
            Object* next = obj->next;
            if (obj->ref_count == 0) {
                object_free(inter, obj);
                free_count++;
            }
            obj = next;
        }
    }
    while (free_count != 0);
}

void object_increment_ref(Object* obj)
{
    if (is_unknown(obj)) return;
    if (is_null(obj)) return;
    obj->ref_count++;
}

void object_decrement_ref(Object* obj)
{
    if (is_unknown(obj)) return;
    if (is_null(obj)) return;
    obj->ref_count--;
    assert(obj->ref_count >= 0);
}

void object_release_internal(Interpreter* inter, Object* obj)
{
    Array<Object*> childs = object_get_childs(inter, obj);
    
    foreach(i, childs.count) {
        object_decrement_ref(childs[i]);
    }
    
    // Release dynamic memory: Strings, arrays and structs
    if (obj->vtype == VType_String)
    {
        Object_String* str = (Object_String*)obj;
        if (str->value.data != NULL) gc_dynamic_free(inter, str->value.data);
    }
    else
    {
        VariableType type = vtype_get(inter, obj->vtype);
        
        if (type.kind == VariableKind_Array) {
            Array<Object*> elements = ((Object_Array*)obj)->elements;
            if (elements.data != NULL) gc_dynamic_free(inter, elements.data);
        }
        else if (type.kind == VariableKind_Struct) {
            Array<Object*> members = ((Object_Struct*)obj)->members;
            if (members.data != NULL) gc_dynamic_free(inter, members.data);
        }
    }
    
    u32 type_size = vtype_get_size(inter, obj->vtype);
    memory_zero(obj + 1, type_size - sizeof(Object));
}

void object_copy(Interpreter* inter, Object* dst, Object* src)
{
    if (is_unknown(dst) || is_null(dst)) {
        assert(0);
        return;
    }
    
    if (is_unknown(src) || is_null(src)) {
        assert(0);
        return;
    }
    
    if (src->vtype != dst->vtype) {
        assert(0);
        return;
    }
    
    object_release_internal(inter, dst);
    
    u32 type_size = vtype_get_size(inter, src->vtype);
    memory_copy(dst + 1, src + 1, type_size - sizeof(Object));
    
    if (src->vtype == VType_String)
    {
        Object_String* src_str = (Object_String*)src;
        Object_String* dst_str = (Object_String*)dst;
        dst_str->value.data = (char*)gc_dynamic_allocate(inter, src_str->value.size);
        dst_str->value.size = src_str->value.size;
        memory_copy(dst_str->value.data, src_str->value.data, src_str->value.size);
    }
    else
    {
        VariableType type = vtype_get(inter, src->vtype);
        
        if (type.kind == VariableKind_Array)
        {
            Object_Array* dst_array = (Object_Array*)dst;
            Object_Array* src_array = (Object_Array*)src;
            
            Object** memory = (Object**)gc_dynamic_allocate(inter, sizeof(Object*) * src_array->elements.count);
            dst_array->elements.data = memory;
            
            foreach(i, src_array->elements.count) {
                Object* element_obj = object_alloc_and_copy(inter, src_array->elements[i]);
                object_increment_ref(element_obj);
                dst_array->elements[i] = element_obj;
            }
        }
        else if (type.kind == VariableKind_Struct)
        {
            Object_Struct* dst_struct = (Object_Struct*)dst;
            Object_Struct* src_struct = (Object_Struct*)src;
            
            Object** memory = (Object**)gc_dynamic_allocate(inter, sizeof(Object*) * src_struct->members.count);
            dst_struct->members.data = memory;
            
            foreach(i, src_struct->members.count) {
                Object* member_obj = object_alloc_and_copy(inter, src_struct->members[i]);
                object_increment_ref(member_obj);
                dst_struct->members[i] = member_obj;
            }
        }
    }
}

Object* object_alloc_and_copy(Interpreter* inter, Object* src)
{
    if (is_unknown(src)) {
        assert(0);
        return nil_obj;
    }
    
    if (is_null(src)) return null_obj;
    
    Object* dst = object_alloc(inter, src->vtype).obj;
    object_copy(inter, dst, src);
    return dst;
}

void* gc_dynamic_allocate(Interpreter* inter, u64 size) {
    inter->allocation_count++;
    return os_allocate_heap(size);
}

void gc_dynamic_free(Interpreter* inter, void* ptr) {
    assert(inter->allocation_count > 0);
    inter->allocation_count--;
    os_free_heap(ptr);
}

void print_memory_usage(Interpreter* inter)
{
    print_separator();
    
    u32 refs = 0;
    for (Scope* scope = inter->current_scope; scope != NULL; scope = scope->previous) {
        refs += scope->object_refs.count;
    }
    
#if 0
    Object* obj = inter->object_list;
    while (obj != NULL)
    {
        Object* next = obj->next;
        log_mem_trace("Obj %u: %u refs\n", obj->ID, obj->ref_count);
        obj = next;
    }
#endif
    
    print_info("Object Refs: %u\n", refs);
    print_info("Object Count: %u\n", inter->object_count);
    print_info("Alloc Count: %u\n", inter->allocation_count);
    print_separator();
}