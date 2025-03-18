#include "inc.h"

internal_fn Object* solve_binary_operation(Interpreter* inter, Object* left, Object* right, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    
    VariableType left_type = vtype_get(inter, left->vtype);
    VariableType right_type = vtype_get(inter, right->vtype);
    
    if (is_int(left) && is_int(right)) {
        if (op == BinaryOperator_Addition) return obj_alloc_temp_int(inter, get_int(left) + get_int(right));
        if (op == BinaryOperator_Substraction) return obj_alloc_temp_int(inter, get_int(left) - get_int(right));
        if (op == BinaryOperator_Multiplication) return obj_alloc_temp_int(inter, get_int(left) * get_int(right));
        if (op == BinaryOperator_Division || op == BinaryOperator_Modulo) {
            i64 divisor = get_int(right);
            if (divisor == 0) {
                report_zero_division(code);
                return obj_alloc_temp_int(inter, i64_max);
            }
            if (op == BinaryOperator_Modulo) return obj_alloc_temp_int(inter, get_int(left) % divisor);
            return obj_alloc_temp_int(inter, get_int(left) / divisor);
        }
        if (op == BinaryOperator_Equals) return obj_alloc_temp_bool(inter, get_int(left) == get_int(right));
        if (op == BinaryOperator_NotEquals) return obj_alloc_temp_bool(inter, get_int(left) != get_int(right));
        if (op == BinaryOperator_LessThan) return obj_alloc_temp_bool(inter, get_int(left) < get_int(right));
        if (op == BinaryOperator_LessEqualsThan) return obj_alloc_temp_bool(inter, get_int(left) <= get_int(right));
        if (op == BinaryOperator_GreaterThan) return obj_alloc_temp_bool(inter, get_int(left) > get_int(right));
        if (op == BinaryOperator_GreaterEqualsThan) return obj_alloc_temp_bool(inter, get_int(left) >= get_int(right));
    }
    
    if (is_bool(left) && is_bool(right)) {
        if (op == BinaryOperator_LogicalOr) return obj_alloc_temp_bool(inter, get_bool(left) || get_bool(right));
        if (op == BinaryOperator_LogicalAnd) return obj_alloc_temp_bool(inter, get_bool(left) && get_bool(right));
    }
    
    if (is_string(left) && is_string(right))
    {
        if (op == BinaryOperator_Addition) {
            String str = string_format(scratch.arena, "%S%S", get_string(left), get_string(right));
            return obj_alloc_temp_string(inter, str);
        }
        else if (op == BinaryOperator_Division)
        {
            if (os_path_is_absolute(get_string(right))) {
                report_right_path_cant_be_absolute(code);
                return left;
            }
            
            String str = path_append(scratch.arena, get_string(left), get_string(right));
            str = path_resolve(scratch.arena, str);
            
            return obj_alloc_temp_string(inter, str);
        }
        else if (op == BinaryOperator_Equals) {
            return obj_alloc_temp_bool(inter, (b8)string_equals(get_string(left), get_string(right)));
        }
        else if (op == BinaryOperator_NotEquals) {
            return obj_alloc_temp_bool(inter, !(b8)string_equals(get_string(left), get_string(right)));
        }
    }
    
    if (is_enum(inter, left->vtype) && is_enum(inter, right->vtype)) {
        if (op == BinaryOperator_Equals) {
            return obj_alloc_temp_bool(inter, left->enum_.index == right->enum_.index);
        }
        else if (op == BinaryOperator_NotEquals) {
            return obj_alloc_temp_bool(inter, left->enum_.index != right->enum_.index);
        }
    }
    
    if (left_type.array_of == right_type.array_of && left_type.kind == VariableKind_Array && right_type.kind == VariableKind_Array)
    {
        VariableType array_of = vtype_get(inter, left_type.array_of);
        
        i64 left_count = left->array.count;
        i64 right_count = right->array.count;
        
        if (op == BinaryOperator_Addition) {
            Object* array = obj_alloc_temp_array(inter, array_of.vtype, left_count + right_count);
            for (i64 i = 0; i < left_count; ++i) {
                obj_copy_element_from_element(inter, array, i, left, i);
            }
            for (i64 i = 0; i < right_count; ++i) {
                obj_copy_element_from_element(inter, array, left_count + i, right, i);
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
            return inter->nil_obj;
        }
        
        Object* array_src = (left_type.kind == VariableKind_Array) ? left : right;
        Object* element = (left_type.kind == VariableKind_Array) ? right : left;
        
        Object* array = obj_alloc_temp_array(inter, element_type.vtype, array_src->array.count + 1);
        
        i64 array_offset = (left_type.kind == VariableKind_Array) ? 0 : 1;
        
        for (i64 i = 0; i < array_src->array.count; ++i) {
            obj_copy_element_from_element(inter, array, i + array_offset, array_src, i);
        }
        
        i64 element_offset = (left_type.kind == VariableKind_Array) ? array_src->array.count : 0;
        obj_copy_element_from_object(inter, array, element_offset, element);
        
        return array;
    }
    
    return inter->nil_obj;
}

internal_fn Object* solve_signed_operation(Interpreter* inter, Object* expresion, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    
    if (is_int(expresion)) {
        if (op == BinaryOperator_Addition) return expresion;
        if (op == BinaryOperator_Substraction) return obj_alloc_temp_int(inter, -get_int(expresion));
    }
    
    if (is_bool(expresion)) {
        if (op == BinaryOperator_LogicalNot) return obj_alloc_temp_bool(inter, !get_bool(expresion));
    }
    
    return inter->nil_obj;
}

Object* interpret_expresion(Interpreter* inter, OpNode* node, ExpresionContext context)
{
    SCRATCH();
    
    if (node->kind == OpKind_Error) return inter->nil_obj;
    if (node->kind == OpKind_None) return inter->void_obj;
    
    if (node->kind == OpKind_Binary)
    {
        auto node0 = (OpNode_Binary*)node;
        Object* left = interpret_expresion(inter, node0->left, context);
        Object* right = interpret_expresion(inter, node0->right, context);
        
        if (is_unknown(left) || is_unknown(right)) return inter->nil_obj;
        
        BinaryOperator op = node0->op;
        
        Object* result = solve_binary_operation(inter, left, right, op, node->code);
        
        if (is_valid(result))
        {
            if (inter->settings.execute && inter->settings.print_execution) {
                String left_string = string_from_obj(scratch.arena, inter, left);
                String right_string = string_from_obj(scratch.arena, inter, right);
                String result_string = string_from_obj(scratch.arena, inter, result);
                log_trace(inter->ctx, node->code, "%S %S %S = %S", left_string, string_from_binary_operator(op), right_string, result_string);
            }
            return result;
        }
        else
        {
            report_invalid_binary_op(node->code, string_from_obj(scratch.arena, inter, left), string_from_binary_operator(op), string_from_obj(scratch.arena, inter, right));
            return inter->nil_obj;
        }
    }
    
    if (node->kind == OpKind_Sign)
    {
        auto node0 = (OpNode_Sign*)node;
        Object* expresion = interpret_expresion(inter, node0->expresion, context);
        BinaryOperator op = node0->op;
        
        Object* result = solve_signed_operation(inter, expresion, op, node0->code);
        
        if (is_valid(result)) {
            
            if (inter->settings.execute && inter->settings.print_execution) {
                String expresion_string = string_from_obj(scratch.arena, inter, expresion);
                String result_string = string_from_obj(scratch.arena, inter, result);
                log_trace(inter->ctx, node->code, STR("%S %S = %S"), string_from_binary_operator(op), expresion_string, result_string);
            }
            
            return result;
        }
        else {
            report_invalid_signed_op(node->code, string_from_binary_operator(op), string_from_obj(scratch.arena, inter, expresion));
            return inter->nil_obj;
        }
    }
    
    if (node->kind == OpKind_IntLiteral) return obj_alloc_temp_int(inter, ((OpNode_Literal*)node)->int_literal);
    if (node->kind == OpKind_StringLiteral) {
        auto node0 = (OpNode_Literal*)node;
        return obj_alloc_temp_string(inter, solve_string_literal(scratch.arena, inter, node0->string_literal, node0->code));
    }
    if (node->kind == OpKind_BoolLiteral) return obj_alloc_temp_bool(inter, ((OpNode_Literal*)node)->bool_literal);
    if (node->kind == OpKind_IdentifierValue) {
        auto node0 = (OpNode_IdentifierValue*)node;
        Object* obj = find_object(inter, node0->identifier, true);
        
        if (obj == NULL) {
            report_object_not_found(node->code, node0->identifier);
            return inter->nil_obj;
        }
        
        return obj;
    }
    
    if (node->kind == OpKind_MemberValue) {
        auto node0 = (OpNode_MemberValue*)node;
        
        OpNode* expresion_node = node0->expresion;
        String member = node0->member;
        assert(member.size);
        
        if (expresion_node->kind == OpKind_IdentifierValue || expresion_node->kind == OpKind_None)
        {
            String identifier = {};
            
            if (expresion_node->kind == OpKind_IdentifierValue)
                identifier = ((OpNode_IdentifierValue*)expresion_node)->identifier;
            
            if (identifier.size == 0 && context.expected_vtype > 0) {
                identifier = string_from_vtype(scratch.arena, inter, context.expected_vtype);
            }
            
            Symbol symbol = find_symbol(inter, identifier);
            
            if (symbol.type == SymbolType_None) {
                report_symbol_not_found(node->code, identifier);
                return inter->nil_obj;
            }
            
            if (symbol.type == SymbolType_Object)
            {
                Object* obj = symbol.object;
                Object* member_obj = member_from_object(inter, obj, member);
                
                if (member_obj == inter->nil_obj) {
                    report_member_not_found_in_object(node->code, member, string_from_vtype(scratch.arena, inter, obj->vtype));
                }
                
                return member_obj;
            }
            
            if (symbol.type == SymbolType_Type)
            {
                Object* member_obj = member_from_type(inter, symbol.vtype, member);
                
                if (member_obj == inter->nil_obj) {
                    report_member_not_found_in_type(node->code, member, string_from_vtype(scratch.arena, inter, symbol.vtype));
                }
                
                return member_obj;
            }
            
            report_member_invalid_symbol(node->code, symbol.identifier);
        }
        else
        {
            Object* obj = interpret_expresion(inter, expresion_node, context);
            if (obj == inter->nil_obj) return inter->nil_obj;
            
            Object* member_obj = member_from_object(inter, obj, member);
            
            if (member_obj == inter->nil_obj) {
                report_member_not_found_in_object(node->code, member, string_from_vtype(scratch.arena, inter, obj->vtype));
            }
            
            return member_obj;
        }
        
        return inter->nil_obj;
    }
    
    if (node->kind == OpKind_FunctionCall) {
        return interpret_function_call(inter, node, true);
    }
    
    if (node->kind == OpKind_ArrayExpresion)
    {
        auto node0 = (OpNode_ArrayExpresion*)node;
        Array<Object*> objects = array_make<Object*>(scratch.arena, node0->nodes.count);
        
        foreach(i, objects.count) {
            objects[i] = interpret_expresion(inter, node0->nodes[i], context);
        }
        
        if (objects.count == 0) {
            if (context.expected_vtype <= 0) {
                return inter->void_obj;
            }
            
            i32 element_vtype = vtype_from_array_element(inter, context.expected_vtype);
            return obj_alloc_temp_array(inter, element_vtype, 0);
        }
        
        i32 vtype = objects[0]->vtype;
        
        // Assert same vtype
        for (i32 i = 1; i < objects.count; ++i) {
            if (objects[i]->vtype != vtype) {
                report_type_missmatch_array_expr(node->code, string_from_vtype(scratch.arena, inter, vtype), string_from_vtype(scratch.arena, inter, objects[i]->vtype));
                return inter->nil_obj;
            }
        }
        
        Object* array = obj_alloc_temp_array(inter, vtype, objects.count);
        foreach(i, objects.count) {
            obj_copy_element_from_object(inter, array, i, objects[i]);
        }
        
        return array;
    }
    
    if (node->kind == OpKind_ArrayElementValue)
    {
        auto node0 = (OpNode_ArrayElementValue*)node;
        Object* indexing_obj = interpret_expresion(inter, node0->expresion, context);
        Object* array = find_object(inter, node0->identifier, true);
        
        if (array == NULL) {
            report_object_not_found(node->code, node0->identifier);
            return inter->nil_obj;
        }
        
        if (!is_int(indexing_obj)) {
            report_indexing_expects_an_int(node->code);
            return inter->nil_obj;
        }
        
        VariableType array_vtype = vtype_get(inter, array->vtype);
        
        if (array_vtype.kind != VariableKind_Array) {
            report_indexing_not_allowed(node->code, string_from_vtype(scratch.arena, inter, array->vtype));
            return inter->nil_obj;
        }
        
        Object* obj = obj_alloc_temp(inter, array_vtype.array_of);
        
        if (inter->settings.execute)
        {
            i64 index = get_int(indexing_obj);
            if (index < 0 || index >= array->array.count) {
                report_indexing_out_of_bounds(node->code);
                return inter->nil_obj;
            }
            
            obj_copy_from_element(inter, obj, array, index);
            return obj;
        }
        else {
            return obj;
        }
    }
    
    report_expr_semantic_unknown(node->code);
    return inter->nil_obj;
}

void interpret_indexed_assignment(Interpreter* inter, Object* array, i64 index, OpNode* assignment, b32 assert_assignment)
{
    SCRATCH();
    assert(inter->settings.execute);
    
    VariableType array_type = vtype_get(inter, array->vtype);
    
    if (array_type.kind != VariableKind_Array) {
        assert(0);
        return;
    }
    
    b32 no_assignment = assignment->kind != OpKind_None && assignment->kind != OpKind_Error;
    
    if (!no_assignment) {
        assert(0);
        return;
    }
    
    i32 expected_vtype = vtype_from_array_element(inter, array->vtype);
    Object* assignment_result = interpret_expresion(inter, assignment, expresion_context_make(expected_vtype));
    
    if (!obj_copy_element_from_object(inter, array, index, assignment_result)) {
        report_type_missmatch_assign(assignment->code, string_from_vtype(scratch.arena, inter, assignment_result->vtype), string_from_vtype(scratch.arena, inter, array->vtype));
        return;
    }
    
    if (inter->settings.print_execution) log_trace(inter->ctx, assignment->code, STR("%S[%l] = %S"), array->identifier, index, string_from_obj(scratch.arena, inter, array));
}

Object* interpret_assignment_for_object_definition(Interpreter* inter, OpNode_ObjectDefinition* node)
{
    SCRATCH();
    i32 vtype = -1;
    
    // Explicit type
    {
        String type_name = node->type->name;
        
        if (type_name.size > 0)
        {
            vtype = vtype_from_name(inter, type_name);
            
            if (vtype <= 0) {
                report_object_type_not_found(node->code, type_name);
                return inter->nil_obj;
            }
            
            if (node->type->is_array) {
                vtype = vtype_from_array_dimension(inter, vtype, node->type->array_dimensions.count);
            }
        }
    }
    
    OpNode* assignment = node->assignment;
    Object* assignment_result = interpret_expresion(inter, assignment, expresion_context_make(vtype));
    
    if (is_unknown(assignment_result)) return inter->nil_obj;
    
    if (node->type->is_array)
    {
        Array<i64> dimensions = array_make<i64>(scratch.arena, node->type->array_dimensions.count);
        
        b8 initialize_array = false;
        
        foreach(i, dimensions.count) {
            Object* dim = interpret_expresion(inter, node->type->array_dimensions[i], expresion_context_make(VType_Int));
            if (is_unknown(dim)) return inter->nil_obj;
            
            if (!is_void(dim) && !is_int(dim)) {
                report_dimensions_expects_an_int(node->code);
                return inter->nil_obj;
            }
            dimensions[i] = is_int(dim) ? get_int(dim) : 0;
            if (dimensions[i] < 0) {
                report_dimensions_must_be_positive(node->code);
                return inter->nil_obj;
            }
            
            if (dimensions[i] > 0) initialize_array = true;
        }
        
        
        if (initialize_array) {
            assert(is_void(assignment_result));
            assignment_result = obj_alloc_temp_array_multidimensional(inter, vtype_from_array_base(inter, vtype), dimensions);
        }
    }
    
    if (is_void(assignment_result))
    {
        if (vtype <= 0) {
            report_object_invalid_type(node->code, "void");
            return inter->nil_obj;
        }
        
        assignment_result = obj_alloc_temp(inter, vtype);
    }
    else
    {
        if (vtype < 0) {
            vtype = assignment_result->vtype;
        }
        else {
            if (assignment_result->vtype != vtype) {
                report_type_missmatch_assign(node->code, string_from_vtype(scratch.arena, inter, assignment_result->vtype), string_from_vtype(scratch.arena, inter, vtype));
                return inter->nil_obj;
            }
        }
    }
    
    return assignment_result;
}

void interpret_object_definition(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    
    auto node = (OpNode_ObjectDefinition*)node0;
    String identifier = node->object_name;
    Object* obj = find_object(inter, identifier, false);
    if (obj != NULL) {
        report_object_duplicated(node->code, identifier);
        return;
    }
    
    Object* assignment_result = interpret_assignment_for_object_definition(inter, node);
    if (is_unknown(assignment_result)) return;
    if (is_void(assignment_result)) return;
    
    obj = define_object(inter, identifier, assignment_result->vtype);
    
    if (inter->settings.execute)
    {
        if (inter->settings.print_execution) log_trace(inter->ctx, node->code, STR("%S %S"), string_from_vtype(scratch.arena, inter, obj->vtype), obj->identifier);
        
        if (is_valid(assignment_result)) {
            if (!obj_copy(inter, obj, assignment_result)) return;
            if (inter->settings.print_execution) log_trace(inter->ctx, assignment->code, STR("%S = %S"), obj->identifier, string_from_obj(scratch.arena, inter, obj));
        }
    }
}

void interpret_variable_assignment(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_Assignment*)node0;
    
    Object* obj = find_object(inter, node->identifier, true);
    if (obj == NULL) {
        report_object_not_found(node->code, node->identifier);
        return;
    }
    
    OpNode* assignment = node->value;
    BinaryOperator op = node->binary_operator;
    
    Object* assignment_obj = interpret_expresion(inter, assignment, expresion_context_make(obj->vtype));
    
    if (is_unknown(assignment_obj)) return;
    if (is_void(assignment_obj)) {
        assert(0);
        return;
    }
    
    if (op != BinaryOperator_None) {
        assignment_obj = solve_binary_operation(inter, obj, assignment_obj, op, assignment->code);
    }
    
    if (assignment_obj->vtype != obj->vtype) {
        report_type_missmatch_assign(assignment->code, string_from_vtype(scratch.arena, inter, assignment_obj->vtype), string_from_vtype(scratch.arena, inter, obj->vtype));
        return;
    }
    
    if (is_unknown(assignment_obj)) return;
    
    if (inter->settings.execute)
    {
        if (!obj_copy(inter, obj, assignment_obj)) return;
        if (inter->settings.print_execution) log_trace(inter->ctx, assignment->code, STR("%S = %S"), obj->identifier, string_from_obj(scratch.arena, inter, obj));
    }
}

