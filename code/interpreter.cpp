#include "inc.h"

void InitializeLanguageGlobals()
{
    Interpreter* inter = yov->inter;
    
    // Yov struct
    if (VType_YovInfo != VType_Nil)
    {
        inter->common_globals.yov = object_alloc(inter, VType_YovInfo);
        Reference ref = inter->common_globals.yov;
        GlobalSave("yov", ref);
        
        set_int_member(inter, ref, "minor", YOV_MINOR_VERSION);
        set_int_member(inter, ref, "major", YOV_MAJOR_VERSION);
        set_int_member(inter, ref, "revision", YOV_REVISION_VERSION);
        ref_member_set_string(inter, ref, "version", YOV_VERSION);
        ref_member_set_string(inter, ref, "path", os_get_executable_path(context.arena));
    }
    
    // OS struct
    if (VType_OS != VType_Nil)
    {
        inter->common_globals.os = object_alloc(inter, VType_OS);
        Reference ref = inter->common_globals.os;
        GlobalSave("os", ref);
        
#if OS_WINDOWS
        set_enum_index_member(inter, ref, "kind", 0);
#else
#error TODO
#endif
    }
    
    // Context struct
    if (VType_Context != VType_Nil)
    {
        inter->common_globals.context = object_alloc(inter, VType_Context);
        Reference ref = inter->common_globals.context;
        GlobalSave("context", ref);
        
        ref_member_set_string(inter, ref, "cd", yov->scripts[0].dir);
        ref_member_set_string(inter, ref, "script_dir", yov->scripts[0].dir);
        ref_member_set_string(inter, ref, "caller_dir", yov->caller_dir);
        
        // Args
        {
            Array<ScriptArg> args = yov->script_args;
            Reference array = alloc_array(inter, VType_String, args.count);
            foreach(i, args.count) {
                ref_set_member(inter, array, i, alloc_string(inter, args[i].name));
            }
            
            ref_set_member(inter, ref, vtype_get_member(VType_Context, "args").index, array);
        }
        
        // Types
        {
            Reference array = alloc_array(inter, VType_Type, yov->vtypes.count);
            
            foreach(i, yov->vtypes.count) {
                Reference element = object_alloc(inter, VType_Type);
                ref_assign_Type(inter, element, yov->vtypes[i]);
                ref_set_member(inter, array, i, element);
            }
            
            ref_set_member(inter, ref, vtype_get_member(VType_Context, "types").index, array);
        }
    }
    
    // Calls struct
    if (VType_CallsContext != VType_Nil)
    {
        Reference ref = object_alloc(inter, VType_CallsContext);
        inter->common_globals.calls = ref;
        GlobalSave("calls", ref);
    }
}

B32 interpreter_run(FunctionDefinition* fn, Array<Value> params)
{
    Interpreter* inter = yov->inter;
    
    object_free_unused_memory(inter);
    
    if (fn != NULL && fn->returns.count == 0 && fn->parameters.count == params.count) {
        run_function_call(inter, -1, fn, params, NO_CODE);
        return true;
    }
    
    return false;
}

void interpreter_run_main()
{
    FunctionDefinition* fn = FunctionFromIdentifier("main");
    
    if (!interpreter_run(fn, {})) {
        report_error(NO_CODE, "Main function not found");
    }
}

void interpreter_finish()
{
    Interpreter* inter = yov->inter;
    yov->inter = NULL;
    if (inter == NULL) return;
    
    ObjectFreeAll(inter);
    
#if DEV
    if (inter->gc.object_count > 0) {
        lang_report_unfreed_objects();
    }
    else if (inter->gc.allocation_count > 0) {
        lang_report_unfreed_dynamic();
    }
    Assert(yov->reports.count > 0 || inter->current_scope == inter->global_scope);
#endif
}

void interpreter_exit(Interpreter* inter, I64 exit_code)
{
    yov_set_exit_code(exit_code);
    yov->exit_requested = true;
}

void interpreter_report_runtime_error(Interpreter* inter, Location location, Result result) {
    report_error(location, result.message);
    interpreter_exit(inter, result.code);
}

Result user_assertion(Interpreter* inter, String message)
{
    if (!yov->settings.user_assert || yov_ask_yesno("User Assertion", message)) return RESULT_SUCCESS;
    return ResultMakeFailed("Operation denied by user");
}

Reference get_cd(Interpreter* inter) {
    I32 index = vtype_get_member(VType_Context, "cd").index;
    return ref_get_member(inter, inter->common_globals.context, index);
}

String get_cd_value(Interpreter* inter) {
    return get_string(get_cd(inter));
}

String path_absolute_to_cd(Arena* arena, Interpreter* inter, String path)
{
    String cd = get_cd_value(inter);
    if (!os_path_is_absolute(path)) path = path_resolve(context.arena, path_append(context.arena, cd, path));
    return StrCopy(arena, path);
}

RedirectStdout get_calls_redirect_stdout(Interpreter* inter)
{
    Reference calls = inter->common_globals.calls;
    VariableTypeChild info = vtype_get_member(calls.vtype, "redirect_stdout");
    Reference redirect_stdout = ref_get_member(inter, calls, info.index);
    return (RedirectStdout)get_enum_index(redirect_stdout);
}

