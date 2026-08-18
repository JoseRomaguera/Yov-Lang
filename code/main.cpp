#include "common.h"


int main()
{
    PROFILE_FUNCTION;
    
    SetupGlobals();
    
    I64 exit_code = CompileAndRunFromArgs();
    //I64 exit_code = CompileAndDebugFromArgs();
    
    ShutdownGlobals();
    return (I32)exit_code;
}

