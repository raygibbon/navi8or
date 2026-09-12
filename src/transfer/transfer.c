#include "nav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nav_transfer_copy_with_buffer(NavProvider *source, const char *source_path,
                                  NavProvider *destination, const char *destination_path,
                                  bool overwrite, size_t buffer_size, NavProgressFn progress, void *userdata,
                                  char *error, size_t error_size)
{
    unsigned char *buffer;
    NavEntry metadata = {0};
    bool total_known = false;
    void *reader = NULL, *writer = NULL;
    uint64_t done = 0;
    bool destination_may_be_partial = false;
    int result = -1;
    if (buffer_size < 4096 || buffer_size > 64u * 1024u * 1024u)
    {
        snprintf(error, error_size, "transfer buffer size is outside 4KB-64MB");
        return -1;
    }
    buffer = malloc(buffer_size);
    if (!buffer)
    {
        snprintf(error, error_size, "unable to allocate transfer buffer");
        return -1;
    }
    if (nav_provider_resources_equal(source, source_path, destination, destination_path))
    {
        snprintf(error, error_size, "source and destination are the same file");
        free(buffer);
        return -1;
    }
    if (nav_provider_supports(source, NAV_CAP_STAT) && source->stat &&
        source->stat(source, source_path, &metadata, error, error_size))
        goto finish;
    total_known = (metadata.flags & NAV_ENTRY_SIZE_KNOWN) != 0;
    if (total_known && metadata.flags & NAV_ENTRY_DIR)
    {
        snprintf(error, error_size, "directory copy is not implemented");
        goto finish;
    }
    if (source->open_read(source, source_path, &reader, error, error_size))
        goto finish;
    if (destination->open_write(destination, destination_path, overwrite,
                                metadata.size, total_known, &writer, error,
                                error_size))
        goto finish;
    if (progress)
        progress(0, metadata.size, total_known, userdata);
    for (;;)
    {
        size_t got = 0;
        if (source->read(source, reader, buffer, buffer_size, &got, error, error_size))
            goto finish;
        if (got == 0)
            break;
        if (destination->write(destination, writer, buffer, got, error, error_size))
            goto finish;
        done += (uint64_t)got;
        if (progress)
            progress(done, metadata.size, total_known, userdata);
    }
    result = 0;
finish:
    /* A remote HTTP PUT cannot be cleaned up: Navi8or deliberately has no
       remote DELETE operation.  Query before close destroys write context. */
    if (writer && destination->write_started)
        destination_may_be_partial = destination->write_started(destination, writer);
    if (writer && destination->close(destination, writer, error, error_size) && result == 0)
        result = -1;
    if (reader && source->close(source, reader, error, error_size) && result == 0)
        result = -1;
    if (writer && result != 0 && destination->remove &&
        nav_provider_supports(destination, NAV_CAP_DELETE)) {
        char original[256], cleanup_error[128] = {0};
        snprintf(original, sizeof original, "%s", error && error[0] ? error :
                 "transfer failed");
        destination->remove(destination, destination_path, cleanup_error,
                            sizeof cleanup_error);
        snprintf(error, error_size, "%s", original);
    }
    if (result != 0 && destination_may_be_partial && error && error_size) {
        size_t length = strnlen(error, error_size);
        if (length + sizeof "\nThe remote server may contain a partial file." <= error_size)
            snprintf(error + length, error_size - length,
                     "\nThe remote server may contain a partial file.");
    }
    free(buffer);
    return result;
}

int nav_transfer_copy(NavProvider *source, const char *source_path, NavProvider *destination, const char *destination_path,
                      bool overwrite, NavProgressFn progress, void *userdata, char *error, size_t error_size)
{
    return nav_transfer_copy_with_buffer(source, source_path, destination, destination_path, overwrite, 64u * 1024u, progress, userdata, error, error_size);
}
