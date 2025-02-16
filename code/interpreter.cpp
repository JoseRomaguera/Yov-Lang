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
                report_error(inter->ctx, code, "Divided by zero");
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
                report_error(inter->ctx, code, STR("Right path can't be absolute"));
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
            report_error(inter->ctx, code, "Type missmatch, can't append a '%S' into '%S'", element_type.name, array_type.name);
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

Object* interpret_expresion(Interpreter* inter, OpNode* node)
{
    SCRATCH();
    
    if (node->kind == OpKind_None) return inter->void_obj;
    
    if (node->kind == OpKind_Binary)
    {
        auto node0 = (OpNode_Binary*)node;
        Object* left = interpret_expresion(inter, node0->left);
        Object* right = interpret_expresion(inter, node0->right);
        BinaryOperator op = node0->op;
        
        Object* result = solve_binary_operation(inter, left, right, op, node->code);
        
        if (is_valid(result))
        {
            if (inter->settings.execute && inter->settings.print_execution) {
                String left_string = string_from_obj(scratch.arena, inter, left);
                String right_string = string_from_obj(scratch.arena, inter, right);
                String result_string = string_from_obj(scratch.arena, inter, result);
                report_info(inter->ctx, node->code, STR("%S %S %S = %S"), left_string, string_from_binary_operator(op), right_string, result_string);
            }
            return result;
        }
        else
        {
            report_error(inter->ctx, node->code, STR("Invalid arithmetic operation '%S %S %S'"), string_from_obj(scratch.arena, inter, left), string_from_binary_operator(op), string_from_obj(scratch.arena, inter, right));
            return inter->nil_obj;
        }
    }
    
    if (node->kind == OpKind_Sign)
    {
        auto node0 = (OpNode_Sign*)node;
        Object* expresion = interpret_expresion(inter, node0->expresion);
        BinaryOperator op = node0->op;
        
        Object* result = solve_signed_operation(inter, expresion, op, node0->code);
        
        if (is_valid(result)) {
            
            if (inter->settings.execute && inter->settings.print_execution) {
                String expresion_string = string_from_obj(scratch.arena, inter, expresion);
                String result_string = string_from_obj(scratch.arena, inter, result);
                report_info(inter->ctx, node->code, STR("%S %S = %S"), string_from_binary_operator(op), expresion_string, result_string);
            }
            
            return result;
        }
        else {
            report_error(inter->ctx, node->code, STR("Invalid signed operation '%S %S'"), string_from_binary_operator(op), string_from_obj(scratch.arena, inter, expresion));
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
            report_error(inter->ctx, node->code, STR("Identifier '%S' not found"), node0->identifier);
            return inter->nil_obj;
        }
        
        return obj;
    }
    
    if (node->kind == OpKind_MemberValue) {
        auto node0 = (OpNode_MemberValue*)node;
        String identifier = node0->identifier;
        Object* obj = find_object(inter, identifier, true);
        
        if (obj == NULL) {
            report_error(inter->ctx, node->code, STR("Identifier '%S' not found"), identifier);
            return inter->nil_obj;
        }
        
        String member = node0->member;
        
        if (member.size == 0) {
            report_error(inter->ctx, node->code, "Member is not specified");
            return inter->nil_obj;
        }
        
        VariableType obj_type = vtype_get(inter, obj->vtype);
        
        if (obj_type.kind == VariableKind_Array)
        {
            if (string_equals(member, STR("count"))) {
                return obj_alloc_temp_int(inter, obj->array.count);
            }
            else {
                report_error(inter->ctx, node->code, "Unknown member '%S' for array", member);
                return inter->nil_obj;
            }
        }
        else {
            report_error(inter->ctx, node->code, "Object '%S' doesn't have members", identifier);
            return inter->nil_obj;
        }
        
        return obj;
    }
    
    if (node->kind == OpKind_FunctionCall) {
        return interpret_function_call(inter, node);
    }
    
    if (node->kind == OpKind_ArrayExpresion)
    {
        auto node0 = (OpNode_ArrayExpresion*)node;
        Array<Object*> objects = array_make<Object*>(scratch.arena, node0->nodes.count);
        
        foreach(i, objects.count) {
            objects[i] = interpret_expresion(inter, node0->nodes[i]);
        }
        
        if (objects.count == 0) {
            return inter->void_obj;
        }
        
        i32 vtype = objects[0]->vtype;
        
        // Assert same vtype
        for (i32 i = 1; i < objects.count; ++i) {
            if (objects[i]->vtype != vtype) {
                report_error(inter->ctx, node->code, "Type missmatch in array expresion, expecting '%S' but found '%S'", string_from_vtype(scratch.arena, inter, vtype), string_from_vtype(scratch.arena, inter, objects[i]->vtype));
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
        Object* indexing_obj = interpret_expresion(inter, node0->expresion);
        Object* array = find_object(inter, node0->identifier, true);
        
        if (array == NULL) {
            report_error(inter->ctx, node->code, "Identifier '%S' not found", node0->identifier);
            return inter->nil_obj;
        }
        
        if (!is_int(indexing_obj)) {
            report_error(inter->ctx, node->code, "Invalid indexing, expecting an Int expresion");
            return inter->nil_obj;
        }
        
        VariableType array_vtype = vtype_get(inter, array->vtype);
        
        if (array_vtype.kind != VariableKind_Array) {
            report_error(inter->ctx, node->code, "Indexing is only allowed for arrays");
            return inter->nil_obj;
        }
        
        Object* obj = obj_alloc_temp(inter, array_vtype.array_of);
        
        if (inter->settings.execute)
        {
            i64 index = get_int(indexing_obj);
            if (index < 0 || index >= array->array.count) {
                report_error(inter->ctx, node->code, "Index out of bounds");
                return inter->nil_obj;
            }
            
            obj_copy_from_element(inter, obj, array, index);
            return obj;
        }
        else {
            return obj;
        }
    }
    
    report_error(inter->ctx, node->code, STR("Unknown expersion"));
    return inter->nil_obj;
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
    
    Object* assignment_result = interpret_expresion(inter, assignment);
    
    if (binary_operator != BinaryOperator_None) {
        assignment_result = solve_binary_operation(inter, obj, assignment_result, binary_operator, assignment->code);
    }
    
    if (!obj_copy(inter, obj, assignment_result)) {
        report_error(inter->ctx, assignment->code, STR("Type missmatch, can't assign '%S' to '%S'"), string_from_vtype(scratch.arena, inter, assignment_result->vtype), string_from_vtype(scratch.arena, inter, obj->vtype));
        return;
    }
    
    if (inter->settings.print_execution) report_info(inter->ctx, assignment->code, STR("%S = %S"), obj->identifier, string_from_obj(scratch.arena, inter, obj));
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
    
    b32 no_assignment = assignment->kind != OpKind_None && assignment->kind != OpKind_Unknown;
    
    if (!no_assignment) {
        if (assert_assignment) {
            report_error(inter->ctx, assignment->code, STR("Invalid assignment"));
        }
        return;
    }
    
    Object* assignment_result = interpret_expresion(inter, assignment);
    
    if (!obj_copy_element_from_object(inter, array, index, assignment_result)) {
        report_error(inter->ctx, assignment->code, STR("Type missmatch, can't assign '%S' to '%S'"), string_from_vtype(scratch.arena, inter, assignment_result->vtype), string_from_vtype(scratch.arena, inter, array->vtype));
        return;
    }
    
    if (inter->settings.print_execution) report_info(inter->ctx, assignment->code, STR("%S[%l] = %S"), array->identifier, index, string_from_obj(scratch.arena, inter, array));
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
    Object* assignment_result = interpret_expresion(inter, assignment);
    
    if (is_unknown(assignment_result)) return;
    
    i32 vtype = -1;
    {
        String type_name = node->type;
        
        if (type_name.size > 0)
        {
            vtype = vtype_from_name(inter, type_name);
            
            if (vtype <= 0) {
                report_error(inter->ctx, node->code, STR("Undefined type '%S'"), type_name);
                return;
            }
            
            if (node->is_array) {
                vtype = vtype_from_array_dimension(inter, vtype, node->array_dimensions.count);
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
    
    if (node->is_array)
    {
        Array<i64> dimensions = array_make<i64>(scratch.arena, node->array_dimensions.count);
        
        b8 initialize_array = false;
        
        foreach(i, dimensions.count) {
            Object* dim = interpret_expresion(inter, node->array_dimensions[i]);
            if (!is_void(dim) && !is_int(dim)) {
                report_error(inter->ctx, node->code, "Expecting an integer for the dimensions of the array");
                return;
            }
            dimensions[i] = is_int(dim) ? get_int(dim) : 0;
            if (dimensions[i] < 0) {
                report_error(inter->ctx, node->code, "Expecting a positive integer for the dimensions of the array");
                return;
            }
            
            if (dimensions[i] > 0) initialize_array = true;
        }
        
        
        if (initialize_array) {
            assert(is_void(assignment_result));
            assignment_result = obj_alloc_temp_array_multidimensional(inter, vtype_from_array_base(inter, vtype), dimensions);
        }
    }
    
    obj = define_object(inter, identifier, vtype);
    
    if (inter->settings.execute)
    {
        if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("%S %S"), string_from_vtype(scratch.arena, inter, vtype), obj->identifier);
        
        if (is_valid(assignment_result)) {
            if (!obj_copy(inter, obj, assignment_result)) {
                report_error(inter->ctx, assignment->code, STR("Type missmatch, can't assign '%S' to '%S'"), string_from_vtype(scratch.arena, inter, assignment_result->vtype), string_from_vtype(scratch.arena, inter, obj->vtype));
                return;
            }
            
            if (inter->settings.print_execution) report_info(inter->ctx, assignment->code, STR("%S = %S"), obj->identifier, string_from_obj(scratch.arena, inter, obj));
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
    SCRATCH();
    
    auto node = (OpNode_ArrayElementAssignment*)node0;
    
    Object* array = find_object(inter, node->identifier, true);
    if (array == NULL) {
        report_error(inter->ctx, node->code, STR("Undefined identifier '%S'"), node->identifier);
        return;
    }
    
    Object* indexing_obj = interpret_expresion(inter, node->indexing_expresion);
    
    if (!is_int(indexing_obj)) {
        report_error(inter->ctx, node->code, "Invalid indexing, expecting an Int expresion");
        return;
    }
    
    VariableType array_vtype = vtype_get(inter, array->vtype);
    
    if (array_vtype.kind != VariableKind_Array) {
        report_error(inter->ctx, node->code, "Can't make indexed assignment to a '%S'", string_from_vtype(scratch.arena, inter, array->vtype));
        return;
    }
    
    if (inter->settings.execute)
    {
        i64 index = get_int(indexing_obj);
        if (index < 0 || index >= array->array.count) {
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
    
    Object* expresion_result = interpret_expresion(inter, node->expresion);
    
    if (!is_bool(expresion_result)) {
        report_error(inter->ctx, node->code, STR("If statement expects a Bool"));
        return;
    }
    
    if (inter->settings.execute) {
        if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("if (%S)"), string_from_obj(scratch.arena, inter, expresion_result));
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
        Object* expresion_result = interpret_expresion(inter, node->expresion);
        
        if (!is_bool(expresion_result)) {
            report_error(inter->ctx, node->code, STR("While statement expects a Bool"));
            return;
        }
        
        if (inter->settings.execute)
        {
            if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("while (%S)"), string_from_obj(scratch.arena, inter, expresion_result));
            
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
        Object* expresion_result = interpret_expresion(inter, node->condition_expresion);
        
        if (!is_bool(expresion_result)) {
            report_error(inter->ctx, node->code, STR("For-statement expects a Bool"));
            return;
        }
        
        if (inter->settings.execute)
        {
            if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("for (%S)"), string_from_obj(scratch.arena, inter, expresion_result));
            
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
    
    Object* array = interpret_expresion(inter, node->expresion);
    if (is_unknown(array)) return;
    
    VariableType array_type = vtype_get(inter, array->vtype);
    
    if (array_type.kind != VariableKind_Array) {
        report_error(inter->ctx, node->code, "For each statement expects an array");
        return;
    }
    
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
            
            if (inter->settings.print_execution) report_info(inter->ctx, node->code, STR("for each[%l] (%S)"), i, string_from_obj(scratch.arena, inter, element));
            
            push_scope(inter);
            interpret_op(inter, node->content);
            pop_scope(inter);
            
            obj_copy_element_from_object(inter, array, i, element);
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

Object* interpret_function_call(Interpreter* inter, OpNode* node0)
{
    SCRATCH();
    auto node = (OpNode_FunctionCall*)node0;
    
    FunctionDefinition* fn = find_function(inter, node->identifier);
    
    if (fn == NULL) {
        report_error(inter->ctx, node->code, STR("Undefined function '%S'"), node->identifier);
        return inter->nil_obj;
    }
    
    if (fn->parameter_vtypes.count != node->parameters.count)
    {
        report_error(inter->ctx, node->code, STR("Function '%S' is expecting %u parameters"), node->identifier, fn->parameter_vtypes.count);
        return inter->nil_obj;
    }
    
    Array<Object*> objects = array_make<Object*>(scratch.arena, fn->parameter_vtypes.count);
    foreach(i, objects.count) {
        objects[i] = interpret_expresion(inter, node->parameters[i]);
        
        if (objects[i]->vtype != fn->parameter_vtypes[i]) {
            report_error(inter->ctx, node->code, "Function '%S' is expecting '%S' as a parameter %u", node->identifier, string_from_vtype(scratch.arena, inter, fn->parameter_vtypes[i]), i + 1);
            return inter->nil_obj;
        }
    }
    
    if (!inter->settings.execute) {
        return obj_alloc_temp(inter, fn->return_vtype);
    }
    
    Object* return_obj = fn->intrinsic_fn(inter, node, objects);
    
    if (inter->settings.print_execution) {
        String return_string = string_from_obj(scratch.arena, inter, return_obj);
        String parameters = STR("TODO");
        report_info(inter->ctx, node->code, STR("%S(%S) => %S"), node->identifier, parameters, return_string);
    }
    
    return return_obj;
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
    }
    
    inter->vtype_table = array_from_pooled_array(inter->ctx->static_arena, list);
    
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
    
    inter->void_obj = arena_push_struct<Object>(inter->ctx->static_arena);
    inter->void_obj->vtype = VType_Void;
    
    inter->nil_obj = arena_push_struct<Object>(inter->ctx->static_arena);
    inter->nil_obj->vtype = VType_Unknown;
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

void interpreter_exit(Interpreter* inter)
{
    inter->ctx->error_count++;// TODO(Jose): Weird
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

u32 vtype_get_stride(i32 vtype) {
    if (vtype == VType_Int) return sizeof(ObjectMemory_Int);
    if (vtype == VType_Bool) return sizeof(ObjectMemory_Bool);
    if (vtype == VType_String) return sizeof(ObjectMemory_String);
    
    u32 dims;
    decode_vtype(vtype, NULL, &dims);
    if (dims > 0) return sizeof(ObjectMemory_Array);
    
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
    obj->scope = inter->scope;
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
    
    u32 stride = vtype_get_stride(element_vtype);
    obj->array = alloc_array_memory(inter, count, stride);
    return obj;
}

ObjectMemory_Array alloc_multidimensional_array_memory(Interpreter* inter, i32 element_vtype, Array<i64> dimensions)
{
    i32 vtype = vtype_from_array_dimension(inter, element_vtype, dimensions.count);
    i32 array_of = vtype_from_array_element(inter, vtype);
    
    i64 count = dimensions[dimensions.count - 1];
    u32 stride = vtype_get_stride(array_of);
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
    
    if (is_array(vtype))
    {
        VariableType array_vtype = vtype_get(inter, vtype);
        u32 stride = vtype_get_stride(array_vtype.array_of);
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
    u32 stride = vtype_get_stride(array_type.array_of);
    
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
    
    u32 stride = vtype_get_stride(array_type.array_of);
    
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
    
    u32 stride = vtype_get_stride(array_type.array_of);
    
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


String string_from_obj(Arena* arena, Interpreter* inter, Object* obj)
{
    SCRATCH(arena);
    
    if (is_string(obj)) return get_string(obj);
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

Object* define_object(Interpreter* inter, String identifier, i32 vtype)
{
    assert(find_object(inter, identifier, false) == NULL);
    assert(identifier.size);
    
    Object* obj = NULL;
    
    for (auto it = pooled_array_make_iterator(&inter->objects); it.valid; ++it) {
        if (it.value->identifier.size == 0) obj = it.value;
    }
    
    if (obj == NULL) obj = array_add(&inter->objects);
    obj->identifier = identifier;
    obj->vtype = vtype;
    obj->scope = inter->scope;
    
    return obj;
}

void undefine_object(Interpreter* inter, Object* obj) {
    // TODO(Jose): FREE MEMORY HERE
    *obj = {};
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
                    report_error(inter->ctx, code, STR("Undefined object '%S'"), identifier);
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