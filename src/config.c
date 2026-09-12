#include "nav.h"
#include "toml.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int make_directory(const char *path, char *error, size_t error_size)
{
    if (mkdir(path, 0700) == 0 || errno == EEXIST)
        return 0;
    snprintf(error, error_size, "cannot create %s: %s", path, strerror(errno));
    return -1;
}

static int write_file(const char *path, const char *contents, char *error, size_t error_size)
{
    FILE *file = fopen(path, "w");
    if (!file)
    {
        snprintf(error, error_size, "cannot write %s: %s", path, strerror(errno));
        return -1;
    }
    if (fputs(contents, file) == EOF || fclose(file) != 0)
    {
        snprintf(error, error_size, "cannot write %s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

int nav_repository_normalize_url(const char *input, char *output,
                                 size_t capacity, char *error,
                                 size_t error_size)
{
    size_t length;
    const char *authority;
    if (!input || !output || capacity == 0) return -1;
    if (!strncmp(input, "https://", 8)) authority = input + 8;
    else if (!strncmp(input, "http://", 7)) authority = input + 7;
    else {
        snprintf(error, error_size, "repository URL must use http:// or https://");
        return -1;
    }
    size_t authority_length = strcspn(authority, "/");
    bool host_missing = authority_length == 0 ||
        (authority[0] == '['
             ? !memchr(authority + 1, ']', authority_length - 1)
             : authority[0] == ':');
    if (host_missing || memchr(authority, '@', authority_length) ||
        strchr(authority, '#') || strchr(authority, '?')) {
        snprintf(error, error_size, "repository URL must contain a host and no query or fragment");
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)input; *p; p++)
        if (isspace(*p) || iscntrl(*p)) {
            snprintf(error, error_size, "repository URL contains whitespace");
            return -1;
        }
    length = strlen(input);
    if (length + (input[length - 1] == '/' ? 1u : 2u) > capacity) {
        snprintf(error, error_size, "repository URL is too long");
        return -1;
    }
    snprintf(output, capacity, "%s%s", input, input[length - 1] == '/' ? "" : "/");
    return 0;
}

static int validate_repository(const NavRepository *repository, char *error,
                               size_t error_size)
{
    char normalized[NAV_URL_MAX];
    if (!repository->name[0]) {
        snprintf(error, error_size, "repository name must not be empty");
        return -1;
    }
    if (nav_repository_normalize_url(repository->url, normalized,
                                     sizeof normalized, error, error_size))
        return -1;
    if (repository->credential[0] && strncasecmp(normalized, "https://", 8)) {
        snprintf(error, error_size,
                 "credentialed repositories require HTTPS");
        return -1;
    }
    return 0;
}

static void config_warning(NavConfig *config, const char *message)
{
    if (!config->warning[0])
        snprintf(config->warning, sizeof config->warning, "%s", message);
}

void nav_config_defaults(NavConfig *config)
{
    memset(config, 0, sizeof *config);
    config->confirm_delete = true;
    config->confirm_overwrite = true;
    config->directories_first = true;
    config->sort = NAV_SORT_NAME;
    config->panel_view = NAV_PANEL_FULL;
    config->viewer_line_numbers = true;
    config->viewer_current_line = true;
    snprintf(config->editor_command, sizeof config->editor_command, "tdx");
    snprintf(config->editor_args[0], sizeof config->editor_args[0], "{file}");
    config->editor_arg_count = 1;
    config->editor_wait = true;
    config->menu_remember_position = true;
    config->transfer_buffer_size = 4u * 1024u * 1024u;
    config->history_enabled = true;
    config->history_max_entries = 100;
    snprintf(config->theme_name, sizeof config->theme_name, "solar-dark");
}

int nav_config_validate(const NavConfig *config, char *error, size_t error_size)
{
    if (!config->editor_command[0])
    {
        snprintf(error, error_size, "editor.command must not be empty");
        return -1;
    }
    if (!config->editor_arg_count || config->editor_arg_count > NAV_EDITOR_ARG_MAX)
    {
        snprintf(error, error_size, "editor.args count is invalid");
        return -1;
    }
    if (!config->theme_name[0])
    {
        snprintf(error, error_size, "theme.name must not be empty");
        return -1;
    }
    if (config->transfer_buffer_size < NAV_TRANSFER_BUFFER_MIN || config->transfer_buffer_size > NAV_TRANSFER_BUFFER_MAX)
    {
        snprintf(error, error_size, "transfer.buffer_size must be between 64KB and 64MB");
        return -1;
    }
    if (config->repository_count > NAV_REPOSITORY_MAX) {
        snprintf(error, error_size, "too many repositories");
        return -1;
    }
    for (size_t index = 0; index < config->repository_count; index++) {
        if (validate_repository(&config->repositories[index], error, error_size))
            return -1;
        for (size_t prior = 0; prior < index; prior++)
            if (!strcasecmp(config->repositories[prior].name,
                            config->repositories[index].name)) {
                snprintf(error, error_size, "repository names must be unique: %s",
                         config->repositories[index].name);
                return -1;
            }
    }
    return 0;
}

int nav_config_write_defaults(char *error, size_t error_size)
{
    char directory[NAV_PATH_MAX], parent[NAV_PATH_MAX], path[NAV_PATH_MAX], themes[NAV_PATH_MAX], theme_path[NAV_PATH_MAX], repositories_path[NAV_PATH_MAX];
    static const char config_text[] =
        "[nav]\nconfig_version = 1\n\n"
        "[general]\nconfirm_delete = true\nconfirm_overwrite = true\n\n"
        "[panels]\nview = \"full\"\nshow_hidden = false\ndirectories_first = true\nsort = \"name\"\ncase_sensitive_sort = false\n\n"
        "[viewer]\nline_numbers = true\nwrap = false\ncurrent_line = true\n\n"
        "[editor]\ncommand = \"tdx\"\nargs = [\"{file}\"]\nwait = true\n\n"
        "[menu]\nremember_position = true\n\n"
        "[transfer]\nbuffer_size = \"4MB\"\n\n"
        "[history]\nenabled = true\nmax_entries = 100\n\n"
        "[theme]\nname = \"solar-dark\"\n";
    static const char theme_text[] =
        "[theme]\nformat = 2\nname = \"Solar Dark\"\n\n"
        "[ui]\nstyle = \"modern\"\n\n"
        "[ui.header]\nforeground = \"light_cyan\"\nbackground = \"black\"\n"
        "[ui.status]\nforeground = \"dark_gray\"\nbackground = \"black\"\n"
        "[ui.message]\nforeground = \"light_cyan\"\nbackground = \"black\"\n"
        "[ui.text]\nforeground = \"light_gray\"\nbackground = \"black\"\n"
        "[ui.accent]\nforeground = \"cyan\"\nbackground = \"black\"\n"
        "[ui.surface_alt]\nforeground = \"light_gray\"\nbackground = \"dark_gray\"\n"
        "[ui.dialog]\nforeground = \"light_gray\"\nbackground = \"dark_gray\"\n"
        "[ui.dialog_title]\nforeground = \"light_cyan\"\nbackground = \"dark_gray\"\n"
        "[ui.text_dim]\nforeground = \"dark_gray\"\nbackground = \"black\"\n"
        "[ui.selection]\nforeground = \"black\"\nbackground = \"cyan\"\n"
        "[ui.keybar]\nforeground = \"light_gray\"\nbackground = \"black\"\n"
        "[ui.keybar_selected]\nforeground = \"black\"\nbackground = \"cyan\"\n"
        "[ui.keybar_key]\nforeground = \"black\"\nbackground = \"cyan\"\n"
        "[ui.keybar_disabled]\nforeground = \"dark_gray\"\nbackground = \"black\"\n"
        "[ui.menu]\nforeground = \"light_gray\"\nbackground = \"black\"\n"
        "[ui.menu_disabled]\nforeground = \"dark_gray\"\nbackground = \"black\"\n"
        "[ui.menu_selected]\nforeground = \"black\"\nbackground = \"cyan\"\n"
        "[ui.menu_selected_disabled]\nforeground = \"dark_gray\"\nbackground = \"cyan\"\n\n"
        "[ui.frame]\nstyle = \"single\"\nspace = false\nshadow = false\n\n"
        "[ui.viewer_line_number]\nforeground = \"dark_gray\"\nbackground = \"black\"\n"
        "[ui.viewer_search_match]\nforeground = \"black\"\nbackground = \"yellow\"\n"
        "[ui.progress]\nforeground = \"black\"\nbackground = \"cyan\"\n\n"
        "[ui.pane_title]\nforeground = \"light_gray\"\nbackground = \"black\"\n"
        "[ui.pane_title_active]\nforeground = \"cyan\"\nbackground = \"black\"\n\n"
        "[ui.background]\nforeground = \"light_gray\"\nbackground = \"black\"\n"
        "[ui.surface]\nforeground = \"light_gray\"\nbackground = \"black\"\n"
        "[ui.border]\nforeground = \"dark_gray\"\nbackground = \"black\"\n"
        "[ui.border_active]\nforeground = \"cyan\"\nbackground = \"black\"\n"
        "[ui.selection_inactive]\nforeground = \"light_gray\"\nbackground = \"dark_gray\"\n"
        "[ui.path]\nforeground = \"dark_gray\"\nbackground = \"black\"\n"
        "[ui.column_header]\nforeground = \"dark_gray\"\nbackground = \"black\"\n"
        "[ui.error]\nforeground = \"white\"\nbackground = \"red\"\n"
        "[ui.warning]\nforeground = \"yellow\"\nbackground = \"black\"\n\n"
        "[symbols]\ndirectory = \"▸\"\nparent = \"↑\"\nselected = \"›\"\nupload = \"↑\"\ndownload = \"↓\"\n";
    static const char repositories_text[] =
        "# Managed by Navi8or's Repositories menu.\n"
        "# tls_verify defaults to true; writable, mkdir, delete, and rename default to false.\n";
    if (nav_platform_config_dir(directory, sizeof directory))
    {
        snprintf(error, error_size, "configuration path is unavailable");
        return -1;
    }
    snprintf(parent, sizeof parent, "%s", directory);
    char *slash = strrchr(parent, '/');
    if (slash)
    {
        *slash = 0;
        if (make_directory(parent, error, error_size))
            return -1;
    }
    if (make_directory(directory, error, error_size))
        return -1;
    if (snprintf(themes, sizeof themes, "%s/themes", directory) >= (int)sizeof themes || make_directory(themes, error, error_size))
        return -1;
    if (snprintf(path, sizeof path, "%s/nav.toml", directory) >= (int)sizeof path || write_file(path, config_text, error, error_size))
        return -1;
    if (snprintf(repositories_path, sizeof repositories_path,
                 "%s/repositories.toml", directory) >= (int)sizeof repositories_path)
        return -1;
    {
        FILE *repositories = fopen(repositories_path, "wx");
        if (repositories) {
            if (fputs(repositories_text, repositories) == EOF || fclose(repositories) != 0) {
                snprintf(error, error_size, "cannot write %s: %s",
                         repositories_path, strerror(errno));
                return -1;
            }
        } else if (errno != EEXIST) {
            snprintf(error, error_size, "cannot write %s: %s",
                     repositories_path, strerror(errno));
            return -1;
        }
    }
    if (snprintf(theme_path, sizeof theme_path, "%s/solar-dark.toml", themes) >= (int)sizeof theme_path)
        return -1;
    return write_file(theme_path, theme_text, error, error_size);
}

static int write_toml_string(FILE *file, const char *value)
{
    if (fputc('"', file) == EOF) return -1;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\') {
            if (fputc('\\', file) == EOF) return -1;
        }
        if (fputc(*p, file) == EOF) return -1;
    }
    return fputc('"', file) == EOF ? -1 : 0;
}