Reference RefFromValue(Scope* scope, Value value)
{
    Interpreter* inter = yov->inter;
    
    if (value.kind == ValueKind_None) return ref_from_object(null_obj);
    if (value.kind == ValueKind_Literal) {
        if (value.vtype == VType_Int) return alloc_int(inter, value.literal_int);
        if (value.vtype == VType_Bool) return alloc_bool(inter, value.literal_bool);
        if (value.vtype == VType_String) return alloc_string(inter, value.literal_string);
        if (value.vtype == VType_Void) return ref_from_object(null_obj);
        if (value.vtype == VType_Type) {
            Reference ref = object_alloc(inter, VType_Type);
            ref_assign_Type(inter, ref, value.literal_type);
            return ref;
        }
        if (value.vtype.kind == VKind_Enum) {
            return alloc_enum(inter, value.vtype, value.literal_int);
        }
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    if (value.kind == ValueKind_Array)
    {
        Array<Value> values = value.array.values;
        B32 is_empty = value.array.is_empty;
        
        if (is_empty)
        {
            Array<I64> dimensions = array_make<I64>(context.arena, values.count);
            foreach(i, dimensions.count) {
                dimensions[i] = get_int(RefFromValue(scope, values[i]));
            }
            
            VType base_vtype = vtype_from_index(value.vtype.base_index);
            Reference array = alloc_array_multidimensional(inter, base_vtype, dimensions);
            return array;
        }
        else
        {
            VType element_vtype = VTypeNext(value.vtype);
            Reference array = alloc_array(inter, element_vtype, values.count);
            
            foreach(i, values.count) {
                ref_set_member(inter, array, i, RefFromValue(scope, values[i]));
            }
            return array;
        }
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return object_alloc(yov->inter, value.vtype);
    }
    
    if (value.kind == ValueKind_StringComposition)
    {
        if (value.vtype == VType_String)
        {
            Array<Value> sources = value.string_composition;
            
            StringBuilder builder = string_builder_make(context.arena);
            foreach(i, sources.count)
            {
                Reference source = RefFromValue(scope, sources[i]);
                if (is_unknown(source)) return ref_from_object(nil_obj);
                append(&builder, string_from_ref(context.arena, source));
            }
            
            return alloc_string(inter, string_from_builder(context.arena, &builder));
        }
    }
    
    if (value.kind == ValueKind_MultipleReturn) {
        return RefFromValue(scope, value.multiple_return[0]);
    }
    
    if (value.kind == ValueKind_LValue || value.kind == ValueKind_Register)
    {
        Reference ref = RegisterGet(scope, ValueGetRegister(value));
        
        I32 op = value.reg.reference_op;
        
        while (op > 0) {
            ref = alloc_reference(inter, ref);
            op--;
        }
        
        while (op < 0)
        {
            if (is_null(ref)) {
                InvalidCodepath();
                return ref_from_object(nil_obj);
            }
            
            ref = dereference(ref);
            op++;
        }
        
        return ref;
    }
    
    InvalidCodepath();
    return ref_from_object(null_obj);
}

void ExecuteIr(IR ir, Array<Reference> output, Array<Value> params, Location location)
{
    Interpreter* inter = yov->inter;
    
#if DEV && 0
    print_info("Globals:\n");
    foreach(i, inter->globals.count)
    {
        Reference ref = inter->globals[i].reference;
        String str = string_from_ref(context.arena, ref);
        print_info("%S = %S\n", inter->globals[i].identifier, str);
    }
    print_info("-----\n\n");
#endif
    
    Assert(ir.parameter_count == params.count);
    
    Scope* scope = ArenaPushStruct<Scope>(context.arena);
    scope->registers = array_make<Reference>(context.arena, ir.local_registers.count);
    foreach(i, scope->registers.count) {
        I32 register_index = RegIndexFromLocal(i);
        RegisterSave(scope, register_index, ref_from_object(null_obj));
    }
    
    scope->prev = inter->current_scope;
    inter->current_scope = scope;
    defer(inter->current_scope = scope->prev);
    
    // Define params
    {
        U32 param_index = 0;
        
        foreach(i, ir.local_registers.count)
        {
            Register reg = ir.local_registers[i];
            if (reg.kind != RegisterKind_Parameter) continue;
            
            I32 register_index = RegIndexFromLocal(i);
            Value param = params[param_index++];
            Reference ref = RefFromValue(scope->prev, param);
            
            if (is_null(RegisterGet(scope, register_index)))
                RegisterSave(scope, register_index, object_alloc(inter, ref.vtype));
            
            run_copy(inter, register_index, ref, location);
        }
    }
    
    U32 pc_decreased_count = 0;
    
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
        B32 show = false;
        
        U32 gc_allocations = inter->gc.allocation_count;
        U32 gc_objects = inter->gc.object_count;
        U64 start_time = OsTimerGet();
#endif
        
        I64 prev_pc = scope->pc;
        run_instruction(inter, unit);
        
#if DEV
        gc_allocations = inter->gc.allocation_count - gc_allocations;
        gc_objects = inter->gc.object_count - gc_objects;
        F64 ellapsed_time = (OsTimerGet() - start_time) / (F64)system_info.timer_frequency;
        
        show &= gc_objects > 0;
        //show &= (ellapsed_time * 1000000.0 > 100) && unit.kind != UnitKind_FunctionCall;
        
        if (show)
        {
            LogTrace("%S", StringFromUnit(context.arena, scope->pc - 1, 3, 3, unit));
            
            String tab = "  ";
            LogTrace("%S ellapsed: %S", tab, StringFromEllapsedTime(ellapsed_time));
            LogTrace("%S gc allocations: %u", tab, gc_allocations);
            LogTrace("%S gc objects: %u", tab, gc_objects);
            
            LogTrace("%S gc total objects: %u", tab, inter->gc.object_count);
            //print_info("%S temp memory: %l\n", tab, inter->temp_arena->memory_position);
        }
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
        Array<Value> returns = ValuesFromReturn(context.arena, ir.value, false);
        Assert(output.count <= returns.count);
        
        foreach(i, Min(output.count, returns.count)) {
            output[i] = RefFromValue(scope, returns[i]);
        }
    }
    
    scope_clear(inter, scope);
}

Reference ExecuteIrSingleReturn(IR ir, Array<Value> params, Location location)
{
    Array<Reference> output = array_make<Reference>(context.arena, 1);
    ExecuteIr(ir, output, params, location);
    return output[0];
}

