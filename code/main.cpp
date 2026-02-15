#include "program.h"
#include "runtime.h"

I64 CompileAndRunFromArgs()
{
    Arena* arena = ArenaAlloc(Gb(32), 8);
    defer (ArenaFree(arena));
    
    Reporter* reporter = ReporterAlloc(arena);
    
    Input* input = InputFromArgs(arena, reporter);
    
    Program* program = ProgramFromInput(arena, input, reporter);
    
    if (!input->settings.analyze_only) {
        RuntimeSettings settings = {};
        settings.user_assert = input->settings.user_assert;
        settings.no_user = input->settings.no_user;
        
        ExecuteProgram(program, reporter, settings);
    }
    
    ReporterPrint(reporter);
    
    if (input->settings.wait_end) {
        OsConsoleWait();
    }
    
    return reporter->exit_code;
}

// TEMP
#include <Windows.h>
I32 StepPressed() {
    if (GetAsyncKeyState('A') & 1) return 1;
    if (GetAsyncKeyState('W') & 1) return 2;
    if (GetAsyncKeyState('D') & 1) return 3;
    if (GetAsyncKeyState(VK_RETURN) & 1) return 4;
    return 0;
}

I64 CompileAndDebugFromArgs()
{
    Arena* arena = ArenaAlloc(Gb(32), 8);
    defer (ArenaFree(arena));
    
    Reporter* reporter = ReporterAlloc(arena);
    Input* input = InputFromArgs(arena, reporter);
    
    Program* program = ProgramFromInput(arena, input, reporter);
    
    if (!input->settings.analyze_only) {
        RuntimeSettings settings = {};
        settings.user_assert = input->settings.user_assert;
        settings.no_user = input->settings.no_user;
        
        Runtime* runtime = RuntimeAlloc(program, reporter, settings);
        RuntimeInitializeGlobals(runtime);
        
        if (!reporter->exit_requested)
        {
            RuntimeStart(runtime, "main");
            
            while (1)
            {
                I32 press = StepPressed();
                
                B32 running = true;
                
                if (press == 1) running = RuntimeStepOver(runtime);
                else if (press == 2) running = RuntimeStepInto(runtime);
                else if (press == 3) running = RuntimeStepOut(runtime);
                else if (press == 4) {
                    RuntimeStepAll(runtime);
                    running = false;
                }
                else OsThreadSleep(10);
                
                if (!running) break;
            }
        }
        
        RuntimeFree(runtime);
    }
    
    ReporterPrint(reporter);
    
    if (input->settings.wait_end) {
        OsConsoleWait();
    }
    
    return reporter->exit_code;
}

int main()
{
    SetupGlobals();
    
    //I64 exit_code = CompileAndRunFromArgs();
    I64 exit_code = CompileAndDebugFromArgs();
    
    ShutdownGlobals();
    return (I32)exit_code;
}