int nav_config_save_repositories(const NavConfig *config, char *error,
                                 size_t error_size)
{
    char directory[NAV_PATH_MAX], path[NAV_PATH_MAX], temporary[NAV_PATH_MAX];
    int descriptor;
    FILE *file;
    if (nav_config_validate(config, error, error_size)) return -1;
    if (nav_platform_config_dir(directory, sizeof directory) ||
        snprintf(path, sizeof path, "%s/repositories.toml", directory) >= (int)sizeof path ||
        snprintf(temporary, sizeof temporary, "%s/.repositories.toml.XXXXXX", directory) >= (int)sizeof temporary) {
        snprintf(error, error_size, "configuration path is unavailable");
        return -1;
    }
    if (make_directory(directory, error, error_size)) return -1;
    descriptor = mkstemp(temporary);
    if (descriptor < 0 || !(file = fdopen(descriptor, "w"))) {
        if (descriptor >= 0) close(descriptor);
        snprintf(error, error_size, "cannot create repository configuration: %s",
                 strerror(errno));
        return -1;
    }
    if (fputs("# Managed by Navi8or. tls_verify=false is for trusted development servers only.\n", file) == EOF)
        goto write_failed;
    for (size_t index = 0; index < config->repository_count; index++) {
        const NavRepository *repository = &config->repositories[index];
        if (fputs("\n[[repositories]]\nname = ", file) == EOF ||
            write_toml_string(file, repository->name) ||
            fputs("\nurl = ", file) == EOF ||
            write_toml_string(file, repository->url) ||
            (repository->credential[0] &&
             (fputs("\ncredential = ", file) == EOF ||
              write_toml_string(file, repository->credential))) ||
            fprintf(file, "\ntls_verify = %s\nwritable = %s\nmkdir = %s\ndelete = %s\nrename = %s\n",
                    repository->tls_verify ? "true" : "false",
                    repository->writable ? "true" : "false",
                    repository->mkdir_enabled ? "true" : "false",
                    repository->delete_enabled ? "true" : "false",
                    repository->rename_enabled ? "true" : "false") < 0)
            goto write_failed;
    }
    bool failed = fflush(file) != 0;
    if (!failed && fsync(descriptor)) failed = true;
    if (fclose(file)) failed = true;
    if (!failed && rename(temporary, path)) failed = true;
    if (failed) {
        snprintf(error, error_size, "cannot save repositories: %s", strerror(errno));
        unlink(temporary);
        return -1;
    }
    return 0;
write_failed:
    snprintf(error, error_size, "cannot save repositories: %s", strerror(errno));
    fclose(file);
    unlink(temporary);
    return -1;
}

