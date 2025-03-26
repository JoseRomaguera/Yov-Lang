#include "inc.h"

void main()
{
    yov_initialize();
    
#if DEV
    if (0) yov_transpile_core_definitions();
    else yov_run();
#else
    yov_run();
#endif
    
    yov_shutdown();
}

#include "common.cpp"
#include "lexer.cpp"
#include "parser.cpp"
#include "interpreter.cpp"
#include "transpiler.cpp"
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