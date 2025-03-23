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
            return obj_alloc_temp_bool(inter, get_enum_index(inter, left) == get_enum_index(inter, right));
        }
        else if (op == BinaryOperator_NotEquals) {
            return obj_alloc_temp_bool(inter, get_enum_index(inter, left) != get_enum_index(inter, right));
        }
    }
    
    if (left_type.array_of == right_type.array_of && left_type.kind == VariableKind_Array && right_type.kind == VariableKind_Array)
    {
        i32 element_vtype = left_type.array_of;
        VariableType element_type = vtype_get(inter, element_vtype);
        
        i64 left_count = get_array_count(left);
        i64 right_count = get_array_count(right);
        
        if (op == BinaryOperator_Addition) {
            Object* array = obj_alloc_temp_array(inter, element_vtype, left_count + right_count);
            for (i64 i = 0; i < left_count; ++i) {
                RawBuffer dst = obj_get_element_memory(inter, array, i);
                RawBuffer src = obj_get_element_memory(inter, left, i);
                obj_copy_data(inter, dst, src, element_vtype);
            }
            for (i64 i = 0; i < right_count; ++i) {
                RawBuffer dst = obj_get_element_memory(inter, array, left_count + i);
                RawBuffer src = obj_get_element_memory(inter, right, i);
                obj_copy_data(inter, dst, src, element_vtype);
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
        
        i64 array_src_count = get_array_count(array_src);
        Object* array = obj_alloc_temp_array(inter, element_type.vtype, array_src_count + 1);
        
        i64 array_offset = (left_type.kind == VariableKind_Array) ? 0 : 1;
        
        for (i64 i = 0; i < array_src_count; ++i) {
            RawBuffer dst = obj_get_element_memory(inter, array, i + array_offset);
            RawBuffer src = obj_get_element_memory(inter, array_src, i);
            obj_copy_data(inter, dst, src, element_type.vtype);
        }
        
        i64 element_offset = (left_type.kind == VariableKind_Array) ? array_src_count : 0;
        RawBuffer dst = obj_get_element_memory(inter, array, element_offset);
        obj_copy_data(inter, dst, element->memory, element_type.vtype);
        
        return array;
    }
    
    report_invalid_binary_op(code, string_from_vtype(scratch.arena, inter, left_type.vtype), string_from_binary_operator(op), string_from_vtype(scratch.arena, inter, right_type.vtype));
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
        
        if (!is_valid(result)) return inter->nil_obj;
        
        if (inter->settings.execute && inter->settings.print_execution) {
            String left_string = string_from_obj(scratch.arena, inter, left);
            String right_string = string_from_obj(scratch.arena, inter, right);
            String result_string = string_from_obj(scratch.arena, inter, result);
            log_trace(inter->ctx, node->code, "%S %S %S = %S", left_string, string_from_binary_operator(op), right_string, result_string);
        }
        return result;
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
    if (node->kind == OpKind_Symbol) {
        auto node0 = (OpNode_Symbol*)node;
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
        Array<Object*> objects{};
        
        i32 explicit_vtype = -1;
        
        if (node0->type->kind == OpKind_ObjectType) {
            explicit_vtype = interpret_object_type(inter, node0->type);
            if (explicit_vtype < 0) return inter->nil_obj;
        }
        
        i32 element_vtype = explicit_vtype;
        if (element_vtype < 0) element_vtype = vtype_from_array_element(inter, context.expected_vtype);
        
        if (!node0->is_empty)
        {
            objects = array_make<Object*>(scratch.arena, node0->nodes.count);
            foreach(i, objects.count) {
                objects[i] = interpret_expresion(inter, node0->nodes[i], expresion_context_make(element_vtype));
                if (objects[i] == inter->nil_obj) return inter->nil_obj;
            }
        }
        
        if (element_vtype < 0 && objects.count > 0) element_vtype = objects[0]->vtype;
        
        if (element_vtype <= 0) {
            report_unknown_array_definition(node->code);
            return inter->nil_obj;
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
                Object* dim = interpret_expresion(inter, node0->nodes[i], expresion_context_make(VType_Int));
                if (is_unknown(dim)) return inter->nil_obj;
                
                if (!is_int(dim)) {
                    report_dimensions_expects_an_int(node->code);
                    return inter->nil_obj;
                }
                u32 index = starting_dimensions + i;
                dimensions[index] = get_int(dim);
                if (dimensions[index] < 0) {
                    report_dimensions_must_be_positive(node->code);
                    return inter->nil_obj;
                }
            }
            
            Object* obj = obj_alloc_temp_array_multidimensional(inter, base_vtype, dimensions, true);
            return obj;
        }
        
        if (objects.count == 0) {
            return obj_alloc_temp_array(inter, element_vtype, 0);
        }
        
        // Assert same vtype
        for (i32 i = 1; i < objects.count; ++i) {
            if (objects[i]->vtype != element_vtype) {
                report_type_missmatch_array_expr(node->code, string_from_vtype(scratch.arena, inter, element_vtype), string_from_vtype(scratch.arena, inter, objects[i]->vtype));
                return inter->nil_obj;
            }
        }
        
        Object* array = obj_alloc_temp_array(inter, element_vtype, objects.count);
        foreach(i, objects.count) {
            RawBuffer dst = obj_get_element_memory(inter, array, i);
            obj_copy_data(inter, dst, objects[i]->memory, element_vtype);
        }
        
        return array;
    }
    
    if (node->kind == OpKind_Indexing)
    {
        auto node0 = (OpNode_Indexing*)node;
        Object* value_obj = interpret_expresion(inter, node0->value, context);
        Object* index_obj = interpret_expresion(inter, node0->index, context);
        
        if (value_obj == inter->nil_obj) return inter->nil_obj;
        if (index_obj == inter->nil_obj) return inter->nil_obj;
        
        if (!is_int(index_obj)) {
            report_indexing_expects_an_int(node->code);
            return inter->nil_obj;
        }
        
        VariableType type = vtype_get(inter, value_obj->vtype);
        
        if (type.kind == VariableKind_Array)
        {
            i64 index = get_int(index_obj);
            b32 out_of_bounds = index < 0 || index >= get_array_count(value_obj);
            
            RawBuffer memory{};
            
            if (out_of_bounds) {
                if (inter->settings.execute) {
                    report_indexing_out_of_bounds(node->code);
                    return inter->nil_obj;
                }
            }
            else {
                memory = obj_get_element_memory(inter, value_obj, index);
            }
            
            Object* obj = obj_alloc_temp(inter, type.array_of, memory);
            return obj;
        }
        
        report_indexing_not_allowed(node->code, string_from_vtype(scratch.arena, inter, type.vtype));
        return inter->nil_obj;
    }
    
    report_expr_semantic_unknown(node->code);
    return inter->nil_obj;
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
            
            vtype = vtype_from_array_dimension(inter, vtype, node->type->array_dimensions);
        }
    }
    
    OpNode* assignment = node->assignment;
    Object* assignment_result = interpret_expresion(inter, assignment, expresion_context_make(vtype));
    
    if (is_unknown(assignment_result)) return inter->nil_obj;
    
    if (is_void(assignment_result))
    {
        if (vtype <= 0) {
            report_object_invalid_type(node->code, "void");
            return inter->nil_obj;
        }
        
        assignment_result = obj_alloc_temp(inter, vtype, {});
        interpret_object_initialize(inter, assignment_result->memory, assignment_result->vtype, NULL);
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
    
    if (find_symbol(inter, identifier).type != SymbolType_None) {
        report_symbol_duplicated(node->code, identifier);
        return;
    }
    
    Object* assignment_result = interpret_assignment_for_object_definition(inter, node);
    if (is_unknown(assignment_result)) return;
    if (is_void(assignment_result)) return;
    
    Object* obj = define_object(inter, identifier, assignment_result->vtype);
    
    if (inter->settings.execute)
    {
        if (inter->settings.print_execution) log_trace(inter->ctx, node->code, STR("%S %S"), string_from_vtype(scratch.arena, inter, obj->vtype), obj->identifier);
        
        if (is_valid(assignment_result)) {
            if (!obj_copy(inter, obj, assignment_result)) return;
            if (inter->settings.print_execution) log_trace(inter->ctx, assignment->code, STR("%S = %S"), obj->identifier, string_from_obj(scratch.arena, inter, obj));
        }
    }
}

