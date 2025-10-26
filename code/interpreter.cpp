#include "inc.h"

internal_fn void define_globals(Interpreter* inter)
{
    SCRATCH();
    
    // Yov struct
    if (VType_YovInfo != nil_vtype)
    {
        inter->common_globals.yov = object_alloc(inter, VType_YovInfo);
        Reference ref = inter->common_globals.yov;
        global_save(inter, "yov", ref);
        
        set_int_member(inter, ref, "minor", YOV_MINOR_VERSION);
        set_int_member(inter, ref, "major", YOV_MAJOR_VERSION);
        set_int_member(inter, ref, "revision", YOV_REVISION_VERSION);
        ref_member_set_string(inter, ref, "version", YOV_VERSION);
        ref_member_set_string(inter, ref, "path", os_get_executable_path(scratch.arena));
    }
    
    // OS struct
    if (VType_OS != nil_vtype)
    {
        inter->common_globals.os = object_alloc(inter, VType_OS);
        Reference ref = inter->common_globals.os;
        global_save(inter, "os", ref);
        
#if OS_WINDOWS
        set_enum_index_member(inter, ref, "kind", 0);
#else
#error TODO
#endif
    }
    
    // Context struct
    if (VType_Context != nil_vtype)
    {
        inter->common_globals.context = object_alloc(inter, VType_Context);
        Reference ref = inter->common_globals.context;
        global_save(inter, "context", ref);
        
        ref_member_set_string(inter, ref, "cd", yov->scripts[0].dir);
        ref_member_set_string(inter, ref, "script_dir", yov->scripts[0].dir);
        ref_member_set_string(inter, ref, "caller_dir", yov->caller_dir);
        
        // Args
        {
            Array<ScriptArg> args = yov->script_args;
            Reference array = alloc_array(inter, VType_String, args.count, true);
            foreach(i, args.count) {
                ref_set_member(inter, array, i, alloc_string(inter, args[i].name));
            }
            
            ref_set_member(inter, ref, vtype_get_member(VType_Context, "args").index, array);
        }
        
        // Types
        {
            Reference array = alloc_array(inter, VType_Type, yov->vtype_table.count, true);
            
            for (auto it = pooled_array_make_iterator(&yov->vtype_table); it.valid; ++it) {
                u32 index = it.index;
                Reference element = object_alloc(inter, VType_Type);
                ref_assign_Type(inter, element, it.value);
                ref_set_member(inter, array, it.index, element);
            }
            
            ref_set_member(inter, ref, vtype_get_member(VType_Context, "types").index, array);
        }
    }
    
    // Calls struct
    if (VType_CallsContext != nil_vtype)
    {
        Reference ref = object_alloc(inter, VType_CallsContext);
        inter->common_globals.calls = ref;
        global_save(inter, "calls", ref);
    }
    
    for (auto it = pooled_array_make_iterator(&yov->globals); it.valid; ++it)
    {
        ObjectDefinition* def = it.value;
        if (!is_null(global_get_by_index(inter, it.index))) continue;
        
        Reference ref = execute_ir_single_return(inter, def->ir, {}, NO_CODE);
        global_save_by_index(inter, it.index, ref);
        
    }
}

Interpreter* interpreter_initialize()
{
    YovScript* main_script = yov_get_script(0);
    
    Interpreter* inter = arena_push_struct<Interpreter>(yov->static_arena);
    
    inter->empty_op = arena_push_struct<OpNode>(yov->static_arena);
    
    inter->global_scope = arena_push_struct<Scope>(yov->static_arena);
    inter->current_scope = inter->global_scope;
    
    inter->globals = pooled_array_make<Reference>(yov->static_arena, 16);
    
    foreach(i, yov->globals.count) {
        array_add(&inter->globals, ref_from_object(null_obj));
    }
    
    define_globals(inter);
    
    return inter;
}

b32 interpreter_run(Interpreter* inter, FunctionDefinition* fn, Array<Value> params)
{
    object_free_unused_memory(inter);
    
    if (fn != NULL && fn->returns.count == 0 && fn->parameters.count == params.count) {
        run_function_call(inter, -1, fn, params, fn->code);
        return true;
    }
    
    invalid_codepath();
    return false;
}

void interpreter_run_main(Interpreter* inter)
{
    FunctionDefinition* fn = find_function("main");
    
    if (!interpreter_run(inter, fn, {})) {
        report_error(NO_CODE, "Main function not found");
    }
}

void interpreter_shutdown(Interpreter* inter)
{
    if (inter == NULL) return;
    
    foreach(i, inter->globals.count) {
        object_decrement_ref(inter->globals[i].parent);
    }
    
    scope_clear(inter, inter->global_scope);
    object_free_unused_memory(inter);
    
    if (inter->gc.object_count > 0) {
        lang_report_unfreed_objects();
    }
    else if (inter->gc.allocation_count > 0) {
        lang_report_unfreed_dynamic();
    }
    
    assert(yov->reports.count > 0 || inter->current_scope == inter->global_scope);
}

void interpreter_exit(Interpreter* inter, i64 exit_code)
{
    yov_set_exit_code(exit_code);
    yov->exit_requested = true;
}

void interpreter_report_runtime_error(Interpreter* inter, CodeLocation code, Result result) {
    report_error(code, result.message);
    interpreter_exit(inter, result.code);
}

Result user_assertion(Interpreter* inter, String message)
{
    if (!yov->settings.user_assert || yov_ask_yesno("User Assertion", message)) return RESULT_SUCCESS;
    return result_failed_make("Operation denied by user");
}

Reference get_cd(Interpreter* inter) {
    i32 index = vtype_get_member(VType_Context, "cd").index;
    return ref_get_member(inter, inter->common_globals.context, index);
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
    Reference calls = inter->common_globals.calls;
    VariableTypeChild info = vtype_get_member(calls.vtype, "redirect_stdout");
    Reference redirect_stdout = ref_get_member(inter, calls, info.index);
    return (RedirectStdout)get_enum_index(redirect_stdout);
}

void global_init(Interpreter* inter, ObjectDefinition def, CodeLocation code)
{
    Reference ref = execute_ir_single_return(inter, def.ir, {}, code);
    global_save(inter, def.name, ref);
}

void global_save(Interpreter* inter, String identifier, Reference ref)
{
    while (inter->globals.count < yov->globals.count) {
        array_add(&inter->globals, ref_from_object(null_obj));
    }
    
    foreach(i, yov->globals.count) {
        if (string_equals(yov->globals[i].name, identifier)) {
            global_save_by_index(inter, i, ref);
            return;
        }
    }
    invalid_codepath();
}

void global_save_by_index(Interpreter* inter, i32 index, Reference ref)
{
    if (index >= inter->globals.count) {
        invalid_codepath();
        return;
    }
    object_decrement_ref(inter->globals[index].parent);
    inter->globals[index] = ref;
    object_increment_ref(inter->globals[index].parent);
}

Reference global_get(Interpreter* inter, String identifier)
{
    foreach(i, yov->globals.count) {
        if (string_equals(yov->globals[i].name, identifier)) {
            return global_get_by_index(inter, i);
        }
    }
    return ref_from_object(nil_obj);
}

Reference global_get_by_index(Interpreter* inter, i32 index)
{
    if (index >= inter->globals.count) {
        invalid_codepath();
        return ref_from_object(null_obj);
    }
    return inter->globals[index];
}

