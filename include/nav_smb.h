#ifndef NAV_SMB_H
#define NAV_SMB_H
#include "nav.h"

/* Canonical URI plus decoded, share-relative components. No local path rules. */
typedef struct
{
    char server[256];
    char share[NAV_NAME_MAX];
    char path[NAV_URL_MAX]; /* Leading slash; / means the share root. */
    char url[NAV_URL_MAX];
} NavSmbUrl;
int nav_smb_url_parse(const char *, NavSmbUrl *, char *, size_t);
bool nav_smb_url_within(const NavSmbUrl *, const NavSmbUrl *);
int nav_smb_url_child(const NavSmbUrl *, const char *, NavSmbUrl *, char *, size_t);
#endif
