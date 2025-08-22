#include "inc.h"

internal_fn void define_globals(Interpreter* inter)
{
    SCRATCH();
    
    inter->global_objects = array_make<Object*>(yov->static_arena, yov->globals.count);
    
    foreach(i, inter->global_objects.count) {
        inter->global_objects[i] = null_obj;
    }
    
    for (auto it = pooled_array_make_iterator(&yov->globals); it.valid; ++it)
    {
        ObjectDefinition* def = it.value;
        if (global_get_by_index(inter, it.index) != null_obj) continue;
        
        Object* obj = execute_IR(inter, def->ir, {}, NO_CODE);
        global_save_by_index(inter, it.index, obj);
        
    }
    
    // Yov struct
    {
        Object* obj = object_alloc(inter, VType_YovInfo);
        inter->globals.yov = obj;
        global_save(inter, "yov", obj);
        
        set_int_member(inter, obj, "minor", YOV_MINOR_VERSION);
        set_int_member(inter, obj, "major", YOV_MAJOR_VERSION);
        set_int_member(inter, obj, "revision", YOV_REVISION_VERSION);
        set_string_member(inter, obj, "version", YOV_VERSION);
        set_string_member(inter, obj, "path", os_get_executable_path(scratch.arena));
    }
    
    // OS struct
    {
        Object* obj = object_alloc(inter, VType_OS);
        inter->globals.os = obj;
        global_save(inter, "os", obj);
        
#if OS_WINDOWS
        set_enum_index_member(inter, obj, "kind", 0);
#else
#error TODO
#endif
    }
    
    // Context struct
    {
        Object* obj = object_alloc(inter, VType_Context);
        inter->globals.context = obj;
        global_save(inter, "context", obj);
        
        set_string_member(inter, obj, "cd", yov->scripts[0].dir);
        set_string_member(inter, obj, "script_dir", yov->scripts[0].dir);
        set_string_member(inter, obj, "caller_dir", yov->caller_dir);
        
        // Args
        {
            Array<ScriptArg> args = yov->args;
            Object* array = alloc_array(inter, VType_String, args.count, true);
            foreach(i, args.count) {
                object_set_child(inter, array, i, alloc_string(inter, args[i].name));
            }
            
            object_set_member(inter, obj, "args", array);
        }
        
        // Types
        {
            Object* array = alloc_array(inter, VType_Type, yov->vtype_table.count, true);
            
            for (auto it = pooled_array_make_iterator(&yov->vtype_table); it.valid; ++it) {
                u32 index = it.index;
                Object* element = object_alloc(inter, VType_Type);
                object_assign_Type(inter, element, it.value);
                object_set_child(inter, array, it.index, element);
            }
            
            object_set_member(inter, obj, "types", array);
        }
    }
    
    // Calls struct
    {
        Object* obj = object_alloc(inter, VType_CallsContext);
        inter->globals.calls = obj;
        global_save(inter, "calls", obj);
    }
}