Reference ref_from_value(Interpreter* inter, Scope* scope, Value value)
{
    SCRATCH();
    
    if (value.kind == ValueKind_None) return ref_from_object(null_obj);
    if (value.kind == ValueKind_Literal) {
        if (value.vtype->ID == VTypeID_Int) return alloc_int(inter, value.literal_int);
        if (value.vtype->ID == VTypeID_Bool) return alloc_bool(inter, value.literal_bool);
        if (value.vtype->ID == VTypeID_String) return alloc_string(inter, value.literal_string);
        if (value.vtype->ID == VTypeID_Void) return ref_from_object(null_obj);
        if (value.vtype == VType_Type) {
            Reference ref = object_alloc(inter, VType_Type);
            ref_assign_Type(inter, ref, value.literal_type);
            return ref;
        }
        if (value.vtype->kind == VariableKind_Enum) {
            return alloc_enum(inter, value.vtype, value.literal_int);
        }
        invalid_codepath();
        return ref_from_object(nil_obj);
    }
    
    if (value.kind == ValueKind_Array)
    {
        Array<Value> values = value.array.values;
        b32 is_empty = value.array.is_empty;
        
        if (is_empty)
        {
            Array<i64> dimensions = array_make<i64>(scratch.arena, values.count);
            foreach(i, dimensions.count) {
                dimensions[i] = get_int(ref_from_value(inter, scope, values[i]));
            }
            
            VariableType* base_vtype = value.vtype->child_base;
            Reference array = alloc_array_multidimensional(inter, base_vtype, dimensions);
            return array;
        }
        else
        {
            VariableType* element_vtype = value.vtype->child_next;
            Reference array = alloc_array(inter, element_vtype, values.count, true);
            
            foreach(i, values.count) {
                ref_set_member(inter, array, i, ref_from_value(inter, scope, values[i]));
            }
            return array;
        }
    }
    
    if (value.kind == ValueKind_Default) {
        return object_alloc(inter, value.vtype);
    }
    
    if (value.kind == ValueKind_StringComposition)
    {
        if (value.vtype->ID == VTypeID_String)
        {
            Array<Value> sources = value.string_composition;
            
            StringBuilder builder = string_builder_make(scratch.arena);
            foreach(i, sources.count)
            {
                Reference source = ref_from_value(inter, scope, sources[i]);
                if (is_unknown(source)) return ref_from_object(nil_obj);
                append(&builder, string_from_ref(scratch.arena, inter, source));
            }
            
            return alloc_string(inter, string_from_builder(scratch.arena, &builder));
        }
    }
    
    if (value.kind == ValueKind_MultipleReturn) {
        return ref_from_value(inter, scope, value.multiple_return[0]);
    }
    
    if (value.kind == ValueKind_Constant) {
        return global_get(inter, value.constant_identifier);
    }
    
    if (value.kind == ValueKind_LValue || value.kind == ValueKind_Register)
    {
        Reference ref = register_get(scope, value.reg.index);
        
        i32 op = value.reg.reference_op;
        
        while (op > 0) {
            ref = alloc_reference(inter, ref);
            op--;
        }
        
        while (op < 0)
        {
            if (is_null(ref)) {
                invalid_codepath();
                return ref_from_object(nil_obj);
            }
            
            ref = dereference(ref);
            op++;
        }
        
        return ref;
    }
    
    invalid_codepath();
    return ref_from_object(null_obj);
}

void execute_ir(Interpreter* inter, IR ir, Array<Reference> output, Array<Value> params, CodeLocation code)
{
    SCRATCH();
    
    assert(ir.param_count == params.count);
    
    Scope* scope = arena_push_struct<Scope>(scratch.arena);
    scope->registers = array_make<Reference>(scratch.arena, ir.registers.count);
    foreach(i, scope->registers.count)
    {
        IR_Register reg = ir.registers[i];
        Reference ref = ref_from_object(null_obj);
        
        if (reg.global_identifier.size > 0) {
            ref = global_get(inter, reg.global_identifier);
        }
        else if (reg.vtype->ID > VTypeID_Void) {
            ref = object_alloc(inter, reg.vtype);
        }
        
        register_save(inter, scope, i, ref);
    }
    
    scope->prev = inter->current_scope;
    inter->current_scope = scope;
    DEFER(inter->current_scope = scope->prev);
    
    // Define params
    foreach(i, params.count) {
        i32 reg = i;
        Value param = params[i];
        Reference ref = ref_from_value(inter, scope->prev, param);
        
        if (is_null(register_get(scope, reg)))
            register_save(inter, scope, reg, object_alloc(inter, ref.vtype));
        
        run_copy(inter, reg, ref, code);
    }
    
    u32 pc_decreased_count = 0;
    
    scope->pc = 0;
    
    while (scope->pc < ir.instructions.count && !yov->exit_requested)
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
        b32 show = false;
        
        u32 gc_allocations = inter->gc.allocation_count;
        u32 gc_objects = inter->gc.object_count;
        u64 start_time = os_timer_get();
#endif
        
        i64 prev_pc = scope->pc;
        run_instruction(inter, unit);
        
#if DEV
        gc_allocations = inter->gc.allocation_count - gc_allocations;
        gc_objects = inter->gc.object_count - gc_objects;
        f64 ellapsed_time = (os_timer_get() - start_time) / (f64)system_info.timer_frequency;
        
        show &= gc_objects > 0;
        //show &= (ellapsed_time * 1000000.0 > 100) && unit.kind != UnitKind_FunctionCall;
        
        if (show)
        {
            print_info("%S\n", string_from_unit(scratch.arena, scope->pc - 1, 3, 3, unit));
            
            String tab = "  ";
            print_info("%S ellapsed: %S\n", tab, string_from_ellapsed_time(scratch.arena, ellapsed_time));
            print_info("%S gc allocations: %u\n", tab, gc_allocations);
            print_info("%S gc objects: %u\n", tab, gc_objects);
            
            print_info("%S gc total objects: %u\n", tab, inter->gc.object_count);
            //print_info("%S temp memory: %l\n", tab, inter->temp_arena->memory_position);
            print_info("\n");
        }
#endif
        
        // Asan
#if DEV && 0
        arena_protect_and_reset(inter->temp_arena);
#endif
        
        if (prev_pc != scope->pc) {
            pc_decreased_count++;
            
            if ((pc_decreased_count + 1) % 32 == 0) {
                object_free_unused_memory(inter);
            }
        }
    }
    
    // Retrieve return value
    {
        Array<Value> returns = values_from_return(scratch.arena, ir.value);
        assert(output.count <= returns.count);
        
        foreach(i, MIN(output.count, returns.count)) {
            output[i] = ref_from_value(inter, scope, returns[i]);
        }
    }
    
    scope_clear(inter, scope);
}

Reference execute_ir_single_return(Interpreter* inter, IR ir, Array<Value> params, CodeLocation code)
{
    SCRATCH();
    Array<Reference> output = array_make<Reference>(scratch.arena, 1);
    execute_ir(inter, ir, output, params, code);
    return output[0];
}

