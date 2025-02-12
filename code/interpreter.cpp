#include "inc.h"

internal_fn Variable* solve_binary_operation(Interpreter* inter, Variable* left, Variable* right, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    
    VariableType left_type = vtype_get(inter, left->vtype);
    VariableType right_type = vtype_get(inter, right->vtype);
    
    if (is_int(left) && is_int(right)) {
        if (op == BinaryOperator_Addition) return var_alloc_int(inter, get_int(left) + get_int(right));
        if (op == BinaryOperator_Substraction) return var_alloc_int(inter, get_int(left) - get_int(right));
        if (op == BinaryOperator_Multiplication) return var_alloc_int(inter, get_int(left) * get_int(right));
        if (op == BinaryOperator_Division) return var_alloc_int(inter, get_int(left) / get_int(right));
        if (op == BinaryOperator_Equals) return var_alloc_bool(inter, get_int(left) == get_int(right));
        if (op == BinaryOperator_NotEquals) return var_alloc_bool(inter, get_int(left) != get_int(right));
        if (op == BinaryOperator_LessThan) return var_alloc_bool(inter, get_int(left) < get_int(right));
        if (op == BinaryOperator_LessEqualsThan) return var_alloc_bool(inter, get_int(left) <= get_int(right));
        if (op == BinaryOperator_GreaterThan) return var_alloc_bool(inter, get_int(left) > get_int(right));
        if (op == BinaryOperator_GreaterEqualsThan) return var_alloc_bool(inter, get_int(left) >= get_int(right));
    }
    
    if (is_string(left) && is_string(right))
    {
        if (op == BinaryOperator_Addition) {
            String str = string_format(scratch.arena, "%S%S", get_string(left), get_string(right));
            return var_alloc_string(inter, str);
        }
        else if (op == BinaryOperator_Division)
        {
            if (os_path_is_absolute(get_string(right))) {
                report_error(inter->ctx, code, STR("Right path can't be absolute"));
                return left;
            }
            
            String str = path_append(scratch.arena, get_string(left), get_string(right));
            str = path_resolve(scratch.arena, str);
            
            return var_alloc_string(inter, str);
        }
        else if (op == BinaryOperator_Equals) {
            return var_alloc_bool(inter, (b8)string_equals(get_string(left), get_string(right)));
        }
        else if (op == BinaryOperator_NotEquals) {
            return var_alloc_bool(inter, !(b8)string_equals(get_string(left), get_string(right)));
        }
    }
    
    if (left_type.array_of == right_type.array_of && left_type.kind == VariableKind_Array && right_type.kind == VariableKind_Array)
    {
        VariableType array_of = vtype_get(inter, left_type.array_of);
        
        if (op == BinaryOperator_Addition) {
            Variable* array = var_alloc_array(inter, left->vtype, left->count + right->count);
            for (i64 i = 0; i < left->count; ++i) {
                Variable* var = var_alloc_from_array(inter, left, i);
                var_assign_array_element(inter, array, i, var);
            }
            for (i64 i = 0; i < right->count; ++i) {
                Variable* var = var_alloc_from_array(inter, right, i);
                var_assign_array_element(inter, array, left->count + i, var);
            }
            return array;
        }
    }
    
    if ((left_type.kind == VariableKind_Array && right_type.kind != VariableKind_Array) || (left_type.kind != VariableKind_Array && right_type.kind == VariableKind_Array))
    {
        VariableType array_type = (left_type.kind == VariableKind_Array) ? left_type : right_type;
        VariableType element_type = (left_type.kind == VariableKind_Array) ? right_type : left_type;
        
        if (array_type.array_of != element_type.vtype) {
            report_error(inter->ctx, code, "Type missmatch, can't append a '%S' into '%S'", element_type.name, array_type.name);
            return inter->nil_var;
        }
        
        Variable* array_src = (left_type.kind == VariableKind_Array) ? left : right;
        Variable* element = (left_type.kind == VariableKind_Array) ? right : left;
        
        Variable* array = var_alloc_array(inter, left->vtype, array_src->count + 1);
        
        i64 array_offset = (left_type.kind == VariableKind_Array) ? 0 : 1;
        
        for (i64 i = 0; i < array_src->count; ++i) {
            Variable* var = var_alloc_from_array(inter, array_src, i);
            var_assign_array_element(inter, array, i + array_offset, var);
        }
        
        i64 element_offset = (left_type.kind == VariableKind_Array) ? array_src->count : 0;
        var_assign_array_element(inter, array, element_offset, element);
        
        return array;
    }
    
    return inter->nil_var;
}

