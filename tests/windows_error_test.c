#include "platform/windows_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buffer[256];
    nav_windows_test_format_error(buffer, sizeof buffer, "CreateFileW", 5);
    assert(strcmp(buffer, "CreateFileW (Windows error 5)") == 0);

    nav_windows_test_format_error(buffer, sizeof buffer, "ReadFile", 1234);
    assert(strcmp(buffer, "ReadFile (Windows error 1234)") == 0);

    /* Guard both sides of a small buffer and verify termination on truncation. */
    unsigned char guarded[10];
    memset(guarded, 0xa5, sizeof guarded);
    nav_windows_test_format_error((char *)guarded + 1, 8, "CreateFileW", 5);
    assert(guarded[0] == 0xa5);
    assert(guarded[9] == 0xa5);
    assert(guarded[8] == '\0');
    assert(strcmp((char *)guarded + 1, "CreateF") == 0);

    puts("Windows DWORD error formatting tests passed");
    return 0;
}