void run_instruction(Interpreter* inter, Unit unit)
{
    CodeLocation code = unit.code;
    
    i32 dst_index = unit.dst_index;
    
    if (unit.kind == UnitKind_Copy) {
        Reference src = ref_from_value(inter, inter->current_scope, unit.copy.src);
        run_copy(inter, dst_index, src, code);
        return;
    }
    
    if (unit.kind == UnitKind_Store) {
        Reference src = ref_from_value(inter, inter->current_scope, unit.store.src);
        run_store(inter, dst_index, src, code);
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
        Reference src = ref_from_object(null_obj);
        i32 offset = unit.jump.offset;
        
        if (condition != 0) src = ref_from_value(inter, inter->current_scope, unit.jump.src);
        run_jump(inter, src, condition, offset, code);
        return;
    }
    
    if (unit.kind == UnitKind_BinaryOperation) {
        Reference dst = register_get(inter->current_scope, dst_index);
        Reference src0 = ref_from_value(inter, inter->current_scope, unit.binary_op.src0);
        Reference src1 = ref_from_value(inter, inter->current_scope, unit.binary_op.src1);
        BinaryOperator op = unit.binary_op.op;
        
        Reference result = run_binary_operation(inter, dst, src0, src1, op, code);
        
        if (result.address != dst.address)
        {
            if (dst.vtype == result.vtype) ref_copy(inter, dst, result);
            else register_save(inter, NULL, dst_index, result);
        }
        return;
    }
    
    if (unit.kind == UnitKind_SignOperation) {
        Reference src = ref_from_value(inter, inter->current_scope, unit.sign_op.src);
        BinaryOperator op = unit.sign_op.op;
        
        Reference result = run_sign_operation(inter, src, op, code);
        register_save(inter, NULL, dst_index, result);
        return;
    }
    
    if (unit.kind == UnitKind_Child)
    {
        SCRATCH();
        b32 child_is_member = unit.child.child_is_member;
        Reference child_index_obj = ref_from_value(inter, inter->current_scope, unit.child.child_index);
        Reference src = ref_from_value(inter, inter->current_scope, unit.child.src);
        
        if (is_unknown(src) || is_unknown(child_index_obj)) return;
        
        if (!is_int(child_index_obj)) {
            report_error(code, "Expecting an integer");
            return;
        }
        
        i64 child_index = get_int(child_index_obj);
        
        if (is_null(src)) {
            report_error(code, "Null reference");
            return;
        }
        
        u32 child_count = ref_get_child_count(src, child_is_member);
        
        if (child_index < 0 || child_index >= child_count) {
            report_error(code, "Out of bounds");
            return;
        }
        
        Reference child = ref_get_child(inter, src, (u32)child_index, child_is_member);
        register_save(inter, NULL, dst_index, child);
        
        return;
    }
    
    if (unit.kind == UnitKind_ResultEval)
    {
        Reference src = ref_from_value(inter, inter->current_scope, unit.result_eval.src);
        
        assert(src.vtype == VType_Result);
        
        if (src.vtype == VType_Result) {
            Result result = Result_from_ref(inter, src);
            if (result.failed) {
                interpreter_report_runtime_error(inter, code, result);
            }
        }
        return;
    }
    
    invalid_codepath();
}

void run_store(Interpreter* inter, i32 dst_index, Reference src, CodeLocation code)
{
    SCRATCH();
    if (is_unknown(src)) {
        invalid_codepath();
        return;
    }
    register_save(inter, NULL, dst_index, src);
}

void run_copy(Interpreter* inter, i32 dst_index, Reference src, CodeLocation code)
{
    SCRATCH();
    
    Reference dst = register_get(inter->current_scope, dst_index);
    
    if (is_unknown(dst) || is_unknown(src)) {
        invalid_codepath();
        return;
    }
    
    VariableType* src_vtype = src.vtype;
    
    if (is_null(dst) || is_null(src)) {
        report_error(code, "Null reference");
        return;
    }
    
    if (dst.vtype->kind == VariableKind_Reference && dst.vtype->child_next == src_vtype) {
        dst = dereference(dst);
        
        if (is_null(dst)) {
            report_error(code, "Null reference");
            return;
        }
    }
    
    ref_copy(inter, dst, src);
}


void run_function_call(Interpreter* inter, i32 dst_index, FunctionDefinition* fn, Array<Value> parameters, CodeLocation code)
{
    SCRATCH();
    
    Array<Reference> returns = array_make<Reference>(scratch.arena, fn->returns.count);
    
    if (fn->is_intrinsic)
    {
        if (fn->intrinsic.fn == NULL) {
            report_intrinsic_not_resolved(code, fn->identifier);
            return;
        }
        
        Array<Reference> params = array_make<Reference>(scratch.arena, parameters.count);
        foreach(i, params.count) {
            params[i] = ref_from_value(inter, inter->current_scope, parameters[i]);
        }
        
        fn->intrinsic.fn(inter, params, returns, code);
        
        foreach(i, returns.count) {
            assert(is_valid(returns[i]));
        }
    }
    else
    {
        execute_ir(inter, fn->defined.ir, returns, parameters, code);
    }
    
    if (dst_index >= 0) {
        foreach(i, returns.count) {
            i32 reg = dst_index + i;
            register_save(inter, NULL, reg, returns[i]);
        }
    }
}

void run_return(Interpreter* inter, CodeLocation code)
{
    Scope* scope = inter->current_scope;
    scope->return_requested = true;
}