Variable* interpret_expresion(Interpreter* inter, OpNode* node)
{
    SCRATCH();
    
    if (node->kind == OpKind_None) return inter->void_var;
    
    if (node->kind == OpKind_Binary)
    {
        auto node0 = (OpNode_Binary*)node;
        Variable* left = interpret_expresion(inter, node0->left);
        Variable* right = interpret_expresion(inter, node0->right);
        BinaryOperator op = node0->op;
        
        Variable* result = solve_binary_operation(inter, left, right, op, node->code);
        
        if (is_valid(result))
        {
            if (inter->settings.execute && inter->settings.print_execution) {
                String left_string = string_from_var(scratch.arena, inter, left);
                String right_string = string_from_var(scratch.arena, inter, right);
                String result_string = string_from_var(scratch.arena, inter, result);
                report_info(inter->ctx, node->code, STR("%S %S %S = %S"), left_string, string_from_binary_operator(op), right_string, result_string);
            }
            return result;
        }
        else
        {
            report_error(inter->ctx, node->code, STR("Invalid arithmetic operation '%S %S %S'"), string_from_var(scratch.arena, inter, left), string_from_binary_operator(op), string_from_var(scratch.arena, inter, right));
            return inter->nil_var;
        }
    }
    
    if (node->kind == OpKind_IntLiteral) return var_alloc_int(inter, ((OpNode_Literal*)node)->int_literal);
    if (node->kind == OpKind_StringLiteral) {
        auto node0 = (OpNode_Literal*)node;
        return var_alloc_string(inter, solve_string_literal(scratch.arena, inter, node0->string_literal, node0->code));
    }
    if (node->kind == OpKind_BoolLiteral) return var_alloc_bool(inter, ((OpNode_Literal*)node)->bool_literal);
    if (node->kind == OpKind_IdentifierValue) {
        auto node0 = (OpNode_IdentifierValue*)node;
        Object* obj = find_object(inter, node0->identifier, true);
        
        if (obj == NULL) {
            report_error(inter->ctx, node->code, STR("Identifier '%S' not found"), node0->identifier);
            return inter->nil_var;
        }
        
        return obj->var;
    }
    
    if (node->kind == OpKind_MemberValue) {
        auto node0 = (OpNode_MemberValue*)node;
        String identifier = node0->identifier;
        Object* obj = find_object(inter, identifier, true);
        
        if (obj == NULL) {
            report_error(inter->ctx, node->code, STR("Identifier '%S' not found"), identifier);
            return inter->nil_var;
        }
        
        String member = node0->member;
        
        if (member.size == 0) {
            report_error(inter->ctx, node->code, "Member is not specified");
            return inter->nil_var;
        }
        
        VariableType obj_type = vtype_get(inter, obj->var->vtype);
        
        if (obj_type.kind == VariableKind_Array)
        {
            if (string_equals(member, STR("count"))) {
                return var_alloc_int(inter, obj->var->count);
            }
            else {
                report_error(inter->ctx, node->code, "Unknown member '%S' for array", member);
                return inter->nil_var;
            }
        }
        else {
            report_error(inter->ctx, node->code, "Object '%S' doesn't have members", identifier);
            return inter->nil_var;
        }
        
        return obj->var;
    }
    
    if (node->kind == OpKind_FunctionCall) {
        return interpret_function_call(inter, node);
    }
    
    if (node->kind == OpKind_ArrayExpresion)
    {
        auto node0 = (OpNode_ArrayExpresion*)node;
        Array<Variable*> vars = array_make<Variable*>(scratch.arena, node0->nodes.count);
        
        foreach(i, vars.count) {
            vars[i] = interpret_expresion(inter, node0->nodes[i]);
        }
        
        if (vars.count == 0) {
            return inter->void_var;
        }
        
        i32 vtype = vars[0]->vtype;
        
        // Assert same vtype
        for (i32 i = 1; i < vars.count; ++i) {
            if (vars[i]->vtype != vtype) {
                report_error(inter->ctx, node->code, STR("Type missmatch in array expresion, expecting '%S' but found '%S'"), string_from_vtype(inter, vtype), string_from_vtype(inter, vars[i]->vtype));
                return inter->nil_var;
            }
        }
        
        i32 array_vtype = vtype_from_array_element(inter, vtype);
        
        Variable* array = var_alloc_array(inter, array_vtype, vars.count);
        foreach(i, vars.count) {
            var_assign_array_element(inter, array, i, vars[i]);
        }
        
        return array;
    }
    
    if (node->kind == OpKind_ArrayElementValue)
    {
        auto node0 = (OpNode_ArrayElementValue*)node;
        Variable* indexing_var = interpret_expresion(inter, node0->expresion);
        Object* array = find_object(inter, node0->identifier, true);
        
        if (array == NULL) {
            report_error(inter->ctx, node->code, "Identifier '%S' not found", node0->identifier);
            return inter->nil_var;
        }
        
        if (!is_int(indexing_var)) {
            report_error(inter->ctx, node->code, "Invalid indexing, expecting an Int expresion");
            return inter->nil_var;
        }
        
        VariableType array_vtype = vtype_get(inter, array->var->vtype);
        
        if (array_vtype.kind != VariableKind_Array) {
            report_error(inter->ctx, node->code, "Indexing is only allowed for arrays");
            return inter->nil_var;
        }
        
        if (inter->settings.execute)
        {
            i64 index = get_int(indexing_var);
            if (index < 0 || index >= array->var->count) {
                report_error(inter->ctx, node->code, "Index out of bounds");
                return inter->nil_var;
            }
            
            return var_alloc_from_array(inter, array->var, index);
        }
        else {
            return var_alloc_generic(inter, array_vtype.array_of);
        }
    }
    
    report_error(inter->ctx, node->code, STR("Unknown expersion"));
    return inter->nil_var;
}

