#pragma once

#include "program.h"

struct Scope {
    IR ir;
    
    I32 return_index;
    U32 return_count;
    
    Array<Reference> registers;
    
    I32 pc; // Program Counter
    
    U32 current_line;
    String current_path;
    
    B32 return_requested;
    U32 scratch_arena_index;
    Scope* prev;
};

struct Runtime {
    Arena* arena;
    
    Program* program;
    Reporter* reporter;
    
    RuntimeSettings settings;
    
    U32 object_id_counter;
    
    struct {
        Object* object_list;
        I32 object_count;
        I32 allocation_count;
    } gc;
    
    Array<Reference> globals;
    Scope* current_scope;
    
    struct {
        Reference yov;
        Reference os;
        Reference context;
        Reference calls;
    } common_globals;
};

Runtime* RuntimeAlloc(Program* program, Reporter* reporter, RuntimeSettings settings);
void RuntimeFree(Runtime* runtime);
void RuntimeInitializeGlobals(Runtime* runtime);

void RuntimeStart(Runtime* runtime, String function_name);

void RuntimePushScope(Runtime* runtime, I32 return_index, U32 return_count, IR ir, Array<Value> params);
void RuntimePopScope(Runtime* runtime);

B32 RuntimeRunUnit(Runtime* runtime);

void RuntimePrintScriptHelp(Runtime* runtime);

void RuntimeExit(Runtime* runtime, I64 exit_code);
void RuntimeReportError(Runtime* runtime, Result result);

String RuntimeGenerateInheritedLangArgs(Runtime* runtime);
CallOutput RuntimeCallScript(Runtime* runtime, String script, String args, String lang_args, RedirectStdout redirect_stdout);

Result RuntimeUserAssertion(Runtime* runtime, String message);
B32 RuntimeAskYesNo(Runtime* runtime, String title, String message);

Reference RuntimeGetCurrentDirRef(Runtime* runtime);
String RuntimeGetCurrentDirStr(Runtime* runtime);

String PathAbsoluteToCD(Arena* arena, Runtime* runtime, String path);
RedirectStdout RuntimeGetCallsRedirectStdout(Runtime* runtime);

Reference RefFromValue(Runtime* runtime, Scope* scope, Value value);

void RunInstruction(Runtime* runtime, Unit unit, String path);
void RunStore(Runtime* runtime, I32 dst_index, Reference src);
void RunCopy(Runtime* runtime, I32 dst_index, Reference src);
void RunReturn(Runtime* runtime);
void RunJump(Runtime* runtime, Reference ref, I32 condition, I32 offset);
void RunFunctionCall(Runtime* runtime, I32 dst_index, FunctionDefinition* fn, Array<Value> parameters);
Reference RunBinaryOperation(Runtime* runtime, Reference dst, Reference left, Reference right, BinaryOperator op);
Reference RunSignOperation(Runtime* runtime, Reference value, BinaryOperator op);

//- SCOPE

void RuntimeStore(Runtime* runtime, Scope* scope, I32 register_index, Reference ref);
void RuntimeStoreGlobal(Runtime* runtime, String identifier, Reference ref);
void RuntimeStoreReturn(Runtime* runtime, Scope* scope, I32 dst_index, Array<Reference> refs);
Reference RuntimeLoad(Runtime* runtime, Scope* scope, I32 register_index);
Reference RuntimeLoadGlobal(Runtime* runtime, String identifier);

//- OBJECT

String StrFromObject(Arena* arena, Runtime* runtime, Object* object, B32 raw = true);
String StrFromRef(Arena* arena, Runtime* runtime, Reference ref, B32 raw = true);

Reference ref_from_object(Object* object);
Reference ref_from_address(Object* parent, VType vtype, void* address);

void ref_set_member(Runtime* runtime, Reference ref, U32 index, Reference member);

Reference ref_get_child(Runtime* runtime, Reference ref, U32 index, B32 is_member);
Reference ref_get_member(Runtime* runtime, Reference ref, U32 index);
Reference ref_get_property(Runtime* runtime, Reference ref, U32 index);

U32 RefGetChildCount(Runtime* runtime, Reference ref, B32 is_member);
U32 RefGetPropertyCount(Runtime* runtime, Reference ref);
U32 RefGetMemberCount(Reference ref);

Reference alloc_int(Runtime* runtime, I64 value);
Reference alloc_bool(Runtime* runtime, B32 value);
Reference alloc_string(Runtime* runtime, String value);
Reference alloc_array(Runtime* runtime, VType element_vtype, I64 count);
Reference alloc_array_multidimensional(Runtime* runtime, VType base_vtype, Array<I64> dimensions);
Reference alloc_array_from_enum(Runtime* runtime, VType enum_vtype);
Reference alloc_enum(Runtime* runtime, VType vtype, I64 index);
Reference alloc_reference(Runtime* runtime, Reference ref);