internal_fn void report_invalid_arguments(Interpreter* inter)
{
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

void interpret(InterpreterSettings settings)
{
    YovScript* main_script = yov_get_script(0);
    
    Interpreter* inter = arena_push_struct<Interpreter>(yov->static_arena);
    inter->settings = settings;
    
    inter->empty_op = arena_push_struct<OpNode>(yov->static_arena);
    
    inter->global_scope = arena_push_struct<Scope>(yov->static_arena);
    inter->current_scope = inter->global_scope;
    
    report_invalid_arguments(inter);
    define_globals(inter);
    
    {
        String main_function_name = "main";
        
        FunctionDefinition* fn = find_function(main_function_name);
        
        if (fn != NULL && fn->return_vtype == void_vtype && fn->parameters.count == 0) {
            run_function_call(inter, -1, fn, {}, fn->code);
        }
        else {
            report_error(NO_CODE, "Main function not found");
        }
    }
    
    foreach(i, inter->global_objects.count) {
        object_decrement_ref(inter->global_objects[i]);
    }
    
    scope_clear(inter, inter->global_scope);
    object_free_unused(inter);
    
    if (inter->object_count > 0) {
        lang_report_unfreed_objects();
    }
    else if (inter->allocation_count > 0) {
        lang_report_unfreed_dynamic();
    }
    
#if DEV && 0
    if (inter->mode == InterpreterMode_Analysis) print_memory_usage(inter);
#endif
    
    assert(yov->error_count != 0 || inter->current_scope == inter->global_scope);
}

void interpreter_exit(Interpreter* inter, i64 exit_code)
{
    yov_set_exit_code(exit_code);
    yov->error_count++;// TODO(Jose): Weird
}

void interpreter_report_runtime_error(Interpreter* inter, CodeLocation code, Result result) {
    report_error(code, result.message);
    interpreter_exit(inter, result.code);
}

Result user_assertion(Interpreter* inter, String message)
{
    if (!inter->settings.user_assertion || yov_ask_yesno("User Assertion", message)) return RESULT_SUCCESS;
    return result_failed_make("Operation denied by user");
}

Object* get_cd(Interpreter* inter) {
    i32 index = vtype_get_member(VType_Context, "cd").index;
    return object_get_child(inter, inter->globals.context, index);
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
    Object* calls = inter->globals.calls;
    VariableTypeChild info = vtype_get_member(calls->vtype, "redirect_stdout");
    Object* redirect_stdout = object_get_child(inter, calls, info.index);
    return (RedirectStdout)get_enum_index(redirect_stdout);
}

void global_save(Interpreter* inter, String identifier, Object* object)
{
    foreach(i, yov->globals.count) {
        if (string_equals(yov->globals[i].name, identifier)) {
            global_save_by_index(inter, i, object);
            return;
        }
    }
    invalid_codepath();
}

void global_save_by_index(Interpreter* inter, i32 index, Object* object)
{
    object_decrement_ref(inter->global_objects[index]);
    inter->global_objects[index] = object;
    object_increment_ref(inter->global_objects[index]);
}

Object* global_get(Interpreter* inter, String identifier)
{
    foreach(i, yov->globals.count) {
        if (string_equals(yov->globals[i].name, identifier)) {
            return global_get_by_index(inter, i);
        }
    }
    return nil_obj;
}

Object* global_get_by_index(Interpreter* inter, i32 index)
{
    return inter->global_objects[index];
}

Object* object_from_value(Interpreter* inter, Scope* scope, Value value)
{
    SCRATCH();
    
    if (value.kind == ValueKind_None) return null_obj;
    if (value.kind == ValueKind_Literal) {
        if (value.vtype->ID == VTypeID_Int) return alloc_int(inter, value.literal_int);
        if (value.vtype->ID == VTypeID_Bool) return alloc_bool(inter, value.literal_bool);
        if (value.vtype->ID == VTypeID_String) return alloc_string(inter, value.literal_string);
        if (value.vtype == VType_Type) {
            Object* obj = object_alloc(inter, VType_Type);
            object_assign_Type(inter, obj, value.literal_type);
            return obj;
        }
        if (value.vtype->kind == VariableKind_Enum) {
            return alloc_enum(inter, value.vtype, value.literal_int);
        }
        if (value.vtype->kind == VariableKind_Array)
        {
            Array<Value> values = value.literal_array.values;
            b32 is_empty = value.literal_array.is_empty;
            
            if (is_empty)
            {
                Array<i64> dimensions = array_make<i64>(scratch.arena, values.count);
                foreach(i, dimensions.count) {
                    dimensions[i] = get_int(object_from_value(inter, scope, values[i]));
                }
                
                VariableType* base_vtype = value.vtype->child_base;
                Object* array = alloc_array_multidimensional(inter, base_vtype, dimensions);
                return array;
            }
            else
            {
                VariableType* element_vtype = value.vtype->child_next;
                Object* array = alloc_array(inter, element_vtype, values.count, true);
                
                foreach(i, values.count) {
                    object_set_child(inter, array, i, object_from_value(inter, scope, values[i]));
                }
                return array;
            }
        }
        invalid_codepath();
        return nil_obj;
    }
    
    if (value.kind == ValueKind_Default) {
        return object_alloc(inter, value.vtype);
    }
    
    if (value.kind == ValueKind_Composition)
    {
        if (value.vtype->ID == VTypeID_String)
        {
            Array<Value> sources = value.composition_string;
            
            StringBuilder builder = string_builder_make(scratch.arena);
            foreach(i, sources.count)
            {
                Object* source = object_from_value(inter, scope, sources[i]);
                if (is_unknown(source)) return nil_obj;
                append(&builder, string_from_object(scratch.arena, inter, source));
            }
            
            return alloc_string(inter, string_from_builder(scratch.arena, &builder));
        }
    }
    
    if (value.kind == ValueKind_Constant) {
        return global_get(inter, value.constant_identifier);
    }
    
    if (value.kind == ValueKind_LValue || value.kind == ValueKind_Register) {
        return register_get(scope, value.register_index);
    }
    
    invalid_codepath();
    return null_obj;
}

Object* execute_IR(Interpreter* inter, IR ir, Array<Value> params, CodeLocation code)
{
    SCRATCH();
    
    Scope* scope = arena_push_struct<Scope>(scratch.arena);
    scope->registers = array_make<Register>(scratch.arena, ir.register_count);
    foreach(i, scope->registers.count) {
        scope->registers[i] = register_make_root(i, null_obj);
    }
    
    Scope* prev_scope = inter->current_scope;
    inter->current_scope = scope;
    DEFER(inter->current_scope = prev_scope);
    
    // Define params
    foreach(i, params.count) {
        i32 reg = i;
        Value param = params[i];
        run_store(inter, param.vtype, {}, reg, code);
        run_assign(inter, reg, param, prev_scope, code);
    }
    
    scope->pc = 0;
    
    while (scope->pc < ir.instructions.count && yov->error_count == 0)
    {
        if (inter->current_scope != scope) break;
        if (scope->return_requested) break;
        
        if (scope->pc >= ir.instructions.count) {
            report_error(NO_CODE, "Program Counter overflow");
            break;
        }
        
        Unit unit = ir.instructions[scope->pc];
        scope->pc++;
        
#if DEV
        //print_info("%S\n", string_from_Unit(scratch.arena, scope->pc - 1, 0, 0, unit));
#endif
        
        run_instruction(inter, unit);
        
        // TODO(Jose): 
        if (scope->pc % 10 == 0) {
            object_free_unused(inter);
        }
    }
    
    // Retrieve return value
    Object* return_object = object_from_value(inter, scope, ir.value);
    
    scope_clear(inter, scope);
    
    return return_object;
}

void run_instruction(Interpreter* inter, Unit unit)
{
    CodeLocation code = unit.code;
    
    i32 dst_index = unit.dst_index;
    
    if (unit.kind == UnitKind_Assignment) {
        Value src = unit.assignment.src;
        run_assign(inter, dst_index, src, inter->current_scope, code);
        return;
    }
    
    if (unit.kind == UnitKind_Store) {
        VariableType* vtype = unit.store.vtype;
        String global_identifier = unit.store.global_identifier;
        
        run_store(inter, vtype, global_identifier, dst_index, code);
        return;
    }
    
    if (unit.kind == UnitKind_FunctionCall)
    {
        SCRATCH();
        FunctionDefinition* fn = unit.function_call.fn;
        Array<Value> parameters = unit.function_call.parameters;
        run_function_call(inter, dst_index, fn, parameters, code);
        return;
    }
    
    if (unit.kind == UnitKind_Return) {
        run_return(inter, code);
        return;
    }
    
    if (unit.kind == UnitKind_Jump)
    {
        i32 condition = unit.jump.condition;
        Object* src = null_obj;
        i32 offset = unit.jump.offset;
        
        if (condition != 0) src = object_from_value(inter, inter->current_scope, unit.jump.src);
        run_jump(inter, src, condition, offset, code);
        return;
    }
    
    if (unit.kind == UnitKind_BinaryOperation) {
        Object* src0 = object_from_value(inter, inter->current_scope, unit.binary_op.src0);
        Object* src1 = object_from_value(inter, inter->current_scope, unit.binary_op.src1);
        BinaryOperator op = unit.binary_op.op;
        
        Object* result = run_binary_operation(inter, src0, src1, op, code);
        register_save_root(inter, dst_index, result);
        return;
    }
    
    if (unit.kind == UnitKind_SignOperation) {
        Object* src = object_from_value(inter, inter->current_scope, unit.sign_op.src);
        BinaryOperator op = unit.sign_op.op;
        
        Object* result = run_sign_operation(inter, src, op, code);
        register_save_root(inter, dst_index, result);
        return;
    }
    
    if (unit.kind == UnitKind_Child)
    {
        SCRATCH();
        b32 child_is_member = unit.child.child_is_member;
        Object* child_index_obj = object_from_value(inter, inter->current_scope, unit.child.child_index);
        Object* src = object_from_value(inter, inter->current_scope, unit.child.src);
        
        if (is_unknown(src) || is_unknown(child_index_obj)) return;
        
        if (!is_int(child_index_obj)) {
            report_error(code, "Expecting an integer");
            return;
        }
        
        i64 child_index = get_int(child_index_obj);
        
        if (src == null_obj) {
            report_error(code, "Null reference");
            return;
        }
        
        // TODO(Jose):
        u32 child_count = u32_max;
        
        if (child_index < 0 || child_index >= child_count) {
            report_error(code, "Out of bounds");
            return;
        }
        
        Object* child = object_get_child(inter, src, (u32)child_index, child_is_member);
        register_save_child(inter, dst_index, child, src, (i32)child_index);
        return;
    }
    
    if (unit.kind == UnitKind_ResultEval)
    {
        Object* src = object_from_value(inter, inter->current_scope, unit.result_eval.src);
        
        assert(src->vtype == VType_Result);
        
        if (src->vtype == VType_Result) {
            Result result = Result_from_object(inter, src);
            if (result.failed) {
                interpreter_report_runtime_error(inter, code, result);
            }
        }
        return;
    }
    
    invalid_codepath();
}

void run_assign(Interpreter* inter, i32 dst_index, Value src, Scope* src_scope, CodeLocation code)
{
    SCRATCH();
    
    Object* dst_obj = register_get(inter->current_scope, dst_index);
    Object* src_obj = object_from_value(inter, src_scope, src);
    
    if (is_unknown(dst_obj)) return;
    if (is_unknown(src_obj)) return;
    
    if (src.take_reference) {
        if (src_obj == null_obj) {
            report_error(code, "Null reference");
            return;
        }
        
        register_save_reference(inter, dst_index, src_obj);
    }
    else {
        if (dst_obj == null_obj) {
            dst_obj = object_alloc(inter, src_obj->vtype);
            register_save_root(inter, dst_index, dst_obj);
        }
        
        if (dst_obj == null_obj || src_obj == null_obj) {
            report_error(code, "Null reference");
            return;
        }
        
        object_copy(inter, dst_obj, src_obj);
    }
}

void run_store(Interpreter* inter, VariableType* vtype, String global_identifier, i32 index, CodeLocation code)
{
    Object* obj = null_obj;
    
    if (global_identifier.size > 0)
    {
        obj = global_get(inter, global_identifier);
        assert(obj != nil_obj);
    }
    
    register_save_root(inter, index, obj);
}

void run_function_call(Interpreter* inter, i32 dst_index, FunctionDefinition* fn, Array<Value> parameters, CodeLocation code)
{
    SCRATCH();
    
    Object* return_object = null_obj;
    
    if (fn->is_intrinsic)
    {
        Array<Object*> params = array_make<Object*>(scratch.arena, parameters.count);
        foreach(i, params.count) {
            params[i] = object_from_value(inter, inter->current_scope, parameters[i]);
        }
        
        Array<Object*> returns = array_make<Object*>(scratch.arena, fn->returns.count);
        
        fn->intrinsic.fn(inter, params, returns, code);
        
        if (vtype_is_tuple(fn->return_vtype)) {
            return_object = object_alloc(inter, fn->return_vtype);
            foreach(i, returns.count) {
                object_set_child(inter, return_object, i, returns[i]);
            }
        }
        else if (fn->return_vtype != void_vtype) {
            assert(returns.count == 1);
            return_object = returns[0];
        }
    }
    else
    {
        return_object = execute_IR(inter, fn->defined.ir, parameters, code);
    }
    
    if (dst_index >= 0) {
        register_save_root(inter, dst_index, return_object);
    }
}

void run_return(Interpreter* inter, CodeLocation code)
{
    Scope* scope = inter->current_scope;
    scope->return_requested = true;
}

void run_jump(Interpreter* inter, Object* object, i32 condition, i32 offset, CodeLocation code)
{
    b32 jump = true;
    
    if (condition != 0)
    {
        if (is_unknown(object)) return;
        
        if (!is_bool(object)) {
            report_expr_expects_bool(code, "If-Statement");
            return;
        }
        
        jump = get_bool(object);
        if (condition < 0) jump = !jump;
    }
    
    if (jump) {
        Scope* scope = inter->current_scope;
        if (scope == inter->global_scope) {
            invalid_codepath();
            return;
        }
        
        scope->pc += offset;
    }
}

Object* run_binary_operation(Interpreter* inter, Object* left, Object* right, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    VariableType* left_vtype = left->vtype;
    VariableType* right_vtype = right->vtype;
    
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
        if (op == BinaryOperator_Equals) return alloc_bool(inter, get_bool(left) == get_bool(right));
        if (op == BinaryOperator_NotEquals) return alloc_bool(inter, get_bool(left) != get_bool(right));
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
    
    if (left_vtype->ID == VType_Type->ID && right_vtype->ID == VType_Type->ID)
    {
        i32 index = vtype_get_member(VType_Type, "ID").index;
        
        i64 left_id = get_int(object_get_child(inter, left, index));
        i64 right_id = get_int(object_get_child(inter, right, index));
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
            Object* string_object = is_string(left) ? left : right;
            Object* codepoint_object = is_int(left) ? left : right;
            
            String codepoint_str = string_from_codepoint(scratch.arena, (u32)get_int(codepoint_object));
            
            String left_str = is_string(left) ? get_string(left) : codepoint_str;
            String right_str = is_string(right) ? get_string(right) : codepoint_str;
            
            String str = string_format(scratch.arena, "%S%S", left_str, right_str);
            return alloc_string(inter, str);
        }
    }
    
    if (is_enum(left) && is_enum(right)) {
        if (op == BinaryOperator_Equals) {
            return alloc_bool(inter, get_enum_index(left) == get_enum_index(right));
        }
        else if (op == BinaryOperator_NotEquals) {
            return alloc_bool(inter, get_enum_index(left) != get_enum_index(right));
        }
    }
    
    if (left_vtype->child_next->ID == right_vtype->child_next->ID && left_vtype->kind == VariableKind_Array && right_vtype->kind == VariableKind_Array)
    {
        VariableType* element_vtype = left_vtype->child_next;
        
        i32 left_count = get_array(left).count;
        i32 right_count = get_array(right).count;
        
        if (op == BinaryOperator_Addition) {
            Object* array = alloc_array(inter, element_vtype, left_count + right_count, true);
            for (u32 i = 0; i < left_count; ++i) {
                Object* src = object_get_child(inter, left, i);
                object_set_child(inter, array, i, src);
            }
            for (u32 i = 0; i < right_count; ++i) {
                Object* src = object_get_child(inter, right, i);
                object_set_child(inter, array, left_count + i, src);
            }
            return array;
        }
    }
    
    if ((left_vtype->kind == VariableKind_Array && right_vtype->kind != VariableKind_Array) || (left_vtype->kind != VariableKind_Array && right_vtype->kind == VariableKind_Array))
    {
        VariableType* array_type = (left_vtype->kind == VariableKind_Array) ? left_vtype : right_vtype;
        VariableType* element_type = (left_vtype->kind == VariableKind_Array) ? right_vtype : left_vtype;
        
        if (array_type->child_next->ID != element_type->ID) {
            report_type_missmatch_append(code, element_type->name, array_type->name);
            return nil_obj;
        }
        
        Object* array_src = (left_vtype->kind == VariableKind_Array) ? left : right;
        Object* element = (left_vtype->kind == VariableKind_Array) ? right : left;
        
        i32 array_src_count = get_array(array_src).count;
        Object* array = alloc_array(inter, element_type, array_src_count + 1, true);
        
        i32 array_offset = (left_vtype->kind == VariableKind_Array) ? 0 : 1;
        
        for (i32 i = 0; i < array_src_count; ++i) {
            Object* src = object_get_child(inter, array_src, i);
            object_set_child(inter, array, i + array_offset, src);
        }
        
        i32 element_offset = (left_vtype->kind == VariableKind_Array) ? array_src_count : 0;
        object_set_child(inter, array, element_offset, element);
        return array;
    }
    
    report_invalid_binary_op(code, left_vtype->name, string_from_binary_operator(op), right_vtype->name);
    return nil_obj;
}