void interpret_assignment(Interpreter* inter, Object* obj, OpNode* assignment, BinaryOperator binary_operator, b32 assert_assignment)
{
    SCRATCH();
    
    assert(inter->settings.execute);
    
    b32 no_assignment = assignment->kind != OpKind_None && assignment->kind != OpKind_Unknown;
    
    if (!no_assignment) {
        if (assert_assignment) {
            report_error(inter->ctx, assignment->code, STR("Invalid assignment"));
        }
        return;
    }
    
    Variable* assignment_result = interpret_expresion(inter, assignment);
    
    if (binary_operator != BinaryOperator_None) {
        assignment_result = solve_binary_operation(inter, obj->var, assignment_result, binary_operator, assignment->code);
    }
    
    if (!obj_assign(inter, obj, assignment_result)) {
        report_error(inter->ctx, assignment->code, STR("Type missmatch, can't assign '%S' to '%S'"), string_from_vtype(inter, assignment_result->vtype), string_from_vtype(inter, obj->var->vtype));
        return;
    }
    
    if (inter->settings.print_execution) report_info(inter->ctx, assignment->code, STR("%S = %S"), obj->identifier, string_from_var(scratch.arena, inter, obj->var));
}

void interpret_indexed_assignment(Interpreter* inter, Object* array, i64 index, OpNode* assignment, b32 assert_assignment)
{
    SCRATCH();
    assert(inter->settings.execute);
    
    VariableType array_type = vtype_get(inter, array->var->vtype);
    
    if (array_type.kind != VariableKind_Array) {
        assert(0);
        return;
    }
    
    b32 no_assignment = assignment->kind != OpKind_None && assignment->kind != OpKind_Unknown;
    
    if (!no_assignment) {
        if (assert_assignment) {
            report_error(inter->ctx, assignment->code, STR("Invalid assignment"));
        }
        return;
    }
    
    Variable* assignment_result = interpret_expresion(inter, assignment);
    
    if (!var_assignment_is_valid(assignment_result, array_type.array_of)) {
        report_error(inter->ctx, assignment->code, STR("Type missmatch, can't assign '%S' to '%S'"), string_from_vtype(inter, assignment_result->vtype), string_from_vtype(inter, array->var->vtype));
        return;
    }
    
    var_assign_array_element(inter, array->var, index, assignment_result);
    
    if (inter->settings.print_execution) report_info(inter->ctx, assignment->code, STR("%S[%l] = %S"), array->identifier, index, string_from_var(scratch.arena, inter, array->var));
}