void run_jump(Interpreter* inter, Reference ref, i32 condition, i32 offset, CodeLocation code)
{
    b32 jump = true;
    
    if (condition != 0)
    {
        if (is_unknown(ref)) return;
        
        if (!is_bool(ref)) {
            report_expr_expects_bool(code, "If-Statement");
            return;
        }
        
        jump = get_bool(ref);
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

Reference run_binary_operation(Interpreter* inter, Reference dst, Reference left, Reference right, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    VariableType* left_vtype = left.vtype;
    VariableType* right_vtype = right.vtype;
    
    b32 can_reuse_left = dst.address == left.address;
    
    if (is_reference(left) && left.vtype == right.vtype)
    {
        void* v0 = dereference(left).address;
        void* v1 = dereference(right).address;
        
        if (op == BinaryOperator_Equals) return alloc_bool(inter, v0 == v1);
        if (op == BinaryOperator_NotEquals) return alloc_bool(inter, v0 != v1);
    }
    
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
        if (op == BinaryOperator_Addition)
        {
            if (can_reuse_left)
            {
                ref_string_append(inter, dst, get_string(right));
                return dst;
            }
            else {
                String str = string_format(scratch.arena, "%S%S", get_string(left), get_string(right));
                return alloc_string(inter, str);
            }
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
        
        i64 left_id = get_int(ref_get_member(inter, left, index));
        i64 right_id = get_int(ref_get_member(inter, right, index));
        if (op == BinaryOperator_Equals) {
            return alloc_bool(inter, left_id == right_id);
        }
        else if (op == BinaryOperator_NotEquals) {
            return alloc_bool(inter, left_id == right_id);
        }
    }
    
    if (right_vtype->ID == VType_Type->ID)
    {
        i32 index = vtype_get_member(VType_Type, "ID").index;
        
        i64 type_id = get_int(ref_get_member(inter, right, index));
        
        if (op == BinaryOperator_Is) {
            return alloc_bool(inter, type_id == left.vtype->ID);
        }
    }
    
    if ((is_string(left) && is_int(right)) || (is_int(left) && is_string(right)))
    {
        if (op == BinaryOperator_Addition)
        {
            Reference string_ref = is_string(left) ? left : right;
            Reference codepoint_ref = is_int(left) ? left : right;
            
            String codepoint_str = string_from_codepoint(scratch.arena, (u32)get_int(codepoint_ref));
            
            if (can_reuse_left && is_string(dst))
            {
                ref_string_append(inter, dst, codepoint_str);
                return dst;
            }
            else {
                String left_str = is_string(left) ? get_string(left) : codepoint_str;
                String right_str = is_string(right) ? get_string(right) : codepoint_str;
                
                String str = string_format(scratch.arena, "%S%S", left_str, right_str);
                return alloc_string(inter, str);
            }
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
        
        i32 left_count = get_array(left)->count;
        i32 right_count = get_array(right)->count;
        
        if (op == BinaryOperator_Addition) {
            Reference array = alloc_array(inter, element_vtype, left_count + right_count, true);
            for (u32 i = 0; i < left_count; ++i) {
                Reference src = ref_get_child(inter, left, i, true);
                ref_set_member(inter, array, i, src);
            }
            for (u32 i = 0; i < right_count; ++i) {
                Reference src = ref_get_child(inter, right, i, true);
                ref_set_member(inter, array, left_count + i, src);
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
            return ref_from_object(nil_obj);
        }
        
        Reference array_src = (left_vtype->kind == VariableKind_Array) ? left : right;
        Reference element = (left_vtype->kind == VariableKind_Array) ? right : left;
        
        i32 array_src_count = get_array(array_src)->count;
        Reference array = alloc_array(inter, element_type, array_src_count + 1, true);
        
        i32 array_offset = (left_vtype->kind == VariableKind_Array) ? 0 : 1;
        
        for (i32 i = 0; i < array_src_count; ++i) {
            Reference src = ref_get_child(inter, array_src, i, true);
            ref_set_member(inter, array, i + array_offset, src);
        }
        
        i32 element_offset = (left_vtype->kind == VariableKind_Array) ? array_src_count : 0;
        ref_set_member(inter, array, element_offset, element);
        return array;
    }
    
    report_invalid_binary_op(code, left_vtype->name, string_from_binary_operator(op), right_vtype->name);
    invalid_codepath();
    return ref_from_object(nil_obj);
}

Reference run_sign_operation(Interpreter* inter, Reference ref, BinaryOperator op, CodeLocation code)
{
    SCRATCH();
    
    VariableType* vtype = ref.vtype;
    
    if (vtype->ID == VTypeID_Int) {
        if (op == BinaryOperator_Addition) return ref;
        if (op == BinaryOperator_Substraction) return alloc_int(inter, -get_int(ref));
    }
    
    if (vtype->ID == VTypeID_Bool) {
        if (op == BinaryOperator_LogicalNot) return alloc_bool(inter, !get_bool(ref));
    }
    
    return ref_from_object(nil_obj);
}

//- SCOPE

void scope_clear(Interpreter* inter, Scope* scope)
{
    if (scope == NULL) {
        lang_report_stack_is_broken();
        return;
    }
    
    foreach(i, scope->registers.count) {
        object_decrement_ref(scope->registers[i].parent);
    }
}

void register_save(Interpreter* inter, Scope* scope, i32 index, Reference ref)
{
    if (scope == NULL) scope = inter->current_scope;
    Reference* reg = &scope->registers[index];
    
    object_decrement_ref(reg->parent);
    *reg = ref;
    object_increment_ref(reg->parent);
}

Reference register_get(Scope* scope, i32 index) {
    return scope->registers[index];
}

//- OBJECT 

String string_from_object(Arena* arena, Interpreter* inter, Object* object, b32 raw) {
    return string_from_ref(arena, inter, ref_from_object(object), raw);
}

String string_from_ref(Arena* arena, Interpreter* inter, Reference ref, b32 raw)
{
    SCRATCH(arena);
    
    if (is_null(ref)) {
        return "null";
    }
    
    VariableType* vtype = ref.vtype;
    
    if (vtype->ID == VTypeID_String) {
        if (raw) return get_string(ref);
        return string_format(arena, "\"%S\"", get_string(ref));
    }
    if (vtype->ID == VTypeID_Int) { return string_format(arena, "%l", get_int(ref)); }
    if (vtype->ID == VTypeID_Bool) { return get_bool(ref) ? "true" : "false"; }
    if (vtype->ID == VTypeID_Void) { return "void"; }
    if (vtype->ID == VTypeID_Unknown) { return "unknown"; }
    
    if (vtype->kind == VariableKind_Array)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, "{ ");
        
        ObjectData_Array* array = get_array(ref);
        
        foreach(i, array->count) {
            Reference element = ref_get_member(inter, ref, i);
            append(&builder, string_from_ref(scratch.arena, inter, element, false));
            if (i < array->count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (vtype->kind == VariableKind_Enum)
    {
        i64 index = get_enum_index(ref);
        if (index < 0 || index >= vtype->_enum.names.count) return "?";
        String name = vtype->_enum.names[(u32)index];
        if (!raw) name = string_format(arena, "\"%S\"", name);
        return name;
    }
    
    if (vtype->kind == VariableKind_Struct)
    {
        StringBuilder builder = string_builder_make(scratch.arena);
        
        append(&builder, "{ ");
        
        foreach(i, vtype->_struct.vtypes.count)
        {
            String member_name = vtype->_struct.names[i];
            
            Reference member = ref_get_member(inter, ref, i);
            appendf(&builder, "%S = %S", member_name, string_from_ref(scratch.arena, inter, member, false));
            if (i < vtype->_struct.vtypes.count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (vtype->kind == VariableKind_Reference) {
        return string_from_ref(arena, inter, dereference(ref), raw);
    }
    
    invalid_codepath();
    return "?";
}

String string_from_compiletime(Arena* arena, Interpreter* inter, Value value, b32 raw)
{
    if (!value_is_compiletime(value)) {
        invalid_codepath();
        return {};
    }
    Reference ref = ref_from_value(inter, inter->global_scope, value);
    String str = string_from_ref(arena, inter, ref, raw);
    object_free_unused_memory(inter);
    return str;
}

b32 bool_from_compiletime(Interpreter* inter, Value value)
{
    if (!value_is_compiletime(value)) {
        invalid_codepath();
        return false;
    }
    Reference ref = ref_from_value(inter, inter->global_scope, value);
    if (ref.vtype->ID != VTypeID_Bool) {
        invalid_codepath();
        return false;
    }
    b32 res = get_bool(ref);
    object_free_unused_memory(inter);
    return res;
}

VariableType* type_from_compiletime(Interpreter* inter, Value value)
{
    if (!value_is_compiletime(value)) {
        invalid_codepath();
        return nil_vtype;
    }
    Reference ref = ref_from_value(inter, inter->global_scope, value);
    if (ref.vtype != VType_Type) {
        invalid_codepath();
        return nil_vtype;
    }
    
    VariableType* res = get_Type(inter, ref);
    object_free_unused_memory(inter);
    return res;
}

Reference ref_from_object(Object* object)
{
    Reference ref = {};
    ref.parent = object;
    ref.vtype = object->vtype;
    ref.address = object + 1;
    return ref;
}

Reference ref_from_address(Object* parent, VariableType* vtype, void* address)
{
    Reference member = {};
    member.parent = parent;
    member.vtype = vtype;
    member.address = address;
    return member;
}

void ref_set_member(Interpreter* inter, Reference ref, u32 index, Reference member)
{
    Reference dst = ref_get_member(inter, ref, index);
    ref_copy(inter, dst, member);
}

Reference ref_get_child(Interpreter* inter, Reference ref, u32 index, b32 is_member)
{
    if (is_member) {
        Reference child = ref_get_member(inter, ref, index);
        // TODO(Jose): What about memory requirements
        return child;
    }
    else return ref_get_property(inter, ref, index);
}

Reference ref_get_member(Interpreter* inter, Reference ref, u32 index)
{
    if (is_unknown(ref) || is_null(ref)) {
        invalid_codepath();
        return ref_from_object(nil_obj);
    }
    
    VariableType* vtype = ref.vtype;
    
    if (vtype->kind == VariableKind_Array)
    {
        ObjectData_Array* array = get_array(ref);
        
        if (index >= array->count) {
            invalid_codepath();
            return ref_from_object(nil_obj);
        }
        
        VariableType* element_vtype = ref.vtype->child_next;
        
        u32 offset = element_vtype->size * index;
        return ref_from_address(ref.parent, element_vtype, array->data + offset);
    }
    
    if (vtype->kind == VariableKind_Struct)
    {
        Array<VariableType*> vtypes = vtype->_struct.vtypes;
        
        if (index >= vtypes.count) {
            invalid_codepath();
            return ref_from_object(nil_obj);
        }
        
        u8* data = (u8*)ref.address;
        u32 offset = vtype->_struct.offsets[index];
        
        return ref_from_address(ref.parent, vtypes[index], data + offset);
    }
    
    invalid_codepath();
    return ref_from_object(nil_obj);
}

Reference ref_get_property(Interpreter* inter, Reference ref, u32 index)
{
    if (is_unknown(ref) || is_null(ref)) {
        invalid_codepath();
        return ref_from_object(nil_obj);
    }
    
    VariableType* vtype = ref.vtype;
    
    if (vtype->ID == VTypeID_String)
    {
        if (index == 0) return alloc_int(inter, get_string(ref).size);
    }
    
    if (vtype->kind == VariableKind_Array)
    {
        if (index == 0) return alloc_int(inter, get_array(ref)->count);
    }
    
    if (vtype->kind == VariableKind_Enum)
    {
        i64 v = get_enum_index(ref);
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
    
    invalid_codepath();
    return ref_from_object(nil_obj);
}

u32 ref_get_child_count(Reference ref, b32 is_member)
{
    if (is_member) return ref_get_member_count(ref);
    else return ref_get_property_count(ref);
}

u32 ref_get_property_count(Reference ref) {
    return vtype_get_properties(ref.vtype).count;
}

u32 ref_get_member_count(Reference ref)
{
    if (ref.vtype->kind == VariableKind_Array) {
        return get_array(ref)->count;
    }
    else if (ref.vtype->kind == VariableKind_Struct) {
        return ref.vtype->_struct.vtypes.count;
    }
    return 0;
}

Reference alloc_int(Interpreter* inter, i64 value)
{
    Reference ref = object_alloc(inter, VType_Int);
    set_int(ref, value);
    return ref;
}

Reference alloc_bool(Interpreter* inter, b32 value)
{
    Reference ref = object_alloc(inter, VType_Bool);
    set_bool(ref, value);
    return ref;
}

Reference alloc_string(Interpreter* inter, String value)
{
    Reference ref = object_alloc(inter, VType_String);
    ref_string_set(inter, ref, value);
    return ref;
}

Reference alloc_array(Interpreter* inter, VariableType* element_vtype, i64 count, b32 null_elements)
{
    VariableType* vtype = vtype_from_dimension(element_vtype, 1);
    
    if (vtype->kind != VariableKind_Array) {
        invalid_codepath();
        return ref_from_object(nil_obj);
    }
    
    Reference ref = object_alloc(inter, vtype);
    ObjectData_Array* array = get_array(ref);
    
    u32 element_size = element_vtype->size;
    
    array->data = (u8*)object_dynamic_allocate(inter, element_size * count);
    array->count = (u32)count;
    return ref;
}

Reference alloc_array_multidimensional(Interpreter* inter, VariableType* base_vtype, Array<i64> dimensions)
{
    if (dimensions.count <= 0) {
        invalid_codepath();
        return ref_from_object(nil_obj);
    }
    
    VariableType* vtype = vtype_from_dimension(base_vtype, dimensions.count);
    VariableType* element_vtype = vtype->child_next;
    
    if (dimensions.count == 1)
    {
        return alloc_array(inter, element_vtype, dimensions[0], false);
    }
    else
    {
        invalid_codepath();
#if 0// TODO(Jose): 
        Object_Array* obj = (Object_Array*)object_alloc(inter, vtype);
        
        u32 count = (u32)dimensions[0];
        Object** element_memory = (Object**)object_dynamic_allocate(inter, sizeof(Object*) * count);
        obj->elements = array_make(element_memory, count);
        
        foreach(i, obj->elements.count) {
            obj->elements[i] = alloc_array_multidimensional(inter, base_vtype, array_subarray(dimensions, 1, dimensions.count - 1));
            object_increment_ref(obj->elements[i]);
        }
        
        return obj;
#endif
        return ref_from_object(nil_obj);
    }
}

Reference alloc_array_from_enum(Interpreter* inter, VariableType* enum_vtype)
{
    if (enum_vtype->kind != VariableKind_Enum) {
        invalid_codepath();
        return ref_from_object(nil_obj);
    }
    
    invalid_codepath();
#if 0// TODO(Jose): 
    Object_Array* array = (Object_Array*)alloc_array(inter, enum_vtype, enum_vtype->_enum.values.count, false);
    foreach(i, array->elements.count) {
        Object* element = array->elements[i];
        set_enum_index(element, i);
    }
    return array;
#endif
    return ref_from_object(nil_obj);
}

Reference alloc_enum(Interpreter* inter, VariableType* vtype, i64 index)
{
    Reference ref = object_alloc(inter, vtype);
    set_enum_index(ref, index);
    return ref;
}

Reference alloc_reference(Interpreter* inter, Reference ref)
{
    Reference res = object_alloc(inter, vtype_from_reference(ref.vtype));
    set_reference(inter, res, ref);
    return res;
}

b32 is_valid(Reference ref) {
    return !is_unknown(ref);
}
b32 is_unknown(Reference ref) {
    if (ref.parent == NULL) return true;
    if (ref.vtype->ID == VTypeID_Unknown) return true;
    return ref.parent->vtype->ID == VTypeID_Unknown;
}

b32 is_const(Reference ref) {
    // TODO(Jose): return value.kind == ValueKind_LValue && value.lvalue.ref->constant;
    return false;
}

b32 is_null(Reference ref) {
    if (is_unknown(ref)) return true;
    return ref.vtype == void_vtype && ref.parent->vtype == void_vtype;
}

b32 is_int(Reference ref) { return is_valid(ref) && ref.vtype->ID == VTypeID_Int; }
b32 is_bool(Reference ref) { return is_valid(ref) && ref.vtype->ID == VTypeID_Bool; }
b32 is_string(Reference ref) { return is_valid(ref) && ref.vtype->ID == VTypeID_String; }

b32 is_array(Reference ref) {
    if (is_unknown(ref)) return false;
    return vtype_is_array(ref.vtype);
}

b32 is_enum(Reference ref) {
    if (is_unknown(ref)) return false;
    return vtype_is_enum(ref.vtype);
}

b32 is_reference(Reference ref) {
    if (is_unknown(ref)) return false;
    return vtype_is_reference(ref.vtype);
}

i64 get_int(Reference ref)
{
    if (!is_int(ref)) {
        invalid_codepath();
        return 0;
    }
    
    i64* data = (i64*)ref.address;
    assert(ref.vtype->size == sizeof(i64));
    return *data;
}

b32 get_bool(Reference ref)
{
    if (!is_bool(ref)) {
        invalid_codepath();
        return false;
    }
    
    b32* data = (b32*)ref.address;
    assert(ref.vtype->size == sizeof(b32));
    return *data;
}

i64 get_enum_index(Reference ref) {
    if (!is_enum(ref)) {
        invalid_codepath();
        return 0;
    }
    
    i64* data = (i64*)ref.address;
    assert(ref.vtype->size == sizeof(i64));
    return *data;
}

String get_string(Reference ref)
{
    if (!is_string(ref)) {
        invalid_codepath();
        return 0;
    }
    
    ObjectData_String* data = (ObjectData_String*)ref.address;
    assert(ref.vtype->size == sizeof(ObjectData_String));
    return string_make(data->chars, data->size);
}

ObjectData_Array* get_array(Reference ref)
{
    if (!is_array(ref)) {
        invalid_codepath();
        return {};
    }
    
    ObjectData_Array* array = (ObjectData_Array*)ref.address;
    assert(ref.vtype->size == sizeof(ObjectData_Array));
    return array;
}

Reference dereference(Reference ref)
{
    if (!is_reference(ref)) {
        invalid_codepath();
        return {};
    }
    
    ObjectData_Ref* data = (ObjectData_Ref*)ref.address;
    assert(ref.vtype->size == sizeof(ObjectData_Ref));
    
    if (data->parent == null_obj || data->parent == NULL) {
        return ref_from_object(null_obj);
    }
    
    Reference deref = {};
    deref.parent = data->parent;
    deref.address = data->address;
    deref.vtype = ref.vtype->child_next;
    return deref;
}

i64 get_int_member(Interpreter* inter, Reference ref, String member)
{
    i32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    return get_int(member_ref);
}

b32 get_bool_member(Interpreter* inter, Reference ref, String member)
{
    i32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    return get_bool(member_ref);
}

String get_string_member(Interpreter* inter, Reference ref, String member)
{
    i32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    return get_string(member_ref);
}

void set_int(Reference ref, i64 v)
{
    if (!is_int(ref)) {
        invalid_codepath();
        return;
    }
    
    i64* data = (i64*)ref.address;
    assert(ref.vtype->size == sizeof(i64));
    *data = v;
}

void set_bool(Reference ref, b32 v)
{
    if (!is_bool(ref)) {
        invalid_codepath();
        return;
    }
    
    b32* data = (b32*)ref.address;
    assert(ref.vtype->size == sizeof(b32));
    *data = v;
}

void set_enum_index(Reference ref, i64 v)
{
    if (!is_enum(ref)) {
        invalid_codepath();
        return;
    }
    
    i64* data = (i64*)ref.address;
    assert(ref.vtype->size == sizeof(i64));
    *data = v;
}

ObjectData_String* ref_string_get_data(Interpreter* inter, Reference ref)
{
    if (!is_string(ref)) {
        invalid_codepath();
        return NULL;
    }
    
    ObjectData_String* data = (ObjectData_String*)ref.address;
    assert(ref.vtype->size == sizeof(ObjectData_String));
    
    return data;
}

void ref_string_prepare(Interpreter* inter, Reference ref, u64 new_size, b32 can_discard)
{
    ObjectData_String* data = ref_string_get_data(inter, ref);
    if (data == NULL) return;
    
    if (data->capacity > 0 && new_size <= 0)
    {
        object_dynamic_free(inter, data->chars);
        *data = {};
        return;
    }
    
    if (new_size <= data->capacity) return;
    
    data->capacity = MAX(new_size, data->capacity * 2);
    
    char* old_chars = data->chars;
    char* new_chars = (char*)object_dynamic_allocate(inter, data->capacity);
    
    if (!can_discard) memory_copy(new_chars, old_chars, data->size);
    
    object_dynamic_free(inter, old_chars);
    data->chars = new_chars;
}

void ref_string_clear(Interpreter* inter, Reference ref)
{
    ObjectData_String* data = ref_string_get_data(inter, ref);
    if (data == NULL) return;
    object_dynamic_free(inter, data->chars);
    *data = {};
}

void ref_string_set(Interpreter* inter, Reference ref, String v)
{
    ObjectData_String* data = ref_string_get_data(inter, ref);
    if (data == NULL) return;
    
    ref_string_prepare(inter, ref, v.size, true);
    
    memory_copy(data->chars, v.data, v.size);
    data->size = v.size;
}

void ref_string_append(Interpreter* inter, Reference ref, String v)
{
    ObjectData_String* data = ref_string_get_data(inter, ref);
    if (data == NULL) return;
    
    u64 new_size = data->size + v.size;
    ref_string_prepare(inter, ref, new_size, false);
    
    memory_copy(data->chars + data->size, v.data, v.size);
    data->size = new_size;
}

void set_reference(Interpreter* inter, Reference ref, Reference src)
{
    if (!is_reference(ref) || (!is_null(src) && ref.vtype->child_next != src.vtype)) {
        invalid_codepath();
        return;
    }
    
    ObjectData_Ref* data = (ObjectData_Ref*)ref.address;
    assert(ref.vtype->size == sizeof(ObjectData_Ref));
    
    object_decrement_ref(data->parent);
    data->parent = src.parent;
    data->address = src.address;
    object_increment_ref(data->parent);
}

void set_int_member(Interpreter* inter, Reference ref, String member, i64 v)
{
    i32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) ref_set_member(inter, ref, index, alloc_int(inter, v));
    else set_int(member_ref, v);
}

void set_bool_member(Interpreter* inter, Reference ref, String member, b32 v)
{
    i32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) ref_set_member(inter, ref, index, alloc_bool(inter, v));
    else set_bool(member_ref, v);
}

void set_enum_index_member(Interpreter* inter, Reference ref, String member, i64 v)
{
    i32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) {
        VariableTypeChild info = vtype_get_member(ref.vtype, member);
        ref_set_member(inter, ref, index, alloc_enum(inter, info.vtype, v));
    }
    else set_enum_index(member_ref, v);
}

void ref_member_set_string(Interpreter* inter, Reference ref, String member, String v)
{
    i32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) ref_set_member(inter, ref, index, alloc_string(inter, v));
    else ref_string_set(inter, member_ref, v);
}

void ref_assign_Result(Interpreter* inter, Reference ref, Result res)
{
    assert(ref.vtype == VType_Result);
    ref_member_set_string(inter, ref, "message", res.message);
    set_int_member(inter, ref, "code", res.code);
    set_bool_member(inter, ref, "failed", res.failed);
}

void ref_assign_CallOutput(Interpreter* inter, Reference ref, CallOutput res)
{
    assert(ref.vtype == VType_CallOutput);
    ref_member_set_string(inter, ref, "stdout", res.stdout);
}

void ref_assign_FileInfo(Interpreter* inter, Reference ref, FileInfo info)
{
    assert(ref.vtype == VType_FileInfo);
    ref_member_set_string(inter, ref, "path", info.path);
    set_bool_member(inter, ref, "is_directory", info.is_directory);
}

void ref_assign_YovParseOutput(Interpreter* inter, Reference ref, Yov* temp_yov)
{
    assert(ref.vtype == VType_YovParseOutput);
    
    SCRATCH();
    PooledArray<Reference> functions = pooled_array_make<Reference>(scratch.arena, 8);
    PooledArray<Reference> structs = pooled_array_make<Reference>(scratch.arena, 8);
    PooledArray<Reference> enums = pooled_array_make<Reference>(scratch.arena, 8);
    PooledArray<Reference> globals = pooled_array_make<Reference>(scratch.arena, 8);
    PooledArray<Reference> reports = pooled_array_make<Reference>(scratch.arena, 8);
    
    for (auto it = pooled_array_make_iterator(&temp_yov->functions); it.valid; ++it) {
        FunctionDefinition* fn = it.value;
        
        Reference ref = object_alloc(inter, VType_FunctionDefinition);
        ref_assign_FunctionDefinition(inter, ref, fn);
        array_add(&functions, ref);
    }
    
    for (auto it = pooled_array_make_iterator(&temp_yov->vtype_table); it.valid; ++it) {
        VariableType* vtype = it.value;
        
        if (vtype->kind == VariableKind_Struct)
        {
            Reference ref = object_alloc(inter, VType_StructDefinition);
            ref_assign_StructDefinition(inter, ref, vtype);
            array_add(&structs, ref);
        }
        else if (vtype->kind == VariableKind_Enum)
        {
            Reference ref = object_alloc(inter, VType_EnumDefinition);
            ref_assign_EnumDefinition(inter, ref, vtype);
            array_add(&enums, ref);
        }
    }
    
    for (auto it = pooled_array_make_iterator(&temp_yov->globals); it.valid; ++it) {
        ObjectDefinition* def = it.value;
        if (string_starts(def->name, "$")) continue;
        
        Reference ref = object_alloc(inter, VType_ObjectDefinition);
        ref_assign_ObjectDefinition(inter, ref, *def);
        array_add(&globals, ref);
    }
    
    for (auto it = pooled_array_make_iterator(&temp_yov->reports); it.valid; ++it) {
        Report* report = it.value;
        Reference ref = alloc_string(inter, string_from_report(scratch.arena, *report));
        array_add(&reports, ref);
    }
    
    
    Reference functions_ref = alloc_array(inter, VType_FunctionDefinition, functions.count, true);
    for (auto it = pooled_array_make_iterator(&functions); it.valid; ++it) {
        ref_set_member(inter, functions_ref, it.index, *it.value);
    }
    
    Reference structs_ref = alloc_array(inter, VType_StructDefinition, structs.count, true);
    for (auto it = pooled_array_make_iterator(&structs); it.valid; ++it) {
        ref_set_member(inter, structs_ref, it.index, *it.value);
    }
    
    Reference enums_ref = alloc_array(inter, VType_EnumDefinition, enums.count, true);
    for (auto it = pooled_array_make_iterator(&enums); it.valid; ++it) {
        ref_set_member(inter, enums_ref, it.index, *it.value);
    }
    
    Reference globals_ref = alloc_array(inter, VType_ObjectDefinition, globals.count, true);
    for (auto it = pooled_array_make_iterator(&globals); it.valid; ++it) {
        ref_set_member(inter, globals_ref, it.index, *it.value);
    }
    
    Reference reports_ref = alloc_array(inter, VType_String, reports.count, true);
    for (auto it = pooled_array_make_iterator(&reports); it.valid; ++it) {
        ref_set_member(inter, reports_ref, it.index, *it.value);
    }
    
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "functions").index, functions_ref);
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "structs").index, structs_ref);
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "enums").index, enums_ref);
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "globals").index, globals_ref);
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "reports").index, reports_ref);
}