static bool read_bool(const toml_table_t *table, const char *key, bool *value)
{
    toml_datum_t datum = toml_bool_in(table, key);
    if (!datum.ok)
        return false;
    *value = datum.u.b != 0;
    return true;
}

static bool read_string(const toml_table_t *table, const char *key, char *value, size_t capacity)
{
    toml_datum_t datum = toml_string_in(table, key);
    if (!datum.ok)
        return false;
    snprintf(value, capacity, "%s", datum.u.s);
    free(datum.u.s);
    return true;
}

static bool read_integer(const toml_table_t *table, const char *key, size_t *value)
{
    toml_datum_t datum = toml_int_in(table, key);
    if (!datum.ok || datum.u.i < 0)
        return false;
    *value = (size_t)datum.u.i;
    return true;
}

static bool read_size(const toml_table_t *table, const char *key, size_t *value)
{
    if (read_integer(table, key, value))
        return true;
    char text[32];
    if (!read_string(table, key, text, sizeof text))
        return false;
    if (!text[0])
        return false;
    char *end;
    unsigned long long number = strtoull(text, &end, 10);
    size_t multiplier = 1;
    if (!strcasecmp(end, "KB"))
        multiplier = 1024u;
    else if (!strcasecmp(end, "MB"))
        multiplier = 1024u * 1024u;
    else if (!strcasecmp(end, "GB"))
        multiplier = 1024u * 1024u * 1024u;
    else if (*end)
        return false;
    if (number > SIZE_MAX / multiplier)
        return false;
    *value = (size_t)number * multiplier;
    return true;
}