void interpret_variable_definition(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    
    auto node = (OpNode_VariableDefinition*)node0;
    String identifier = node->identifier;
    Object* obj = find_object(inter, identifier, false);
    if (obj != NULL) {
        report_error(inter->ctx, node->code, STR("Duplicated identifier '%S'"), identifier);
        return;
    }
    
    OpNode* assignment = node->assignment;
    Variable* assignment_result = interpret_expresion(inter, assignment);
    
    if (is_unknown(assignment_result)) return;
    
    i32 vtype = -1;
    {
        String type_name = node->type;
        
        if (type_name.size > 0)
        {
            b32 is_array = node->is_array;
            if (is_array) type_name = string_format(scratch.arena, "%S[]", type_name);
            
            vtype = -1;
            foreach(i, inter->vtype_table.count) {
                if (string_equals(inter->vtype_table[i].name, type_name)) {
                    vtype = i;
                }
            }
            
            if (vtype <= 0) {
                report_error(inter->ctx, node->code, STR("Undefined type '%S'"), type_name);
                return;
            }
        }
        else
        {
            if (assignment_result->vtype == VType_Void) {
                report_error(inter->ctx, node->code, "Implicit variable definition expects an assignment");
                return;
            }
            
            vtype = assignment_result->vtype;
        }
    }
    
    Variable* var = var_alloc_generic(inter, vtype);
    obj = define_object(inter, identifier, var);
    
    if (inter->settings.execute)
    {
        if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("%S %S"), string_from_vtype(inter, vtype), obj->identifier);
        
        if (is_valid(assignment_result)) {
            if (!obj_assign(inter, obj, assignment_result)) {
                report_error(inter->ctx, assignment->code, STR("Type missmatch, can't assign '%S' to '%S'"), string_from_vtype(inter, assignment_result->vtype), string_from_vtype(inter, obj->var->vtype));
                return;
            }
            
            if (inter->settings.print_execution) report_info(inter->ctx, assignment->code, STR("%S = %S"), obj->identifier, string_from_var(scratch.arena, inter, obj->var));
        }
    }
}

void interpret_variable_assignment(Interpreter* inter, OpNode* node0)
{
    auto node = (OpNode_Assignment*)node0;
    
    Object* obj = find_object(inter, node->identifier, true);
    if (obj == NULL) {
        report_error(inter->ctx, node->code, STR("Undefined identifier '%S'"), node->identifier);
        return;
    }
    
    if (inter->settings.execute) {
        OpNode* assignment = node->value;
        interpret_assignment(inter, obj, assignment, node->binary_operator, false);
    }
}

void interpret_array_element_assignment(Interpreter* inter, OpNode* node0)
{
    auto node = (OpNode_ArrayElementAssignment*)node0;
    
    Object* array = find_object(inter, node->identifier, true);
    if (array == NULL) {
        report_error(inter->ctx, node->code, STR("Undefined identifier '%S'"), node->identifier);
        return;
    }
    
    Variable* indexing_var = interpret_expresion(inter, node->indexing_expresion);
    
    if (!is_int(indexing_var)) {
        report_error(inter->ctx, node->code, "Invalid indexing, expecting an Int expresion");
        return;
    }
    
    VariableType array_vtype = vtype_get(inter, array->var->vtype);
    
    if (array_vtype.kind != VariableKind_Array) {
        report_error(inter->ctx, node->code, STR("Can't make indexed assignment to a '%S'"), string_from_vtype(inter, array->var->vtype));
        return;
    }
    
    if (inter->settings.execute)
    {
        i64 index = get_int(indexing_var);
        if (index < 0 || index >= array->var->count) {
            report_error(inter->ctx, node->code, "Index out of bounds");
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
    
    Variable* expresion_result = interpret_expresion(inter, node->expresion);
    
    if (!is_bool(expresion_result)) {
        report_error(inter->ctx, node->code, STR("If statement expects a Bool"));
        return;
    }
    
    if (inter->settings.execute) {
        if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("if (%S)"), string_from_var(scratch.arena, inter, expresion_result));
        b32 result = get_bool(expresion_result);
        
        push_scope(inter);
        if (result) interpret_op(inter, node->success);
        else interpret_op(inter, node->failure);
        pop_scope(inter);
    }
    else {
        push_scope(inter);
        interpret_op(inter, node->success);
        pop_scope(inter);
        
        push_scope(inter);
        interpret_op(inter, node->failure);
        pop_scope(inter);
    }
    
}

void interpret_while_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_WhileStatement*)node0;
    
    while (1)
    {
        Variable* expresion_result = interpret_expresion(inter, node->expresion);
        
        if (!is_bool(expresion_result)) {
            report_error(inter->ctx, node->code, STR("While statement expects a Bool"));
            return;
        }
        
        if (inter->settings.execute)
        {
            if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("while (%S)"), string_from_var(scratch.arena, inter, expresion_result));
            
            if (!get_bool(expresion_result)) break;
            
            push_scope(inter);
            interpret_op(inter, node->content);
            pop_scope(inter);
        }
        else
        {
            push_scope(inter);
            interpret_op(inter, node->content);
            pop_scope(inter);
            break;
        }
    }
}