void ref_assign_FunctionDefinition(Interpreter* inter, Reference ref, FunctionDefinition* fn)
{
    ref_member_set_string(inter, ref, "identifier", fn->identifier);
    
    Reference parameters = alloc_array(inter, VType_ObjectDefinition, fn->parameters.count, false);
    foreach(i, fn->parameters.count) {
        Reference param = ref_get_member(inter, parameters, i);
        ref_assign_ObjectDefinition(inter, param, fn->parameters[i]);
    }
    
    Reference returns = alloc_array(inter, VType_ObjectDefinition, fn->returns.count, false);
    foreach(i, fn->returns.count) {
        Reference ret = ref_get_member(inter, returns, i);
        ref_assign_ObjectDefinition(inter, ret, fn->returns[i]);
    }
    
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "parameters").index, parameters);
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "returns").index, returns);
}

void ref_assign_StructDefinition(Interpreter* inter, Reference ref, VariableType* vtype)
{
    ref_member_set_string(inter, ref, "identifier", vtype->name);
    
    Reference members = alloc_array(inter, VType_ObjectDefinition, vtype->_struct.names.count, false);
    foreach(i, vtype->_struct.names.count) {
        Reference mem = ref_get_member(inter, members, i);
        String name = vtype->_struct.names[i];
        VariableType* mem_vtype = vtype->_struct.vtypes[i];
        ref_assign_ObjectDefinition(inter, mem, obj_def_make(name, mem_vtype));
    }
    
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "members").index, members);
}

