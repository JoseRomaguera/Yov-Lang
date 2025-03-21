#include "inc.h"

#if DEV

inline_fn void log_token(Token token)
{
    if (token.kind == TokenKind_Separator) print_info(STR("-> Separator"));
    else if (token.kind == TokenKind_Identifier) {
        print_info("-> Identifier: ");
        print_info(token.value);
    }
    else if (token.kind == TokenKind_IfKeyword) print_info("-> if");
    else if (token.kind == TokenKind_ElseKeyword) print_info("-> else");
    else if (token.kind == TokenKind_WhileKeyword) print_info("-> while");
    else if (token.kind == TokenKind_ForKeyword) print_info("-> for");
    else if (token.kind == TokenKind_EnumKeyword) print_info("-> enum");
    else if (token.kind == TokenKind_IntLiteral) {
        print_info("-> Int Literal: ");
        print_info(token.value);
    }
    else if (token.kind == TokenKind_BoolLiteral) {
        print_info("-> Bool Literal: ");
        print_info(token.value);
    }
    else if (token.kind == TokenKind_StringLiteral) {
        print_info("-> String Literal: ");
        print_info(token.value);
    }
    else if (token.kind == TokenKind_Comment) {
        print_info("-> Comment: ");
        print_info(token.value);
    }
    else if (token.kind == TokenKind_Comma) print_info(STR("-> ,"));
    else if (token.kind == TokenKind_Dot) print_info(STR("-> ."));
    else if (token.kind == TokenKind_Colon) print_info(STR("-> :"));
    else if (token.kind == TokenKind_OpenBrace) print_info(STR("-> {"));
    else if (token.kind == TokenKind_CloseBrace) print_info(STR("-> }"));
    else if (token.kind == TokenKind_OpenBracket) print_info(STR("-> ["));
    else if (token.kind == TokenKind_CloseBracket) print_info(STR("-> ]"));
    else if (token.kind == TokenKind_OpenParenthesis) print_info(STR("-> ("));
    else if (token.kind == TokenKind_CloseParenthesis) print_info(STR("-> )"));
    else if (token.kind == TokenKind_Assignment) {
        if (token.binary_operator == BinaryOperator_None) print_info(STR("-> ="));
        else print_info("-> %S=", string_from_binary_operator(token.binary_operator));
    }
    else if (token.kind == TokenKind_BinaryOperator) print_info("-> %S", string_from_binary_operator(token.binary_operator));
    else if (token.kind == TokenKind_OpenString) print_info(STR("-> Open String"));
    else if (token.kind == TokenKind_CloseString) print_info(STR("-> Close String"));
    else if (token.kind == TokenKind_NextLine) print_info(STR("-> Next Line"));
    else if (token.kind == TokenKind_NextSentence) print_info(STR("-> ;"));
    else if (token.kind == TokenKind_Error) print_info(STR("-> Error"));
    else if (token.kind == TokenKind_None) print_info(STR("-> None"));
    else print_info(STR("NOT DEFINED"));
    
    print_info(STR("\n"));
}

inline_fn void log_tokens(Array<Token> tokens)
{
    foreach(i, tokens.count) {
        log_token(tokens[i]);
    }
}

void log_ast(OpNode* node, i32 depth)
{
    SCRATCH();
    
    if (node == NULL) return;
    
    foreach(i, depth) print_info("  ");
    
    if (node->kind == OpKind_Block) print_info("block");
    if (node->kind == OpKind_Error) print_error("error");
    if (node->kind == OpKind_None) print_info("none");
    if (node->kind == OpKind_IfStatement) print_info("if-statement");
    if (node->kind == OpKind_WhileStatement) print_info("while-statement");
    if (node->kind == OpKind_ForStatement) print_info("for-statement");
    if (node->kind == OpKind_ForeachArrayStatement) print_info("foreach-statement");
    if (node->kind == OpKind_Assignment) print_info("assignment");
    if (node->kind == OpKind_FunctionCall) print_info("function call");
    if (node->kind == OpKind_ObjectDefinition) {
        auto node0 = (OpNode_ObjectDefinition*)node;
        print_info("objdef: '%S'", node0->object_name);
    }
    if (node->kind == OpKind_Binary) {
        auto node0 = (OpNode_Binary*)node;
        print_info("binary %S", string_from_binary_operator(node0->op));
    }
    if (node->kind == OpKind_Sign) {
        auto node0 = (OpNode_Sign*)node;
        print_info("sign %S", string_from_binary_operator(node0->op));
    }
    if (node->kind == OpKind_IntLiteral) {
        auto node0 = (OpNode_Literal*)node;
        print_info("int literal: %u", node0->int_literal);
    }
    if (node->kind == OpKind_StringLiteral) {
        auto node0 = (OpNode_Literal*)node;
        print_info("str literal: %S", node0->string_literal);
    }
    if (node->kind == OpKind_BoolLiteral) {
        auto node0 = (OpNode_Literal*)node;
        print_info("bool literal: %s", node0->bool_literal ? "true" : "false");
    }
    if (node->kind == OpKind_Symbol) {
        auto node0 = (OpNode_Symbol*)node;
        print_info("Symbol: %S", node0->identifier);
    }
    if (node->kind == OpKind_MemberValue) {
        auto node0 = (OpNode_MemberValue*)node;
        print_info("member_value: %S", node0->member);
    }
    if (node->kind == OpKind_ArrayExpresion) { print_info("array expresion"); }
    if (node->kind == OpKind_ArrayElementValue) {
        auto node0 = (OpNode_ArrayElementValue*)node;
        print_info("array element value: %S", node0->identifier);
    }
    
    print_info("\n");
    
    Array<OpNode*> childs = get_node_childs(scratch.arena, node);
    
    foreach(i, childs.count) {
        log_ast(childs[i], depth + 1);
    }
}

