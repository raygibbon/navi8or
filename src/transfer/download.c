/* A staged, cancellable resource copy. Existing copy/move semantics stay intact. */
#include "nav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int nav_download_copy(NavProvider *source, const char *resource,
                      NavProvider *destination, const char *target, bool overwrite,
                      size_t capacity, NavProgressFn progress, NavCancelFn cancel,
                      void *data, char *error, size_t size)
{
    void *reader = NULL, *writer = NULL;
    unsigned char *buffer = NULL;
    NavEntry metadata = {0};
    uint64_t done = 0;
    int result = -1;
    bool created = false;
    char partial[NAV_PATH_MAX];
    if (!nav_provider_supports(source, NAV_CAP_READ) ||
        !nav_provider_supports(destination, NAV_CAP_WRITE | NAV_CAP_RENAME | NAV_CAP_DELETE) ||
        !destination->rename_path || !destination->remove) {
        snprintf(error, size, "Download destination needs write, rename and delete support"); return -1;
    }
    if (snprintf(partial, sizeof partial, "%s.part", target) >= (int)sizeof partial ||
        capacity < 4096 || capacity > NAV_TRANSFER_BUFFER_MAX) {
        snprintf(error, size, "Invalid download path or buffer size"); return -1;
    }
    if (nav_provider_resources_equal(source, resource, destination, target)) {
        snprintf(error, size, "Source and destination are the same resource"); return -1;
    }
    if (source->stat && source->stat(source, resource, &metadata, error, size)) return -1;
    if (metadata.flags & NAV_ENTRY_DIR) {
        snprintf(error, size, "Directory download is not implemented"); return -1;
    }
    bool known = nav_entry_has_known_size(&metadata);
    if (!overwrite && destination->stat) {
        NavEntry existing;
        if (!destination->stat(destination, target, &existing, error, size)) {
            snprintf(error, size, "Destination already exists"); return -1;
        }
    }
    buffer = malloc(capacity);
    if (!buffer) { snprintf(error, size, "Out of memory"); return -1; }
    if (cancel && cancel(data)) { snprintf(error, size, "Download cancelled"); goto finish; }
    if (source->open_read(source, resource, &reader, error, size) ||
        destination->open_write(destination, partial, false, metadata.size, known,
                                &writer, error, size)) goto finish;
    created = true;
    if (progress) progress(0, metadata.size, known, data);
    for (;;) {
        size_t got = 0;
        if (cancel && cancel(data)) { snprintf(error, size, "Download cancelled"); goto finish; }
        int read_result = source->read_cancellable ?
            source->read_cancellable(source, reader, buffer, capacity, &got, cancel, data, error, size) :
            source->read(source, reader, buffer, capacity, &got, error, size);
        if (read_result) goto finish;
        if (!got) break;
        if (destination->write(destination, writer, buffer, got, error, size)) goto finish;
        done += got;
        if (progress) progress(done, metadata.size, known, data);
    }
    if (known && done != metadata.size) {
        snprintf(error, size, "Incomplete download"); goto finish;
    }
    result = 0;
finish:
    if (writer) {
        char closing[256] = {0};
        if (destination->close(destination, writer, closing, sizeof closing) && result == 0) {
            snprintf(error, size, "%s", closing); result = -1;
        }
    }
    if (reader) {
        char closing[256] = {0};
        if (source->close(source, reader, closing, sizeof closing) && result == 0) {
            snprintf(error, size, "%s", closing); result = -1;
        }
    }
    if (!result && destination->rename_path(destination, partial, target, error, size)) result = -1;
    if (created && result) {
        char ignored[128]; destination->remove(destination, partial, ignored, sizeof ignored);
    }
    free(buffer);
    return result;
}