Object* run_sign_operation(Interpreter* inter, Object* object, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    
    VariableType* vtype = object->vtype;
    
    if (vtype->ID == VTypeID_Int) {
        if (op == BinaryOperator_Addition) return object;
        if (op == BinaryOperator_Substraction) return alloc_int(inter, -get_int(object));
    }
    
    if (vtype->ID == VTypeID_Bool) {
        if (op == BinaryOperator_LogicalNot) return alloc_bool(inter, !get_bool(object));
    }
    
    return nil_obj;
}

//- SCOPE

void scope_clear(Interpreter* inter, Scope* scope)
{
    if (scope == NULL) {
        lang_report_stack_is_broken();
        return;
    }
    
    foreach(i, scope->registers.count) {
        object_decrement_ref(scope->registers[i].object);
    }
}

void register_save_root(Interpreter* inter, i32 index, Object* object)
{
    Scope* scope = inter->current_scope;
    object_decrement_ref(scope->registers[index].object);
    
    scope->registers[index] = register_make_root(index, object);
    object_increment_ref(object);
}

void register_save_child(Interpreter* inter, i32 index, Object* object, Object* parent, i32 child_index)
{
    Scope* scope = inter->current_scope;
    object_decrement_ref(scope->registers[index].object);
    
    scope->registers[index] = register_make_child(object, parent, child_index);
    object_increment_ref(object);
}

