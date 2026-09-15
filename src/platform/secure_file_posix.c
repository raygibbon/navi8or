#include "secure_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

static void platform_error(char *error, size_t size, const char *message, int code)
{
    if (error && size) snprintf(error, size, "%s: %s", message, strerror(code));
}

int nav_platform_file_exists(const char *path, char *error, size_t error_size)
{
    struct stat status;
    if (!stat(path, &status)) return 1;
    if (errno == ENOENT) return 0;
    platform_error(error, error_size, "Unable to inspect vault", errno);
    return -1;
}

int nav_platform_read_file(const char *path, size_t maximum,
                           unsigned char **data, size_t *length,
                           char *error, size_t error_size)
{
    struct stat status;
    unsigned char *buffer = NULL;
    size_t offset = 0, size;
    int fd, saved;
    if (data) *data = NULL;
    if (length) *length = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        platform_error(error, error_size,
                       errno == ENOENT ? "Vault unavailable" : "Unable to open vault",
                       errno);
        return -1;
    }
    if (fstat(fd, &status)) {
        saved = errno; close(fd);
        platform_error(error, error_size, "Unable to inspect vault", saved);
        return -1;
    }
    if (status.st_size < 0 || (uintmax_t)status.st_size > maximum) {
        close(fd);
        if (error && error_size) snprintf(error, error_size, "Vault file is too large");
        return -1;
    }
    size = (size_t)status.st_size;
    if (size && !(buffer = malloc(size))) {
        close(fd);
        if (error && error_size) snprintf(error, error_size, "Out of memory");
        return -1;
    }
    while (offset < size) {
        ssize_t count = read(fd, buffer + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            saved = count < 0 ? errno : EIO;
            free(buffer); close(fd);
            platform_error(error, error_size, "Unable to read vault", saved);
            return -1;
        }
        offset += (size_t)count;
    }
    if (close(fd)) {
        saved = errno; free(buffer);
        platform_error(error, error_size, "Unable to close vault", saved);
        return -1;
    }
    if (data) *data = buffer; else free(buffer);
    if (length) *length = size;
    return 0;
}

static int write_all(int fd, const unsigned char *data, size_t length)
{
    while (length) {
        ssize_t count = write(fd, data, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        data += count;
        length -= (size_t)count;
    }
    return 0;
}

static int sync_parent_directory(const char *path)
{
    char *directory = strdup(path), *slash;
    int fd, result = 0, saved = 0;
    if (!directory) { errno = ENOMEM; return -1; }
    slash = strrchr(directory, '/');
    if (!slash) strcpy(directory, ".");
    else if (slash == directory) slash[1] = '\0';
    else *slash = '\0';
    fd = open(directory, O_RDONLY | O_DIRECTORY);
    free(directory);
    if (fd < 0) return -1;
    if (fsync(fd)) { result = -1; saved = errno; }
    if (close(fd) && !result) { result = -1; saved = errno; }
    if (result) errno = saved;
    return result;
}

#ifdef NAV_SECURE_FILE_TESTING
static int fail_next_write;
void nav_platform_secure_file_fail_next_write(void) { fail_next_write = 1; }
#endif

int nav_platform_secure_write_atomic(const char *path,
                                     const unsigned char *data, size_t length,
                                     char *error, size_t error_size)
{
    static const char suffix[] = ".tmp.XXXXXX";
    char *temporary;
    size_t path_length;
    int fd = -1, saved = 0;
    const char *message = "Unable to create temporary vault file";
    path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof suffix) {
        if (error && error_size) snprintf(error, error_size, "Vault path is too long");
        return -1;
    }
    temporary = malloc(path_length + sizeof suffix);
    if (!temporary) {
        if (error && error_size) snprintf(error, error_size, "Out of memory");
        return -1;
    }
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, suffix, sizeof suffix);
    fd = mkstemp(temporary);
    if (fd < 0) { saved = errno; goto failure; }
    if (fchmod(fd, 0600)) { message = "Unable to secure temporary vault file"; saved = errno; goto failure; }
    if (write_all(fd, data, length)) { message = "Unable to write vault"; saved = errno; goto failure; }
    if (fsync(fd)) { message = "Unable to sync vault"; saved = errno; goto failure; }
    if (close(fd)) { fd = -1; message = "Unable to close temporary vault file"; saved = errno; goto failure; }
    fd = -1;
#ifdef NAV_SECURE_FILE_TESTING
    if (fail_next_write) { fail_next_write = 0; message = "Unable to replace vault"; saved = EIO; goto failure; }
#endif
    if (rename(temporary, path)) { message = "Unable to replace vault"; saved = errno; goto failure; }
    free(temporary);
    /* Some filesystems do not support directory fsync. The rename has already
       succeeded, so this durability enhancement is best-effort. */
    (void)sync_parent_directory(path);
    return 0;
failure:
    if (fd >= 0) (void)close(fd);
    (void)unlink(temporary);
    free(temporary);
    platform_error(error, error_size, message, saved ? saved : EIO);
    return -1;
}