void interpret_for_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_ForStatement*)node0;
    
    push_scope(inter);
    
    interpret_op(inter, node->initialize_sentence);
    
    while (1)
    {
        Variable* expresion_result = interpret_expresion(inter, node->condition_expresion);
        
        if (!is_bool(expresion_result)) {
            report_error(inter->ctx, node->code, STR("For-statement expects a Bool"));
            return;
        }
        
        if (inter->settings.execute)
        {
            if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("for (%S)"), string_from_var(scratch.arena, inter, expresion_result));
            
            if (!get_bool(expresion_result)) break;
            
            push_scope(inter);
            interpret_op(inter, node->content);
            pop_scope(inter);
            
            interpret_op(inter, node->update_sentence);
        }
        else
        {
            push_scope(inter);
            interpret_op(inter, node->content);
            pop_scope(inter);
            interpret_op(inter, node->update_sentence);
            break;
        }
    }
    
    pop_scope(inter);
}

void interpret_foreach_array_statement(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_ForeachArrayStatement*)node0;
    
    push_scope(inter);
    
    Variable* array = interpret_expresion(inter, node->expresion);
    if (is_unknown(array)) return;
    
    VariableType array_type = vtype_get(inter, array->vtype);
    
    if (array_type.kind != VariableKind_Array) {
        report_error(inter->ctx, node->code, "For each statement expects an array");
        return;
    }
    
    Object* element = define_object(inter, node->element_name, var_alloc_generic(inter, array_type.array_of));
    
    Object* index = NULL;
    if (node->index_name.size > 0) index = define_object(inter, node->index_name, var_alloc_int(inter, 0));
    
    for (i64 i = 0; i < array->count; ++i)
    {
        if (inter->settings.execute)
        {
            Variable* value = var_alloc_from_array(inter, array, i);
            obj_assign(inter, element, value);
            
            if (index != NULL) {
                obj_assign(inter, index, var_alloc_int(inter, i));
            }
            
            if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("for each[%l] (%S)"), i, string_from_var(scratch.arena, inter, value));
            
            push_scope(inter);
            interpret_op(inter, node->content);
            pop_scope(inter);
        }
        else
        {
            push_scope(inter);
            interpret_op(inter, node->content);
            pop_scope(inter);
            
            break;
        }
    }
    
    pop_scope(inter);
}

Variable* interpret_function_call(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_FunctionCall*)node0;
    
    FunctionDefinition* fn = find_function(inter, node->identifier);
    
    if (fn == NULL) {
        report_error(inter->ctx, node->code, STR("Undefined function '%S'"), node->identifier);
        return inter->nil_var;
    }
    
    if (fn->parameter_vtypes.count != node->parameters.count)
    {
        report_error(inter->ctx, node->code, STR("Function '%S' is expecting %u parameters"), node->identifier, fn->parameter_vtypes.count);
        return inter->nil_var;
    }
    
    Array<Variable*> vars = array_make<Variable*>(scratch.arena, fn->parameter_vtypes.count);
    foreach(i, vars.count) {
        vars[i] = interpret_expresion(inter, node->parameters[i]);
        
        if (!var_assignment_is_valid(vars[i], fn->parameter_vtypes[i])) {
            report_error(inter->ctx, node->code, STR("Function '%S' is expecting '%S' as a parameter %u"), node->identifier, string_from_vtype(inter, fn->parameter_vtypes[i]), i + 1);
            return inter->nil_var;
        }
    }
    
    if (!inter->settings.execute) {
        return var_alloc_generic(inter, fn->return_vtype);
    }
    
    Variable* return_var = fn->intrinsic_fn(inter, node, vars);
    
    if (inter->settings.print_execution) {
        String return_string = string_from_var(scratch.arena, inter, return_var);
        String parameters = STR("TODO");
        report_info(inter->ctx, node->code, STR("%S(%S) => %S"), node->identifier, parameters, return_string);
    }
    
    return return_var;
}

internal_fn void interpret_block(Interpreter* inter, OpNode* block0)
{
    auto block = (OpNode_Block*)block0;
    assert(block->kind == OpKind_Block);
    
    push_scope(inter);
    
    Array<OpNode*> ops = block->ops;
    
    i32 op_index = 0;
    while (op_index < ops.count)
    {
        if (interpretion_failed(inter)) break;
        
        i32 next_op_index = op_index + 1;
        
        OpNode* node = ops[op_index];
        interpret_op(inter, node);
        op_index = next_op_index;
    }
    
    pop_scope(inter);
}