void ref_assign_EnumDefinition(Interpreter* inter, Reference ref, VariableType* vtype)
{
    ref_member_set_string(inter, ref, "identifier", vtype->name);
    
    Reference elements = alloc_array(inter, VType_String, vtype->_enum.names.count, false);
    Reference values = alloc_array(inter, VType_Int, vtype->_enum.names.count, false);
    foreach(i, vtype->_enum.names.count) {
        Reference element = ref_get_member(inter, elements, i);
        Reference value = ref_get_member(inter, values, i);
        ref_string_set(inter, element, vtype->_enum.names[i]);
        set_int(value, vtype->_enum.values[i]);
    }
    
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "elements").index, elements);
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "values").index, values);
}

void ref_assign_ObjectDefinition(Interpreter* inter, Reference ref, ObjectDefinition def)
{
    ref_member_set_string(inter, ref, "identifier", def.name);
    
    VariableTypeChild type_info = vtype_get_member(ref.vtype, "type");
    Reference type = ref_get_member(inter, ref, type_info.index);
    ref_assign_Type(inter, type, def.vtype);
}

void ref_assign_Type(Interpreter* inter, Reference ref, VariableType* vtype)
{
    assert(ref.vtype == VType_Type);
    set_int_member(inter, ref, "ID", vtype->ID);
    ref_member_set_string(inter, ref, "name", vtype->name);
}