void interpret_assignment(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_Assignment*)node0;
    
    Object* obj = interpret_expresion(inter, node->destination, expresion_context_make(-1));
    if (obj == inter->nil_obj) return;
    
    // TODO(Jose): Check if the object is temporal
    
    OpNode* assignment = node->source;
    BinaryOperator op = node->binary_operator;
    
    Object* assignment_obj = interpret_expresion(inter, assignment, expresion_context_make(obj->vtype));
    
    if (is_unknown(assignment_obj)) return;
    if (is_void(assignment_obj)) {
        assert(0);
        return;
    }
    
    if (op != BinaryOperator_None) {
        assignment_obj = solve_binary_operation(inter, obj, assignment_obj, op, assignment->code);
        if (is_unknown(assignment_obj)) return;
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
    
    if (node->expresion->kind == OpKind_Symbol)
    {
        OpNode_Symbol* expresion_node = (OpNode_Symbol*)node->expresion;
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
    
    i64 count = get_array_count(array);
    for (i64 i = 0; i < count; ++i)
    {
        if (inter->settings.execute)
        {
            obj_copy_data(inter, element->memory, obj_get_element_memory(inter, array, i), element->vtype);
            
            if (index != NULL) {
                set_int(index, i);
            }
            
            if (inter->settings.print_execution) log_trace(inter->ctx, node->code, STR("for each[%l] (%S)"), i, string_from_obj(scratch.arena, inter, element));
            
            push_scope(inter, ScopeType_Block, 0);
            interpret_op(inter, node, node->content);
            pop_scope(inter);
            
            obj_copy_data(inter, obj_get_element_memory(inter, array, i), element->memory, element->vtype);
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
    
    Symbol symbol = find_symbol(inter, node->identifier);
    
    if (symbol.type == SymbolType_Function)
    {
        FunctionDefinition* fn = symbol.function;
        
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
    
    if (symbol.type == SymbolType_Type)
    {
        VariableType type = vtype_get(inter, symbol.vtype);
        
        Object* obj = obj_alloc_temp(inter, symbol.vtype, {});
        interpret_object_initialize(inter, obj->memory, obj->vtype, NULL);
        return obj;
    }
    
    if (symbol.type == SymbolType_None) {
        report_symbol_not_found(node->code, node->identifier);
    }
    else {
        report_symbol_not_invokable(node->code, node->identifier);
    }
    return inter->nil_obj;
}

i32 interpret_object_type(Interpreter* inter, OpNode* node0)
{
    OpNode_ObjectType* node = (OpNode_ObjectType*)node0;
    i32 vtype = vtype_from_name(inter, node->name);
    vtype = vtype_from_array_dimension(inter, vtype, node->array_dimensions);
    if (vtype < 0) report_object_type_not_found(node->code, node->name);
    return vtype;
}

void interpret_object_initialize(Interpreter* inter, RawBuffer buffer, i32 vtype, OpNode* expresion)
{
    SCRATCH();
    
    // TODO(Jose): FREE MEMORY?
    memory_zero(buffer.data, buffer.size);
    
    if (expresion == NULL || expresion->kind == OpKind_None) 
    {
        VariableType type = vtype_get(inter, vtype);
        
        if (type.kind == VariableKind_Struct)
        {
            u8* data = (u8*)buffer.data;
            
            foreach(i, type.struct_vtypes.count)
            {
                i32 member_vtype = type.struct_vtypes[i];
                u32 member_stride = type.struct_strides[i];
                OpNode* member_initialize_expresion = type.struct_initialize_expresions[i];
                u32 member_size = vtype_get_size(inter, member_vtype);
                
                RawBuffer member_buffer = { data + member_stride, member_size };
                interpret_object_initialize(inter, member_buffer, member_vtype, member_initialize_expresion);
            }
        }
        else if (type.kind == VariableKind_Array)
        {
            ObjectMemory_Array* array = (ObjectMemory_Array*)buffer.data;
            
            i32 element_vtype = type.array_of;
            u32 element_size = vtype_get_size(inter, element_vtype);
            
            u8* data = (u8*)array->data;
            
            foreach(i, array->count)
            {
                u32 element_stride = element_size * i;
                RawBuffer element_buffer = { data + element_stride, element_size };
                interpret_object_initialize(inter, element_buffer, element_vtype, NULL);
            }
        }
    }
    else
    {
        Object* obj = interpret_expresion(inter, expresion, expresion_context_make(vtype));
        if (obj == inter->nil_obj) return;
        if (obj == inter->void_obj) return;
        
        if (obj->vtype != vtype) {
            report_type_missmatch_assign(expresion->code, string_from_vtype(scratch.arena, inter, obj->vtype), string_from_vtype(scratch.arena, inter, vtype));
            return;
        }
        
        assert(buffer.size == obj->memory.size);
        memory_copy(buffer.data, obj->memory.data, buffer.size);
    }
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
    else if (node->kind == OpKind_Assignment) interpret_assignment(inter, node);
    else if (node->kind == OpKind_ObjectDefinition) interpret_object_definition(inter, node);
    else if (node->kind == OpKind_IfStatement) interpret_if_statement(inter, node);
    else if (node->kind == OpKind_WhileStatement) interpret_while_statement(inter, node);
    else if (node->kind == OpKind_ForStatement) interpret_for_statement(inter, node);
    else if (node->kind == OpKind_ForeachArrayStatement) interpret_foreach_array_statement(inter, node);
    else if (node->kind == OpKind_FunctionCall) interpret_function_call(inter, node, false);
    else if (node->kind == OpKind_Return) interpret_return(inter, node);
    else if (node->kind == OpKind_Error) {}
    else if (node->kind == OpKind_EnumDefinition || node->kind == OpKind_StructDefinition || node->kind == OpKind_FunctionDefinition) {
        if (parent != inter->root) {
            report_nested_definition(node->code);
        }
    }
    else if (node->kind == OpKind_Import) {} // Ignore
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
            
            Object* obj = interpret_expresion(inter, value_node, expresion_context_make(VType_Int));
            
            if (obj->vtype < 0) continue;
            if (obj->vtype != VType_Int) {
                report_enum_value_expects_an_int(node->code);
                valid = false;
                continue;
            }
            
            values[i] = get_int(obj);
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
            
            Array<i32> vtypes = array_make<i32>(inter->ctx->static_arena, node->members.count);
            Array<String> names = array_make<String>(inter->ctx->static_arena, node->members.count);
            Array<u32> strides = array_make<u32>(inter->ctx->static_arena, node->members.count);
            Array<OpNode*> initialize_expresions = array_make<OpNode*>(inter->ctx->static_arena, node->members.count);
            
            u32 stride = 0;
            
            foreach(i, vtypes.count) {
                OpNode_ObjectDefinition* member = node->members[i];
                names[i] = member->object_name;
                
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
                
                vtypes[i] = interpret_object_type(inter, member->type);
                if (vtypes[i] < 0) {
                    valid = false;
                    continue;
                }
                
                initialize_expresions[i] = member->assignment;
                
                strides[i] = stride;
                stride += vtype_get_size(inter, vtypes[i]);
            }
            
            if (!valid) continue;
            
            VariableType t = {};
            t.name = name;
            t.kind = VariableKind_Struct;
            t.vtype = inter->vtype_table.count;
            t.struct_names = names;
            t.struct_vtypes = vtypes;
            t.struct_strides = strides;
            t.struct_stride = stride;
            t.struct_initialize_expresions = initialize_expresions;
            array_add(&inter->vtype_table, t);
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
            return_vtype = interpret_object_type(inter, node->return_node);
            if (return_vtype < 0) valid = false;
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
    for (auto it = pooled_array_make_iterator(&inter->ctx->scripts); it.valid; ++it)
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
    if (!inter->settings.execute)
    {
        for (i32 vtype = VType_Void + 1; vtype < inter->vtype_table.count; vtype++)
        {
            RawBuffer memory = obj_memory_alloc_empty(inter, vtype);
            interpret_object_initialize(inter, memory, vtype, NULL);
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
    set_string(inter, obj, inter->ctx->scripts[0].dir);
    
    obj = define_object(inter, STR("context_caller_dir"), VType_String);
    set_string(inter, obj, inter->ctx->caller_dir);
    
    inter->cd_obj = define_object(inter, STR("cd"), VType_String);
    set_string(inter, inter->cd_obj, inter->ctx->scripts[0].dir);
    
    obj = define_object(inter, STR("yov_major"), VType_Int);
    set_int(obj, YOV_MAJOR_VERSION);
    obj = define_object(inter, STR("yov_minor"), VType_Int);
    set_int(obj, YOV_MINOR_VERSION);
    obj = define_object(inter, STR("yov_revision"), VType_Int);
    set_int(obj, YOV_REVISION_VERSION);
    obj = define_object(inter, STR("yov_version"), VType_String);
    set_string(inter, obj, YOV_VERSION);
    
    // Args
    {
        Array<ProgramArg> args = inter->ctx->args;
        Object* array = obj_alloc_temp_array(inter, VType_String, args.count);
        foreach(i, args.count) {
            Object* element = obj_alloc_temp_string(inter, args[i].name);
            obj_copy_data(inter, obj_get_element_memory(inter, array, i), element->memory, VType_String);
        }
        
        obj = define_object(inter, STR("context_args"), VType_StringArray);
        obj_copy(inter, obj, array);
    }
}

void interpret(Yov* ctx, InterpreterSettings settings)
{
    YovScript* main_script = yov_get_script(ctx, 0);
    
    Interpreter* inter = arena_push_struct<Interpreter>(ctx->static_arena);
    inter->ctx = ctx;
    inter->settings = settings;
    inter->root = main_script->ast;
    
    inter->void_obj = arena_push_struct<Object>(inter->ctx->static_arena);
    inter->void_obj->vtype = VType_Void;
    
    inter->nil_obj = arena_push_struct<Object>(inter->ctx->static_arena);
    inter->nil_obj->vtype = VType_Unknown;
    
    inter->global_scope = alloc_scope(inter, ScopeType_Global);
    inter->current_scope = inter->global_scope;
    
    register_definitions(inter);
    define_globals(inter);
    
    interpret_block(inter, inter->root);
    
    assert(inter->ctx->error_count != 0 || inter->global_scope == inter->current_scope);
}

void interpreter_exit(Interpreter* inter)
{
    inter->ctx->error_count++;// TODO(Jose): Weird
}

void interpreter_report_runtime_error(Interpreter* inter, CodeLocation code, String resolved_line, String message_error)
{
    String script_path = yov_get_script(inter->ctx, code.script_id)->path;
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
    if (vtype == VType_Void) return 0;
    if (vtype == VType_Int) return sizeof(ObjectMemory_Int);
    if (vtype == VType_Bool) return sizeof(ObjectMemory_Bool);
    if (vtype == VType_String) return sizeof(ObjectMemory_String);
    
    u32 dims;
    decode_vtype(vtype, NULL, &dims);
    if (dims > 0) return sizeof(ObjectMemory_Array);
    
    VariableType type = vtype_get(inter, vtype);
    if (type.kind == VariableKind_Enum) return sizeof(ObjectMemory_Enum);
    if (type.kind == VariableKind_Struct) return type.struct_stride;
    
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

RawBuffer obj_memory_alloc_empty(Interpreter* inter, i32 vtype)
{
    u32 size = vtype_get_size(inter, vtype);
    RawBuffer buffer;
    buffer.data = arena_push(inter->ctx->static_arena, size);
    buffer.size = size;
    return buffer;
}

void* obj_memory_dynamic_alloc(Interpreter* inter, u64 size) {
    return arena_push(inter->ctx->static_arena, size);
}

Object* obj_alloc_temp(Interpreter* inter, i32 vtype, RawBuffer memory)
{
    if (vtype < 0) return inter->nil_obj;
    if (vtype == VType_Void) return inter->void_obj;
    
    Object* obj = arena_push_struct<Object>(inter->ctx->static_arena);
    obj->identifier = STR("TEMP");
    obj->vtype = vtype;
    obj->scope = inter->current_scope;
    obj->memory = memory;
    if (obj->memory.size == 0) obj->memory = obj_memory_alloc_empty(inter, vtype);
    return obj;
}

Object* obj_alloc_temp_int(Interpreter* inter, i64 value)
{
    Object* obj = obj_alloc_temp(inter, VType_Int, {});
    set_int(obj, value);
    return obj;
}

Object* obj_alloc_temp_bool(Interpreter* inter, b32 value)
{
    Object* obj = obj_alloc_temp(inter, VType_Bool, {});
    set_bool(obj, value);
    return obj;
}

Object* obj_alloc_temp_string(Interpreter* inter, String value)
{
    Object* obj = obj_alloc_temp(inter, VType_String, {});
    set_string(inter, obj, value);
    return obj;
}

Object* obj_alloc_temp_array(Interpreter* inter, i32 element_vtype, i64 count)
{
    i32 vtype = vtype_from_array_dimension(inter, element_vtype, 1);
    Object* obj = obj_alloc_temp(inter, vtype, {});
    ObjectMemory_Array* array = (ObjectMemory_Array*)obj->memory.data;
    
    u32 stride = vtype_get_size(inter, element_vtype);
    resize_array(inter, array, count, stride);
    return obj;
}

ObjectMemory_Array alloc_multidimensional_array_memory(Interpreter* inter, i32 element_vtype, Array<i64> dimensions, b32 initialize_elements)
{
    i32 vtype = vtype_from_array_dimension(inter, element_vtype, dimensions.count);
    i32 array_of = vtype_from_array_element(inter, vtype);
    
    i64 count = dimensions[dimensions.count - 1];
    u32 stride = vtype_get_size(inter, array_of);
    ObjectMemory_Array mem{};
    resize_array(inter, &mem, count, stride);
    
    if (initialize_elements) {
        u8* data = (u8*)mem.data;
        foreach(i, count) {
            RawBuffer element = { data + i * stride, stride };
            interpret_object_initialize(inter, element, array_of, NULL);
        }
    }
    
    if (dimensions.count > 1)
    {
        ObjectMemory_Array* elements = (ObjectMemory_Array*)mem.data;
        for (i64 i = 0; i < count; ++i) {
            ObjectMemory_Array* element = elements + i;
            *element = alloc_multidimensional_array_memory(inter, element_vtype, array_subarray(dimensions, 0, dimensions.count - 1), initialize_elements);
        }
    }
    
    return mem;
}


Object* obj_alloc_temp_array_multidimensional(Interpreter* inter, i32 element_vtype, Array<i64> dimensions, b32 initialize_elements)
{
    i32 vtype = vtype_from_array_dimension(inter, element_vtype, dimensions.count);
    Object* obj = obj_alloc_temp(inter, vtype, {});
    ObjectMemory_Array* array = (ObjectMemory_Array*)obj->memory.data;
    
    i32 array_of = vtype_from_array_element(inter, vtype);
    *array = alloc_multidimensional_array_memory(inter, element_vtype, dimensions, initialize_elements);
    return obj;
}

Object* obj_alloc_temp_enum(Interpreter* inter, i32 vtype, i64 index)
{
    Object* obj = obj_alloc_temp(inter, vtype, {});
    set_enum_index(inter, obj, index);
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
    ObjectMemory_Enum* data = (ObjectMemory_Enum*)get_array_data(obj);
    foreach(i, enum_type.enum_values.count) {
        data[i].index = i;
    }
    return obj;
}

RawBuffer obj_copy_data(Interpreter* inter, Object* obj) {
    RawBuffer dst = obj_memory_alloc_empty(inter, obj->vtype);
    obj_copy_data(inter, dst, obj->memory, obj->vtype);
    return dst;
}

void obj_copy_data(Interpreter* inter, RawBuffer dst, RawBuffer src, i32 vtype)
{
    assert(vtype_get_size(inter, vtype) == src.size);
    assert(dst.size == src.size);
    
    // TODO(Jose): FREE DYNAMIC MEMORY FROM dst
    memory_copy(dst.data, src.data, src.size);
    
    VariableType type = vtype_get(inter, vtype);
    
    if (vtype == VType_String)
    {
        ObjectMemory_String* src_mem = (ObjectMemory_String*)src.data;
        ObjectMemory_String* dst_mem = (ObjectMemory_String*)dst.data;
        
        dst_mem->data = (char*)obj_memory_dynamic_alloc(inter, dst_mem->size);
        memory_copy(dst_mem->data, src_mem->data, dst_mem->size);
    }
    else if (type.kind == VariableKind_Struct)
    {
        u8* src_mem = (u8*)src.data;
        u8* dst_mem = (u8*)dst.data;
        
        foreach(i, type.struct_strides.count)
        {
            u32 member_stride = type.struct_strides[i];
            i32 member_vtype = type.struct_vtypes[i];
            VariableType member_type = vtype_get(inter, member_vtype);
            u32 member_size = vtype_get_size(inter, member_vtype);
            
            RawBuffer src_member = { src_mem + member_stride, (u64)member_size };
            RawBuffer dst_member = { dst_mem + member_stride, (u64)member_size };
            obj_copy_data(inter, dst_member, src_member, member_vtype);
        }
    }
    else if (type.kind == VariableKind_Array)
    {
        ObjectMemory_Array* src_mem = (ObjectMemory_Array*)src.data;
        ObjectMemory_Array* dst_mem = (ObjectMemory_Array*)dst.data;
        
        i32 element_vtype = type.array_of;
        u32 element_size = vtype_get_size(inter, element_vtype);
        dst_mem->data = obj_memory_dynamic_alloc(inter, dst_mem->count * element_size);
        
        foreach(i, src_mem->count)
        {
            u64 element_stride = i * element_size;
            
            RawBuffer src_element = { (u8*)src_mem->data + element_stride, (u64)element_size };
            RawBuffer dst_element = { (u8*)dst_mem->data + element_stride, (u64)element_size };
            obj_copy_data(inter, dst_element, src_element, element_vtype);
        }
    }
}

RawBuffer obj_get_element_memory(Interpreter* inter, RawBuffer memory, i32 vtype, i64 index)
{
    VariableType type = vtype_get(inter, vtype);
    
    if (type.kind == VariableKind_Array)
    {
        ObjectMemory_Array* mem = (ObjectMemory_Array*)memory.data;
        
        if (index < 0 || index >= mem->count) {
            assert(0);
            return {};
        }
        
        i32 element_vtype = type.array_of;
        u32 element_size = vtype_get_size(inter, element_vtype);
        u64 element_stride = element_size * index;
        
        RawBuffer element = { (u8*)mem->data + element_stride, (u64)element_size };
        return element;
    }
    
    if (type.kind == VariableKind_Struct)
    {
        if (index < 0 || index >= type.struct_vtypes.count) {
            assert(0);
            return {};
        }
        
        i32 member_vtype = type.struct_vtypes[index];
        u32 member_stride = type.struct_strides[index];
        u32 member_size = vtype_get_size(inter, member_vtype);
        
        RawBuffer member = { (u8*)memory.data + member_stride, (u64)member_size };
        return member;
    }
    
    assert(0);
    return {};
}

RawBuffer obj_get_element_memory(Interpreter* inter, Object* obj, i64 index) {
    return obj_get_element_memory(inter, obj->memory, obj->vtype, index);
}

b32 obj_copy(Interpreter* inter, Object* dst, Object* src)
{
    if (dst->vtype != src->vtype) return false;
    obj_copy_data(inter, dst->memory, src->memory, dst->vtype);
    return true;
}

String string_from_obj(Arena* arena, Interpreter* inter, Object* obj, b32 raw) {
    return string_from_obj_memory(arena, inter, obj->memory, obj->vtype, raw);
}

String string_from_obj_memory(Arena* arena, Interpreter* inter, RawBuffer memory, i32 vtype, b32 raw)
{
    SCRATCH(arena);
    
    if (vtype == VType_String) {
        if (raw) return get_string(memory);
        return string_format(arena, "\"%S\"", get_string(memory));
    }
    if (vtype == VType_Int) { return string_format(arena, "%l", get_int(memory)); }
    if (vtype == VType_Bool) { return STR(get_bool(memory) ? "true" : "false"); }
    if (vtype == VType_Void) { return STR("void"); }
    if (vtype == VType_Unknown) { return STR("unknown"); }
    
    VariableType type = vtype_get(inter, vtype);
    
    if (type.kind == VariableKind_Array)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, STR("{ "));
        
        assert(memory.size == sizeof(ObjectMemory_Array));
        ObjectMemory_Array* array = (ObjectMemory_Array*)memory.data;
        
        foreach(i, array->count) {
            RawBuffer element_memory = obj_get_element_memory(inter, memory, vtype, i);
            append(&builder, string_from_obj_memory(scratch.arena, inter, element_memory, type.array_of, false));
            if (i < array->count - 1) append(&builder, ", ");
        }
        
        append(&builder, STR(" }"));
        
        return string_from_builder(arena, &builder);
    }
    
    if (type.kind == VariableKind_Enum)
    {
        i64 index = get_enum_index(inter, memory);
        if (index < 0 || index >= type.enum_names.count) return STR("?");
        return type.enum_names[(u32)index];
    }
    
    if (type.kind == VariableKind_Struct)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, STR("{ "));
        
        foreach(i, type.struct_vtypes.count)
        {
            i32 member_vtype = type.struct_vtypes[i];
            RawBuffer member_memory = obj_get_element_memory(inter, memory, vtype, i);
            append(&builder, string_from_obj_memory(scratch.arena, inter, member_memory, member_vtype, false));
            if (i < type.struct_vtypes.count - 1) append(&builder, ", ");
        }
        
        append(&builder, STR(" }"));
        
        return string_from_builder(arena, &builder);
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
    obj->memory = obj_memory_alloc_empty(inter, vtype);
    
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
        return obj_alloc_temp(inter, fn->return_vtype, {});
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
            return obj_alloc_temp_int(inter, get_array_count(obj));
        }
    }
    
    if (obj_type.kind == VariableKind_Enum)
    {
        i64 index = get_enum_index(inter, obj);
        if (string_equals(member, STR("index"))) {
            return obj_alloc_temp_int(inter, index);
        }
        if (string_equals(member, STR("value"))) {
            if (index < 0 || index >= obj_type.enum_values.count) {
                return obj_alloc_temp_string(inter, STR("?"));
            }
            return obj_alloc_temp_int(inter, obj_type.enum_values[(u32)index]);
        }
        if (string_equals(member, STR("name"))) {
            if (index < 0 || index >= obj_type.enum_names.count) {
                return obj_alloc_temp_string(inter, STR("?"));
            }
            return obj_alloc_temp_string(inter, obj_type.enum_names[(u32)index]);
        }
    }
    
    if (obj_type.kind == VariableKind_Struct)
    {
        foreach(i, obj_type.struct_names.count)
        {
            if (string_equals(member, obj_type.struct_names[i]))
            {
                RawBuffer member_memory = obj_get_element_memory(inter, obj, i);
                Object* member = obj_alloc_temp(inter, obj_type.struct_vtypes[i], member_memory);
                return member;
            }
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

b32 is_valid(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype > 0;
}
b32 is_unknown(const Object* obj) {
    if (obj == NULL) return true;
    return obj->vtype < 0;
}

b32 is_void(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype == VType_Void;
}
b32 is_int(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype == VType_Int;
}
b32 is_bool(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype == VType_Bool;
}
b32 is_string(const Object* obj) {
    if (obj == NULL) return false;
    return obj->vtype == VType_String;
}
b32 is_array(i32 vtype) {
    u32 dims;
    decode_vtype(vtype, NULL, &dims);
    return dims > 0;
}
b32 is_array(Object* obj) {
    if (obj == NULL) return false;
    return is_array(obj->vtype);
}
b32 is_enum(Interpreter* inter, i32 vtype) {
    return vtype_get(inter, vtype).kind == VariableKind_Enum;
}
b32 is_enum(Interpreter* inter, Object* obj) {
    if (obj == NULL) return false;
    return is_enum(inter, obj->vtype);
}
b32 is_struct(Interpreter* inter, i32 vtype) {
    return vtype_get(inter, vtype).kind == VariableKind_Struct;
}

i64 get_int(Object* obj) {
    assert(is_int(obj));
    return get_int(obj->memory);
}
i64 get_int(RawBuffer memory) {
    if (memory.size != sizeof(ObjectMemory_Int)) {
        assert(0);
        return 0;
    }
    return ((ObjectMemory_Int*)memory.data)->value;
}

b32 get_bool(Object* obj) {
    assert(is_bool(obj));
    return get_bool(obj->memory);
}
b32 get_bool(RawBuffer memory) {
    if (memory.size != sizeof(ObjectMemory_Bool)) {
        assert(0);
        return false;
    }
    return ((ObjectMemory_Bool*)memory.data)->value;
}

i64 get_enum_index(Interpreter* inter, Object* obj) {
    assert(is_enum(inter, obj));
    return get_enum_index(inter, obj->memory);
}
i64 get_enum_index(Interpreter* inter, RawBuffer memory) {
    if (memory.size != sizeof(ObjectMemory_Enum)) {
        assert(0);
        return 0;
    }
    return ((ObjectMemory_Enum*)memory.data)->index;
}

String get_string(Object* obj) {
    assert(is_string(obj));
    return get_string(obj->memory);
}
String get_string(RawBuffer memory) {
    if (memory.size != sizeof(ObjectMemory_String)) {
        assert(0);
        return {};
    }
    ObjectMemory_String* mem = (ObjectMemory_String*)memory.data;
    String str;
    str.size = mem->size;
    str.data = mem->data;
    return str;
}

void set_int(Object* obj, i64 value)
{
    assert(is_int(obj));
    if (obj->memory.size != sizeof(ObjectMemory_Int)) {
        assert(0);
        return;
    }
    ObjectMemory_Int* mem = (ObjectMemory_Int*)obj->memory.data;
    mem->value = value;
}

void set_bool(Object* obj, b32 value)
{
    assert(is_bool(obj));
    if (obj->memory.size != sizeof(ObjectMemory_Bool)) {
        assert(0);
        return;
    }
    ObjectMemory_Bool* mem = (ObjectMemory_Bool*)obj->memory.data;
    mem->value = value;
}
void set_enum_index(Interpreter* inter, Object* obj, i64 value)
{
    assert(is_enum(inter, obj));
    if (obj->memory.size != sizeof(ObjectMemory_Enum)) {
        assert(0);
        return;
    }
    ObjectMemory_Enum* mem = (ObjectMemory_Enum*)obj->memory.data;
    mem->index = value;
}

void set_string(Interpreter* inter, Object* obj, String value)
{
    // TODO(Jose): HANDLE DYNAMIC MEMORY
    
    ObjectMemory_String dst;
    dst.size = value.size;
    dst.data = (char*)obj_memory_dynamic_alloc(inter, value.size);
    memory_copy(dst.data, value.data, value.size);
    
    assert(sizeof(dst) == obj->memory.size);
    memory_copy(obj->memory.data, &dst, obj->memory.size);
}

i64 get_array_count(Object* obj) {
    assert(is_array(obj));
    if (obj->memory.size != sizeof(ObjectMemory_Array)) {
        assert(0);
        return 0;
    }
    ObjectMemory_Array* mem = (ObjectMemory_Array*)obj->memory.data;
    return mem->count;
}

void* get_array_data(Object* obj) {
    assert(is_array(obj));
    if (obj->memory.size != sizeof(ObjectMemory_Array)) {
        assert(0);
        return 0;
    }
    ObjectMemory_Array* mem = (ObjectMemory_Array*)obj->memory.data;
    return mem->data;
}

void resize_array(Interpreter* inter, ObjectMemory_Array* array, i64 count, i32 stride)
{
    // TODO(Jose): HANDLE DYNAMIC MEMORY
    array->count = count;
    array->data = obj_memory_dynamic_alloc(inter, count * stride);
}