void interpret_op(Interpreter* inter, OpNode* node)
{
    if (interpretion_failed(inter)) return;
    if (node->kind == OpKind_None) return;
    
    if (node->kind == OpKind_Block) interpret_block(inter, node);
    else if (node->kind == OpKind_VariableDefinition) interpret_variable_definition(inter, node);
    else if (node->kind == OpKind_VariableAssignment) interpret_variable_assignment(inter, node);
    else if (node->kind == OpKind_ArrayElementAssignment) interpret_array_element_assignment(inter, node);
    else if (node->kind == OpKind_IfStatement) interpret_if_statement(inter, node);
    else if (node->kind == OpKind_WhileStatement) interpret_while_statement(inter, node);
    else if (node->kind == OpKind_ForStatement) interpret_for_statement(inter, node);
    else if (node->kind == OpKind_ForeachArrayStatement) interpret_foreach_array_statement(inter, node);
    else if (node->kind == OpKind_FunctionCall) interpret_function_call(inter, node);
    else {
        report_error(inter->ctx, node->code, STR("Unknown operation: {line}"));
    }
}

internal_fn void define_vtype_table(Interpreter* inter)
{
    SCRATCH();
    
    PooledArray<VariableType> list = pooled_array_make<VariableType>(scratch.arena, 32);
    
    VariableType t;
    
    // Void
    {
        t = {};
        t.name = STR("void");
        t.kind = VariableKind_Void;
        t.size = 0;
        t.vtype = list.count;
        assert(VType_Void == t.vtype);
        array_add(&list, t);
    }
    
    // Int
    {
        t = {};
        t.name = STR("Int");
        t.kind = VariableKind_Primitive;
        t.size = sizeof(i64);
        t.vtype = list.count;
        assert(VType_Int == t.vtype);
        array_add(&list, t);
        
        t.name = STR("Int[]");
        t.kind = VariableKind_Array;
        t.array_of = VType_Int;
        t.vtype = list.count;
        array_add(&list, t);
    }
    
    // Bool
    {
        t = {};
        t.name = STR("Bool");
        t.kind = VariableKind_Primitive;
        t.size = sizeof(b8);
        t.vtype = list.count;
        assert(VType_Bool == t.vtype);
        array_add(&list, t);
        
        t.name = STR("Bool[]");
        t.kind = VariableKind_Array;
        t.array_of = VType_Bool;
        t.vtype = list.count;
        array_add(&list, t);
    }
    
    // String
    {
        t = {};
        t.name = STR("String");
        t.kind = VariableKind_Primitive;
        t.size = sizeof(String);
        t.vtype = list.count;
        assert(VType_String == t.vtype);
        array_add(&list, t);
        
        t.name = STR("String[]");
        t.kind = VariableKind_Array;
        t.array_of = VType_String;
        t.vtype = list.count;
        array_add(&list, t);
    }
    
    inter->vtype_table = array_from_pooled_array(inter->ctx->static_arena, list);
}

internal_fn void define_globals(Interpreter* inter)
{
    define_object(inter, STR("context_script_dir"), var_alloc_string(inter, inter->ctx->script_dir));
    define_object(inter, STR("context_caller_dir"), var_alloc_string(inter, inter->ctx->caller_dir));
    inter->cd_obj = define_object(inter, STR("cd"), var_alloc_string(inter, inter->ctx->script_dir));
    
    // Args
    {
        Array<ProgramArg> args = inter->ctx->args;
        Variable* array = var_alloc_array(inter, VType_StringArray, args.count);
        foreach(i, args.count) {
            Variable* var = var_alloc_string(inter, args[i].name);
            var_assign_array_element(inter, array, i, var);
        }
        
        Object* obj = define_object(inter, STR("context_args"), array);
    }
    
    inter->void_var = arena_push_struct<Variable>(inter->ctx->static_arena);
    inter->void_var->vtype = VType_Void;
    
    inter->nil_var = arena_push_struct<Variable>(inter->ctx->static_arena);
    inter->nil_var->vtype = VType_Unknown;
}

Array<FunctionDefinition> get_intrinsic_functions(Arena* arena, Interpreter* inter);

internal_fn void define_functions(Interpreter* inter) {
    inter->functions = get_intrinsic_functions(inter->ctx->static_arena, inter);
}

void interpret(Yov* ctx, OpNode* block, InterpreterSettings settings)
{
    Interpreter* inter = arena_push_struct<Interpreter>(ctx->static_arena);
    inter->ctx = ctx;
    inter->settings = settings;
    
    inter->objects = pooled_array_make<Object>(ctx->static_arena, 32);
    
    define_vtype_table(inter);
    define_globals(inter);
    define_functions(inter);
    
    interpret_block(inter, block);
    
    assert(inter->ctx->error_count != 0 || inter->scope == 0);
}