void run_instruction(Interpreter* inter, Unit unit)
{
    Location location = unit.location;
    
    I32 dst_index = unit.dst_index;
    
    if (unit.kind == UnitKind_Copy) {
        Reference src = RefFromValue(inter->current_scope, unit.src);
        run_copy(inter, dst_index, src, location);
        return;
    }
    
    if (unit.kind == UnitKind_Store) {
        Reference src = RefFromValue(inter->current_scope, unit.src);
        run_store(inter, dst_index, src, location);
        return;
    }
    
    if (unit.kind == UnitKind_FunctionCall)
    {
        FunctionDefinition* fn = unit.function_call.fn;
        Array<Value> parameters = unit.function_call.parameters;
        run_function_call(inter, dst_index, fn, parameters, location);
        return;
    }
    
    if (unit.kind == UnitKind_Return) {
        run_return(inter, location);
        return;
    }
    
    if (unit.kind == UnitKind_Jump)
    {
        I32 condition = unit.jump.condition;
        Reference src = ref_from_object(null_obj);
        I32 offset = unit.jump.offset;
        
        if (condition != 0) src = RefFromValue(inter->current_scope, unit.src);
        run_jump(inter, src, condition, offset, location);
        return;
    }
    
    if (unit.kind == UnitKind_BinaryOperation) {
        Reference dst = RegisterGet(inter->current_scope, dst_index);
        Reference src0 = RefFromValue(inter->current_scope, unit.src);
        Reference src1 = RefFromValue(inter->current_scope, unit.binary_op.src1);
        BinaryOperator op = unit.binary_op.op;
        
        if (is_null(src0)) {
            report_error(location, "Null reference");
            return;
        }
        if (is_null(src1)) {
            report_error(location, "Null reference");
            return;
        }
        
        Reference result = run_binary_operation(inter, dst, src0, src1, op, location);
        
        if (result.address != dst.address)
        {
            if (dst.vtype == result.vtype) RefCopy(dst, result);
            else RegisterSave(NULL, dst_index, result);
        }
        return;
    }
    
    if (unit.kind == UnitKind_SignOperation) {
        Reference src = RefFromValue(inter->current_scope, unit.src);
        BinaryOperator op = unit.sign_op.op;
        
        if (is_null(src)) {
            report_error(location, "Null reference");
            return;
        }
        
        Reference result = run_sign_operation(inter, src, op, location);
        RegisterSave(NULL, dst_index, result);
        return;
    }
    
    if (unit.kind == UnitKind_Child)
    {
        B32 child_is_member = unit.child.child_is_member;
        Reference child_index_obj = RefFromValue(inter->current_scope, unit.child.child_index);
        Reference src = RefFromValue(inter->current_scope, unit.src);
        
        if (is_unknown(src) || is_unknown(child_index_obj)) return;
        
        if (!is_int(child_index_obj)) {
            report_error(location, "Expecting an integer");
            return;
        }
        
        I64 child_index = get_int(child_index_obj);
        
        if (is_null(src)) {
            report_error(location, "Null reference");
            return;
        }
        
        U32 child_count = ref_get_child_count(src, child_is_member);
        
        if (child_index < 0 || child_index >= child_count) {
            report_error(location, "Out of bounds");
            return;
        }
        
        Reference child = ref_get_child(inter, src, (U32)child_index, child_is_member);
        RegisterSave(NULL, dst_index, child);
        
        return;
    }
    
    if (unit.kind == UnitKind_ResultEval)
    {
        Reference src = RefFromValue(inter->current_scope, unit.src);
        
        Assert(src.vtype == VType_Result);
        
        if (src.vtype == VType_Result) {
            Result result = Result_from_ref(inter, src);
            if (result.failed) {
                interpreter_report_runtime_error(inter, location, result);
            }
        }
        return;
    }
    
    InvalidCodepath();
}

void run_store(Interpreter* inter, I32 dst_index, Reference src, Location location)
{
    if (is_unknown(src)) {
        InvalidCodepath();
        return;
    }
    RegisterSave(NULL, dst_index, src);
}

void run_copy(Interpreter* inter, I32 dst_index, Reference src, Location location)
{
    Reference dst = RegisterGet(inter->current_scope, dst_index);
    
    if (is_unknown(dst) || is_unknown(src)) {
        InvalidCodepath();
        return;
    }
    
    VType src_vtype = src.vtype;
    
    if (is_null(dst) || is_null(src)) {
        report_error(location, "Null reference");
        return;
    }
    
    if (dst.vtype.kind == VKind_Reference && VTypeNext(dst.vtype) == src_vtype) {
        dst = dereference(dst);
        
        if (is_null(dst)) {
            report_error(location, "Null reference");
            return;
        }
    }
    
    RefCopy(dst, src);
}


void run_function_call(Interpreter* inter, I32 dst_index, FunctionDefinition* fn, Array<Value> parameters, Location location)
{
    Array<Reference> returns = array_make<Reference>(context.arena, fn->returns.count);
    
    if (fn->is_intrinsic)
    {
        if (fn->intrinsic.fn == NULL) {
            report_intrinsic_not_resolved(location, fn->identifier);
            return;
        }
        
        Array<Reference> params = array_make<Reference>(context.arena, parameters.count);
        foreach(i, params.count) {
            params[i] = RefFromValue(inter->current_scope, parameters[i]);
        }
        
        fn->intrinsic.fn(inter, params, returns, location);
        
        foreach(i, returns.count) {
            Assert(is_valid(returns[i]));
        }
    }
    else
    {
        ExecuteIr(fn->defined.ir, returns, parameters, location);
    }
    
    if (dst_index >= 0) {
        foreach(i, returns.count) {
            I32 reg = dst_index + i;
            RegisterSave(NULL, reg, returns[i]);
        }
    }
}

void run_return(Interpreter* inter, Location location)
{
    Scope* scope = inter->current_scope;
    scope->return_requested = true;
}

void run_jump(Interpreter* inter, Reference ref, I32 condition, I32 offset, Location location)
{
    B32 jump = true;
    
    if (condition != 0)
    {
        if (is_unknown(ref)) return;
        
        if (!is_bool(ref)) {
            report_expr_expects_bool(location, StrFromCStr("If-Statement"));
            return;
        }
        
        jump = get_bool(ref);
        if (condition < 0) jump = !jump;
    }
    
    if (jump) {
        Scope* scope = inter->current_scope;
        if (scope == inter->global_scope) {
            InvalidCodepath();
            return;
        }
        
        scope->pc += offset;
    }
}