void interpret_array_element_assignment(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    
    auto node = (OpNode_ArrayElementAssignment*)node0;
    
    Object* array = find_object(inter, node->identifier, true);
    if (array == NULL) {
        report_object_not_found(node->code, node->identifier);
        return;
    }
    
    i32 expected_vtype = vtype_from_array_element(inter, array->vtype);
    Object* indexing_obj = interpret_expresion(inter, node->indexing_expresion, expresion_context_make(expected_vtype));
    
    if (!is_int(indexing_obj)) {
        report_indexing_expects_an_int(node->code);
        return;
    }
    
    VariableType array_vtype = vtype_get(inter, array->vtype);
    
    if (array_vtype.kind != VariableKind_Array) {
        report_indexing_not_allowed(node->code, string_from_vtype(scratch.arena, inter, array->vtype));
        return;
    }
    
    if (inter->settings.execute)
    {
        i64 index = get_int(indexing_obj);
        if (index < 0 || index >= array->array.count) {
            report_indexing_out_of_bounds(node->code);
            return;
        }
        
        OpNode* assignment = node->value;
        if (node->binary_operator != BinaryOperator_None) {
            assert(0);
            // TODO(Jose): 
        }
        interpret_indexed_assignment(inter, array, index, assignment, false);
    }
}

