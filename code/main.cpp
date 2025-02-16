#include "inc.h"

inline_fn void log_token(Token token)
{
    if (token.kind == TokenKind_Separator) print_info(STR("-> Separator"));
    else if (token.kind == TokenKind_Identifier) {
        print_info("-> Identifier: ");
        print_info(token.value);
    }
    else if (token.kind == TokenKind_Keyword) {
        print_info("-> Keyword: %S", string_from_keyword(token.keyword));
    }
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
    else if (token.kind == TokenKind_Unknown) print_info(STR("-> Unknown"));
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
    if (node->kind == OpKind_Unknown) print_error("unknown");
    if (node->kind == OpKind_None) print_info("none");
    if (node->kind == OpKind_IfStatement) print_info("if-statement");
    if (node->kind == OpKind_WhileStatement) print_info("while-statement");
    if (node->kind == OpKind_ForStatement) print_info("for-statement");
    if (node->kind == OpKind_ForeachArrayStatement) print_info("foreach-statement");
    if (node->kind == OpKind_VariableAssignment) print_info("variable assignment");
    if (node->kind == OpKind_FunctionCall) print_info("function call");
    if (node->kind == OpKind_VariableDefinition) {
        auto node0 = (OpNode_VariableDefinition*)node;
        print_info("vardef: '%S %S'", node0->type, node0->identifier);
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
    if (node->kind == OpKind_IdentifierValue) {
        auto node0 = (OpNode_IdentifierValue*)node;
        print_info("identifier: %S", node0->identifier);
    }
    if (node->kind == OpKind_MemberValue) {
        auto node0 = (OpNode_MemberValue*)node;
        print_info("member_value: %S.%S", node0->identifier, node0->member);
    }
    if (node->kind == OpKind_ArrayExpresion) { print_info("array expresion"); }
    if (node->kind == OpKind_ArrayElementValue) {
        auto node0 = (OpNode_ArrayElementValue*)node;
        print_info("array element value: %S", node0->identifier);
    }
    if (node->kind == OpKind_ArrayElementAssignment) {
        auto node0 = (OpNode_ArrayElementAssignment*)node;
        print_info("array element assignment: %S", node0->identifier);
    }
    
    print_info("\n");
    
    if (node->kind == OpKind_Block) {
        auto node0 = (OpNode_Block*)node;
        foreach(i, node0->ops.count) {
            log_ast(node0->ops[i], depth + 1);
        }
    }
    else if (node->kind == OpKind_IfStatement) {
        auto node0 = (OpNode_IfStatement*)node;
        log_ast(node0->expresion, depth + 1);
        log_ast(node0->success, depth + 1);
        log_ast(node0->failure, depth + 1);
    }
    else if (node->kind == OpKind_WhileStatement) {
        auto node0 = (OpNode_WhileStatement*)node;
        log_ast(node0->expresion, depth + 1);
        log_ast(node0->content, depth + 1);
    }
    else if (node->kind == OpKind_ForStatement) {
        auto node0 = (OpNode_ForStatement*)node;
        log_ast(node0->initialize_sentence, depth + 1);
        log_ast(node0->condition_expresion, depth + 1);
        log_ast(node0->update_sentence, depth + 1);
        log_ast(node0->content, depth + 1);
    }
    else if (node->kind == OpKind_ForeachArrayStatement) {
        auto node0 = (OpNode_ForeachArrayStatement*)node;
        log_ast(node0->expresion, depth + 1);
        log_ast(node0->content, depth + 1);
    }
    else if (node->kind == OpKind_VariableAssignment) {
        auto node0 = (OpNode_Assignment*)node;
        log_ast(node0->value, depth + 1);
    }
    else if (node->kind == OpKind_VariableDefinition) {
        auto node0 = (OpNode_VariableDefinition*)node;
        log_ast(node0->assignment, depth + 1);
    }
    else if (node->kind == OpKind_FunctionCall) {
        auto node0 = (OpNode_FunctionCall*)node;
        Array<OpNode*> params = node0->parameters;
        foreach(i, params.count)
            log_ast(params[i], depth + 1);
    }
    else if (node->kind == OpKind_ArrayExpresion) {
        auto node0 = (OpNode_ArrayExpresion*)node;
        Array<OpNode*> exps = node0->nodes;
        foreach(i, exps.count)
            log_ast(exps[i], depth + 1);
    }
    else if (node->kind == OpKind_ArrayElementValue) {
        auto node0 = (OpNode_ArrayElementValue*)node;
        log_ast(node0->expresion, depth + 1);
    }
    else if (node->kind == OpKind_ArrayElementAssignment) {
        auto node0 = (OpNode_ArrayElementAssignment*)node;
        log_ast(node0->indexing_expresion, depth + 1);
        log_ast(node0->value, depth + 1);
    }
    else if (node->kind == OpKind_Binary) {
        auto node0 = (OpNode_Binary*)node;
        log_ast(node0->left, depth + 1);
        log_ast(node0->right, depth + 1);
    }
    else if (node->kind == OpKind_Sign) {
        auto node0 = (OpNode_Sign*)node;
        log_ast(node0->expresion, depth + 1);
    }
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
    
    if (args.count == 1 && string_equals(args[0], STR("-h"))) {
        print_info("TODO\n");
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
        
        RawBuffer raw_file;
        if (os_read_entire_file(ctx->static_arena, ctx->script_path, &raw_file))
        {
            String file = STR(raw_file);
            
            Array<Token> tokens = generate_tokens(ctx, file, true);
            OpNode* ast = generate_ast(ctx, tokens);
            
            {
                InterpreterSettings settings{};
                settings.print_execution = (b8)trace;
                settings.user_assertion = (b8)user_assert;
                
                // Semantic analysis of all the AST before execution
                {
                    print_info("Analizing...\n");
                    settings.execute = false;
                    interpret(ctx, ast, settings);
                }
                
                // Execute
                if (!analyze_only && ctx->error_count == 0)
                {
                    print_info("Executing...\n");
                    print_separator();
                    settings.execute = true;
                    interpret(ctx, ast, settings);
                }
            }
            
#if DEV
            if (1)
            {
                print_separator();
                
                u32 static_memory = (u32)ctx->static_arena->memory_position;
                u32 temp_memory = (u32)ctx->temp_arena->memory_position;
                u32 total_memory = static_memory + temp_memory;
                
                print_info("Static Memory: %u\n", static_memory);
                print_info("Temp Memory: %u\n", temp_memory);
                print_info("Total Memory: %u\n", total_memory);
                
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