void register_save_reference(Interpreter* inter, i32 index, Object* object)
{
    Scope* scope = inter->current_scope;
    Register* reg = &scope->registers[index];
    
    if (reg->parent == NULL) {
        register_save_root(inter, index, object);
    }
    else {
        object_decrement_ref(reg->object);
        object_set_child(inter, reg->parent, reg->child_index, object);
        
        reg->object = object;
        object_increment_ref(object);
    }
}

Object* register_get(Scope* scope, i32 index) {
    return scope->registers[index].object;
}

Register register_make_root(i32 index, Object* object)
{
    Register reg{};
    reg.object = object;
    reg.child_index = -1;
    return reg;
}

Register register_make_child(Object* object, Object* parent, i32 child_index)
{
    Register reg{};
    reg.object = object;
    reg.parent = parent;
    reg.child_index = child_index;
    return reg;
}

//- OBJECT 

String string_from_object(Arena* arena, Interpreter* inter, Object* object, b32 raw) {
    SCRATCH(arena);
    
    if (is_null(object)) {
        return "null";
    }
    
    VariableType* vtype = object->vtype;
    
    if (vtype->ID == VTypeID_String) {
        if (raw) return get_string(object);
        return string_format(arena, "\"%S\"", get_string(object));
    }
    if (vtype->ID == VTypeID_Int) { return string_format(arena, "%l", get_int(object)); }
    if (vtype->ID == VTypeID_Bool) { return get_bool(object) ? "true" : "false"; }
    if (vtype->ID == VTypeID_Void) { return "void"; }
    if (vtype->ID == VTypeID_Unknown) { return "unknown"; }
    
    if (vtype->kind == VariableKind_Array)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, "{ ");
        
        Array<Object*> array = get_array(object);
        
        foreach(i, array.count) {
            Object* element = array[i];
            append(&builder, string_from_object(scratch.arena, inter, element, false));
            if (i < array.count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (vtype->kind == VariableKind_Enum)
    {
        i64 index = get_enum_index(object);
        if (index < 0 || index >= vtype->_enum.names.count) return "?";
        String name = vtype->_enum.names[(u32)index];
        if (!raw) name = string_format(arena, "\"%S\"", name);
        return name;
    }
    
    if (vtype->kind == VariableKind_Struct)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, "{ ");
        
        Object_Struct* struct_obj = (Object_Struct*)object;
        
        foreach(i, vtype->_struct.vtypes.count)
        {
            String member_name = vtype->_struct.names[i];
            Object* member = struct_obj->members[i];
            appendf(&builder, "%S = %S", member_name, string_from_object(scratch.arena, inter, member, false));
            if (i < vtype->_struct.vtypes.count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    invalid_codepath();
    return "?";
}

Array<Object*> object_get_childs_objects(Arena* arena, Interpreter* inter, Object* obj)
{
    VariableType* vtype = obj->vtype;
    
    if (vtype->kind == VariableKind_Array) {
        return ((Object_Array*)obj)->elements;
    }
    else if (vtype->kind == VariableKind_Struct) {
        return ((Object_Struct*)obj)->members;
    }
    else if (vtype->kind == VariableKind_Primitive) {}
    else if (vtype->kind == VariableKind_Enum) {}
    else {
        invalid_codepath();
    }
    
    return {};
}

b32 object_set_child(Interpreter* inter, Object* obj, u32 index, Object* child)
{
    VariableType* vtype = obj->vtype;
    
    if (vtype->kind == VariableKind_Array)
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
    else if (vtype->kind == VariableKind_Struct)
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
    else {
        invalid_codepath();
    }
    
    return {};
}

b32 object_set_member(Interpreter* inter, Object* object, String member_name, Object* child)
{
    VariableType* vtype = object->vtype;
    
    if (vtype->kind == VariableKind_Struct)
    {
        foreach(i, vtype->_struct.names.count)
        {
            if (string_equals(member_name, vtype->_struct.names[i])) {
                return object_set_child(inter, object, i, child);
            }
        }
    }
    
    return false;
}

Object* object_get_child(Interpreter* inter, Object* object, u32 index, b32 is_member)
{
    if (is_unknown(object) || is_null(object)) {
        invalid_codepath();
        return nil_obj;
    }
    
    VariableType* vtype = object->vtype;
    
    if (is_member)
    {
        if (vtype->kind == VariableKind_Array || vtype->kind == VariableKind_Struct)
        {
            Array<Object*> array = {};
            
            if (vtype->kind == VariableKind_Array) array = ((Object_Array*)object)->elements;
            else if (vtype->kind == VariableKind_Struct) array = ((Object_Struct*)object)->members;
            
            if (index < 0 || index >= array.count) {
                invalid_codepath();
                return nil_obj;
            }
            
            return array[index];
        }
    }
    else
    {
        if (vtype->ID == VTypeID_String)
        {
            if (index == 0) return alloc_int(inter, get_string(object).size);
        }
        
        if (vtype->kind == VariableKind_Array)
        {
            if (index == 0) return alloc_int(inter, get_array(object).count);
        }
        
        if (vtype->kind == VariableKind_Enum)
        {
            i64 v = get_enum_index(object);
            if (index == 0) return alloc_int(inter, v);
            if (index == 1) {
                if (v < 0 || v >= vtype->_enum.values.count) return alloc_int(inter, -1);
                return alloc_int(inter, vtype->_enum.values[v]);
            }
            if (index == 2) {
                if (v < 0 || v >= vtype->_enum.names.count) return alloc_string(inter, "?");
                return alloc_string(inter, vtype->_enum.names[v]);
            }
        }
    }
    
    invalid_codepath();
    return nil_obj;
}

Object* alloc_int(Interpreter* inter, i64 value)
{
    Object* obj = object_alloc(inter, VType_Int);
    set_int(obj, value);
    return obj;
}

Object* alloc_bool(Interpreter* inter, b32 value)
{
    Object* obj = object_alloc(inter, VType_Bool);
    set_bool(obj, value);
    return obj;
}

Object* alloc_string(Interpreter* inter, String value)
{
    Object* obj = object_alloc(inter, VType_String);
    set_string(inter, obj, value);
    return obj;
}

Object* alloc_array(Interpreter* inter, VariableType* element_vtype, i64 count, b32 null_elements)
{
    VariableType* vtype = vtype_from_dimension(element_vtype, 1);
    
    if (vtype->kind != VariableKind_Array) {
        invalid_codepath();
        return nil_obj;
    }
    
    Object_Array* obj = (Object_Array*)object_alloc(inter, vtype);
    
    Object** element_memory = (Object**)gc_dynamic_allocate(inter, sizeof(Object*) * count);
    obj->elements = array_make(element_memory, (u32)count);
    
    if (null_elements) {
        foreach(i, obj->elements.count) {
            obj->elements[i] = null_obj;
        }
    }
    else {
        foreach(i, obj->elements.count) {
            obj->elements[i] = object_alloc(inter, element_vtype);
            object_increment_ref(obj->elements[i]);
        }
    }
    
    return obj;
}

Object* alloc_array_multidimensional(Interpreter* inter, VariableType* base_vtype, Array<i64> dimensions)
{
    if (dimensions.count <= 0) {
        invalid_codepath();
        return nil_obj;
    }
    
    VariableType* vtype = vtype_from_dimension(base_vtype, dimensions.count);
    VariableType* element_vtype = vtype->child_next;
    
    if (dimensions.count == 1)
    {
        return alloc_array(inter, element_vtype, dimensions[0], false);
    }
    else
    {
        Object_Array* obj = (Object_Array*)object_alloc(inter, vtype);
        
        u32 count = (u32)dimensions[0];
        Object** element_memory = (Object**)gc_dynamic_allocate(inter, sizeof(Object*) * count);
        obj->elements = array_make(element_memory, count);
        
        foreach(i, obj->elements.count) {
            obj->elements[i] = alloc_array_multidimensional(inter, base_vtype, array_subarray(dimensions, 1, dimensions.count - 1));
            object_increment_ref(obj->elements[i]);
        }
        
        return obj;
    }
}

Object* alloc_array_from_enum(Interpreter* inter, VariableType* enum_vtype)
{
    if (enum_vtype->kind != VariableKind_Enum) {
        invalid_codepath();
        return nil_obj;
    }
    
    Object_Array* array = (Object_Array*)alloc_array(inter, enum_vtype, enum_vtype->_enum.values.count, false);
    foreach(i, array->elements.count) {
        Object* element = array->elements[i];
        set_enum_index(element, i);
    }
    return array;
}

Object* alloc_enum(Interpreter* inter, VariableType* vtype, i64 index)
{
    Object* obj = object_alloc(inter, vtype);
    set_enum_index(obj, index);
    return obj;
}

b32 is_valid(Object* obj) {
    return !is_unknown(obj);
}
b32 is_unknown(Object* obj) {
    if (obj == NULL) return true;
    return obj->vtype->ID == VTypeID_Unknown;
}

b32 is_const(Object* obj) {
    // TODO(Jose): return value.kind == ValueKind_LValue && value.lvalue.ref->constant;
    return false;
}

b32 is_null(Object* obj) {
    if (obj == NULL) return true;
    return obj->vtype == void_vtype;
}

b32 is_int(Object* obj) { return obj->vtype->ID == VTypeID_Int; }
b32 is_bool(Object* obj) { return obj->vtype->ID == VTypeID_Bool; }
b32 is_string(Object* obj) { return obj->vtype->ID == VTypeID_String; }

b32 is_array(Object* obj) {
    if (is_unknown(obj)) return false;
    return vtype_is_array(obj->vtype);
}

b32 is_enum(Object* obj) {
    if (is_unknown(obj)) return false;
    return vtype_is_enum(obj->vtype);
}

i64 get_int(Object* obj)
{
    if (!is_int(obj)) {
        invalid_codepath();
        return 0;
    }
    
    Object_Int* obj0 = (Object_Int*)obj;
    return obj0->value;
}

b32 get_bool(Object* obj)
{
    if (!is_bool(obj)) {
        invalid_codepath();
        return 0;
    }
    
    Object_Bool* obj0 = (Object_Bool*)obj;
    return obj0->value;
}

i64 get_enum_index(Object* obj) {
    if (!is_enum(obj)) {
        invalid_codepath();
        return 0;
    }
    
    Object_Enum* obj0 = (Object_Enum*)obj;
    return obj0->index;
}

String get_string(Object* obj)
{
    if (!is_string(obj)) {
        invalid_codepath();
        return 0;
    }
    
    Object_String* obj0 = (Object_String*)obj;
    return obj0->value;
}

Array<Object*> get_array(Object* obj)
{
    if (!is_array(obj)) {
        invalid_codepath();
        return {};
    }
    
    Object_Array* obj0 = (Object_Array*)obj;
    return obj0->elements;
}

i64 get_int_member(Interpreter* inter, Object* obj, String member)
{
    i32 index = vtype_get_member(obj->vtype, member).index;
    Object* member_object = object_get_child(inter, obj, index);
    return get_int(member_object);
}

b32 get_bool_member(Interpreter* inter, Object* obj, String member)
{
    i32 index = vtype_get_member(obj->vtype, member).index;
    Object* member_object = object_get_child(inter, obj, index);
    return get_bool(member_object);
}

String get_string_member(Interpreter* inter, Object* obj, String member)
{
    i32 index = vtype_get_member(obj->vtype, member).index;
    Object* member_object = object_get_child(inter, obj, index);
    return get_string(member_object);
}

void set_int(Object* obj, i64 v)
{
    if (!is_int(obj)) {
        invalid_codepath();
        return;
    }
    
    Object_Int* obj_int = (Object_Int*)obj;
    obj_int->value = v;
}

void set_bool(Object* obj, b32 v)
{
    if (!is_bool(obj)) {
        invalid_codepath();
        return;
    }
    
    Object_Bool* obj_bool = (Object_Bool*)obj;
    obj_bool->value = v;
}

void set_enum_index(Object* obj, i64 v)
{
    if (!is_enum(obj)) {
        invalid_codepath();
        return;
    }
    
    Object_Enum* obj_enum = (Object_Enum*)obj;
    obj_enum->index = v;
}

void set_string(Interpreter* inter, Object* obj, String v)
{
    if (!is_string(obj)) {
        invalid_codepath();
        return;
    }
    
    Object_String* obj_str = (Object_String*)obj;
    
    char* old_data = obj_str->value.data;
    char* new_data = (v.size > 0) ? (char*)gc_dynamic_allocate(inter, v.size) : NULL;
    
    memory_copy(new_data, v.data, v.size);
    if (old_data != NULL) gc_dynamic_free(inter, old_data);
    
    obj_str->value.data = new_data;
    obj_str->value.size = v.size;
}

void set_int_member(Interpreter* inter, Object* obj, String member, i64 v)
{
    i32 index = vtype_get_member(obj->vtype, member).index;
    Object* member_object = object_get_child(inter, obj, index);
    if (is_unknown(member_object)) return;
    if (is_null(member_object)) object_set_member(inter, obj, member, alloc_int(inter, v));
    else set_int(member_object, v);
}

void set_bool_member(Interpreter* inter, Object* obj, String member, b32 v)
{
    i32 index = vtype_get_member(obj->vtype, member).index;
    Object* member_object = object_get_child(inter, obj, index);
    if (is_unknown(member_object)) return;
    if (is_null(member_object)) object_set_member(inter, obj, member, alloc_bool(inter, v));
    else set_bool(member_object, v);
}

void set_enum_index_member(Interpreter* inter, Object* obj, String member, i64 v)
{
    i32 index = vtype_get_member(obj->vtype, member).index;
    Object* member_object = object_get_child(inter, obj, index);
    if (is_unknown(member_object)) return;
    if (is_null(member_object)) {
        VariableTypeChild info = vtype_get_member(obj->vtype, member);
        object_set_member(inter, obj, member, alloc_enum(inter, info.vtype, v));
    }
    else set_enum_index(member_object, v);
}

void set_string_member(Interpreter* inter, Object* obj, String member, String v)
{
    i32 index = vtype_get_member(obj->vtype, member).index;
    Object* member_object = object_get_child(inter, obj, index);
    if (is_unknown(member_object)) return;
    if (is_null(member_object)) object_set_member(inter, obj, member, alloc_string(inter, v));
    else set_string(inter, member_object, v);
}

void object_assign_Result(Interpreter* inter, Object* obj, Result res)
{
    set_string_member(inter, obj, "message", res.message);
    set_int_member(inter, obj, "code", res.code);
    set_bool_member(inter, obj, "failed", res.failed);
}

void object_assign_CallOutput(Interpreter* inter, Object* obj, CallOutput res)
{
    set_string_member(inter, obj, "stdout", res.stdout);
}

void object_assign_FileInfo(Interpreter* inter, Object* obj, FileInfo info)
{
    set_string_member(inter, obj, "path", info.path);
    set_bool_member(inter, obj, "is_directory", info.is_directory);
}

void object_assign_Type(Interpreter* inter, Object* obj, VariableType* vtype)
{
    set_int_member(inter, obj, "ID", vtype->ID);
    set_string_member(inter, obj, "name", vtype->name);
}

Object* object_from_Result(Interpreter* inter, Result res)
{
    Object* obj = object_alloc(inter, VType_Result);
    object_assign_Result(inter, obj, res);
    return obj;
}

Result Result_from_object(Interpreter* inter, Object* obj)
{
    if (obj->vtype->ID != VType_Result->ID) {
        invalid_codepath();
        return {};
    }
    
    Result res;
    res.message = get_string_member(inter, obj, "message");
    res.code = (i32)get_int_member(inter, obj, "code");
    res.failed = get_bool_member(inter, obj, "failed");
    return res;
}

//- GARBAGE COLLECTOR

u32 object_generate_id(Interpreter* inter) {
    return ++inter->object_id_counter;
}

Object* object_alloc(Interpreter* inter, VariableType* vtype)
{
    SCRATCH();
    assert(vtype->ID > VTypeID_Any);
    
    u32 ID = object_generate_id(inter);
    
    log_mem_trace("Alloc obj(%u): %S\n", ID, string_from_vtype(scratch.arena, inter, vtype));
    
    u32 type_size = vtype_get_size(vtype);
    assert(type_size > sizeof(Object));
    
    Object* obj = (Object*)gc_dynamic_allocate(inter, type_size);
    
    obj->next = inter->object_list;
    if (inter->object_list) inter->object_list->prev = obj;
    inter->object_list = obj;
    inter->object_count++;
    
    obj->ID = ID;
    obj->vtype = vtype;
    obj->ref_count = 0;
    
    if (vtype->kind == VariableKind_Struct)
    {
        Object** members_memory = (Object**)gc_dynamic_allocate(inter, sizeof(Object*) * vtype->_struct.vtypes.count);
        Array<Object*> members = array_make(members_memory, vtype->_struct.vtypes.count);
        
        foreach(i, vtype->_struct.vtypes.count)
        {
            VariableType* member_vtype = vtype->_struct.vtypes[i];
            IR member_initialize_ir = vtype->_struct.irs[i];
            u32 member_size = vtype_get_size(member_vtype);
            
            Object* member = null_obj;
            
            b32 null_members = false;
            
            if (!null_members)
            {
                b32 has_initializer = member_initialize_ir.value.kind != ValueKind_None;
                
                if (has_initializer) {
                    member = execute_IR(inter, member_initialize_ir, {}, NO_CODE);
                }
                else {
                    member = object_alloc(inter, member_vtype);
                }
            }
            
            object_increment_ref(member);
            members[i] = member;
        }
        
        ((Object_Struct*)obj)->members = members;
    }
    else if (vtype->kind == VariableKind_Array) { }
    else if (vtype->kind == VariableKind_Enum) { }
    else if (vtype->kind == VariableKind_Primitive) { }
    else {
        invalid_codepath();
    }
    
    return obj;
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
    
    *obj = {};
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
    SCRATCH();
    Array<Object*> childs = object_get_childs_objects(scratch.arena, inter, obj);
    
    foreach(i, childs.count) {
        object_decrement_ref(childs[i]);
    }
    
    if (obj->vtype->ID == VTypeID_String)
    {
        Object_String* str = (Object_String*)obj;
        if (str->value.data != NULL) gc_dynamic_free(inter, str->value.data);
    }
    else
    {
        VariableType* vtype = obj->vtype;
        
        if (vtype->kind == VariableKind_Array) {
            Array<Object*> elements = ((Object_Array*)obj)->elements;
            if (elements.data != NULL) gc_dynamic_free(inter, elements.data);
        }
        else if (vtype->kind == VariableKind_Struct) {
            Array<Object*> members = ((Object_Struct*)obj)->members;
            if (members.data != NULL) gc_dynamic_free(inter, members.data);
        }
    }
    
    u32 type_size = vtype_get_size(obj->vtype);
    memory_zero(obj + 1, type_size - sizeof(Object));
}

void object_copy(Interpreter* inter, Object* dst, Object* src)
{
    if (is_unknown(dst) || is_null(dst)) {
        invalid_codepath();
        return;
    }
    
    if (is_unknown(src) || is_null(src)) {
        invalid_codepath();
        return;
    }
    
    if (src->vtype != dst->vtype) {
        invalid_codepath();
        return;
    }
    
    object_release_internal(inter, dst);
    
    u32 type_size = vtype_get_size(src->vtype);
    memory_copy(dst + 1, src + 1, type_size - sizeof(Object));
    
    if (src->vtype->ID == VTypeID_String)
    {
        Object_String* src_str = (Object_String*)src;
        Object_String* dst_str = (Object_String*)dst;
        dst_str->value.data = (char*)gc_dynamic_allocate(inter, src_str->value.size);
        dst_str->value.size = src_str->value.size;
        memory_copy(dst_str->value.data, src_str->value.data, src_str->value.size);
    }
    else
    {
        VariableType* vtype = src->vtype;
        
        if (vtype->kind == VariableKind_Array)
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
        else if (vtype->kind == VariableKind_Struct)
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
        invalid_codepath();
        return nil_obj;
    }
    
    if (is_null(src)) return null_obj;
    
    Object* dst = object_alloc(inter, src->vtype);
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
    
#if 1
    Object* obj = inter->object_list;
    while (obj != NULL)
    {
        Object* next = obj->next;
        log_mem_trace("Obj %u: %u refs\n", obj->ID, obj->ref_count);
        obj = next;
    }
#endif
    
    print_info("Object Count: %u\n", inter->object_count);
    print_info("Alloc Count: %u\n", inter->allocation_count);
    print_separator();
}