B32 is_valid(Reference ref);
B32 is_unknown(Reference ref);
B32 is_const(Reference ref);
B32 is_null(Reference ref);
B32 is_int(Reference ref);
B32 is_bool(Reference ref);
B32 is_string(Reference ref);
B32 is_array(Reference ref);
B32 is_enum(Reference ref);
B32 is_reference(Reference ref);

I64 get_int(Reference ref);
B32 get_bool(Reference ref);
I64 get_enum_index(Reference ref);
String get_string(Reference ref);
ObjectData_Array* get_array(Reference ref);
Reference RefDereference(Runtime* runtime, Reference ref);

I64 get_int_member(Runtime* runtime, Reference ref, String member);
B32 get_bool_member(Runtime* runtime, Reference ref, String member);
String get_string_member(Runtime* runtime, Reference ref, String member);

void set_int(Reference ref, I64 v);
void set_bool(Reference ref, B32 v);
void set_enum_index(Reference ref, I64 v);

ObjectData_String* ref_string_get_data(Runtime* runtime, Reference ref);
void ref_string_prepare(Runtime* runtime, Reference ref, U64 new_size, B32 can_discard);
void ref_string_clear(Runtime* runtime, Reference ref);
void ref_string_set(Runtime* runtime, Reference ref, String v);
void ref_string_append(Runtime* runtime, Reference ref, String v);

void set_reference(Runtime* runtime, Reference ref, Reference src);

void set_int_member(Runtime* runtime, Reference ref, String member, I64 v);
void ref_member_set_bool(Runtime* runtime, Reference ref, String member, B32 v);
void set_enum_index_member(Runtime* runtime, Reference ref, String member, I64 v);
void ref_member_set_string(Runtime* runtime, Reference ref, String member, String v);

enum CopyMode {
    CopyMode_NoOverride,
    CopyMode_Override,
};
inline_fn CopyMode get_enum_CopyMode(Reference ref) {
    return (CopyMode)get_enum_index(ref);
}

void ref_assign_Result(Runtime* runtime, Reference ref, Result res);
void ref_assign_CallOutput(Runtime* runtime, Reference ref, CallOutput out);
void ref_assign_FileInfo(Runtime* runtime, Reference ref, FileInfo info);
void ref_assign_FunctionDefinition(Runtime* runtime, Reference ref, FunctionDefinition* fn);
void ref_assign_StructDefinition(Runtime* runtime, Reference ref, VType vtype);
void ref_assign_EnumDefinition(Runtime* runtime, Reference ref, VType vtype);
void ref_assign_ObjectDefinition(Runtime* runtime, Reference ref, ObjectDefinition def);

void ref_assign_Type(Runtime* runtime, Reference ref, VType vtype);
VType get_Type(Runtime* runtime, Reference ref);

Reference ref_from_Result(Runtime* runtime, Result res);
Result Result_from_ref(Runtime* runtime, Reference ref);

U32 object_generate_id(Runtime* runtime);
Reference object_alloc(Runtime* runtime, VType vtype);
void object_free(Runtime* runtime, Object* obj, B32 release_internal_refs);

void object_increment_ref(Object* obj);
void object_decrement_ref(Object* obj);

void ref_release_internal(Runtime* runtime, Reference ref, B32 release_refs);
void RefCopy(Runtime* runtime, Reference dst, Reference src);
Reference ref_alloc_and_copy(Runtime* runtime, Reference src);

void* object_dynamic_allocate(Runtime* runtime, U64 size);
void object_dynamic_free(Runtime* runtime, void* ptr);
void object_free_unused_memory(Runtime* runtime);
void ObjectFreeAll(Runtime* runtime);

void* gc_allocate(Runtime* runtime, U64 size);
void gc_free(Runtime* runtime, void* ptr);
void gc_free_unused(Runtime* runtime);

void LogMemoryUsage(Runtime* runtime);

//- REPORTS 

#define ReportErrorRT(_text, ...) ReportErrorEx(reporter, NO_CODE, runtime->current_scope->current_line, runtime->current_scope->current_path, _text, __VA_ARGS__)

#define ReportNullRef() ReportErrorRT("Null reference")
#define ReportZeroDivision() ReportErrorRT("Divided by zero")
#define ReportRightPathCantBeAbsolute() ReportErrorRT("Right path can't be absolute")
#define ReportInvalidOp() ReportErrorRT("Invalid Op")

#define lang_report_stack_is_broken() ReportErrorNoCode("[LANG_ERROR] The stack is broken")
#define lang_report_unfreed_objects() ReportErrorNoCode("[LANG_ERROR] Not all objects have been freed")
#define lang_report_unfreed_dynamic() ReportErrorNoCode("[LANG_ERROR] Not all dynamic allocations have been freed")