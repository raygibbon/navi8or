/* Exercise the exact archives used by nav, including default TLS/proxy policy. */
#include <curl/curl.h>
#include <openssl/opensslv.h>
#include <sodium.h>
#include <stdio.h>
#include <string.h>
static size_t discard(char *data, size_t size, size_t count, void *context)
{
    (void)data; (void)context;
    return size * count;
}
int main(int argc, char **argv)
{
    CURL *easy;
    CURLcode result;
    if (argc == 1) {
        const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
        const char *const *protocol;
        printf("curl %s; %s; libsodium %s; zlib %s\n", info->version,
               info->ssl_version, sodium_version_string(), info->libz_version);
        for (protocol = info->protocols; *protocol; ++protocol)
            printf("protocol: %s\n", *protocol);
        return 0;
    }
    if (argc != 2 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    easy = curl_easy_init();
    if (!easy) { curl_global_cleanup(); return 1; }
    curl_easy_setopt(easy, CURLOPT_URL, argv[1]);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    result = curl_easy_perform(easy);
    if (result != CURLE_OK) fprintf(stderr, "%s\n", curl_easy_strerror(result));
    curl_easy_cleanup(easy);
    curl_global_cleanup();
    return result == CURLE_OK ? 0 : 1;
}
