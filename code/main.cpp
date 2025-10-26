#include "inc.h"

int main()
{
    os_setup_system_info();
    
    yov_initialize_thread();
    yov_initialize(true);
    
    yov_config_from_args();
    i64 exit_code = yov_run();
    
    yov_shutdown();
    yov_shutdown_thread();
    
    return (i32)exit_code;
}