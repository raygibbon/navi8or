#ifndef NAV_PLATFORM_SECURE_FILE_H
#define NAV_PLATFORM_SECURE_FILE_H

#include <stddef.h>

int nav_platform_file_exists(const char *path, char *error, size_t error_size);
int nav_platform_read_file(const char *path, size_t maximum,
                           unsigned char **data, size_t *length,
                           char *error, size_t error_size);
int nav_platform_secure_write_atomic(const char *path,
                                     const unsigned char *data, size_t length,
                                     char *error, size_t error_size);

#ifdef NAV_SECURE_FILE_TESTING
void nav_platform_secure_file_fail_next_write(void);
#endif

#endif