Reference run_binary_operation(Interpreter* inter, Reference dst, Reference left, Reference right, BinaryOperator op, Location location)
{
    VType left_vtype = left.vtype;
    VType right_vtype = right.vtype;
    
    B32 can_reuse_left = dst.address == left.address;
    
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
            I64 divisor = get_int(right);
            if (divisor == 0) {
                report_zero_division(location);
                return alloc_int(inter, I64_MAX);
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
                String str = StrFormat(context.arena, "%S%S", get_string(left), get_string(right));
                return alloc_string(inter, str);
            }
        }
        else if (op == BinaryOperator_Division)
        {
            if (os_path_is_absolute(get_string(right))) {
                report_right_path_cant_be_absolute(location);
                return left;
            }
            
            String str = path_append(context.arena, get_string(left), get_string(right));
            str = path_resolve(context.arena, str);
            
            return alloc_string(inter, str);
        }
        else if (op == BinaryOperator_Equals) {
            return alloc_bool(inter, (B8)StrEquals(get_string(left), get_string(right)));
        }
        else if (op == BinaryOperator_NotEquals) {
            return alloc_bool(inter, !(B8)StrEquals(get_string(left), get_string(right)));
        }
    }
    
    if (left_vtype == VType_Type && right_vtype == VType_Type)
    {
        I32 index = vtype_get_member(VType_Type, "name").index;
        
        String left_name = get_string(ref_get_member(inter, left, index));
        String right_name = get_string(ref_get_member(inter, right, index));
        
        VType left = vtype_from_name(left_name);
        VType right = vtype_from_name(right_name);
        
        if (op == BinaryOperator_Equals) {
            return alloc_bool(inter, left == right);
        }
        else if (op == BinaryOperator_NotEquals) {
            return alloc_bool(inter, left != right);
        }
    }
    
    if (right_vtype == VType_Type)
    {
        I32 index = vtype_get_member(VType_Type, "name").index;
        
        String name = get_string(ref_get_member(inter, right, index));
        
        VType type = vtype_from_name(name);
        
        if (op == BinaryOperator_Is) {
            return alloc_bool(inter, type == left.vtype);
        }
    }
    
    if ((is_string(left) && is_int(right)) || (is_int(left) && is_string(right)))
    {
        if (op == BinaryOperator_Addition)
        {
            Reference string_ref = is_string(left) ? left : right;
            Reference codepoint_ref = is_int(left) ? left : right;
            
            String codepoint_str = string_from_codepoint(context.arena, (U32)get_int(codepoint_ref));
            
            if (can_reuse_left && is_string(dst))
            {
                ref_string_append(inter, dst, codepoint_str);
                return dst;
            }
            else {
                String left_str = is_string(left) ? get_string(left) : codepoint_str;
                String right_str = is_string(right) ? get_string(right) : codepoint_str;
                
                String str = StrFormat(context.arena, "%S%S", left_str, right_str);
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
    
    if (left_vtype.kind == VKind_Array && right_vtype.kind == VKind_Array && VTypeNext(left_vtype) == VTypeNext(right_vtype))
    {
        VType element_vtype = VTypeNext(left_vtype);
        
        I32 left_count = get_array(left)->count;
        I32 right_count = get_array(right)->count;
        
        if (op == BinaryOperator_Addition) {
            Reference array = alloc_array(inter, element_vtype, left_count + right_count);
            for (U32 i = 0; i < left_count; ++i) {
                Reference src = ref_get_child(inter, left, i, true);
                ref_set_member(inter, array, i, src);
            }
            for (U32 i = 0; i < right_count; ++i) {
                Reference src = ref_get_child(inter, right, i, true);
                ref_set_member(inter, array, left_count + i, src);
            }
            return array;
        }
    }
    
    if ((left_vtype.kind == VKind_Array && right_vtype.kind != VKind_Array) || (left_vtype.kind != VKind_Array && right_vtype.kind == VKind_Array))
    {
        VType array_type = (left_vtype.kind == VKind_Array) ? left_vtype : right_vtype;
        VType element_type = (left_vtype.kind == VKind_Array) ? right_vtype : left_vtype;
        
        if (VTypeNext(array_type) != element_type) {
            report_type_missmatch_append(location, VTypeGetName(element_type), VTypeGetName(array_type));
            return ref_from_object(nil_obj);
        }
        
        Reference array_src = (left_vtype.kind == VKind_Array) ? left : right;
        Reference element = (left_vtype.kind == VKind_Array) ? right : left;
        
        I32 array_src_count = get_array(array_src)->count;
        Reference array = alloc_array(inter, element_type, array_src_count + 1);
        
        I32 array_offset = (left_vtype.kind == VKind_Array) ? 0 : 1;
        
        for (I32 i = 0; i < array_src_count; ++i) {
            Reference src = ref_get_child(inter, array_src, i, true);
            ref_set_member(inter, array, i + array_offset, src);
        }
        
        I32 element_offset = (left_vtype.kind == VKind_Array) ? array_src_count : 0;
        ref_set_member(inter, array, element_offset, element);
        return array;
    }
    
    report_invalid_binary_op(location, VTypeGetName(left_vtype), string_from_binary_operator(op), VTypeGetName(right_vtype));
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

Reference run_sign_operation(Interpreter* inter, Reference ref, BinaryOperator op, Location location)
{
    VType vtype = ref.vtype;
    
    if (vtype == VType_Int) {
        if (op == BinaryOperator_Addition) return ref;
        if (op == BinaryOperator_Substraction) return alloc_int(inter, -get_int(ref));
    }
    
    if (vtype == VType_Bool) {
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

void RegisterSave(Scope* scope, I32 register_index, Reference ref)
{
    Interpreter* inter = yov->inter;
    if (scope == NULL) scope = inter->current_scope;
    
    I32 local_index = LocalFromRegIndex(register_index);
    
    Reference* reg;
    
    if (local_index >= 0) {
        reg = &scope->registers[local_index];
    }
    else {
        reg = &yov->globals[register_index].reference;
    }
    
    if (is_unknown(ref)) {
        InvalidCodepath();
    }
    
    object_decrement_ref(reg->parent);
    *reg = ref;
    object_increment_ref(reg->parent);
}

void GlobalSave(String identifier, Reference ref)
{
    I32 global_index = GlobalIndexFromIdentifier(identifier);
    if (global_index < 0) {
        InvalidCodepath();
        return;
    }
    
    U32 register_index = RegIndexFromGlobal(global_index);
    RegisterSave(NULL, register_index, ref);
}

Reference RegisterGet(Scope* scope, I32 register_index)
{
    Interpreter* inter = yov->inter;
    if (scope == NULL) scope = inter->current_scope;
    I32 local_index = LocalFromRegIndex(register_index);
    
    if (local_index >= 0) {
        return scope->registers[local_index];
    }
    else {
        return yov->globals[register_index].reference;
    }
}

Reference GlobalGet(String identifier)
{
    I32 global_index = GlobalIndexFromIdentifier(identifier);
    if (global_index < 0) {
        return ref_from_object(null_obj);
    }
    return RegisterGet(NULL, RegIndexFromGlobal(global_index));
}

//- OBJECT 

String string_from_object(Arena* arena, Interpreter* inter, Object* object, B32 raw) {
    return string_from_ref(arena, ref_from_object(object), raw);
}

String string_from_ref(Arena* arena, Reference ref, B32 raw)
{
    if (is_null(ref)) {
        return "null";
    }
    
    VType vtype = ref.vtype;
    
    if (vtype == VType_String) {
        if (raw) return get_string(ref);
        return StrFormat(arena, "\"%S\"", get_string(ref));
    }
    if (vtype == VType_Int) { return StrFormat(arena, "%l", get_int(ref)); }
    if (vtype == VType_Bool) { return get_bool(ref) ? "true" : "false"; }
    if (vtype == VType_Void) { return "void"; }
    if (vtype == VType_Nil) { return "nil"; }
    
    if (vtype.kind == VKind_Array)
    {
        StringBuilder builder = string_builder_make(context.arena);
        
        append(&builder, "{ ");
        
        ObjectData_Array* array = get_array(ref);
        
        foreach(i, array->count) {
            Reference element = ref_get_member(yov->inter, ref, i);
            append(&builder, string_from_ref(context.arena, element, false));
            if (i < array->count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (vtype.kind == VKind_Enum)
    {
        I64 index = get_enum_index(ref);
        if (index < 0 || index >= vtype._enum->names.count) return "?";
        String name = vtype._enum->names[(U32)index];
        if (!raw) name = StrFormat(arena, "\"%S\"", name);
        return name;
    }
    
    if (vtype.kind == VKind_Struct)
    {
        StringBuilder builder = string_builder_make(context.arena);
        
        append(&builder, "{ ");
        
        foreach(i, vtype._struct->vtypes.count)
        {
            String member_name = vtype._struct->names[i];
            
            Reference member = ref_get_member(yov->inter, ref, i);
            appendf(&builder, "%S = %S", member_name, string_from_ref(context.arena, member, false));
            if (i < vtype._struct->vtypes.count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (vtype.kind == VKind_Reference) {
        U64 address = (U64)ref.address;
        return StrFormat(arena, "%l", address);
    }
    
    InvalidCodepath();
    return "?";
}

String StringFromCompiletime(Arena* arena, Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return false;
    }
    
    if (value.vtype == VType_String) {
        if (value.kind == ValueKind_Literal) {
            return value.literal_string;
        }
        
        if (value.kind == ValueKind_ZeroInit) {
            return {};
        }
    }
    else {
        return StrFromValue(arena, value, true);
    }
    
    InvalidCodepath();
    return false;
}

B32 B32FromCompiletime(Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return false;
    }
    
    if (value.vtype != VType_Bool) {
        InvalidCodepath();
        return false;
    }
    
    if (value.kind == ValueKind_Literal) {
        return value.literal_bool;
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return false;
    }
    
    InvalidCodepath();
    return false;
}

VType TypeFromCompiletime(Value value)
{
    if (!ValueIsCompiletime(value)) {
        InvalidCodepath();
        return VType_Void;
    }
    
    if (value.vtype != VType_Type) {
        InvalidCodepath();
        return VType_Void;
    }
    
    if (value.kind == ValueKind_Literal) {
        return value.literal_type;
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return VType_Void;
    }
    
    InvalidCodepath();
    return VType_Void;
}

Reference ref_from_object(Object* object)
{
    Reference ref = {};
    ref.parent = object;
    ref.vtype = object->vtype;
    ref.address = object + 1;
    return ref;
}

Reference ref_from_address(Object* parent, VType vtype, void* address)
{
    Reference member = {};
    member.parent = parent;
    member.vtype = vtype;
    member.address = address;
    return member;
}

void ref_set_member(Interpreter* inter, Reference ref, U32 index, Reference member)
{
    Reference dst = ref_get_member(inter, ref, index);
    RefCopy(dst, member);
}

Reference ref_get_child(Interpreter* inter, Reference ref, U32 index, B32 is_member)
{
    if (is_member) {
        Reference child = ref_get_member(inter, ref, index);
        // TODO(Jose): What about memory requirements
        return child;
    }
    else return ref_get_property(inter, ref, index);
}

Reference ref_get_member(Interpreter* inter, Reference ref, U32 index)
{
    if (is_unknown(ref) || is_null(ref)) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    VType vtype = ref.vtype;
    
    if (vtype.kind == VKind_Array)
    {
        ObjectData_Array* array = get_array(ref);
        
        if (index >= array->count) {
            InvalidCodepath();
            return ref_from_object(nil_obj);
        }
        
        VType element_vtype = VTypeNext(ref.vtype);
        
        U32 offset = VTypeGetSize(element_vtype) * index;
        return ref_from_address(ref.parent, element_vtype, array->data + offset);
    }
    
    if (vtype.kind == VKind_Struct)
    {
        Array<VType> vtypes = vtype._struct->vtypes;
        
        if (index >= vtypes.count) {
            InvalidCodepath();
            return ref_from_object(nil_obj);
        }
        
        U8* data = (U8*)ref.address;
        U32 offset = vtype._struct->offsets[index];
        
        return ref_from_address(ref.parent, vtypes[index], data + offset);
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

Reference ref_get_property(Interpreter* inter, Reference ref, U32 index)
{
    if (is_unknown(ref) || is_null(ref)) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    VType vtype = ref.vtype;
    
    if (vtype == VType_String)
    {
        if (index == 0) return alloc_int(inter, get_string(ref).size);
    }
    
    if (vtype.kind == VKind_Array)
    {
        if (index == 0) return alloc_int(inter, get_array(ref)->count);
    }
    
    if (vtype.kind == VKind_Enum)
    {
        I64 v = get_enum_index(ref);
        if (index == 0) return alloc_int(inter, v);
        if (index == 1) {
            if (v < 0 || v >= vtype._enum->values.count) return alloc_int(inter, -1);
            return alloc_int(inter, vtype._enum->values[v]);
        }
        if (index == 2) {
            if (v < 0 || v >= vtype._enum->names.count) return alloc_string(inter, "?");
            return alloc_string(inter, vtype._enum->names[v]);
        }
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

U32 ref_get_child_count(Reference ref, B32 is_member)
{
    if (is_member) return ref_get_member_count(ref);
    else return ref_get_property_count(ref);
}

U32 ref_get_property_count(Reference ref) {
    return VTypeGetProperties(ref.vtype).count;
}

U32 ref_get_member_count(Reference ref)
{
    if (ref.vtype.kind == VKind_Array) {
        return get_array(ref)->count;
    }
    else if (ref.vtype.kind == VKind_Struct) {
        return ref.vtype._struct->vtypes.count;
    }
    return 0;
}

Reference alloc_int(Interpreter* inter, I64 value)
{
    Reference ref = object_alloc(inter, VType_Int);
    set_int(ref, value);
    return ref;
}

Reference alloc_bool(Interpreter* inter, B32 value)
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

Reference alloc_array(Interpreter* inter, VType element_vtype, I64 count)
{
    VType vtype = vtype_from_dimension(element_vtype, 1);
    
    if (vtype.kind != VKind_Array) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    Reference ref = object_alloc(inter, vtype);
    ObjectData_Array* array = get_array(ref);
    
    U32 element_size = VTypeGetSize(element_vtype);
    
    array->data = (U8*)object_dynamic_allocate(inter, element_size * count);
    array->count = (U32)count;
    return ref;
}

Reference alloc_array_multidimensional(Interpreter* inter, VType base_vtype, Array<I64> dimensions)
{
    if (dimensions.count <= 0) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    VType vtype = vtype_from_dimension(base_vtype, dimensions.count);
    VType element_vtype = VTypeNext(vtype);
    
    if (dimensions.count == 1)
    {
        return alloc_array(inter, element_vtype, dimensions[0]);
    }
    else
    {
        U32 count = (U32)dimensions[0];
        Reference ref = alloc_array(inter, VTypeNext(vtype), count);
        
        foreach(i, count) {
            Reference element_src = alloc_array_multidimensional(inter, base_vtype, array_subarray(dimensions, 1, dimensions.count - 1));
            Reference element_dst = ref_get_child(inter, ref, i, true);
            RefCopy(element_dst, element_src);
        }
        
        return ref;
    }
}

Reference alloc_array_from_enum(Interpreter* inter, VType enum_vtype)
{
    if (enum_vtype.kind != VKind_Enum) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    InvalidCodepath();
#if 0// TODO(Jose): 
    Object_Array* array = (Object_Array*)alloc_array(inter, enum_vtype, enum_vtype._enum->values.count, false);
    foreach(i, array->elements.count) {
        Object* element = array->elements[i];
        set_enum_index(element, i);
    }
    return array;
#endif
    return ref_from_object(nil_obj);
}

Reference alloc_enum(Interpreter* inter, VType vtype, I64 index)
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

B32 is_valid(Reference ref) {
    return !is_unknown(ref);
}
B32 is_unknown(Reference ref) {
    if (ref.parent == NULL) return true;
    if (ref.vtype == VType_Nil) return true;
    return ref.parent->vtype == VType_Nil;
}

B32 is_const(Reference ref) {
    // TODO(Jose): return value.kind == ValueKind_LValue && value.lvalue.ref->constant;
    return false;
}

B32 is_null(Reference ref) {
    if (is_unknown(ref)) return true;
    return ref.vtype == VType_Void && ref.parent->vtype == VType_Void;
}

B32 is_int(Reference ref) { return is_valid(ref) && ref.vtype == VType_Int; }
B32 is_bool(Reference ref) { return is_valid(ref) && ref.vtype == VType_Bool; }
B32 is_string(Reference ref) { return is_valid(ref) && ref.vtype == VType_String; }

B32 is_array(Reference ref) {
    if (is_unknown(ref)) return false;
    return vtype_is_array(ref.vtype);
}

B32 is_enum(Reference ref) {
    if (is_unknown(ref)) return false;
    return vtype_is_enum(ref.vtype);
}

B32 is_reference(Reference ref) {
    if (is_unknown(ref)) return false;
    return vtype_is_reference(ref.vtype);
}

I64 get_int(Reference ref)
{
    if (!is_int(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    I64* data = (I64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(I64));
    return *data;
}

B32 get_bool(Reference ref)
{
    if (!is_bool(ref)) {
        InvalidCodepath();
        return false;
    }
    
    B32* data = (B32*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(B32));
    return *data;
}

I64 get_enum_index(Reference ref) {
    if (!is_enum(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    I64* data = (I64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(I64));
    return *data;
}

String get_string(Reference ref)
{
    if (!is_string(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    ObjectData_String* data = (ObjectData_String*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_String));
    return StrMake(data->chars, data->size);
}

ObjectData_Array* get_array(Reference ref)
{
    if (!is_array(ref)) {
        InvalidCodepath();
        return {};
    }
    
    ObjectData_Array* array = (ObjectData_Array*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_Array));
    return array;
}

Reference dereference(Reference ref)
{
    if (!is_reference(ref)) {
        InvalidCodepath();
        return {};
    }
    
    ObjectData_Ref* data = (ObjectData_Ref*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_Ref));
    
    if (data->parent == null_obj || data->parent == NULL) {
        return ref_from_object(null_obj);
    }
    
    Reference deref = {};
    deref.parent = data->parent;
    deref.address = data->address;
    deref.vtype = VTypeNext(ref.vtype);
    return deref;
}

I64 get_int_member(Interpreter* inter, Reference ref, String member)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    return get_int(member_ref);
}

B32 get_bool_member(Interpreter* inter, Reference ref, String member)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    return get_bool(member_ref);
}

String get_string_member(Interpreter* inter, Reference ref, String member)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    return get_string(member_ref);
}

void set_int(Reference ref, I64 v)
{
    if (!is_int(ref)) {
        InvalidCodepath();
        return;
    }
    
    I64* data = (I64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(I64));
    *data = v;
}

void set_bool(Reference ref, B32 v)
{
    if (!is_bool(ref)) {
        InvalidCodepath();
        return;
    }
    
    B32* data = (B32*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(B32));
    *data = v;
}

void set_enum_index(Reference ref, I64 v)
{
    if (!is_enum(ref)) {
        InvalidCodepath();
        return;
    }
    
    I64* data = (I64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(I64));
    *data = v;
}

ObjectData_String* ref_string_get_data(Interpreter* inter, Reference ref)
{
    if (!is_string(ref)) {
        InvalidCodepath();
        return NULL;
    }
    
    ObjectData_String* data = (ObjectData_String*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_String));
    
    return data;
}

void ref_string_prepare(Interpreter* inter, Reference ref, U64 new_size, B32 can_discard)
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
    
    data->capacity = Max(new_size, data->capacity * 2);
    
    char* old_chars = data->chars;
    char* new_chars = (char*)object_dynamic_allocate(inter, data->capacity);
    
    if (!can_discard) MemoryCopy(new_chars, old_chars, data->size);
    
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
    
    MemoryCopy(data->chars, v.data, v.size);
    data->size = v.size;
}

void ref_string_append(Interpreter* inter, Reference ref, String v)
{
    ObjectData_String* data = ref_string_get_data(inter, ref);
    if (data == NULL) return;
    
    U64 new_size = data->size + v.size;
    ref_string_prepare(inter, ref, new_size, false);
    
    MemoryCopy(data->chars + data->size, v.data, v.size);
    data->size = new_size;
}

void set_reference(Interpreter* inter, Reference ref, Reference src)
{
    if (!is_reference(ref) || (!is_null(src) && VTypeNext(ref.vtype) != src.vtype)) {
        InvalidCodepath();
        return;
    }
    
    ObjectData_Ref* data = (ObjectData_Ref*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_Ref));
    
    object_decrement_ref(data->parent);
    data->parent = src.parent;
    data->address = src.address;
    object_increment_ref(data->parent);
}

void set_int_member(Interpreter* inter, Reference ref, String member, I64 v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) ref_set_member(inter, ref, index, alloc_int(inter, v));
    else set_int(member_ref, v);
}

void ref_member_set_bool(Interpreter* inter, Reference ref, String member, B32 v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) ref_set_member(inter, ref, index, alloc_bool(inter, v));
    else set_bool(member_ref, v);
}

void set_enum_index_member(Interpreter* inter, Reference ref, String member, I64 v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
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
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(inter, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) ref_set_member(inter, ref, index, alloc_string(inter, v));
    else ref_string_set(inter, member_ref, v);
}

void ref_assign_Result(Interpreter* inter, Reference ref, Result res)
{
    Assert(ref.vtype == VType_Result);
    ref_member_set_string(inter, ref, "message", res.message);
    set_int_member(inter, ref, "code", res.code);
    ref_member_set_bool(inter, ref, "failed", res.failed);
}

void ref_assign_CallOutput(Interpreter* inter, Reference ref, CallOutput res)
{
    Assert(ref.vtype == VType_CallOutput);
    ref_member_set_string(inter, ref, "stdout", res.stdout);
}

void ref_assign_FileInfo(Interpreter* inter, Reference ref, FileInfo info)
{
    Assert(ref.vtype == VType_FileInfo);
    ref_member_set_string(inter, ref, "path", info.path);
    ref_member_set_bool(inter, ref, "is_directory", info.is_directory);
}

void ref_assign_FunctionDefinition(Interpreter* inter, Reference ref, FunctionDefinition* fn)
{
    ref_member_set_string(inter, ref, "identifier", fn->identifier);
    
    Reference parameters = alloc_array(inter, VType_ObjectDefinition, fn->parameters.count);
    foreach(i, fn->parameters.count) {
        Reference param = ref_get_member(inter, parameters, i);
        ref_assign_ObjectDefinition(inter, param, fn->parameters[i]);
    }
    
    Reference returns = alloc_array(inter, VType_ObjectDefinition, fn->returns.count);
    foreach(i, fn->returns.count) {
        Reference ret = ref_get_member(inter, returns, i);
        ref_assign_ObjectDefinition(inter, ret, fn->returns[i]);
    }
    
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "parameters").index, parameters);
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "returns").index, returns);
}

void ref_assign_StructDefinition(Interpreter* inter, Reference ref, VType vtype)
{
    ref_member_set_string(inter, ref, "identifier", VTypeGetName(vtype));
    
    Reference members = alloc_array(inter, VType_ObjectDefinition, vtype._struct->names.count);
    foreach(i, vtype._struct->names.count) {
        Reference mem = ref_get_member(inter, members, i);
        String name = vtype._struct->names[i];
        VType mem_vtype = vtype._struct->vtypes[i];
        ref_assign_ObjectDefinition(inter, mem, ObjDefMake(name, mem_vtype, NO_CODE, false, ValueFromZero(mem_vtype)));
    }
    
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "members").index, members);
}

void ref_assign_EnumDefinition(Interpreter* inter, Reference ref, VType vtype)
{
    ref_member_set_string(inter, ref, "identifier", VTypeGetName(vtype));
    
    Reference elements = alloc_array(inter, VType_String, vtype._enum->names.count);
    Reference values = alloc_array(inter, VType_Int, vtype._enum->names.count);
    foreach(i, vtype._enum->names.count) {
        Reference element = ref_get_member(inter, elements, i);
        Reference value = ref_get_member(inter, values, i);
        ref_string_set(inter, element, vtype._enum->names[i]);
        set_int(value, vtype._enum->values[i]);
    }
    
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "elements").index, elements);
    ref_set_member(inter, ref, vtype_get_child(ref.vtype, "values").index, values);
}

