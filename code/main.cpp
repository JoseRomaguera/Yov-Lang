#include "inc.h"

int main()
{
    yov_initialize(true);
    
    yov_config_from_args();
    if (yov->exit_requested) goto on_exit;
    
#if DEV && 0
    yov_transpile_core_definitions();
    goto on_exit;
#endif
    
    ScriptArg* help_arg = yov_find_arg("-help");
    
    if (help_arg != NULL && !string_equals(help_arg->value, "")) {
        report_arg_wrong_value(NO_CODE, help_arg->name, help_arg->value);
    }
    
    yov_compile(help_arg != NULL, true);
    if (yov->exit_requested) goto on_exit;
    
    if (help_arg != NULL) {
        yov_print_script_help();
        goto on_exit;
    }
    
    // Execute
    if (!yov->settings.analyze_only) {
        InterpreterSettings settings{};
        settings.print_execution = yov->settings.trace;
        settings.user_assertion = yov->settings.user_assert;
        
        interpret(settings);
    }
    
    on_exit:
    yov_print_reports();
    
    i64 exit_code = yov->exit_code;
    yov_shutdown();
    
    return (i32)exit_code;
}

#include "common.cpp"
#include "lexer.cpp"
#include "parser.cpp"
#include "types.cpp"
#include "ir.cpp"
#include "interpreter.cpp"
#include "intrinsics.cpp"
#include "os_windows.cpp"