static void read_args(const toml_table_t *table, NavConfig *config)
{
    toml_array_t *array = toml_array_in(table, "args");
    if (!array || toml_array_type(array) != 's')
        return;
    size_t count = (size_t)toml_array_nelem(array);
    if (count > NAV_EDITOR_ARG_MAX)
        count = NAV_EDITOR_ARG_MAX;
    config->editor_arg_count = 0;
    for (size_t index = 0; index < count; index++)
    {
        toml_datum_t datum = toml_string_at(array, (int)index);
        if (!datum.ok)
            continue;
        snprintf(config->editor_args[config->editor_arg_count], NAV_EDITOR_ARG_MAX_LENGTH, "%s", datum.u.s);
        free(datum.u.s);
        config->editor_arg_count++;
    }
}

static void load_repositories(NavConfig *config, const char *directory)
{
    char path[NAV_PATH_MAX], parse_error[256] = {0}, warning[256];
    FILE *file;
    toml_table_t *root;
    toml_array_t *array;
    if (snprintf(path, sizeof path, "%s/repositories.toml", directory) >=
        (int)sizeof path) {
        config_warning(config, "repository configuration path is too long");
        return;
    }
    file = fopen(path, "r");
    if (!file) {
        if (errno != ENOENT) {
            snprintf(warning, sizeof warning, "cannot read repositories.toml: %s",
                     strerror(errno));
            config_warning(config, warning);
        }
        return;
    }
    root = toml_parse_file(file, parse_error, sizeof parse_error);
    fclose(file);
    if (!root) {
        snprintf(warning, sizeof warning, "repositories.toml: %.210s", parse_error);
        config_warning(config, warning);
        return;
    }
    array = toml_array_in(root, "repositories");
    if (!array) {
        toml_free(root);
        return;
    }
    if (toml_array_kind(array) != 't') {
        config_warning(config, "repositories must be an array of tables");
        toml_free(root);
        return;
    }
    for (int index = 0; index < toml_array_nelem(array); index++) {
        toml_table_t *table = toml_table_at(array, index);
        NavRepository repository = {.tls_verify = true};
        char normalized[NAV_URL_MAX], validation[192] = {0};
        bool valid = table && read_string(table, "name", repository.name,
                                          sizeof repository.name) &&
                     read_string(table, "url", repository.url,
                                 sizeof repository.url);
        if (valid && toml_key_exists(table, "credential") &&
            !read_string(table, "credential", repository.credential,
                         sizeof repository.credential))
            valid = false;
        if (valid && toml_key_exists(table, "tls_verify") &&
            !read_bool(table, "tls_verify", &repository.tls_verify))
            valid = false;
        if (valid && toml_key_exists(table, "writable") &&
            !read_bool(table, "writable", &repository.writable))
            valid = false;
        if (valid && toml_key_exists(table, "mkdir") &&
            !read_bool(table, "mkdir", &repository.mkdir_enabled))
            valid = false;
        if (valid && toml_key_exists(table, "delete") &&
            !read_bool(table, "delete", &repository.delete_enabled))
            valid = false;
        if (valid && toml_key_exists(table, "rename") &&
            !read_bool(table, "rename", &repository.rename_enabled))
            valid = false;
        if (valid) {
            if (nav_repository_normalize_url(repository.url, normalized,
                                             sizeof normalized, validation,
                                             sizeof validation))
                valid = false;
            else if (repository.credential[0] &&
                     strncasecmp(normalized, "https://", 8)) {
                snprintf(validation, sizeof validation,
                         "credentialed repositories require HTTPS");
                valid = false;
            } else
                snprintf(repository.url, sizeof repository.url, "%s",
                         normalized);
        }
        for (size_t prior = 0; valid && prior < config->repository_count; prior++)
            if (!strcasecmp(config->repositories[prior].name, repository.name)) {
                snprintf(validation, sizeof validation,
                         "duplicate repository name: %.120s", repository.name);
                valid = false;
            }
        if (!repository.name[0]) {
            snprintf(validation, sizeof validation, "repository %d has an empty name",
                     index + 1);
            valid = false;
        }
        if (!valid) {
            if (!validation[0])
                snprintf(validation, sizeof validation,
                         "repository %d requires string name/url/credential and boolean tls_verify/writable/mkdir/delete/rename",
                         index + 1);
            config_warning(config, validation);
            continue;
        }
        if (config->repository_count == NAV_REPOSITORY_MAX) {
            config_warning(config, "additional repositories were ignored");
            break;
        }
        config->repositories[config->repository_count++] = repository;
    }
    toml_free(root);
}

