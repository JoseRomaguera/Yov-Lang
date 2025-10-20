#include "inc.h"

int main()
{
    yov_initialize(true);
    
    yov_config_from_args();
    
    InterpreterSettings settings = {};
    settings.print_execution = yov->settings.trace;
    settings.user_assertion = yov->settings.user_assert;
    
    if (yov->exit_requested) goto on_exit;
    
    ScriptArg* help_arg = yov_find_arg("-help");
    
    if (help_arg != NULL && !string_equals(help_arg->value, "")) {
        report_arg_wrong_value(NO_CODE, help_arg->name, help_arg->value);
    }
    
    Interpreter* inter = yov_compile(settings, help_arg != NULL, true);
    if (yov->exit_requested) goto on_exit;
    
    if (help_arg != NULL) {
        yov_print_script_help(inter);
        goto on_exit;
    }
    
    // Execute
    if (!yov->settings.analyze_only) {
        interpreter_run_main(inter);
        if (yov->exit_requested && yov->reports.count > 0) {
            yov_set_exit_code(-1);
        }
    }
    
    interpreter_shutdown(inter);
    
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