VariableType* get_Type(Interpreter* inter, Reference ref)
{
    i64 ID = get_int_member(inter, ref, "ID");
    return vtype_get(ID);
}

Reference ref_from_Result(Interpreter* inter, Result res)
{
    Reference ref = object_alloc(inter, VType_Result);
    ref_assign_Result(inter, ref, res);
    return ref;
}

Result Result_from_ref(Interpreter* inter, Reference ref)
{
    if (ref.vtype->ID != VType_Result->ID) {
        invalid_codepath();
        return {};
    }
    
    Result res;
    res.message = get_string_member(inter, ref, "message");
    res.code = (i32)get_int_member(inter, ref, "code");
    res.failed = get_bool_member(inter, ref, "failed");
    return res;
}

u32 object_generate_id(Interpreter* inter) {
    return ++inter->object_id_counter;
}

Reference object_alloc(Interpreter* inter, VariableType* vtype)
{
    SCRATCH();
    assert(vtype->ID > VTypeID_Void);
    
    u32 ID = object_generate_id(inter);
    
    log_mem_trace("Alloc obj(%u): %S\n", ID, string_from_vtype(scratch.arena, inter, vtype));
    
    u32 type_size = sizeof(Object) + vtype->size;
    
    Object* obj = NULL;
    
    obj = (Object*)object_dynamic_allocate(inter, type_size);
    
    b32 use_gc = true;
    
    if (use_gc) {
        obj->next = inter->gc.object_list;
        if (inter->gc.object_list) inter->gc.object_list->prev = obj;
        inter->gc.object_list = obj;
        inter->gc.object_count++;
    }
    
    obj->ID = ID;
    obj->vtype = vtype;
    obj->ref_count = 0;
    
    Reference ref = ref_from_object(obj);
    ref_init(inter, ref, {});
    return ref;
}

void object_free(Interpreter* inter, Object* obj)
{
    SCRATCH();
    assert(obj->ref_count == 0);
    
    log_mem_trace("Free obj(%u): %S\n", obj->ID, string_from_vtype(scratch.arena, inter, obj->vtype));
    
    b32 use_gc = true;
    
    if (use_gc)
    {
        if (obj == inter->gc.object_list)
        {
            assert(obj->prev == NULL);
            inter->gc.object_list = obj->next;
            if (inter->gc.object_list != NULL) inter->gc.object_list->prev = NULL;
        }
        else
        {
            if (obj->next != NULL) obj->next->prev = obj->prev;
            obj->prev->next = obj->next;
        }
        inter->gc.object_count--;
    }
    
    ref_release_internal(inter, ref_from_object(obj));
    
    *obj = {};
    
    if (use_gc) {
        object_dynamic_free(inter, obj);
    }
}