int nav_config_load(NavConfig *config, char *error, size_t error_size)
{
    char directory[NAV_PATH_MAX], path[NAV_PATH_MAX], parse_error[256] = {0};
    NavConfig candidate;
    nav_config_defaults(&candidate);
    if (nav_platform_config_dir(directory, sizeof directory) || snprintf(path, sizeof path, "%s/nav.toml", directory) >= (int)sizeof path)
    {
        snprintf(error, error_size, "configuration path is unavailable");
        *config = candidate;
        return -1;
    }
    FILE *file = fopen(path, "r");
    if (!file)
    {
        if (errno == ENOENT)
        {
            *config = candidate;
            return nav_config_write_defaults(error, error_size);
        }
        snprintf(error, error_size, "cannot read %s: %s", path, strerror(errno));
        *config = candidate;
        return -1;
    }
    toml_table_t *root = toml_parse_file(file, parse_error, sizeof parse_error);
    fclose(file);
    if (!root)
    {
        snprintf(error, error_size, "configuration error in %s: %s; using defaults", path, parse_error);
        *config = candidate;
        return -1;
    }
    toml_table_t *general = toml_table_in(root, "general");
    toml_table_t *panels = toml_table_in(root, "panels");
    toml_table_t *viewer = toml_table_in(root, "viewer");
    toml_table_t *editor = toml_table_in(root, "editor");
    toml_table_t *menu = toml_table_in(root, "menu");
    toml_table_t *transfer = toml_table_in(root, "transfer");
    toml_table_t *history = toml_table_in(root, "history");
    toml_table_t *theme = toml_table_in(root, "theme");
    if (general)
    {
        read_bool(general, "confirm_delete", &candidate.confirm_delete);
        read_bool(general, "confirm_overwrite", &candidate.confirm_overwrite);
    }
    if (panels)
    {
        char sort[16], view[16];
        read_bool(panels, "show_hidden", &candidate.show_hidden);
        read_bool(panels, "directories_first", &candidate.directories_first);
        read_bool(panels, "case_sensitive_sort", &candidate.case_sensitive_sort);
        if (read_string(panels, "view", view, sizeof view))
        {
            if (!strcmp(view, "full"))
                candidate.panel_view = NAV_PANEL_FULL;
            else if (!strcmp(view, "brief"))
                candidate.panel_view = NAV_PANEL_BRIEF;
            else
                snprintf(candidate.warning, sizeof candidate.warning, "panels.view=\"%s\" is invalid; using \"full\"", view);
        }
        if (read_string(panels, "sort", sort, sizeof sort))
        {
            if (!strcmp(sort, "size"))
                candidate.sort = NAV_SORT_SIZE;
            else if (!strcmp(sort, "date"))
                candidate.sort = NAV_SORT_DATE;
            else if (strcmp(sort, "name"))
                snprintf(candidate.warning, sizeof candidate.warning, "panels.sort=\"%s\" is invalid; using \"name\"", sort);
        }
    }
    if (viewer)
    {
        read_bool(viewer, "line_numbers", &candidate.viewer_line_numbers);
        read_bool(viewer, "wrap", &candidate.viewer_wrap);
        read_bool(viewer, "current_line", &candidate.viewer_current_line);
    }
    if (editor)
    {
        read_string(editor, "command", candidate.editor_command, sizeof candidate.editor_command);
        read_args(editor, &candidate);
        read_bool(editor, "wait", &candidate.editor_wait);
    }
    if (menu)
        read_bool(menu, "remember_position", &candidate.menu_remember_position);
    if (transfer)
    {
        size_t buffer_size;
        if (read_size(transfer, "buffer_size", &buffer_size))
        {
            if (buffer_size < NAV_TRANSFER_BUFFER_MIN || buffer_size > NAV_TRANSFER_BUFFER_MAX)
                snprintf(candidate.warning, sizeof candidate.warning, "transfer.buffer_size is outside 64KB-64MB; using 4MB");
            else
                candidate.transfer_buffer_size = buffer_size;
        }
    }
    if (history)
    {
        read_bool(history, "enabled", &candidate.history_enabled);
        read_integer(history, "max_entries", &candidate.history_max_entries);
        if (candidate.history_max_entries == 0)
            candidate.history_max_entries = 1;
        if (candidate.history_max_entries > NAV_HISTORY_MAX)
            candidate.history_max_entries = NAV_HISTORY_MAX;
    }
    if (theme)
        read_string(theme, "name", candidate.theme_name, sizeof candidate.theme_name);
    toml_free(root);
    load_repositories(&candidate, directory);
    if (nav_config_validate(&candidate, error, error_size))
    {
        *config = candidate;
        return -1;
    }
    *config = candidate;
    return 0;
}
