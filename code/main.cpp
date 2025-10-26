#include "inc.h"

int main()
{
    yov_initialize(true);
    yov_config_from_args();
    i64 exit_code = yov_run();
    yov_shutdown();
    
    return (i32)exit_code;
}