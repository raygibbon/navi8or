#include "nav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void path_join(char *out, size_t size, const char *root, const char *suffix)
{
    assert(snprintf(out, size, "%s/%s", root, suffix) < (int)size);
}

static int file_contains(const char *path, const char *needle)
{
    char contents[16384];
    FILE *file = fopen(path, "r");
    size_t length;
    assert(file);
    length = fread(contents, 1, sizeof contents - 1, file);
    assert(!ferror(file));
    assert(fclose(file) == 0);
    contents[length] = 0;
    return strstr(contents, needle) != NULL;
}

static void test_explicit_config(void)
{
    char root[] = "/tmp/nav-explicit-XXXXXX", path[4096], missing[4096], error[256];
    NavConfig config;
    assert(mkdtemp(root));
    path_join(path, sizeof path, root, "custom.toml");
    path_join(missing, sizeof missing, root, "missing.toml");
    assert(nav_config_load_file(&config, missing, error, sizeof error) != 0);
    assert(access(missing, F_OK) != 0);
    FILE *file = fopen(path, "w"); assert(file);
    fputs("[app]\ntheme = \"classic-dos\"\nshow_hidden = true\n"
          "[keys.panel]\n\"F5\" = \"file.view\"\n\"Ctrl+X C\" = \"file.copy\"\n", file);
    assert(fclose(file) == 0);
    assert(nav_config_load_file(&config, path, error, sizeof error) == 0);
    assert(config.explicit_config && !strcmp(config.config_path, path));
    assert(config.show_hidden && !strcmp(config.theme_name, "classic-dos"));
    assert(nav_config_save_repositories(&config, error, sizeof error) == 0);
    char sidecar[4096]; path_join(sidecar, sizeof sidecar, root, "repositories.toml");
    assert(access(sidecar, F_OK) == 0); unlink(sidecar);
    NavInput input = {0}; NavTermEvent event = {.type = NAV_TERM_EVENT_KEY, .key = NAV_KEY_F5};
    assert(nav_input_resolve(&input, &config.keymap, NAV_CONTEXT_PANEL, &event).command == NAV_CMD_VIEW);
    const char *invalid[] = {
        "[keys.panel]\n\"F5\" = \"does.not.exist\"\n",
        "[keys.panel]\n\"F5\" = \"file.copy\"\n\"f5\" = \"file.view\"\n",
        "[keys.panel]\n\"Ctrl+F\" = \"file.copy\"\n",
        "[keys.invalid]\n\"F5\" = \"file.copy\"\n",
        "[keys.panel]\n\"F5\" = 42\n"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        file = fopen(path, "w"); assert(file); fputs(invalid[i], file); fclose(file);
        assert(nav_config_load_file(&config, path, error, sizeof error) != 0);
        assert(error[0]);
    }
    unlink(path); rmdir(root);
}

