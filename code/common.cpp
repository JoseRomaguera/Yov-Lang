#include "inc.h"

void assertion_failed(const char* text)
{
    os_print(Severity_Error, STR(text));
    *((u8*)0) = 0;
}

void memory_copy(void* dst, const void* src, u64 size)
{
    // TODO(Jose): Optimize
    u64 iteration_stride = 4 * sizeof(size_t);
    u64 iterations = size / iteration_stride;
    
    void* end_dst = (u8*)dst + (iterations * iteration_stride);
    void* end_src = (u8*)src + (iterations * iteration_stride);
    
    size_t* d = (size_t*)dst;
    const size_t* s = (const size_t*)src;
    while (d < end_dst) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
        d += 4;
        s += 4;
    }
    
    u8* byte_dst = (u8*)end_dst;
    const u8* byte_src = (const u8*)end_src;
    
    end_dst = (u8*)dst + size;
    
    while (byte_dst < end_dst) {
        *byte_dst = *byte_src;
        byte_dst++;
        byte_src++;
    }
}

void memory_zero(void* dst, u64 size)
{
    // NOTE(Jose): MSVC with /O2 flag is replacing this function with memset, thats why we are using volatile keyword
    
    u64 iteration_stride = 4 * sizeof(size_t);
    u64 iterations = size / iteration_stride;
    
    void* end_dst = (u8*)dst + (iterations * iteration_stride);
    
    volatile size_t* d = (size_t*)dst;
    while (d < end_dst) {
        d[0] = 0;
        d[1] = 0;
        d[2] = 0;
        d[3] = 0;
        d += 4;
    }
    
    volatile u8* byte_dst = (u8*)end_dst;
    end_dst = (u8*)dst + size;
    
    while (byte_dst < end_dst) {
        *byte_dst = 0;
        byte_dst++;
    }
}

//- ARENA 

Arena* arena_alloc(u64 capacity, u32 alignment)
{
    alignment = MAX(alignment, 1);
    Arena* arena = (Arena*)os_allocate_heap(sizeof(Arena));
    arena->reserved_pages = pages_from_bytes(capacity);
    arena->memory = os_reserve_virtual_memory(arena->reserved_pages, false);
    arena->alignment = alignment;
    return arena;
}

void arena_free(Arena* arena) {
    if (arena == NULL) return;
    os_release_virtual_memory(arena->memory);
    os_free_heap(arena);
}

void* arena_push(Arena* arena, u64 size)
{
    u64 position = arena->memory_position;
    position = u64_divide_high(position, arena->alignment) * arena->alignment;
    
    u64 commited_size = (u64)arena->commited_pages * (u64)os.page_size;
    u64 reserved_size = (u64)arena->reserved_pages * (u64)os.page_size;
    
    u64 end_position = position + (u64)size;
    
    if (end_position > reserved_size) {
        return NULL; // TODO(Jose): Fatal error, out of memory
    }
    
    if (end_position > commited_size)
    {
        u32 commited_pages_needed = pages_from_bytes(end_position);
        u32 page_count = commited_pages_needed - arena->commited_pages;
        page_count = MAX(page_count, pages_from_bytes(KB(200)));
        os_commit_virtual_memory(arena->memory, arena->commited_pages, page_count);
    }
    
    arena->memory_position = end_position;
    return (u8*)arena->memory + position;
}

void arena_pop_to(Arena* arena, u64 position)
{
    assert(position <= arena->memory_position);
    u64 bytes_poped = arena->memory_position - position;
    arena->memory_position = position;
    memory_zero((u8*)arena->memory + arena->memory_position, bytes_poped);
}

Arena* scratch_arenas[2];

void initialize_scratch_arenas()
{
    foreach(i, array_count(scratch_arenas)) {
        scratch_arenas[i] = arena_alloc(GB(16), 8);
    }
}

void shutdown_scratch_arenas() {
    foreach(i, array_count(scratch_arenas)) arena_free(scratch_arenas[i]);
}

