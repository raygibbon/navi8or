#include "nav.h"
#include "nav_ui.h"
#include "nav_version.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    NavConfig config;
    char error[256] = {0};
    const char *config_file = NULL;
    int argument = 1;

    while (argument < argc && argv[argument][0] == '-') {
        const char *option = argv[argument++];
        if (!strcmp(option, "--")) break;
        if (!strcmp(option, "--version")) {
            puts(NAV_APP_IDENTITY " Viewer helper");
            return 0;
        }
        if (!strcmp(option, "--help")) {
            puts("Usage: nav-viewer-c [-i profile-file] file\n"
                 "Standalone legacy C Viewer for Navi8or.");
            return 0;
        }
        if (option[1] == 'i') {
            config_file = option[2] ? option + 2 :
                          argument < argc ? argv[argument++] : NULL;
            if (config_file && config_file[0] && config_file[0] != '-')
                continue;
            fprintf(stderr, "nav-viewer-c: -i requires a profile file\n");
        } else
            fprintf(stderr, "nav-viewer-c: unknown option: %s\n", option);
        return 2;
    }
    if (argc - argument != 1) {
        fprintf(stderr, "nav-viewer-c: exactly one file is required\n");
        return 2;
    }
    if (nav_config_load_file(&config, config_file, error, sizeof error)) {
        fprintf(stderr, "nav-viewer-c: %s\n",
                error[0] ? error : "unable to load configuration");
        return 2;
    }
    if (nav_ui_viewer_run_local_file(argv[argument], &config, error,
                                     sizeof error)) {
        fprintf(stderr, "nav-viewer-c: %s\n",
                error[0] ? error : "unable to open Viewer");
        return 1;
    }
    return 0;
}
