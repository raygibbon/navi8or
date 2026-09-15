#include "nav.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    assert(nav_external_url_valid("https://example.com/a(b)?q=x&n=2#part"));
    assert(nav_external_url_valid("http://example.com/utf8/é"));
    const char *bad[] = {"file:///tmp/a", "https://user:secret@example.com/", "http://", "https:///a", "https://example.com/\nhi", "https://example.com\\@evil/", "-x", "https://example.com/a b"};
    char error[256];
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        assert(!nav_external_url_valid(bad[i]));
        assert(nav_open_external_url(bad[i], error, sizeof error) == -1);
    }
    if (argc == 2) {
        int result = nav_open_external_url(argv[1], error, sizeof error);
        if (result) fprintf(stderr, "%s\n", error);
        return result ? 1 : 0;
    }
    return 0;
}
