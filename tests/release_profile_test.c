#include "nav.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    NavConfig *config = calloc(1, sizeof *config);
    char error[256];
    if (!config || argc != 2) return 2;
    int result = nav_config_load_file(config, argv[1], error, sizeof error);
    if (result) fprintf(stderr, "%s\n", error);
    free(config);
    return result ? 1 : 0;
}