VariableType vtype_get(Interpreter* inter, i32 vtype)
{
    if (vtype < 0 || vtype >= inter->vtype_table.count) {
        VariableType t{};
        t.name = STR("Unknown");
        t.kind = VariableKind_Unknown;
        return t;
    }
    return inter->vtype_table[vtype];
}

i32 vtype_from_array_element(Interpreter* inter, i32 vtype)
{
    foreach(i, inter->vtype_table.count) {
        if (inter->vtype_table[i].array_of == vtype) return i;
    }
    return -1;
}

Variable* var_alloc_generic(Interpreter* inter, i32 vtype)
{
    VariableType type = vtype_get(inter, vtype);
    if (type.kind == VariableKind_Primitive) return var_alloc_primitive(inter, vtype);
    if (type.kind == VariableKind_Array) return var_alloc_array(inter, vtype, 0);
    return inter->nil_var;
}

Variable* var_alloc_primitive(Interpreter* inter, i32 vtype)
{
    VariableType type = vtype_get(inter, vtype);
    if (type.kind != VariableKind_Primitive) {
        assert(0);
        return inter->nil_var;
    }
    
    Variable* var = arena_push_struct<Variable>(inter->ctx->static_arena);
    var->vtype = vtype;
    var->data = arena_push(inter->ctx->static_arena, type.size);
    
    return var;
}

Variable* var_alloc_array(Interpreter* inter, i32 vtype, u32 length)
{
    VariableType type = vtype_get(inter, vtype);
    if (type.kind != VariableKind_Array) {
        assert(0);
        return inter->nil_var;
    }
    
    Variable* var = arena_push_struct<Variable>(inter->ctx->static_arena);
    var->vtype = vtype;
    var->count = length;
    var->data = arena_push(inter->ctx->static_arena, type.size * var->count);
    
    return var;
}

Variable* var_copy(Interpreter* inter, const Variable* src)
{
    // TODO(Jose): // TODO(Jose): // TODO(Jose): 
    // TODO(Jose): Variable* dst = var_alloc(inter, src->vtype);
    // TODO(Jose): VariableType type = vtype_get(inter, src->vtype);
    // TODO(Jose): memory_copy(dst->data, src->data, type.size);
    // TODO(Jose): Copy String and Array external memory
    // TODO(Jose): return dst;
    return (Variable*)src;// TODO(Jose): 
}

Variable* var_alloc_int(Interpreter* inter, i64 v) {
    Variable* var = var_alloc_primitive(inter, VType_Int);
    get_int(var) = v;
    return var;
}
Variable* var_alloc_bool(Interpreter* inter, b8 v) {
    Variable* var = var_alloc_primitive(inter, VType_Bool);
    get_bool(var) = v;
    return var;
}
Variable* var_alloc_string(Interpreter* inter, String v) {
    // TODO(Jose): Care about memory used by strings!!
    Variable* var = var_alloc_primitive(inter, VType_String);
    get_string(var) = string_copy(inter->ctx->static_arena, v);
    return var;
}

Variable* var_alloc_from_array(Interpreter* inter, Variable* array, i64 index)
{
    if (index < 0 || index >= array->count) {
        assert(0);
        return inter->nil_var;
    }
    
    VariableType array_type = vtype_get(inter, array->vtype);
    if (array_type.kind != VariableKind_Array) {
        assert(0);
        return inter->nil_var;
    }
    
    VariableType type = vtype_get(inter, array_type.array_of);
    
    if (type.kind == VariableKind_Primitive)
    {
        Variable* var = var_alloc_primitive(inter, array_type.array_of);
        
        // TODO(Jose): Safer
        // TODO(Jose): Care about memory used by strings!!
        u64 offset = array_type.size * index;
        memory_copy(var->data, (u8*)array->data + offset, array_type.size);
        return var;
    }
    
    assert(0);
    return inter->nil_var;
}

void var_assign_array_element(Interpreter* inter, Variable* array, i64 index, Variable* src)
{
    if (index < 0 || index >= array->count) {
        assert(0);
        return;
    }
    
    VariableType array_type = vtype_get(inter, array->vtype);
    if (array_type.kind != VariableKind_Array) {
        assert(0);
        return;
    }
    
    if (src->vtype != array_type.array_of) {
        assert(0);
        return;
    }
    
    VariableType type = vtype_get(inter, array_type.array_of);
    
    if (type.kind == VariableKind_Primitive) {
        void* dst = (u8*)array->data + index * type.size;
        memory_copy(dst, src->data, type.size);
    }
    else {
        assert(0);
        return;
    }
}

b32 var_assignment_is_valid(const Variable* t0, const Variable* t1)
{
    if (!is_valid(t0)) return false;
    if (!is_valid(t1)) return false;
    return var_assignment_is_valid(t0, t1->vtype);
}