#endif

void yov_run_script(Yov* ctx, b32 trace, b32 user_assert, b32 analyze_only)
{
    RawBuffer raw_file;
    if (!os_read_entire_file(ctx->static_arena, ctx->script_path, &raw_file)) {
        print_error("File '%S' not found\n", ctx->script_path);
        return;
    }
    
    ctx->script_text = STR(raw_file);
    
    Array<Token> tokens = generate_tokens(ctx, ctx->script_text, true);
    OpNode* ast = generate_ast(ctx, tokens, true);
    
    InterpreterSettings settings{};
    settings.print_execution = (b8)trace;
    settings.user_assertion = (b8)user_assert;
    
    // Semantic analysis of all the AST before execution
    {
        settings.execute = false;
        interpret(ctx, ast, settings);
    }
    
    if (ctx->error_count != 0) {
        yov_print_reports(ctx);
        return;
    }
    
    // Execute
    if (!analyze_only)
    {
        settings.execute = true;
        interpret(ctx, ast, settings);
    }
    
#if DEV
    if (1)
    {
        SCRATCH();
        
        print_separator();
        
        u64 static_memory = (u32)ctx->static_arena->memory_position;
        u64 temp_memory = (u32)ctx->temp_arena->memory_position;
        u64 total_memory = static_memory + temp_memory;
        
        print_info("Static Memory: %S\n", string_from_memory(scratch.arena, static_memory));
        print_info("Temp Memory: %S\n", string_from_memory(scratch.arena, temp_memory));
        print_info("Total Memory: %S\n", string_from_memory(scratch.arena, total_memory));
        
        if (0) {
            print_info(STR("\n// TOKENS\n"));
            log_tokens(tokens);
            print_info("\n\n");
        }
        if (0) {
            print_info(STR("// AST\n"));
            log_ast(ast, 0);
            print_info("\n\n");
        }
    }
#endif
}

void main()
{
    os_initialize();
    initialize_scratch_arenas();
    
    Arena* static_arena = arena_alloc(GB(32), 8);
    
    Array<String> args = os_get_args(static_arena);
    
    if (args.count <= 0) {
        print_error("Script not specified\n");
        return;
    }
    
    if (args.count == 1 && string_equals(args[0], STR("help"))) {
        print_info("TODO\n");
        return;
    }
    
    if (args.count == 1 && string_equals(args[0], STR("version"))) {
        print_info(YOV_VERSION);
        return;
    }
    
    b32 error_in_interpreter_args = false;
    b32 analyze_only = false;
    b32 trace = false;
    b32 user_assert = false;
    
    i32 script_args_start_index = args.count;
    for (i32 i = 1; i < args.count; ++i) {
        String arg = args[i];
        
        if (string_equals(arg, STR("/"))) {
            script_args_start_index = i + 1;
            break;
        }
        
        if (string_equals(arg, STR("analyze"))) analyze_only = true;
        else if (string_equals(arg, STR("trace"))) trace = true;
        else if (string_equals(arg, STR("user_assert"))) user_assert = true;
        else {
            error_in_interpreter_args = true;
            print_error("Unknown interpreter argument '%S'\n", arg);
        }
    }
    
    Array<String> script_args = array_subarray(args, script_args_start_index, args.count - script_args_start_index);
    
    Yov* ctx = yov_initialize(static_arena, args[0]);
    
    if (generate_program_args(ctx, script_args) && !error_in_interpreter_args) {
        yov_run_script(ctx, trace, user_assert, analyze_only);
    }
    
    yov_shutdown(ctx);
    
#if DEV
    os_console_wait();
#endif
    
    arena_free(static_arena);
    shutdown_scratch_arenas();
    os_shutdown();
}

#include "common.cpp"
#include "lexer.cpp"
#include "parser.cpp"
#include "interpreter.cpp"
#include "intrinsics.cpp"
#include "os_windows.cpp"

void* memcpy(void* dst, const void* src, size_t size) {
    memory_copy(dst, src, (u64)size);
    return dst;
}
void* memset(void* dst, int value, size_t size) {
    assert(value == 0);
    memory_zero(dst, size);
    return dst;
}
extern "C" int _fltused = 0;