void object_increment_ref(Object* obj)
{
    if (obj == NULL || obj->vtype->ID == VTypeID_Unknown || obj->vtype->ID == VTypeID_Void) return;
    obj->ref_count++;
}

void object_decrement_ref(Object* obj)
{
    if (obj == NULL || obj->vtype->ID == VTypeID_Unknown || obj->vtype->ID == VTypeID_Void) return;
    obj->ref_count--;
    assert(obj->ref_count >= 0);
}

void ref_release_internal(Interpreter* inter, Reference ref)
{
    SCRATCH();
    
    VariableType* vtype = ref.vtype;
    
    if (!vtype_needs_internal_release(vtype)) return;
    
    if (vtype->kind == VariableKind_Array)
    {
        ObjectData_Array* array = get_array(ref);
        VariableType* element_vtype = vtype->child_next;
        
        if (vtype_needs_internal_release(element_vtype))
        {
            u32 element_size = element_vtype->size;
            
            u8* it = array->data;
            u8* end = array->data + element_size * array->count;
            
            while (it < end)
            {
                Reference member = ref_from_address(ref.parent, element_vtype, it);
                ref_release_internal(inter, member);
                it += element_size;
            }
        }
        
        object_dynamic_free(inter, array->data);
        *array = {};
    }
    else if (vtype->kind == VariableKind_Struct)
    {
        Array<VariableType*> vtypes = vtype->_struct.vtypes;
        
        foreach(i, vtypes.count)
        {
            u8* data = (u8*)ref.address;
            u32 offset = vtype->_struct.offsets[i];
            
            Reference member = ref_from_address(ref.parent, vtypes[i], data + offset);
            ref_release_internal(inter, member);
        }
    }
    else if (vtype->kind == VariableKind_Reference) {
        Reference deref = dereference(ref);
        object_decrement_ref(deref.parent);
    }
    else if (vtype->ID == VTypeID_String) {
        ref_string_clear(inter, ref);
    }
}

void ref_init(Interpreter* inter, Reference ref, IR ir)
{
    b32 has_initializer = ir.value.kind != ValueKind_None;
    
    if (has_initializer) {
        Reference ir_ref = execute_ir_single_return(inter, ir, {}, NO_CODE);
        ref_copy(inter, ref, ir_ref);
    }
    else
    {
        VariableType* vtype = ref.vtype;
        
        if (vtype->kind == VariableKind_Struct)
        {
            foreach(i, vtype->_struct.vtypes.count)
            {
                IR member_initialize_ir = vtype->_struct.irs[i];
                Reference member = ref_get_member(inter, ref, i);
                ref_init(inter, member, member_initialize_ir);
            }
        }
        else if (vtype->kind == VariableKind_Array) {
            
        }
        else if (vtype->kind == VariableKind_Enum) { }
        else if (vtype->kind == VariableKind_Primitive) { }
        else if (vtype->kind == VariableKind_Reference) {
            set_reference(inter, ref, ref_from_object(null_obj));
        }
        else {
            invalid_codepath();
        }
    }
}

void ref_copy(Interpreter* inter, Reference dst, Reference src)
{
    if (is_unknown(dst) || is_null(dst)) {
        invalid_codepath();
        return;
    }
    
    if (is_unknown(src) || is_null(src)) {
        invalid_codepath();
        return;
    }
    
    if (src.vtype != dst.vtype) {
        invalid_codepath();
        return;
    }
    
    VariableType* vtype = dst.vtype;
    
    if (vtype->kind == VariableKind_Struct)
    {
        foreach(i, vtype->_struct.vtypes.count) {
            Reference dst_mem = ref_get_member(inter, dst, i);
            Reference src_mem = ref_get_member(inter, src, i);
            ref_copy(inter, dst_mem, src_mem);
        }
    }
    else if (vtype->kind == VariableKind_Primitive || vtype->kind == VariableKind_Enum)
    {
        if (vtype->ID == VTypeID_String)
        {
            ref_string_set(inter, dst, get_string(src));
        }
        else {
            i64* v0 = (i64*)dst.address;
            i64* v1 = (i64*)src.address;
            memory_copy(dst.address, src.address, vtype->size);
        }
    }
    else if (vtype->kind == VariableKind_Array)
    {
        ref_release_internal(inter, dst);
        
        ObjectData_Array* dst_array = get_array(dst);
        ObjectData_Array* src_array = get_array(src);
        
        u32 element_size = vtype->child_next->size;
        dst_array->data = (u8*)object_dynamic_allocate(inter, src_array->count * element_size);
        dst_array->count = src_array->count;
        
        foreach(i, dst_array->count) {
            Reference dst_element = ref_get_member(inter, dst, i);
            Reference src_element = ref_get_member(inter, src, i);
            ref_copy(inter, dst_element, src_element);
        }
    }
    else if (vtype->kind == VariableKind_Reference)
    {
        Reference dst_deref = dereference(dst);
        Reference src_deref = dereference(src);
        
        object_decrement_ref(dst_deref.parent);
        object_increment_ref(src_deref.parent);
        memory_copy(dst.address, src.address, vtype->size);
    }
    else {
        invalid_codepath();
    }
}

Reference ref_alloc_and_copy(Interpreter* inter, Reference src)
{
    if (is_unknown(src)) {
        invalid_codepath();
        return ref_from_object(nil_obj);
    }
    
    if (is_null(src)) return ref_from_object(null_obj);
    
    Reference dst = object_alloc(inter, src.vtype);
    ref_copy(inter, dst, src);
    return dst;
}

void* object_dynamic_allocate(Interpreter* inter, u64 size)
{
    if (size == 0) return NULL;
    
    b32 use_gc = true;
    
    if (use_gc) {
        return gc_allocate(inter, size);
    }
    else {
#if 0
        Arena* arena = NULL;
        if (memory == ObjectMemory_Temp) arena = inter->temp_arena;
        else if (memory == ObjectMemory_Static) arena = yov->static_arena;
        else {
            invalid_codepath();
            return NULL;
        }
        
        return arena_push(arena, size);
#endif
    }
    
    return NULL;
}

void object_dynamic_free(Interpreter* inter, void* ptr)
{
    if (ptr == NULL) return;
    
    b32 use_gc = true;
    
    if (use_gc) {
        gc_free(inter, ptr);
    }
    else {
        //assert(memory != ObjectMemory_Static);
    }
}

void object_free_unused_memory(Interpreter* inter)
{
    //arena_pop_to(inter->temp_arena, 0);
    gc_free_unused(inter);
}

void* gc_allocate(Interpreter* inter, u64 size)
{
    inter->gc.allocation_count++;
    return os_allocate_heap(size);
}

void gc_free(Interpreter* inter, void* ptr)
{
    assert(inter->gc.allocation_count > 0);
    inter->gc.allocation_count--;
    os_free_heap(ptr);
}

void gc_free_unused(Interpreter* inter)
{
    u32 free_count;
    do {
        Object* obj = inter->gc.object_list;
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

void print_memory_usage(Interpreter* inter)
{
    print_separator();
    
#if 1
    Object* obj = inter->gc.object_list;
    while (obj != NULL)
    {
        Object* next = obj->next;
        log_mem_trace("Obj %u: %u refs\n", obj->ID, obj->ref_count);
        obj = next;
    }
#endif
    
    print_info("Object Count: %u\n", inter->gc.object_count);
    print_info("Alloc Count: %u\n", inter->gc.allocation_count);
    print_separator();
}