ScratchArena arena_create_scratch(Arena* conflict0, Arena* conflict1)
{
    u32 index = 0;
    while (scratch_arenas[index] == conflict0 || scratch_arenas[index] == conflict1) index++;
    
    assert(index < array_count(scratch_arenas));
    
    ScratchArena scratch;
    scratch.arena = scratch_arenas[index];
    scratch.start_position = scratch.arena->memory_position;
    return scratch;
}

void arena_destroy_scratch(ScratchArena scratch)
{
    arena_pop_to(scratch.arena, scratch.start_position);
}

//- REPORT 

void print_ex(Severity severity, String str, ...)
{
    SCRATCH();
    
    va_list args;
	va_start(args, str);
    String result = string_format_with_args(scratch.arena, str, args);
    va_end(args);
    os_print(severity, result);
}

ProgramContext* program_context_initialize(Arena* arena, ProgramSettings settings, String script_path)
{
    SCRATCH(arena);
    
    ProgramContext* ctx = arena_push_struct<ProgramContext>(arena);
    ctx->static_arena = arena;
    ctx->temp_arena = arena_alloc(GB(32), 8);
    ctx->settings = settings;
    
    ctx->reports = pooled_array_make<Report>(arena, 32);
    
    // Paths
    {
        ctx->caller_dir = os_get_working_path(ctx->static_arena);
        
        if (!os_path_is_absolute(script_path)) script_path = path_append(scratch.arena, ctx->caller_dir, script_path);
        ctx->script_path = string_copy(ctx->static_arena, script_path);
        
        ctx->script_name = path_get_last_element(ctx->script_path);
        ctx->script_dir = path_resolve(ctx->static_arena, path_append(scratch.arena, ctx->script_path, STR("..")));
    }
    
#if DEV && 0
    print_info("Caller dir = %S\n", ctx->caller_dir);
    print_info("Script path = %S\n", ctx->script_path);
    print_info("Script name = %S\n", ctx->script_name);
    print_info("Script dir = %S\n", ctx->script_dir);
    print_info("\n------------------------------\n\n");
#endif
    
    return ctx;
}

void program_context_shutdown(ProgramContext* ctx)
{
    if (ctx == NULL) return;
    arena_free(ctx->temp_arena);
}

b32 generate_program_args(ProgramContext* ctx, Array<String> raw_args)
{
    SCRATCH();
    ctx->args = array_make<ProgramArg>(ctx->static_arena, raw_args.count);
    
    foreach(i, raw_args.count)
    {
        String raw = raw_args[i];
        
        Array<String> split = string_split(scratch.arena, raw, STR("="));
        
        ProgramArg arg{};
        if (split.count == 1) {
            arg.name = split[0];
            arg.value = STR("1");
        }
        else if (split.count == 2) {
            arg.name = split[0];
            arg.value = split[1];
        }
        else {
            print_error("Invalid arg '%S', expected format: name=value\n", raw);
            return false;
        }
        
        ctx->args[i] = arg;
    }
    
    return true;
}

void report_ex(ProgramContext* ctx, Severity severity, CodeLocation code, String text, ...)
{
    SCRATCH();
    
    va_list args;
	va_start(args, text);
    String formatted_text = string_format_with_args(scratch.arena, text, args);
    va_end(args);
    
    formatted_text = string_replace(scratch.arena, formatted_text, STR("{line}"), STR("*line*"));
    
    Report report;
    report.text = string_copy(ctx->static_arena, formatted_text);
    report.code = code;
    report.severity = severity;
    array_add(&ctx->reports, report);
    
    print_report(report);
    
    if (severity == Severity_Error) ctx->error_count++;
}

void print_report(Report report)
{
    String prefix = STR("INFO");
    
    if (report.severity == Severity_Warning) {
        prefix = STR("WARNING");
    }
    else if (report.severity == Severity_Error) {
        prefix = STR("ERROR");
    }
    
    print(report.severity, "[%S]%u: %S\n", prefix, (u32)report.code.line, report.text);
}

void print_reports(Array<Report> reports)
{
    foreach(i, reports.count) {
        print_report(reports[i]);
    }
}