void interpret_if_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_IfStatement*)node0;
    
    Object* expresion_result = interpret_expresion(inter, node->expresion, expresion_context_make(VType_Bool));
    
    if (is_unknown(expresion_result)) return;
    
    if (!is_bool(expresion_result)) {
        report_expr_expects_bool(node->code, "If-Statement");
        return;
    }
    
    if (inter->settings.execute) {
        if (inter->settings.print_execution) log_trace(inter->ctx, node->code, STR("if (%S)"), string_from_obj(scratch.arena, inter, expresion_result));
        b32 result = get_bool(expresion_result);
        
        push_scope(inter, ScopeType_Block, 0);
        if (result) interpret_op(inter, node, node->success);
        else interpret_op(inter, node, node->failure);
        pop_scope(inter);
    }
    else {
        
        Scope* scope = get_returnable_scope(inter);
        Object* previous_return_obj = scope->return_obj;
        
        push_scope(inter, ScopeType_Block, 0);
        interpret_op(inter, node, node->success);
        pop_scope(inter);
        Object* success_return_obj = scope->return_obj;
        scope->return_obj = previous_return_obj;
        
        push_scope(inter, ScopeType_Block, 0);
        interpret_op(inter, node, node->failure);
        pop_scope(inter);
        Object* failed_return_obj = scope->return_obj;
        scope->return_obj = previous_return_obj;
        
        if (success_return_obj != inter->nil_obj && failed_return_obj != inter->nil_obj) {
            scope->return_obj = success_return_obj;
        }
    }
    
}

void interpret_while_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_WhileStatement*)node0;
    
    while (1)
    {
        Object* expresion_result = interpret_expresion(inter, node->expresion, expresion_context_make(VType_Bool));
        if (skip_ops(inter)) return;
        
        if (!is_bool(expresion_result)) {
            report_expr_expects_bool(node->code, "While-Statement");
            return;
        }
        
        if (inter->settings.execute)
        {
            if (inter->settings.print_execution) log_trace(inter->ctx, node->code, STR("while (%S)"), string_from_obj(scratch.arena, inter, expresion_result));
            
            if (!get_bool(expresion_result)) break;
            
            push_scope(inter, ScopeType_Block, 0);
            interpret_op(inter, node, node->content);
            pop_scope(inter);
        }
        else
        {
            Scope* scope = get_returnable_scope(inter);
            Object* previous_return_obj = scope->return_obj;
            push_scope(inter, ScopeType_Block, 0);
            interpret_op(inter, node, node->content);
            pop_scope(inter);
            scope->return_obj = previous_return_obj;
            break;
        }
        
        if (skip_ops(inter)) return;
    }
}

