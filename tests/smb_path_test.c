#include "nav_smb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    NavSmbUrl root, target, child;
    char error[256];
    assert(!nav_smb_url_parse("smb://SERVER/share/projects/./game/../",
                              &root, error, sizeof error));
    assert(!strcmp(root.server, "server"));
    assert(!strcmp(root.share, "share"));
    assert(!strcmp(root.path, "/projects"));
    assert(!strcmp(root.url, "smb://server/share/projects"));
    assert(!nav_smb_url_parse("smb://server/share/projects/game/main.c",
                              &target, error, sizeof error));
    assert(!strcmp(target.path, "/projects/game/main.c"));
    assert(nav_smb_url_within(&root, &target));
    assert(!nav_smb_url_child(&root, "space name.txt", &child, error, sizeof error));
    assert(!strcmp(child.url, "smb://server/share/projects/space%20name.txt"));
    assert(nav_smb_url_within(&root, &child));
    static const char *const outside[] = {
        "smb://other/share/projects/file", "smb://server/other/projects/file",
        "smb://server/share/projects2/file", "smb://server/share/projects/../file",
        "smb://server/share/projects/%2e%2e/file",
    };
    for (size_t index = 0; index < sizeof outside / sizeof outside[0]; index++) {
        assert(!nav_smb_url_parse(outside[index], &target, error, sizeof error));
        assert(!nav_smb_url_within(&root, &target));
    }
    static const char *const malformed[] = {
        "smb://", "smb://server/", "smb:///share", "http://server/share",
        "smb://user:password@server/share", "smb://server/share/../../file",
        "smb://server/share/%2e%2e%2ffile", "smb://server/share/%252e%252e/file",
        "smb://server/share/path\\file", "smb://server/share/file:stream",
        "smb://server/share/%00", "smb://server/share/file.",
    };
    for (size_t index = 0; index < sizeof malformed / sizeof malformed[0]; index++)
        assert(nav_smb_url_parse(malformed[index], &target, error, sizeof error));
    assert(nav_smb_url_child(&root, "..", &child, error, sizeof error));
    puts("SMB paths: ok");
    return 0;
}
