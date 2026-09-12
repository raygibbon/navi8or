#ifndef NAV_VAULT_INTERNAL_H
#define NAV_VAULT_INTERNAL_H

#include <stddef.h>

#define NAV_VAULT_PAYLOAD_MAX (16u * 1024u * 1024u)

/* Private parser entry point for format-corruption tests. */
int nav_vault_validate_payload(const unsigned char *data, size_t length,
                               char *error, size_t error_size);

#endif