void interpret_for_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_ForStatement*)node0;
    
    push_scope(inter, ScopeType_Block, 0);
    
    interpret_op(inter, node, node->initialize_sentence);
    
    while (1)
    {
        Object* expresion_result = interpret_expresion(inter, node->condition_expresion, expresion_context_make(VType_Bool));
        if (skip_ops(inter)) return;
        
        if (!is_bool(expresion_result)) {
            report_expr_expects_bool(node->code, "For-Statement");
            return;
        }
        
        if (inter->settings.execute)
        {
            if (inter->settings.print_execution) log_trace(inter->ctx, node->code, STR("for (%S)"), string_from_obj(scratch.arena, inter, expresion_result));
            
            if (!get_bool(expresion_result)) break;
            
            push_scope(inter, ScopeType_Block, 0);
            interpret_op(inter, node, node->content);
            pop_scope(inter);
            
            interpret_op(inter, node, node->update_sentence);
        }
        else
        {
            Scope* scope = get_returnable_scope(inter);
            Object* previous_return_obj = scope->return_obj;
            push_scope(inter, ScopeType_Block, 0);
            interpret_op(inter, node, node->content);
            pop_scope(inter);
            interpret_op(inter, node, node->update_sentence);
            scope->return_obj = previous_return_obj;
            break;
        }
        
        if (skip_ops(inter)) return;
    }
    
    pop_scope(inter);
}

void interpret_foreach_array_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_ForeachArrayStatement*)node0;
    
    push_scope(inter, ScopeType_Block, 0);
    
    Object* array = inter->void_obj;
    
    if (node->expresion->kind == OpKind_IdentifierValue)
    {
        OpNode_IdentifierValue* expresion_node = (OpNode_IdentifierValue*)node->expresion;
        Symbol symbol = find_symbol(inter, expresion_node->identifier);
        
        if (symbol.type == SymbolType_None) {
            report_symbol_not_found(expresion_node->code, expresion_node->identifier);
            return;
        }
        if (symbol.type == SymbolType_Object) {
            array = symbol.object;
        }
        if (symbol.type == SymbolType_Type) {
            VariableType type = vtype_get(inter, symbol.vtype);
            if (type.kind == VariableKind_Enum) {
                array = obj_alloc_temp_array_from_enum(inter, symbol.vtype);
            }
        }
    }
    else
    {
        array = interpret_expresion(inter, node->expresion, expresion_context_make(-1));
    }
    
    if (is_unknown(array)) return;
    
    VariableType array_type = vtype_get(inter, array->vtype);
    
    if (array_type.kind != VariableKind_Array) {
        report_for_expects_an_array(node->code);
        return;
    }
    
    if (skip_ops(inter)) return;
    
    Object* element = define_object(inter, node->element_name, array_type.array_of);
    
    Object* index = NULL;
    if (node->index_name.size > 0) index = define_object(inter, node->index_name, VType_Int);
    
    for (i64 i = 0; i < array->array.count; ++i)
    {
        if (inter->settings.execute)
        {
            obj_copy_from_element(inter, element, array, i);
            
            if (index != NULL) {
                obj_set_int(index, i);
            }
            
            if (inter->settings.print_execution) log_trace(inter->ctx, node->code, STR("for each[%l] (%S)"), i, string_from_obj(scratch.arena, inter, element));
            
            push_scope(inter, ScopeType_Block, 0);
            interpret_op(inter, node, node->content);
            pop_scope(inter);
            
            obj_copy_element_from_object(inter, array, i, element);
        }
        else
        {
            Scope* scope = get_returnable_scope(inter);
            Object* previous_return_obj = scope->return_obj;
            push_scope(inter, ScopeType_Block, 0);
            interpret_op(inter, node, node->content);
            pop_scope(inter);
            scope->return_obj = previous_return_obj;
            break;
        }
        
        if (skip_ops(inter)) return;
    }
    
    pop_scope(inter);
}

Object* interpret_function_call(Interpreter* inter, OpNode* node0, b32 is_expresion)
{
    SCRATCH();
    auto node = (OpNode_FunctionCall*)node0;
    
    FunctionDefinition* fn = find_function(inter, node->identifier);
    
    if (fn == NULL) {
        report_function_not_found(node->code, node->identifier);
        return inter->nil_obj;
    }
    
    Array<Object*> objects = array_make<Object*>(scratch.arena, node->parameters.count);
    
    foreach(i, objects.count) {
        i32 expected_vtype = -1;
        if (i < fn->parameters.count) expected_vtype = fn->parameters[i].vtype;
        objects[i] = interpret_expresion(inter, node->parameters[i], expresion_context_make(expected_vtype));
    }
    
    if (skip_ops(inter)) return inter->nil_obj;
    
    return call_function(inter, fn, objects, node, is_expresion);
}

void interpret_return(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    OpNode_Return* node = (OpNode_Return*)node0;
    Scope* scope = get_returnable_scope(inter);
    
    scope->return_obj = inter->nil_obj;
    
    Object* obj = interpret_expresion(inter, node->expresion, expresion_context_make(scope->expected_return_vtype));
    if (is_unknown(obj)) return;
    
    if (obj->vtype != scope->expected_return_vtype) {
        report_function_wrong_return_type(node->code, string_from_vtype(scratch.arena, inter, scope->expected_return_vtype));
        return;
    }
    
    scope->return_obj = obj;
}

internal_fn void interpret_block(Interpreter* inter, OpNode* block0)
{
    auto block = (OpNode_Block*)block0;
    assert(block->kind == OpKind_Block);
    
    push_scope(inter, ScopeType_Block, 0);
    
    Array<OpNode*> ops = block->ops;
    
    foreach(i, ops.count)
    {
        if (skip_ops(inter)) break;
        
        OpNode* node = ops[i];
        interpret_op(inter, block, node);
    }
    
    pop_scope(inter);
}

void interpret_op(Interpreter* inter, OpNode* parent, OpNode* node)
{
    if (skip_ops(inter)) return;
    if (node->kind == OpKind_None) return;
    
    if (node->kind == OpKind_Block) interpret_block(inter, node);
    else if (node->kind == OpKind_ObjectDefinition) interpret_object_definition(inter, node);
    else if (node->kind == OpKind_VariableAssignment) interpret_variable_assignment(inter, node);
    else if (node->kind == OpKind_ArrayElementAssignment) interpret_array_element_assignment(inter, node);
    else if (node->kind == OpKind_IfStatement) interpret_if_statement(inter, node);
    else if (node->kind == OpKind_WhileStatement) interpret_while_statement(inter, node);
    else if (node->kind == OpKind_ForStatement) interpret_for_statement(inter, node);
    else if (node->kind == OpKind_ForeachArrayStatement) interpret_foreach_array_statement(inter, node);
    else if (node->kind == OpKind_FunctionCall) interpret_function_call(inter, node, false);
    else if (node->kind == OpKind_Return) interpret_return(inter, node);
    else if (node->kind == OpKind_Error) {}
    else if (node->kind == OpKind_EnumDefinition || node->kind == OpKind_FunctionDefinition) {
        if (parent != inter->root) {
            report_nested_definition(node->code);
        }
    }
    else {
        report_semantic_unknown_op(node->code);
    }
}

