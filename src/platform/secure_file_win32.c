/* Navi8or-owned secure-file backend. TDX has temporary-file helpers, but no
 * encrypted Vault atomic-write/ACL implementation to reuse. */
#include "secure_file.h"
#include "windows_internal.h"
#include <aclapi.h>
#include <bcrypt.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void format_win32_error(char *error, size_t size, const char *context, DWORD code)
{
    if (error && size) {
        snprintf(error, size, "%s (Windows error %lu)", context, (unsigned long)code);
    }
}

#ifdef NAV_SECURE_FILE_TESTING
void nav_windows_test_format_error(char *error, size_t size, const char *context, DWORD code)
{
    format_win32_error(error, size, context, code);
}
#endif

static int failure_code(char *error, size_t size, const char *message, DWORD code)
{
    format_win32_error(error, size, message, code);
    return -1;
}
int nav_platform_file_exists(const char *path, char *error, size_t size)
{
    wchar_t *wide = nav_windows_wide(path);
    if (!wide) return failure_code(error, size, "Invalid vault path",
                                   errno == ENOMEM ? ERROR_NOT_ENOUGH_MEMORY : ERROR_NO_UNICODE_TRANSLATION);
    DWORD attributes = GetFileAttributesW(wide);
    DWORD code = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    free(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES) return 1;
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return 0;
    return failure_code(error, size, "Unable to inspect vault", code);
}
int nav_platform_read_file(const char *path, size_t maximum, unsigned char **data,
                           size_t *length, char *error, size_t error_size)
{
    wchar_t *wide = nav_windows_wide(path);
    DWORD code = wide ? ERROR_SUCCESS :
        (errno == ENOMEM ? ERROR_NOT_ENOUGH_MEMORY : ERROR_NO_UNICODE_TRANSLATION);
    HANDLE file = INVALID_HANDLE_VALUE;
    if (wide) {
        file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file == INVALID_HANDLE_VALUE) code = GetLastError();
    }
    unsigned char *buffer = NULL; BY_HANDLE_FILE_INFORMATION info;
    size_t offset = 0; int result = -1; free(wide);
    if (data) *data = NULL;
    if (length) *length = 0;
    if (file == INVALID_HANDLE_VALUE) return failure_code(error, error_size, "Unable to open vault", code);
    if (!GetFileInformationByHandle(file, &info)) { code = GetLastError(); goto done; }
    uint64_t size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    if (size > maximum || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        code = ERROR_INVALID_DATA; goto done;
    }
    buffer = size ? malloc((size_t)size) : NULL;
    if (size && !buffer) { code = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    while (offset < size) {
        DWORD got, chunk = size - offset > 65536 ? 65536 : (DWORD)(size - offset);
        if (!ReadFile(file, buffer + offset, chunk, &got, NULL)) { code = GetLastError(); goto done; }
        if (!got) { code = ERROR_HANDLE_EOF; goto done; }
        offset += got;
    }
    if (data) { *data = buffer; buffer = NULL; }
    if (length) *length = (size_t)size;
    result = 0;
done:
    if (result) failure_code(error, error_size, "Unable to read bounded vault file", code);
    free(buffer); CloseHandle(file); return result;
}
int nav_platform_secure_write_atomic(const char *path, const unsigned char *data,
                                     size_t length, char *error, size_t error_size)
{
    wchar_t *target = nav_windows_wide(path), *temporary = NULL;
    HANDLE token = NULL, file = INVALID_HANDLE_VALUE;
    TOKEN_USER *user = NULL; DWORD needed = 0; PACL acl = NULL;
    SECURITY_DESCRIPTOR descriptor; SECURITY_ATTRIBUTES security = {sizeof security, &descriptor, FALSE};
    EXPLICIT_ACCESSW access_entry = {0}; int result = -1; bool created = false;
    DWORD code = ERROR_SUCCESS;
    if (!target) {
        code = errno == ENOMEM ? ERROR_NOT_ENOUGH_MEMORY : ERROR_NO_UNICODE_TRANSLATION;
        goto done;
    }
    temporary = malloc((wcslen(target) + 40) * sizeof *temporary);
    if (!temporary) { code = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { code = GetLastError(); goto done; }
    if (!GetTokenInformation(token, TokenUser, NULL, 0, &needed)) {
        code = GetLastError();
        if (code != ERROR_INSUFFICIENT_BUFFER) goto done;
    }
    if (!needed) { code = ERROR_INVALID_DATA; goto done; }
    user = malloc(needed);
    if (!user) { code = ERROR_NOT_ENOUGH_MEMORY; goto done; }
    if (!GetTokenInformation(token, TokenUser, user, needed, &needed)) { code = GetLastError(); goto done; }
    access_entry.grfAccessPermissions = GENERIC_ALL;
    access_entry.grfAccessMode = SET_ACCESS;
    access_entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access_entry.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access_entry.Trustee.ptstrName = (LPWSTR)user->User.Sid;
    code = SetEntriesInAclW(1, &access_entry, NULL, &acl);
    if (code != ERROR_SUCCESS) goto done;
    if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) ||
        !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) { code = GetLastError(); goto done; }
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        unsigned char random[12]; wchar_t suffix[25];
        NTSTATUS status = BCryptGenRandom(NULL, random, sizeof random, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status != 0) {
            snprintf(error, error_size, "Unable to save vault securely (BCryptGenRandom NTSTATUS 0x%08lx)", (unsigned long)status);
            goto cleanup;
        }
        for (size_t i = 0; i < sizeof random; i++) swprintf(suffix + i * 2, 3, L"%02x", random[i]);
        swprintf(temporary, wcslen(target) + 40, L"%ls.%ls.tmp", target, suffix);
        file = CreateFileW(temporary, GENERIC_WRITE, 0, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) { created = true; break; }
        code = GetLastError();
        if (code != ERROR_FILE_EXISTS) goto done;
    }
    if (!created) goto done;
    while (length) {
        DWORD written, chunk = length > 65536 ? 65536 : (DWORD)length;
        if (!WriteFile(file, data, chunk, &written, NULL)) { code = GetLastError(); goto done; }
        if (!written) { code = ERROR_WRITE_FAULT; goto done; }
        data += written; length -= written;
    }
    if (!FlushFileBuffers(file)) { code = GetLastError(); goto done; }
    if (!CloseHandle(file)) { code = GetLastError(); file = INVALID_HANDLE_VALUE; goto done; }
    file = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(temporary, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { code = GetLastError(); goto done; }
    result = 0;
done:
    if (result) failure_code(error, error_size, "Unable to save vault securely", code);
cleanup:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (created && result) DeleteFileW(temporary);
    if (token) CloseHandle(token);
    if (acl) LocalFree(acl);
    free(user); free(temporary); free(target); return result;
}