void ref_assign_ObjectDefinition(Interpreter* inter, Reference ref, ObjectDefinition def)
{
    ref_member_set_string(inter, ref, "identifier", def.name);
    ref_member_set_bool(inter, ref, "is_constant", def.is_constant);
    
    VariableTypeChild type_info = vtype_get_member(ref.vtype, "type");
    Reference type = ref_get_member(inter, ref, type_info.index);
    ref_assign_Type(inter, type, def.vtype);
}

void ref_assign_Type(Interpreter* inter, Reference ref, VType vtype)
{
    Assert(ref.vtype == VType_Type);
    ref_member_set_string(inter, ref, "name", VTypeGetName(vtype));
}

VType get_Type(Interpreter* inter, Reference ref)
{
    String name = get_string_member(inter, ref, "name");
    return vtype_from_name(name);
}

Reference ref_from_Result(Interpreter* inter, Result res)
{
    Reference ref = object_alloc(inter, VType_Result);
    ref_assign_Result(inter, ref, res);
    return ref;
}

Result Result_from_ref(Interpreter* inter, Reference ref)
{
    if (ref.vtype != VType_Result) {
        InvalidCodepath();
        return {};
    }
    
    Result res;
    res.message = get_string_member(inter, ref, "message");
    res.code = (I32)get_int_member(inter, ref, "code");
    res.failed = get_bool_member(inter, ref, "failed");
    return res;
}