b32 var_assignment_is_valid(const Variable* t0, i32 vtype)
{
    return t0->vtype == vtype;
}

String string_from_var(Arena* arena, Interpreter* inter, Variable* var)
{
    SCRATCH(arena);
    
    if (is_string(var)) return get_string(var);
    if (is_int(var)) { return string_format(arena, "%l", get_int(var)); }
    if (is_bool(var)) { return STR(get_bool(var) ? "true" : "false"); }
    if (var->vtype == VType_Void) { return STR("void"); }
    if (var->vtype == VType_Unknown) { return STR("unknown"); }
    
    VariableType type = vtype_get(inter, var->vtype);
    
    if (type.kind == VariableKind_Array)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, STR("{ "));
        
        // TODO(Jose): Use scratch arena for allocating vars
        foreach(i, var->count) {
            Variable* element = var_alloc_from_array(inter, var, i);
            append(&builder, string_from_var(scratch.arena, inter, element));
            if (i < var->count - 1) append(&builder, ", ");
        }
        
        append(&builder, STR(" }"));
        
        return string_from_builder(arena, &builder);
    }
    
    assert(0);
    return STR("?");
}

String string_from_vtype(Interpreter* inter, i32 vtype) {
    String name = vtype_get(inter, vtype).name;
    assert(name.size);
    return name;
}

b32 obj_assign(Interpreter* inter, Object* obj, const Variable* src)
{
    if (!var_assignment_is_valid(obj->var, src)) return false;
    
    assert(obj->var->vtype == src->vtype);
    obj->var = var_copy(inter, src);
    
    return true;
}

i32 push_scope(Interpreter* inter) {
    return ++inter->scope;
}

void pop_scope(Interpreter* inter)
{
    if (inter->scope <= 0) {
        report_error(inter->ctx, {}, STR("The stack is broken..."));
        return;
    }
    inter->scope--;
    
    for (auto it = pooled_array_make_iterator_tail(&inter->objects); it.valid; --it) {
        Object* obj = it.value;
        if (obj->scope > inter->scope) {
            undefine_object(inter, obj);
        }
    }
}

Object* find_object(Interpreter* inter, String identifier, b32 parent_scopes)
{
    if (identifier.size == 0) return NULL;
    
    Object* match = NULL;
    i32 match_scope = -1;
    
    for (auto it = pooled_array_make_iterator(&inter->objects); it.valid; ++it) {
        Object* obj = it.value;
        if (string_equals(obj->identifier, identifier)) {
            if (!parent_scopes && obj->scope != inter->scope) continue;
            if (obj->scope > match_scope) {
                match = obj;
                match_scope = obj->scope;
            }
        }
    }
    
    return match;
}

Object* define_object(Interpreter* inter, String identifier, Variable* var)
{
    assert(find_object(inter, identifier, false) == NULL);
    assert(identifier.size);
    
    Object* obj = NULL;
    
    for (auto it = pooled_array_make_iterator(&inter->objects); it.valid; ++it) {
        if (it.value->identifier.size == 0) obj = it.value;
    }
    
    if (obj == NULL) obj = array_add(&inter->objects);
    obj->identifier = identifier;
    obj->var = var;
    obj->scope = inter->scope;
    return obj;
}

void undefine_object(Interpreter* inter, Object* obj) {
    obj->identifier = {};
}

FunctionDefinition* find_function(Interpreter* inter, String identifier)
{
    foreach(i, inter->functions.count) {
        FunctionDefinition* fn = &inter->functions[i];
        if (string_equals(fn->identifier, identifier)) return fn;
    }
    return NULL;
}

String solve_string_literal(Arena* arena, Interpreter* inter, String src, CodeLocation code)
{
    SCRATCH(arena);
    
    String res = src;
    res = string_replace(scratch.arena, res, STR("\\n"), STR("\n"));
    
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
                    report_error(inter->ctx, code, STR("Undefined object '%S'"), identifier);
                }
                else {
                    append(&builder, string_from_var(scratch.arena, inter, obj->var));
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
    if (!os_path_is_absolute(path)) path = path_resolve(scratch.arena, path_append(scratch.arena, get_string(cd_obj->var), path));
    return string_copy(arena, path);
}

b32 user_assertion(Interpreter* inter, String message)
{
    if (!inter->settings.user_assertion) return true;
    return os_ask_yesno(STR("User Assertion"), message);
}

b32 interpretion_failed(Interpreter* inter)
{
    if (!inter->settings.execute) return false;
    return inter->ctx->error_count > 0;
}