internal_fn i32 define_enum(Interpreter* inter, String name, Array<String> names, Array<i64> values)
{
    SCRATCH();
    
    if (values.count == 0) {
        values = array_make<i64>(scratch.arena, names.count);
        foreach(i, values.count) values[i] = i;
    }
    
    names = array_copy(inter->ctx->static_arena, names);
    values = array_copy(inter->ctx->static_arena, values);
    
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

void register_intrinsic_functions(Interpreter* inter);

internal_fn void register_definitions(Interpreter* inter)
{
    SCRATCH();
    
    inter->vtype_table = pooled_array_make<VariableType>(inter->ctx->static_arena, 32);
    inter->functions = pooled_array_make<FunctionDefinition>(inter->ctx->static_arena, 32);
    
    PooledArray<VariableType>* list = &inter->vtype_table;
    
    VariableType t;
    
    // Void
    {
        t = {};
        t.name = STR("void");
        t.kind = VariableKind_Void;
        t.vtype = list->count;
        assert(VType_Void == t.vtype);
        array_add(list, t);
    }
    
    // Int
    {
        t = {};
        t.name = STR("Int");
        t.kind = VariableKind_Primitive;
        t.vtype = list->count;
        assert(VType_Int == t.vtype);
        array_add(list, t);
    }
    
    // Bool
    {
        t = {};
        t.name = STR("Bool");
        t.kind = VariableKind_Primitive;
        t.vtype = list->count;
        assert(VType_Bool == t.vtype);
        array_add(list, t);
    }
    
    // String
    {
        t = {};
        t.name = STR("String");
        t.kind = VariableKind_Primitive;
        t.vtype = list->count;
        assert(VType_String == t.vtype);
        array_add(list, t);
    }
    
    // Default Enums
    {
        String CopyMode[] = {
            STR("NoOverride"),
            STR("Override"),
        };
        
        
        i32 vtype;
        vtype = define_enum(inter, STR("CopyMode"), { CopyMode, array_count(CopyMode) }, {});
        assert(vtype == VType_Enum_CopyMode);
    }
    
    // Script Definitions
    if (inter->root->kind == OpKind_Block)
    {
        OpNode_Block* block = (OpNode_Block*)inter->root;
        
        foreach(i, block->ops.count)
        {
            OpNode* node0 = block->ops[i];
            
            if (node0->kind == OpKind_EnumDefinition)
            {
                OpNode_EnumDefinition* node = (OpNode_EnumDefinition*)node0;
                
                assert(node->values.count == node->names.count);
                
                Array<i64> values = array_make<i64>(scratch.arena, node->values.count);
                foreach(i, values.count)
                {
                    OpNode* value_node = node->values[i];
                    
                    if (value_node == NULL) {
                        values[i] = i;
                        continue;
                    }
                    
                    Object* obj = interpret_expresion(inter, value_node, expresion_context_make(VType_Int));
                    
                    if (obj->vtype < 0) continue;
                    if (obj->vtype != VType_Int) {
                        report_enum_value_expects_an_int(node->code);
                        continue;
                    }
                    
                    values[i] = get_int(obj);
                }
                
                define_enum(inter, node->identifier, node->names, values);
            }
            else if (node0->kind == OpKind_FunctionDefinition)
            {
                OpNode_FunctionDefinition* node = (OpNode_FunctionDefinition*)node0;
                
                b32 valid = true;
                
                Array<ParameterDefinition> parameters = array_make<ParameterDefinition>(inter->ctx->static_arena, node->parameters.count);
                
                foreach(i, parameters.count)
                {
                    OpNode_ObjectDefinition* param_node = node->parameters[i];
                    ParameterDefinition* def = &parameters[i];
                    
                    Object* assignment_result = interpret_assignment_for_object_definition(inter, param_node);
                    if (is_unknown(assignment_result) || is_void(assignment_result)) {
                        valid = false;
                        continue;
                    }
                    
                    b32 has_default_value = (param_node->assignment->kind != OpKind_None && param_node->assignment->kind != OpKind_Error);
                    
                    def->vtype = assignment_result->vtype;
                    def->name = param_node->object_name;
                    def->default_value = has_default_value ? assignment_result : inter->nil_obj;
                }
                
                i32 return_vtype = VType_Void;
                
                if (node->return_node->kind == OpKind_ObjectType) {
                    OpNode_ObjectType* type_node = (OpNode_ObjectType*)node->return_node;
                    return_vtype = vtype_from_name(inter, type_node->name);
                    return_vtype = vtype_from_array_dimension(inter, return_vtype, type_node->array_dimensions.count);
                }
                
                if (!valid) continue;
                
                FunctionDefinition fn{};
                fn.identifier = node->identifier;
                fn.return_vtype = return_vtype;
                fn.parameters = parameters;
                fn.defined_fn = node->block;
                array_add(&inter->functions, fn);
            }
        }
    }
    
    register_intrinsic_functions(inter);
    
    foreach(i, inter->vtype_table.count) {
        VariableType t = inter->vtype_table[i];
        assert(t.kind != VariableKind_Array);
    }
}

internal_fn void define_globals(Interpreter* inter)
{
    Object* obj;
    
    obj = define_object(inter, STR("context_script_dir"), VType_String);
    obj_set_string(inter, obj, inter->ctx->script_dir);
    
    obj = define_object(inter, STR("context_caller_dir"), VType_String);
    obj_set_string(inter, obj, inter->ctx->caller_dir);
    
    inter->cd_obj = define_object(inter, STR("cd"), VType_String);
    obj_set_string(inter, inter->cd_obj, inter->ctx->script_dir);
    
    obj = define_object(inter, STR("yov_major"), VType_Int);
    obj_set_int(obj, YOV_MAJOR_VERSION);
    obj = define_object(inter, STR("yov_minor"), VType_Int);
    obj_set_int(obj, YOV_MINOR_VERSION);
    obj = define_object(inter, STR("yov_revision"), VType_Int);
    obj_set_int(obj, YOV_REVISION_VERSION);
    obj = define_object(inter, STR("yov_version"), VType_String);
    obj_set_string(inter, obj, YOV_VERSION);
    
    // Args
    {
        Array<ProgramArg> args = inter->ctx->args;
        Object* array = obj_alloc_temp_array(inter, VType_String, args.count);
        foreach(i, args.count) {
            Object* element = obj_alloc_temp_string(inter, args[i].name);
            obj_copy_element_from_object(inter, array, i, element);
        }
        
        obj = define_object(inter, STR("context_args"), VType_StringArray);
        obj_copy(inter, obj, array);
    }
}

void interpret(Yov* ctx, OpNode* block, InterpreterSettings settings)
{
    Interpreter* inter = arena_push_struct<Interpreter>(ctx->static_arena);
    inter->ctx = ctx;
    inter->settings = settings;
    inter->root = block;
    
    inter->void_obj = arena_push_struct<Object>(inter->ctx->static_arena);
    inter->void_obj->vtype = VType_Void;
    
    inter->nil_obj = arena_push_struct<Object>(inter->ctx->static_arena);
    inter->nil_obj->vtype = VType_Unknown;
    
    inter->global_scope = alloc_scope(inter, ScopeType_Global);
    inter->current_scope = inter->global_scope;
    
    register_definitions(inter);
    define_globals(inter);
    
    interpret_block(inter, block);
    
    assert(inter->ctx->error_count != 0 || inter->global_scope == inter->current_scope);
}

void interpreter_exit(Interpreter* inter)
{
    inter->ctx->error_count++;// TODO(Jose): Weird
}

void interpreter_report_runtime_error(Interpreter* inter, CodeLocation code, String resolved_line, String message_error)
{
    String script_path = yov_get_script_path(inter->ctx, code.script_id);
    print_error("%S(%u): %S\n\t--> %S\n", script_path, (u32)code.line, resolved_line, message_error);
    
    interpreter_exit(inter);
}

Result user_assertion(Interpreter* inter, String message)
{
    Result res{};
    res.success = true;
    if (!inter->settings.user_assertion) return res;
    res.success = os_ask_yesno(STR("User Assertion"), message);
    if (!res.success) res.message = STR("Operation denied by user");
    return res;
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

i32 vtype_from_name(Interpreter* inter, String name)
{
    foreach(i, inter->vtype_table.count) {
        if (string_equals(inter->vtype_table[i].name, name)) {
            return i;
        }
    }
    return -1;
}

i32 vtype_from_array_dimension(Interpreter* inter, i32 vtype, u32 dimension)
{
    if (vtype < 0 || vtype >= inter->vtype_table.count) return -1;
    
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

u32 vtype_get_stride(Interpreter* inter, i32 vtype) {
    if (vtype == VType_Int) return sizeof(ObjectMemory_Int);
    if (vtype == VType_Bool) return sizeof(ObjectMemory_Bool);
    if (vtype == VType_String) return sizeof(ObjectMemory_String);
    
    u32 dims;
    decode_vtype(vtype, NULL, &dims);
    if (dims > 0) return sizeof(ObjectMemory_Array);
    
    if (is_enum(inter, vtype)) return sizeof(ObjectMemory_Enum);
    
    assert(0);
    return 0;
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

internal_fn ObjectMemory_String alloc_string_memory(Interpreter* inter, String value)
{
    ObjectMemory_String dst;
    dst.size = value.size;
    dst.data = (char*)arena_push(inter->ctx->static_arena, value.size);
    memory_copy(dst.data, value.data, value.size);
    return dst;
}

internal_fn ObjectMemory_Array alloc_array_memory(Interpreter* inter, i64 count, i32 stride)
{
    ObjectMemory_Array dst;
    dst.count = count;
    dst.data = arena_push(inter->ctx->static_arena, count * stride);
    return dst;
}

Object* obj_alloc_temp(Interpreter* inter, i32 vtype)
{
    Object* obj = arena_push_struct<Object>(inter->ctx->static_arena);
    obj->identifier = STR("TEMP");
    obj->vtype = vtype;
    obj->scope = inter->current_scope;
    return obj;
}

Object* obj_alloc_temp_int(Interpreter* inter, i64 value)
{
    Object* obj = obj_alloc_temp(inter, VType_Int);
    obj->integer.value = value;
    return obj;
}

Object* obj_alloc_temp_bool(Interpreter* inter, b32 value)
{
    Object* obj = obj_alloc_temp(inter, VType_Bool);
    obj->boolean.value = value;
    return obj;
}

Object* obj_alloc_temp_string(Interpreter* inter, String value)
{
    Object* obj = obj_alloc_temp(inter, VType_String);
    obj_set_string(inter, obj, value);
    return obj;
}

Object* obj_alloc_temp_array(Interpreter* inter, i32 element_vtype, i64 count)
{
    i32 vtype = vtype_from_array_dimension(inter, element_vtype, 1);
    Object* obj = obj_alloc_temp(inter, vtype);
    
    u32 stride = vtype_get_stride(inter, element_vtype);
    obj->array = alloc_array_memory(inter, count, stride);
    return obj;
}

ObjectMemory_Array alloc_multidimensional_array_memory(Interpreter* inter, i32 element_vtype, Array<i64> dimensions)
{
    i32 vtype = vtype_from_array_dimension(inter, element_vtype, dimensions.count);
    i32 array_of = vtype_from_array_element(inter, vtype);
    
    i64 count = dimensions[dimensions.count - 1];
    u32 stride = vtype_get_stride(inter, array_of);
    ObjectMemory_Array mem = alloc_array_memory(inter, count, stride);
    
    if (dimensions.count > 1)
    {
        ObjectMemory_Array* elements = (ObjectMemory_Array*)mem.data;
        for (i64 i = 0; i < count; ++i) {
            ObjectMemory_Array* element = elements + i;
            *element = alloc_multidimensional_array_memory(inter, element_vtype, array_subarray(dimensions, 0, dimensions.count - 1));
        }
    }
    
    return mem;
}


Object* obj_alloc_temp_array_multidimensional(Interpreter* inter, i32 element_vtype, Array<i64> dimensions)
{
    i32 vtype = vtype_from_array_dimension(inter, element_vtype, dimensions.count);
    Object* obj = obj_alloc_temp(inter, vtype);
    
    i32 array_of = vtype_from_array_element(inter, vtype);
    obj->array = alloc_multidimensional_array_memory(inter, element_vtype, dimensions);
    return obj;
}

Object* obj_alloc_temp_enum(Interpreter* inter, i32 vtype, i64 index)
{
    Object* obj = obj_alloc_temp(inter, vtype);
    obj->enum_.index = index;
    return obj;
}

Object* obj_alloc_temp_array_from_enum(Interpreter* inter, i32 enum_vtype)
{
    VariableType enum_type = vtype_get(inter, enum_vtype);
    
    if (enum_type.kind != VariableKind_Enum) {
        assert(0);
        return inter->nil_obj;
    }
    
    Object* obj = obj_alloc_temp_array(inter, enum_vtype, enum_type.enum_values.count);
    ObjectMemory_Enum* data = (ObjectMemory_Enum*)obj->array.data;
    foreach(i, enum_type.enum_values.count) {
        data[i].index = i;
    }
    return obj;
}

ObjectMemory* obj_get_data(Object* obj) {
    return (ObjectMemory*)&obj->integer;
}

ObjectMemory obj_copy_data(Interpreter* inter, Object* obj)
{
    return obj_copy_data(inter, *obj_get_data(obj), obj->vtype);
}
ObjectMemory obj_copy_data(Interpreter* inter, ObjectMemory src, i32 vtype)
{
    if (vtype == VType_Int || vtype == VType_Bool) return src;
    
    if (vtype == VType_String) {
        String str;
        str.data = src.string.data;
        str.size = src.string.size;
        
        ObjectMemory res{};
        res.string = alloc_string_memory(inter, str);
        return res;
    }
    
    if (is_enum(inter, vtype)) return src;
    
    if (is_array(vtype))
    {
        VariableType array_vtype = vtype_get(inter, vtype);
        u32 stride = vtype_get_stride(inter, array_vtype.array_of);
        ObjectMemory_Array dst = alloc_array_memory(inter, src.array.count, stride);
        foreach(i, src.array.count) {
            ObjectMemory* dst_ptr = (ObjectMemory*)((u8*)dst.data + stride * i);
            ObjectMemory* src_ptr = (ObjectMemory*)((u8*)src.array.data + stride * i);
            ObjectMemory src = obj_copy_data(inter, *src_ptr, array_vtype.array_of);
            memory_copy(dst_ptr, &src, stride);
        }
        
        ObjectMemory res{};
        res.array = dst;
        return res;
    }
    
    assert(0);
    return {};
}

void obj_copy_element_from_element(Interpreter* inter, Object* dst_array, i64 dst_index, Object* src_array, i64 src_index)
{
    if (!is_array(dst_array) || !is_array(src_array)) {
        assert(0);
        return;
    }
    
    if (dst_array->vtype != src_array->vtype) {
        assert(0);
        return;
    }
    
    VariableType array_type = vtype_get(inter, dst_array->vtype);
    u32 stride = vtype_get_stride(inter, array_type.array_of);
    
    // TODO(Jose): FREE MEMORY HERE
    
    void* dst_data = (u8*)dst_array->array.data + stride * dst_index;
    ObjectMemory* src_data = (ObjectMemory*)((u8*)src_array->array.data + stride * src_index);
    ObjectMemory res = obj_copy_data(inter, *src_data, array_type.array_of);
    memory_copy(dst_data, &res, stride);
}

b32 obj_copy_element_from_object(Interpreter* inter, Object* dst_array, i64 dst_index, Object* src)
{
    if (!is_array(dst_array)) {
        assert(0);
        return false;
    }
    
    VariableType array_type = vtype_get(inter, dst_array->vtype);
    
    if (array_type.array_of != src->vtype) {
        assert(0);
        return false;
    }
    
    u32 stride = vtype_get_stride(inter, array_type.array_of);
    
    // TODO(Jose): FREE MEMORY HERE
    
    void* dst_data = (u8*)dst_array->array.data + stride * dst_index;
    ObjectMemory res = obj_copy_data(inter, src);
    memory_copy(dst_data, &res, stride);
    return true;
}

void obj_copy_from_element(Interpreter* inter, Object* dst, Object* src_array, i64 src_index)
{
    if (!is_array(src_array)) {
        assert(0);
        return;
    }
    
    VariableType array_type = vtype_get(inter, src_array->vtype);
    
    if (array_type.array_of != dst->vtype) {
        assert(0);
        return;
    }
    
    u32 stride = vtype_get_stride(inter, array_type.array_of);
    
    // TODO(Jose): FREE MEMORY HERE
    
    ObjectMemory* element_data = (ObjectMemory*)((u8*)src_array->array.data + stride * src_index);
    ObjectMemory* dst_memory = obj_get_data(dst);
    *dst_memory = obj_copy_data(inter, *element_data, array_type.array_of);
}

b32 obj_copy(Interpreter* inter, Object* dst, Object* src)
{
    if (dst->vtype != src->vtype) return false;
    
    ObjectMemory* dst_memory = obj_get_data(dst);
    *dst_memory = obj_copy_data(inter, src);
    
    return true;
}

void obj_set_int(Object* dst, i64 value)
{
    if (dst->vtype != VType_Int) {
        assert(0);
        return;
    }
    dst->integer.value = value;
}

void obj_set_bool(Object* dst, b32 value)
{
    if (dst->vtype != VType_Bool) {
        assert(0);
        return;
    }
    dst->boolean.value = value;
}

void obj_set_string(Interpreter* inter, Object* dst, String value)
{
    // TODO(Jose): FREE MEMORY HERE
    
    dst->string = alloc_string_memory(inter, value);
}


String string_from_obj(Arena* arena, Interpreter* inter, Object* obj, b32 raw)
{
    SCRATCH(arena);
    
    if (is_string(obj)) {
        if (raw) return get_string(obj);
        return string_format(arena, "\"%S\"", get_string(obj));
    }
    if (is_int(obj)) { return string_format(arena, "%l", get_int(obj)); }
    if (is_bool(obj)) { return STR(get_bool(obj) ? "true" : "false"); }
    if (obj->vtype == VType_Void) { return STR("void"); }
    if (obj->vtype == VType_Unknown) { return STR("unknown"); }
    
    VariableType type = vtype_get(inter, obj->vtype);
    
    if (type.kind == VariableKind_Array)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, STR("{ "));
        
        Object* element = obj_alloc_temp(inter, type.array_of);
        foreach(i, obj->array.count) {
            obj_copy_from_element(inter, element, obj, i);
            append(&builder, string_from_obj(scratch.arena, inter, element));
            if (i < obj->array.count - 1) append(&builder, ", ");
        }
        
        append(&builder, STR(" }"));
        
        return string_from_builder(arena, &builder);
    }
    
    if (type.kind == VariableKind_Enum)
    {
        i64 index = obj->enum_.index;
        if (index < 0 || index >= type.enum_names.count) return STR("?");
        return type.enum_names[(u32)index];
    }
    
    assert(0);
    return STR("?");
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

Scope* alloc_scope(Interpreter* inter, ScopeType type)
{
    Scope* scope = arena_push_struct<Scope>(inter->ctx->static_arena);
    scope->objects = pooled_array_make<Object>(inter->ctx->static_arena, 32);
    scope->type = type;
    scope->return_obj = inter->void_obj;
    return scope;
}

Scope* push_scope(Interpreter* inter, ScopeType type, i32 expected_return_vtype)
{
    Scope* scope = NULL;
    
    if (inter->free_scope != NULL) {
        scope = inter->free_scope;
        inter->free_scope = scope->next;
        scope->next = NULL;
        scope->previous = NULL;
        scope->type = type;
    }
    
    if (scope == NULL) scope = alloc_scope(inter, type);
    
    scope->expected_return_vtype = expected_return_vtype;
    
    scope->previous = inter->current_scope;
    inter->current_scope->next = scope->previous;
    inter->current_scope = scope;
    
    return scope;
}

void pop_scope(Interpreter* inter)
{
    if (inter->current_scope == inter->global_scope) {
        report_stack_is_broken();
        return;
    }
    
    Scope* scope = inter->current_scope;
    inter->current_scope = scope->previous;
    inter->current_scope->next = NULL;
    
    scope->next = NULL;
    scope->previous = NULL;
    // TODO(Jose): FREE MEMORY
    array_reset(&scope->objects);
    
    // TODO(Jose): FREE MEMORY
    scope->return_obj = inter->void_obj;
    
    scope->next = inter->free_scope;
    inter->free_scope = scope;
}

Scope* get_returnable_scope(Interpreter* inter)
{
    Scope* scope = inter->current_scope;
    
    while (true) {
        if (scope->type == ScopeType_Function) return scope;
        if (scope->type == ScopeType_Global) return scope;
        scope = scope->previous;
    }
    assert(0);
    return inter->global_scope;
}

Symbol find_symbol(Interpreter* inter, String identifier)
{
    Symbol symbol{};
    symbol.identifier = identifier;
    
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
    
    {
        Object* obj = find_object(inter, identifier, true);
        
        if (obj != NULL) {
            symbol.type = SymbolType_Object;
            symbol.object = obj;
            return symbol;
        }
    }
    
    return {};
}

Object* find_object(Interpreter* inter, String identifier, b32 parent_scopes)
{
    if (identifier.size == 0) return NULL;
    
    Scope* scope = inter->current_scope;
    
    while (true)
    {
        for (auto it = pooled_array_make_iterator(&scope->objects); it.valid; ++it) {
            Object* obj = it.value;
            if (string_equals(obj->identifier, identifier)) {
                return obj;
            }
        }
        
        if (!parent_scopes) break;
        
        if (scope->type == ScopeType_Function) break;
        if (scope->type == ScopeType_Global) break;
        scope = scope->previous;
    }
    
    return NULL;
}

Object* define_object(Interpreter* inter, String identifier, i32 vtype)
{
    assert(find_object(inter, identifier, false) == NULL);
    assert(identifier.size);
    
    Scope* scope = inter->current_scope;
    
    Object* obj = array_add(&scope->objects);
    obj->identifier = identifier;
    obj->vtype = vtype;
    obj->scope = scope;
    
    return obj;
}

FunctionDefinition* find_function(Interpreter* inter, String identifier)
{
    foreach(i, inter->functions.count) {
        FunctionDefinition* fn = &inter->functions[i];
        if (string_equals(fn->identifier, identifier)) return fn;
    }
    return NULL;
}

Object* call_function(Interpreter* inter, FunctionDefinition* fn, Array<Object*> parameters, OpNode* parent_node, b32 is_expresion)
{
    SCRATCH();
    
    CodeLocation code = parent_node->code;
    
    if (parameters.count != fn->parameters.count) {
        report_function_expecting_parameters(code, fn->identifier, fn->parameters.count);
        return inter->nil_obj;
    }
    
    // Ignore errors
    foreach(i, parameters.count) {
        if (parameters[i]->vtype < 0) return inter->nil_obj;
    }
    
    foreach(i, parameters.count) {
        if (parameters[i]->vtype != fn->parameters[i].vtype) {
            report_function_wrong_parameter_type(code, fn->identifier, string_from_vtype(scratch.arena, inter, fn->parameters[i].vtype), i + 1);
            return inter->nil_obj;
        }
    }
    
    String resolved_line;
    
    // Generate resolved line
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        append(&builder, fn->identifier);
        append(&builder, "(");
        foreach(i, parameters.count) {
            append(&builder, string_from_obj(scratch.arena, inter, parameters[i], false));
            if (i != parameters.count - 1) append(&builder, ", ");
        }
        append(&builder, ")");
        resolved_line = string_from_builder(scratch.arena, &builder);
    }
    
    b32 is_intrinsic = fn->intrinsic_fn != NULL;
    
    if (is_intrinsic && !inter->settings.execute) {
        return obj_alloc_temp(inter, fn->return_vtype);
    }
    
    FunctionReturn ret{};
    
    if (is_intrinsic) {
        ret = fn->intrinsic_fn(inter, parameters, code);
    }
    else {
        ret.return_obj = inter->void_obj;
        ret.error_result = RESULT_SUCCESS;
        
        Scope* fn_scope = push_scope(inter, ScopeType_Function, fn->return_vtype);
        
        foreach(i, fn->parameters.count) {
            ParameterDefinition param_def = fn->parameters[i];
            Object* param = define_object(inter, param_def.name, param_def.vtype);
            obj_copy(inter, param, parameters[i]);
        }
        
        interpret_op(inter, parent_node, fn->defined_fn);
        ret = { fn_scope->return_obj, RESULT_SUCCESS };
        
        pop_scope(inter);
    }
    
    if (ret.return_obj->vtype < 0) return inter->nil_obj;
    
    if (ret.return_obj->vtype != fn->return_vtype)
    {
        CodeLocation fn_code = (fn->defined_fn == NULL) ? code : fn->defined_fn->code;
        report_function_no_return(fn_code, fn->identifier);
        return inter->nil_obj;
    }
    
    if (!ret.error_result.success && !is_expresion) {
        interpreter_report_runtime_error(inter, code, resolved_line, ret.error_result.message);
        return ret.return_obj;
    }
    
    if (inter->settings.print_execution) {
        String return_string = string_from_obj(scratch.arena, inter, ret.return_obj);
        String parameters_string = STR("TODO");
        log_trace(inter->ctx, node->code, STR("%S(%S) => %S"), fn->identifier, parameters_string, return_string);
    }
    
    return ret.return_obj;
}

Object* member_from_object(Interpreter* inter, Object* obj, String member)
{
    VariableType obj_type = vtype_get(inter, obj->vtype);
    
    if (obj_type.kind == VariableKind_Array)
    {
        if (string_equals(member, STR("count"))) {
            return obj_alloc_temp_int(inter, obj->array.count);
        }
    }
    
    if (obj_type.kind == VariableKind_Enum)
    {
        if (string_equals(member, STR("index"))) {
            return obj_alloc_temp_int(inter, obj->enum_.index);
        }
        if (string_equals(member, STR("value"))) {
            i64 index = obj->enum_.index;
            if (index < 0 || index >= obj_type.enum_values.count) {
                return obj_alloc_temp_string(inter, STR("?"));
            }
            return obj_alloc_temp_int(inter, obj_type.enum_values[(u32)index]);
        }
        if (string_equals(member, STR("name"))) {
            i64 index = obj->enum_.index;
            if (index < 0 || index >= obj_type.enum_names.count) {
                return obj_alloc_temp_string(inter, STR("?"));
            }
            return obj_alloc_temp_string(inter, obj_type.enum_names[(u32)index]);
        }
    }
    
    return inter->nil_obj;
}

Object* member_from_type(Interpreter* inter, i32 vtype, String member)
{
    SCRATCH();
    VariableType type = vtype_get(inter, vtype);
    
    if (type.kind == VariableKind_Enum)
    {
        if (string_equals(member, STR("name"))) {
            String name = string_from_vtype(scratch.arena, inter, vtype);
            return obj_alloc_temp_string(inter, name);
        }
        if (string_equals(member, STR("count"))) {
            return obj_alloc_temp_int(inter, type.enum_values.count);
        }
        if (string_equals(member, STR("array"))) {
            return obj_alloc_temp_array_from_enum(inter, vtype);
        }
        
        i64 value_index = -1;
        foreach(i, type.enum_names.count) {
            if (string_equals(type.enum_names[i], member)) {
                value_index = i;
                break;
            }
        }
        
        if (value_index < 0) return inter->nil_obj;
        return obj_alloc_temp_enum(inter, vtype, value_index);
    }
    
    return inter->nil_obj;
}

String solve_string_literal(Arena* arena, Interpreter* inter, String src, CodeLocation code)
{
    SCRATCH(arena);
    
    String res = src;
    res = string_replace(scratch.arena, res, STR("\\n"), STR("\n"));
    res = string_replace(scratch.arena, res, STR("\\t"), STR("\t"));
    res = string_replace(scratch.arena, res, STR("\\\""), STR("\""));
    res = string_replace(scratch.arena, res, STR("\\\\"), STR("\\"));
    
    // Variable replacement
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        u64 last_variable_index = 0;
        
        u64 cursor = 0;
        while (cursor < res.size) {
            u32 codepoint = string_get_codepoint(res, &cursor);
            if (codepoint == '{')
            {
                u64 start_identifier = cursor;
                i32 depth = 1;
                
                while (cursor < res.size) {
                    u32 codepoint = string_get_codepoint(res, &cursor);
                    if (codepoint == '{') depth++;
                    else if (codepoint == '}') {
                        depth--;
                        if (depth == 0) break;
                    }
                }
                
                append(&builder, string_substring(res, last_variable_index, start_identifier - last_variable_index - 1));
                
                String identifier = string_substring(res, start_identifier, cursor - start_identifier - 1);
                Object* obj = find_object(inter, identifier, true);
                if (obj == NULL) {
                    report_object_not_found(code, identifier);
                }
                else {
                    append(&builder, string_from_obj(scratch.arena, inter, obj));
                }
                
                last_variable_index = cursor;
            }
        }
        
        append(&builder, string_substring(res, last_variable_index, cursor - last_variable_index));
        
        res = string_from_builder(scratch.arena, &builder);
    }
    
    return string_copy(arena, res);
}

String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path)
{
    SCRATCH(arena);
    
    Object* cd_obj = inter->cd_obj;
    if (!os_path_is_absolute(path)) path = path_resolve(scratch.arena, path_append(scratch.arena, get_string(cd_obj), path));
    return string_copy(arena, path);
}

b32 interpretion_failed(Interpreter* inter)
{
    if (!inter->settings.execute) return false;
    return inter->ctx->error_count > 0;
}

b32 skip_ops(Interpreter* inter)
{
    if (interpretion_failed(inter)) return true;
    Scope* returnable_scope = get_returnable_scope(inter);
    if (inter->settings.execute && returnable_scope->expected_return_vtype > 0 && returnable_scope->return_obj != inter->void_obj) return true;
    return false;
}