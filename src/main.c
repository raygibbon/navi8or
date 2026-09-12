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
    if (argc > 1 && !strcmp(argv[1], "--help"))
    {
        puts("Usage: nav [left-directory] [right-directory]\nNavi8or - keyboard-first local and remote repository navigator.");
        return 0;
    }
    memset(&app, 0, sizeof app);
    if (nav_config_load(&app.config, error, sizeof error))
    {
        fprintf(stderr, "nav: %s\n", error[0] ? error : "unable to load configuration");
        nav_config_defaults(&app.config);
    }
    app.show_hidden = app.config.show_hidden;
    if (app.config.warning[0])
        fprintf(stderr, "nav: configuration warning: %s\n", app.config.warning);
    if (!nav_platform_config_dir(config_directory, sizeof config_directory) &&
        snprintf(vault_path, sizeof vault_path, "%s/vault.bin",
                 config_directory) < (int)sizeof vault_path &&
        access(vault_path, F_OK) == 0 &&
        nav_credential_store_open_vault(&app.credential_store, vault_path,
                                        error, sizeof error))
        fprintf(stderr, "nav: unable to open credential Vault: %s\n", error);
    if (!getcwd(cwd, sizeof cwd))
    {
        perror("nav");
        nav_credential_store_close(app.credential_store);
        return 1;
    }
    left = argc > 1 ? argv[1] : cwd;
    right = argc > 2 ? argv[2] : cwd;
    if ((!strncmp(left, "http://", 7) || !strncmp(left, "https://", 8)) || (!strncmp(right, "http://", 7) || !strncmp(right, "https://", 8)))
    {
        fprintf(stderr, "nav: HTTP providers are planned but not implemented\n");
        nav_credential_store_close(app.credential_store);
        return 2;
    }
    for (int i = 0; i < 2; i++)
    {
        app.panes[i].provider = nav_local_provider();
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