int main(void)
{
    char root[] = "/tmp/nav-config-XXXXXX", path[4096], repositories_path[4096], error[256] = {0};
    assert(mkdtemp(root));
    assert(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    unsetenv("HOME");
    NavConfig config;
    test_explicit_config();
    assert(nav_config_load(&config, error, sizeof error) == 0);
    path_join(path, sizeof path, root, "nav/nav.toml");
    assert(access(path, F_OK) == 0);
    path_join(repositories_path, sizeof repositories_path, root,
              "nav/repositories.toml");
    assert(access(repositories_path, F_OK) == 0);
    path_join(path, sizeof path, root, "nav/themes/solar-dark.toml");
    assert(access(path, F_OK) == 0);
    assert(file_contains(path, "format = 2"));
    assert(file_contains(path, "style = \"modern\""));
    assert(file_contains(path, "[ui.background]"));
    assert(file_contains(path, "[ui.keybar_key]"));
    assert(file_contains(path, "[ui.keybar_disabled]"));
    assert(!file_contains(path, "[tdx."));
    assert(config.editor_arg_count == 1);
    assert(config.panel_view == NAV_PANEL_FULL);
    assert(!strcmp(config.editor_args[0], "{file}"));
    path_join(path, sizeof path, root, "nav/nav.toml");
    FILE *file = fopen(path, "w");
    assert(file);
    fputs("[editor]\ncommand = \"nvim\"\nargs = [\"--clean\", \"{file}\"]\nwait = false\n\n[panels]\nsort = \"date\"\nview = \"full\"\n", file);
    fclose(file);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(!strcmp(config.editor_command, "nvim"));
    assert(config.editor_arg_count == 2 && !strcmp(config.editor_args[0], "--clean"));
    assert(!strcmp(config.editor_args[1], "{file}") && !config.editor_wait);
    assert(config.sort == NAV_SORT_DATE);
    assert(config.panel_view == NAV_PANEL_FULL);
    file = fopen(repositories_path, "w");
    assert(file);
    fputs("[[repositories]]\nname = \"One\"\nurl = \"https://example.test/root\"\n"
          "credential = \"local-basic\"\n\n"
          "[[repositories]]\nname = \"Two\"\nurl = \"http://localhost:8000/\"\ntls_verify = false\nwritable = true\nmkdir = true\ndelete = true\nrename = true\n\n"
          "[[repositories]]\nname = \"Three\"\n"
          "url = \"https://bearer.example.test/\"\n"
          "credential = \"local-bearer\"\n",
          file);
    fclose(file);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(config.repository_count == 3);
    assert(!strcmp(config.repositories[0].name, "One"));
    assert(!strcmp(config.repositories[0].url, "https://example.test/root/"));
    assert(!strcmp(config.repositories[0].credential, "local-basic"));
    assert(config.repositories[0].tls_verify);
    assert(!config.repositories[0].writable && !config.repositories[0].mkdir_enabled &&
           !config.repositories[0].delete_enabled && !config.repositories[0].rename_enabled);
    assert(!config.repositories[1].tls_verify && config.repositories[1].writable &&
           config.repositories[1].mkdir_enabled && config.repositories[1].delete_enabled &&
           config.repositories[1].rename_enabled);
    assert(!strcmp(config.repositories[2].credential, "local-bearer"));
    assert(nav_config_save_repositories(&config, error, sizeof error) == 0);
    assert(file_contains(repositories_path, "credential = \"local-basic\""));
    assert(!file_contains(repositories_path, "testpass"));
    assert(!file_contains(repositories_path, "bearer token"));
    assert(!file_contains(repositories_path, "auth_type"));
    assert(!file_contains(repositories_path, "password") &&
           !file_contains(repositories_path, "bearer-test-token"));
    assert(nav_config_load(&config, error, sizeof error) == 0 &&
           config.repository_count == 3);
    assert(!config.repositories[0].writable && config.repositories[1].writable &&
           !strcmp(config.repositories[0].credential, "local-basic") &&
           !config.repositories[1].credential[0] &&
           config.repositories[1].mkdir_enabled && config.repositories[1].delete_enabled &&
           config.repositories[1].rename_enabled &&
           !strcmp(config.repositories[2].credential, "local-bearer"));
    file = fopen(repositories_path, "w");
    assert(file);
    fputs("[[repositories]]\nname = \"Bad Writable\"\n"
          "url = \"https://example.test/\"\nwritable = \"yes\"\n", file);
    fclose(file);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(config.repository_count == 0 && strstr(config.warning, "writable"));
    file = fopen(repositories_path, "w"); assert(file);
    fputs("[[repositories]]\nname = \"Bad Mutation\"\n"
          "url = \"https://example.test/\"\nmkdir = \"yes\"\n", file);
    assert(fclose(file) == 0);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(config.repository_count == 0 && strstr(config.warning, "mkdir"));
    file = fopen(repositories_path, "w"); assert(file);
    fputs("[[repositories]]\nname = \"Bad Rename\"\n"
          "url = \"https://example.test/\"\nrename = \"yes\"\n", file);
    assert(fclose(file) == 0);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(config.repository_count == 0 && strstr(config.warning, "rename"));
    file = fopen(repositories_path, "w"); assert(file);
    fputs("[[repositories]]\nname = \"Bad Credential\"\n"
          "url = \"https://example.test/\"\ncredential = true\n", file);
    assert(fclose(file) == 0);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(config.repository_count == 0 && strstr(config.warning, "credential"));
    file = fopen(repositories_path, "w"); assert(file);
    fputs("[[repositories]]\nname = \"Insecure Credential\"\n"
          "url = \"http://example.test/\"\ncredential = \"local-basic\"\n", file);
    assert(fclose(file) == 0);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(config.repository_count == 0 && strstr(config.warning, "require HTTPS"));
    file = fopen(repositories_path, "w");
    assert(file);
    fputs("[[repositories]]\nname = \"Good\"\nurl = \"https://example.test/\"\n\n"
          "[[repositories]]\nname = \"Good\"\nurl = \"https://duplicate.test/\"\n\n"
          "[[repositories]]\nname = \"\"\nurl = \"https://empty.test/\"\n\n"
          "[[repositories]]\nname = \"Bad URL\"\nurl = \"file:///tmp\"\n",
          file);
    fclose(file);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(config.repository_count == 1 && !strcmp(config.repositories[0].name, "Good"));
    assert(config.warning[0]);
    assert(nav_repository_normalize_url("http://:8000/", path, sizeof path,
                                        error, sizeof error) != 0);
    assert(nav_repository_normalize_url("https://user@example.test/", path,
                                        sizeof path, error, sizeof error) != 0);
    config.repository_count = 2;
    config.repositories[1] = config.repositories[0];
    assert(nav_config_validate(&config, error, sizeof error) != 0);
    file = fopen(path, "w");
    assert(file); fputs("[panels]\nview = \"banana\"\n", file); fclose(file);
    assert(nav_config_load(&config, error, sizeof error) == 0);
    assert(config.panel_view == NAV_PANEL_FULL && strstr(config.warning, "panels.view"));
    file = fopen(path, "w");
    assert(file);
    fputs("[broken\n", file);
    fclose(file);
    assert(nav_config_load(&config, error, sizeof error) != 0);
    assert(config.editor_arg_count == 1 && !strcmp(config.editor_command, "tdx"));
    unlink(repositories_path);
    unlink(path);
    path_join(path, sizeof path, root, "nav/themes/solar-dark.toml");
    unlink(path);
    path_join(path, sizeof path, root, "nav/themes");
    rmdir(path);
    path_join(path, sizeof path, root, "nav");
    rmdir(path);
    rmdir(root);
    return 0;
}
