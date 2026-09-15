#include "nav.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

uint64_t nav_platform_milliseconds(void)
{
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

int nav_open_external_url(const char *url, char *error, size_t capacity)
{
    if (!nav_external_url_valid(url)) {
        snprintf(error, capacity, "Invalid browser URL (embedded credentials are not allowed)"); return -1;
    }
    /* Double fork keeps the browser outside Navi8or's process lifecycle;
     * the pipe reports exec failure rather than silently claiming success. */
    int pipefd[2];
    if (pipe(pipefd)) goto failure;
    if (fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) < 0) {
        close(pipefd[0]); close(pipefd[1]); goto failure;
    }
    pid_t child = fork();
    if (child == 0) {
        close(pipefd[0]);
        pid_t browser = fork();
        if (browser == 0) {
            int nullfd = open("/dev/null", O_RDWR);
            if (nullfd >= 0) {
                dup2(nullfd, STDIN_FILENO); dup2(nullfd, STDOUT_FILENO); dup2(nullfd, STDERR_FILENO);
                if (nullfd > STDERR_FILENO) close(nullfd);
            }
            execlp("xdg-open", "xdg-open", url, (char *)NULL);
        } else if (browser > 0) _exit(0);
        int saved = errno; ssize_t reported = write(pipefd[1], &saved, sizeof saved);
        (void)reported; _exit(127);
    }
    close(pipefd[1]);
    if (child < 0) { close(pipefd[0]); goto failure; }
    int status, saved = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) { }
    ssize_t got;
    do { got = read(pipefd[0], &saved, sizeof saved); } while (got < 0 && errno == EINTR);
    close(pipefd[0]);
    if (got == 0) return 0;
    errno = saved ? saved : EIO;
failure:
    snprintf(error, capacity, "Unable to launch default browser: %s", strerror(errno)); return -1;
}

int nav_platform_config_dir(char *path, size_t capacity)
{
    const char *base = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (base && base[0])
        return snprintf(path, capacity, "%s/nav", base) >= (int)capacity ? -1 : 0;
    if (!home || !home[0])
        return -1;
    return snprintf(path, capacity, "%s/.config/nav", home) >= (int)capacity ? -1 : 0;
}

const NavEditorConfig *nav_editor_default_config(void)
{
    static const char *arguments[] = {"{file}"};
    static const NavEditorConfig config = {"tdx", arguments, 1, true};
    return &config;
}

static char *expand_file(const char *argument, const char *path)
{
    static const char marker[] = "{file}";
    size_t marker_length = sizeof marker - 1;
    size_t path_length = strlen(path), length = 0, count = 0;
    const char *scan = argument, *found;
    while ((found = strstr(scan, marker)))
    {
        count++;
        scan = found + marker_length;
    }
    if (!count)
        return strdup(argument);
    length = strlen(argument) - count * marker_length + count * path_length + 1;
    char *result = malloc(length), *output = result;
    if (!result)
        return NULL;
    scan = argument;
    while ((found = strstr(scan, marker)))
    {
        size_t prefix = (size_t)(found - scan);
        memcpy(output, scan, prefix);
        output += prefix;
        memcpy(output, path, path_length);
        output += path_length;
        scan = found + marker_length;
    }
    strcpy(output, scan);
    return result;
}

int nav_platform_launch_editor(const NavEditorConfig *config, const char *path,
                               char *error, size_t error_size)
{
    char **arguments;
    int status = 0;
    pid_t pid;
    if (!config || !config->executable || !config->executable[0])
    {
        snprintf(error, error_size, "editor is not configured");
        return -1;
    }
    arguments = calloc(config->argument_count + 2, sizeof *arguments);
    if (!arguments)
    {
        snprintf(error, error_size, "out of memory");
        return -1;
    }
    arguments[0] = strdup(config->executable);
    for (size_t i = 0; arguments[0] && i < config->argument_count; i++)
        arguments[i + 1] = expand_file(config->arguments[i], path);
    for (size_t i = 0; i < config->argument_count + 1; i++)
        if (!arguments[i])
        {
            snprintf(error, error_size, "out of memory");
            goto fail;
        }
    pid = fork();
    if (pid < 0)
    {
        snprintf(error, error_size, "cannot launch editor: %s", strerror(errno));
        goto fail;
    }
    if (pid == 0)
    {
        execvp(config->executable, arguments);
        _exit(127);
    }
    if (config->wait)
    {
        if (waitpid(pid, &status, 0) < 0)
        {
            snprintf(error, error_size, "cannot wait for editor: %s", strerror(errno));
            goto fail;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            snprintf(error, error_size, "editor exited unsuccessfully");
            goto fail;
        }
    }
    for (size_t i = 0; i < config->argument_count + 1; i++)
        if (arguments[i]) {
            free(arguments[i]);
        }
    free(arguments);
    return 0;
fail:
    for (size_t i = 0; i < config->argument_count + 1; i++)
        if (arguments[i]) {
            free(arguments[i]);
        }
    free(arguments);
    return -1;
}