U32 object_generate_id(Interpreter* inter) {
    return ++inter->object_id_counter;
}

Reference object_alloc(Interpreter* inter, VType vtype)
{
    Assert(VTypeValid(vtype));
    
    U32 ID = object_generate_id(inter);
    
    LogMemory("Alloc obj(%u): %S", ID, VTypeGetName(vtype));
    
    Assert(VTypeGetSize(vtype) > 0);
    U32 type_size = sizeof(Object) + VTypeGetSize(vtype);
    
    Object* obj = NULL;
    
    obj = (Object*)object_dynamic_allocate(inter, type_size);
    
    B32 use_gc = true;
    
    if (use_gc) {
        obj->next = inter->gc.object_list;
        if (inter->gc.object_list) inter->gc.object_list->prev = obj;
        inter->gc.object_list = obj;
        inter->gc.object_count++;
    }
    
    obj->ID = ID;
    obj->vtype = vtype;
    obj->ref_count = 0;
    
    return ref_from_object(obj);
}

void object_free(Interpreter* inter, Object* obj, B32 release_internal_refs)
{
    Assert(obj->ref_count == 0);
    
    LogMemory("Free obj(%u): %S", obj->ID, VTypeGetName(obj->vtype));
    
    B32 use_gc = true;
    
    if (use_gc)
    {
        if (obj == inter->gc.object_list)
        {
            Assert(obj->prev == NULL);
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
    
    ref_release_internal(inter, ref_from_object(obj), release_internal_refs);
    
    *obj = {};
    
    if (use_gc) {
        object_dynamic_free(inter, obj);
    }
}

void object_increment_ref(Object* obj)
{
    if (obj == NULL || obj->vtype == VType_Nil || obj->vtype == VType_Void) return;
    obj->ref_count++;
}

void object_decrement_ref(Object* obj)
{
    if (obj == NULL || obj->vtype == VType_Nil || obj->vtype == VType_Void) return;
    obj->ref_count--;
    Assert(obj->ref_count >= 0);
}

void ref_release_internal(Interpreter* inter, Reference ref, B32 release_refs)
{
    VType vtype = ref.vtype;
    
    if (!VTypeNeedsInternalRelease(vtype)) return;
    
    if (vtype.kind == VKind_Array)
    {
        ObjectData_Array* array = get_array(ref);
        VType element_vtype = VTypeNext(vtype);
        
        if (VTypeNeedsInternalRelease(element_vtype))
        {
            U32 element_size = VTypeGetSize(element_vtype);
            
            U8* it = array->data;
            U8* end = array->data + element_size * array->count;
            
            while (it < end)
            {
                Reference member = ref_from_address(ref.parent, element_vtype, it);
                ref_release_internal(inter, member, release_refs);
                it += element_size;
            }
        }
        
        object_dynamic_free(inter, array->data);
        *array = {};
    }
    else if (vtype.kind == VKind_Struct)
    {
        Array<VType> vtypes = vtype._struct->vtypes;
        
        foreach(i, vtypes.count)
        {
            U8* data = (U8*)ref.address;
            U32 offset = vtype._struct->offsets[i];
            
            Reference member = ref_from_address(ref.parent, vtypes[i], data + offset);
            ref_release_internal(inter, member, release_refs);
        }
    }
    else if (vtype.kind == VKind_Reference && release_refs) {
        Reference deref = dereference(ref);
        object_decrement_ref(deref.parent);
    }
    else if (vtype == VType_String) {
        ref_string_clear(inter, ref);
    }
}

void RefCopy(Reference dst, Reference src)
{
    Interpreter* inter = yov->inter;
    
    if (is_unknown(dst) || is_null(dst)) {
        InvalidCodepath();
        return;
    }
    
    if (is_unknown(src) || is_null(src)) {
        InvalidCodepath();
        return;
    }
    
    if (src.vtype != dst.vtype) {
        InvalidCodepath();
        return;
    }
    
    VType vtype = dst.vtype;
    
    if (vtype.kind == VKind_Struct)
    {
        foreach(i, vtype._struct->vtypes.count) {
            Reference dst_mem = ref_get_member(inter, dst, i);
            Reference src_mem = ref_get_member(inter, src, i);
            RefCopy(dst_mem, src_mem);
        }
    }
    else if (vtype.kind == VKind_Primitive || vtype.kind == VKind_Enum)
    {
        if (vtype == VType_String)
        {
            ref_string_set(inter, dst, get_string(src));
        }
        else {
            I64* v0 = (I64*)dst.address;
            I64* v1 = (I64*)src.address;
            MemoryCopy(dst.address, src.address, VTypeGetSize(vtype));
        }
    }
    else if (vtype.kind == VKind_Array)
    {
        ref_release_internal(inter, dst, true);
        
        ObjectData_Array* dst_array = get_array(dst);
        ObjectData_Array* src_array = get_array(src);
        
        U32 element_size = VTypeGetSize(VTypeNext(vtype));
        dst_array->data = (U8*)object_dynamic_allocate(inter, src_array->count * element_size);
        dst_array->count = src_array->count;
        
        foreach(i, dst_array->count) {
            Reference dst_element = ref_get_member(inter, dst, i);
            Reference src_element = ref_get_member(inter, src, i);
            RefCopy(dst_element, src_element);
        }
    }
    else if (vtype.kind == VKind_Reference)
    {
        Reference dst_deref = dereference(dst);
        Reference src_deref = dereference(src);
        
        object_decrement_ref(dst_deref.parent);
        object_increment_ref(src_deref.parent);
        MemoryCopy(dst.address, src.address, VTypeGetSize(vtype));
    }
    else {
        InvalidCodepath();
    }
}

Reference ref_alloc_and_copy(Interpreter* inter, Reference src)
{
    if (is_unknown(src)) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    if (is_null(src)) return ref_from_object(null_obj);
    
    Reference dst = object_alloc(inter, src.vtype);
    RefCopy(dst, src);
    return dst;
}

void* object_dynamic_allocate(Interpreter* inter, U64 size)
{
    if (size == 0) return NULL;
    
    B32 use_gc = true;
    
    if (use_gc) {
        return gc_allocate(inter, size);
    }
    else {
#if 0
        Arena* arena = NULL;
        if (memory == ObjectMemory_Temp) arena = inter->temp_arena;
        else if (memory == ObjectMemory_Static) arena = yov->static_arena;
        else {
            InvalidCodepath();
            return NULL;
        }
        
        return ArenaPush(arena, size);
#endif
    }
    
    return NULL;
}

void object_dynamic_free(Interpreter* inter, void* ptr)
{
    if (ptr == NULL) return;
    
    B32 use_gc = true;
    
    if (use_gc) {
        gc_free(inter, ptr);
    }
    else {
        //Assert(memory != ObjectMemory_Static);
    }
}

void object_free_unused_memory(Interpreter* inter)
{
    //arena_pop_to(inter->temp_arena, 0);
    gc_free_unused(inter);
}

void ObjectFreeAll(Interpreter* inter)
{
    Object* obj = inter->gc.object_list;
    
    while (obj != NULL)
    {
        Object* next = obj->next;
        obj->ref_count = 0;
        object_free(inter, obj, false);
        obj = next;
    }
}

void* gc_allocate(Interpreter* inter, U64 size)
{
    inter->gc.allocation_count++;
    return OsHeapAllocate(size);
}

void gc_free(Interpreter* inter, void* ptr)
{
    Assert(inter->gc.allocation_count > 0);
    inter->gc.allocation_count--;
    OsHeapFree(ptr);
}

void gc_free_unused(Interpreter* inter)
{
    U32 free_count;
    do {
        Object* obj = inter->gc.object_list;
        free_count = 0;
        
        while (obj != NULL)
        {
            Object* next = obj->next;
            if (obj->ref_count == 0) {
                object_free(inter, obj, true);
                free_count++;
            }
            obj = next;
        }
    }
    while (free_count != 0);
}

void LogMemoryUsage(Interpreter* inter)
{
    LogMemory(SEPARATOR_STRING);
    
    Object* obj = inter->gc.object_list;
    while (obj != NULL)
    {
        Object* next = obj->next;
        LogMemory("Obj %u: %u refs", obj->ID, obj->ref_count);
        obj = next;
    }
    
    LogMemory("Object Count: %u", inter->gc.object_count);
    LogMemory("Alloc Count: %u", inter->gc.allocation_count);
    LogMemory(SEPARATOR_STRING);
}
