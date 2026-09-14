#include "nav.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    NavApp app;
    char cwd[NAV_PATH_MAX], config_directory[NAV_PATH_MAX];
    char vault_path[NAV_PATH_MAX], error[256];
    const char *left, *right;
    int result;
    const char *config_file = NULL;
    int argument = 1;
    while (argument < argc && argv[argument][0] == '-') {
        const char *option = argv[argument++];
        if (!strcmp(option, "--")) break;
        if (!strcmp(option, "--help")) {
            puts("Usage: nav [-i profile-file] [left-directory] [right-directory]\nNavi8or - keyboard-first local and remote repository navigator.");
            return 0;
        }
        if (option[1] == 'i') {
            config_file = option[2] ? option + 2 : argument < argc ? argv[argument++] : NULL;
            if (config_file && config_file[0] && config_file[0] != '-') continue;
            fprintf(stderr, "nav: -i requires a profile file\n");
        } else fprintf(stderr, "nav: unknown option: %s\n", option);
        return 2;
    }
    if (argc - argument > 2) { fprintf(stderr, "nav: too many directories\n"); return 2; }
    memset(&app, 0, sizeof app);
    if (nav_config_load_file(&app.config, config_file, error, sizeof error))
    {
        fprintf(stderr, "nav: %s\n", error[0] ? error : "unable to load configuration");
        if (config_file) return 2;
    }
    app.show_hidden = app.config.show_hidden;
    if (app.config.warning[0])
        fprintf(stderr, "nav: configuration warning: %s\n", app.config.warning);
    if (!nav_platform_config_dir(config_directory, sizeof config_directory) &&
        snprintf(vault_path, sizeof vault_path, "%s/vault.bin",
                 config_directory) < (int)sizeof vault_path &&
        nav_platform_access(vault_path, F_OK) == 0 &&
        nav_credential_store_open_vault(&app.credential_store, vault_path,
                                        error, sizeof error))
        fprintf(stderr, "nav: unable to open credential Vault: %s\n", error);
    if (!nav_platform_getcwd(cwd, sizeof cwd))
    {
        perror("nav");
        nav_credential_store_close(app.credential_store);
        return 1;
    }
    left = argument < argc ? argv[argument++] : cwd;
    right = argument < argc ? argv[argument] : cwd;
    for (int i = 0; i < 2; i++)
    {
        app.panes[i].provider = nav_provider_for_location(NULL, i ? right : left,
                                                         error, sizeof error);
        if (!app.panes[i].provider) {
            fprintf(stderr, "nav: %s\n", error);
            for (int opened = 0; opened < i; opened++)
                nav_provider_destroy(app.panes[opened].provider);
            nav_credential_store_close(app.credential_store);
            return 2;
        }
        app.panes[i].view = app.config.panel_view;
        app.panes[i].history.current = -1;
        app.panes[i].sort_mode = app.config.sort;
        app.panes[i].directories_first = app.config.directories_first;
        app.panes[i].case_sensitive_sort = app.config.case_sensitive_sort;
        app.panes[i].history_enabled = app.config.history_enabled;
        app.panes[i].history.limit = app.config.history_max_entries > NAV_HISTORY_MAX ? NAV_HISTORY_MAX : (int)app.config.history_max_entries;
    }
    if (nav_pane_open(&app.panes[0], left, app.show_hidden, true, error, sizeof error) || nav_pane_open(&app.panes[1], right, app.show_hidden, true, error, sizeof error))
    {
        fprintf(stderr, "nav: %s\n", error);
        nav_listing_free(&app.panes[0].listing);
        nav_listing_free(&app.panes[1].listing);
        nav_provider_destroy(app.panes[0].provider);
        nav_provider_destroy(app.panes[1].provider);
        nav_credential_store_close(app.credential_store);
        return 1;
    }
    nav_pane_sort(&app.panes[0], app.config.sort);
    nav_pane_sort(&app.panes[1], app.config.sort);
    app.running = true;
    result = nav_ui_run(&app);
    nav_listing_free(&app.panes[0].listing);
    nav_listing_free(&app.panes[1].listing);
    nav_provider_destroy(app.panes[0].provider);
    nav_provider_destroy(app.panes[1].provider);
    nav_credential_store_close(app.credential_store);
    return result;
}
