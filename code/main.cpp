#include "program.h"

Input* CompileAndRunFromArgs(Arena* arena, Reporter* reporter)
{
    Input* input = InputFromArgs(arena, reporter);
    
    if (reporter->exit_requested) {
        return input;
    }
    
    Program* program = ProgramFromInput(arena, input, reporter);
    
    if (reporter->exit_requested) {
        return input;
    }
    
    if (!input->settings.analyze_only) {
        RuntimeSettings settings = {};
        settings.user_assert = input->settings.user_assert;
        settings.no_user = input->settings.no_user;
        
        ExecuteProgram(program, reporter, settings);
    }
    
    return input;
}

int main()
{
    SetupGlobals();
    
    Arena* arena = ArenaAlloc(Gb(32), 8);
    
    Reporter* reporter = ReporterAlloc(arena);
    Input* input = CompileAndRunFromArgs(arena, reporter);
    ReporterPrint(reporter);
    
    if (input->settings.wait_end) {
        OsConsoleWait();
    }
    
    I64 exit_code = reporter->exit_code;
    
    ShutdownGlobals();
    return (I32)exit_code;
}

