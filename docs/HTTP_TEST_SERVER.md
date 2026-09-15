# Local HTTPS repository test server

This setup provides local HTTPS endpoints for developing and testing Navi8or's HTTP repository provider.

It supports three authentication modes:

| Mode | URL | Authentication |
|---|---|---|
| Anonymous | `https://localhost:8443/` | None |
| Basic | `https://localhost:8444/` | Username/password |
| Bearer | `https://localhost:8445/` | Bearer token |

The endpoints can exercise:

- directory browsing
- HTTP Range reads for F3 Viewer
- full GET downloads for F5
- PUT uploads for F5
- MOVE rename for F6
- MKCOL directory creation for F7
- DELETE for F8
- TLS verification behavior
- Basic authentication
- Bearer-token authentication

This setup is for **local development only**. Do not expose these writable test endpoints to a LAN or the public Internet.

Navi8or does not depend on nginx or on any of the paths shown below.

## 1. Install packages

On Debian:

```sh
sudo apt update
sudo apt install nginx openssl libcurl4-openssl-dev apache2-utils
```

`apache2-utils` provides `htpasswd`, which is used for the Basic-auth test endpoint.

## 2. Prepare the test tree

For read-only browsing of the live Navi8or checkout, a bind mount is convenient:

```sh
sudo mkdir -p /srv/navi8or-test
sudo mount --bind /home/rgibbon/Sync/git/navi8or /srv/navi8or-test
```

Changes in the worktree become immediately visible through nginx.

For mutation testing, using a separate disposable directory is safer than allowing nginx to modify the live source checkout:

```sh
sudo mkdir -p /srv/navi8or-upload-test
sudo chown www-data:www-data /srv/navi8or-upload-test
sudo chmod 0755 /srv/navi8or-upload-test
```

You can use either tree while developing, but a dedicated writable directory is strongly recommended for PUT, MOVE, MKCOL, and DELETE testing.

## 3. Create a localhost TLS certificate

Create a self-signed certificate valid for `localhost` and `127.0.0.1`:

```sh
sudo mkdir -p /etc/nginx/navi8or-test

sudo openssl req \
  -x509 \
  -newkey rsa:2048 \
  -nodes \
  -days 365 \
  -keyout /etc/nginx/navi8or-test/key.pem \
  -out /etc/nginx/navi8or-test/cert.pem \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

This certificate is for development only.

With Navi8or's TLS verification enabled, the certificate should fail verification unless you explicitly trust it. With `Verify TLS` disabled, Navi8or should connect after its normal insecure-TLS confirmation.

Navi8or must never disable TLS verification globally.

## 4. Create the Basic-auth test account

Create a password file:

```sh
sudo htpasswd -c /etc/nginx/navi8or-test/htpasswd navi8or
```

For a disposable development environment you can use a simple test password, for example:

```text
Username: navi8or
Password: testpass
```

Do not reuse a real password.

## 5. Configure nginx

Create:

```text
/etc/nginx/sites-available/navi8or-test
```

with the following content:

```nginx
# Bearer-token validation for the development endpoint on port 8445.
#
# Debian includes files from sites-enabled inside nginx's http {} context,
# where the map directive is valid.
#
# Development token:
#     navi8or-test-token-12345
#
# Do not use this fixed-token scheme in production.

map $http_authorization $navi8or_bearer_ok {
    default 0;
    "Bearer navi8or-test-token-12345" 1;
}

# ---------------------------------------------------------------------------
# Anonymous HTTPS endpoint
# ---------------------------------------------------------------------------

server {
    listen 127.0.0.1:8443 ssl;
    server_name localhost;

    ssl_certificate     /etc/nginx/navi8or-test/cert.pem;
    ssl_certificate_key /etc/nginx/navi8or-test/key.pem;

    root /srv/navi8or-test;

    autoindex on;
    autoindex_exact_size on;
    autoindex_localtime on;

    location / {
        # Development-only anonymous DAV endpoint.
        # This server is deliberately bound to localhost.
        dav_methods PUT DELETE MKCOL COPY MOVE;
        create_full_put_path on;
        dav_access user:rw group:rw all:r;

        try_files $uri $uri/ =404;
    }
}

# ---------------------------------------------------------------------------
# HTTP Basic-auth endpoint
# ---------------------------------------------------------------------------

server {
    listen 127.0.0.1:8444 ssl;
    server_name localhost;

    ssl_certificate     /etc/nginx/navi8or-test/cert.pem;
    ssl_certificate_key /etc/nginx/navi8or-test/key.pem;

    root /srv/navi8or-test;

    autoindex on;
    autoindex_exact_size on;
    autoindex_localtime on;

    auth_basic "Navi8or Test";
    auth_basic_user_file /etc/nginx/navi8or-test/htpasswd;

    location / {
        dav_methods PUT DELETE MKCOL COPY MOVE;
        create_full_put_path on;
        dav_access user:rw group:rw all:r;

        try_files $uri $uri/ =404;
    }
}

# ---------------------------------------------------------------------------
# Bearer-token endpoint
# ---------------------------------------------------------------------------

server {
    listen 127.0.0.1:8445 ssl;
    server_name localhost;

    ssl_certificate     /etc/nginx/navi8or-test/cert.pem;
    ssl_certificate_key /etc/nginx/navi8or-test/key.pem;

    root /srv/navi8or-test;

    autoindex on;
    autoindex_exact_size on;
    autoindex_localtime on;

    location / {
        # Safe use of nginx "if" for this development fixture because the only
        # action is an immediate return.
        if ($navi8or_bearer_ok = 0) {
            return 401;
        }

        dav_methods PUT DELETE MKCOL COPY MOVE;
        create_full_put_path on;
        dav_access user:rw group:rw all:r;

        try_files $uri $uri/ =404;
    }
}
```

All three servers are explicitly bound to `127.0.0.1`.

Do not change these listeners to `0.0.0.0` unless you intentionally understand the security consequences.

## 6. Enable the nginx site

Enable the configuration:

```sh
sudo ln -s /etc/nginx/sites-available/navi8or-test \
  /etc/nginx/sites-enabled/navi8or-test
```

If the symlink already exists, do not recreate it.

Validate the configuration:

```sh
sudo nginx -t
```

Then reload nginx:

```sh
sudo systemctl reload nginx
```

## 7. Test the anonymous endpoint

This should return an HTML directory index:

```sh
curl -k https://localhost:8443/
```

A ranged request should return a bounded response:

```sh
curl -k \
  -H 'Range: bytes=0-65535' \
  -D - \
  https://localhost:8443/README.md \
  -o /tmp/navi8or-range-test.bin
```

For a server supporting byte ranges, expect:

```text
206 Partial Content
```

## 8. Test HTTP Basic authentication

Without credentials:

```sh
curl -k https://localhost:8444/
```

Expect:

```text
401 Unauthorized
```

With credentials:

```sh
curl -k \
  -u navi8or:testpass \
  https://localhost:8444/
```

The directory index should be returned.

Range reads must also authenticate:

```sh
curl -k \
  -u navi8or:testpass \
  -H 'Range: bytes=0-65535' \
  https://localhost:8444/README.md \
  -o /tmp/navi8or-basic-range.bin
```

## 9. Test Bearer authentication

Without a token:

```sh
curl -k https://localhost:8445/
```

Expect:

```text
401 Unauthorized
```

With the development token:

```sh
curl -k \
  -H 'Authorization: Bearer navi8or-test-token-12345' \
  https://localhost:8445/
```

The directory index should be returned.

Range reads must also carry the token:

```sh
curl -k \
  -H 'Authorization: Bearer navi8or-test-token-12345' \
  -H 'Range: bytes=0-65535' \
  https://localhost:8445/README.md \
  -o /tmp/navi8or-bearer-range.bin
```

## 10. Optional large streaming fixtures

Do not commit generated large fixtures to Git.

Create a 5 MiB fixture:

```sh
dd if=/dev/zero \
  of=/home/rgibbon/Sync/git/navi8or/http-5mb.bin \
  bs=1M \
  count=5
```

Create a sparse 100 MiB fixture:

```sh
truncate -s 100M \
  /home/rgibbon/Sync/git/navi8or/http-100mb.bin
```

For a large line-oriented Viewer fixture:

```sh
awk 'BEGIN {
    for (i = 0; i < 1000000; i++)
        printf "line %08d remote range test data\n", i
}' > /home/rgibbon/Sync/git/navi8or/http-large-lines.txt
```

After testing, remove the fixtures:

```sh
rm -f \
  /home/rgibbon/Sync/git/navi8or/http-5mb.bin \
  /home/rgibbon/Sync/git/navi8or/http-100mb.bin \
  /home/rgibbon/Sync/git/navi8or/http-large-lines.txt
```

## 11. Test F3 range-backed viewing

Open a large fixture with F3.

Use:

- PageDown
- PageUp
- End
- search for a late line

Navi8or's HTTP Viewer should use bounded Range requests rather than downloading the complete object before displaying it.

The current Viewer cache uses sixteen 64 KiB blocks and evicts least-recently-used blocks as required.

The nginx access log should show bounded `206` requests.

For example:

```sh
sudo tail -f /var/log/nginx/access.log
```

Browsing or merely highlighting a file must not fetch that file's body.

## 12. Test DAV mutations manually with curl

For mutation tests, prefer using a dedicated writable tree such as `/srv/navi8or-upload-test`.

If you change the nginx `root` for one of the test endpoints to that directory, ensure nginx can write to it.

### PUT

```sh
printf 'hello from Navi8or\n' > /tmp/navi8or-put.txt

curl -k \
  -T /tmp/navi8or-put.txt \
  https://localhost:8443/navi8or-put.txt
```

For Basic auth:

```sh
curl -k \
  -u navi8or:testpass \
  -T /tmp/navi8or-put.txt \
  https://localhost:8444/navi8or-put.txt
```

For Bearer auth:

```sh
curl -k \
  -H 'Authorization: Bearer navi8or-test-token-12345' \
  -T /tmp/navi8or-put.txt \
  https://localhost:8445/navi8or-put.txt
```

### MKCOL

Anonymous:

```sh
curl -k \
  -X MKCOL \
  https://localhost:8443/test-folder/
```

Basic:

```sh
curl -k \
  -u navi8or:testpass \
  -X MKCOL \
  https://localhost:8444/test-folder/
```

Bearer:

```sh
curl -k \
  -H 'Authorization: Bearer navi8or-test-token-12345' \
  -X MKCOL \
  https://localhost:8445/test-folder/
```

### MOVE

Anonymous example:

```sh
curl -k \
  -X MOVE \
  -H 'Destination: https://localhost:8443/navi8or-renamed.txt' \
  -H 'Overwrite: F' \
  https://localhost:8443/navi8or-put.txt
```

For authenticated endpoints, include the corresponding Basic credentials or Bearer header.

### DELETE

Anonymous:

```sh
curl -k \
  -X DELETE \
  https://localhost:8443/navi8or-renamed.txt
```

Basic:

```sh
curl -k \
  -u navi8or:testpass \
  -X DELETE \
  https://localhost:8444/navi8or-renamed.txt
```

Bearer:

```sh
curl -k \
  -H 'Authorization: Bearer navi8or-test-token-12345' \
  -X DELETE \
  https://localhost:8445/navi8or-renamed.txt
```

## 13. Navi8or repository configurations

### Anonymous repository

In Navi8or choose:

```text
Repositories -> Add Repository
```

Use:

```text
Name:         Local Navi8or Anonymous
URL:          https://localhost:8443/
Verify TLS:   Off
Writable PUT: On
MkDir:        On
Delete:       On
Rename:       On
Credential:   <none>
```

### Basic-auth repository

Target configuration:

```text
Name:         Local Navi8or Basic
URL:          https://localhost:8444/
Verify TLS:   Off
Writable PUT: On
MkDir:        On
Delete:       On
Rename:       On
Credential:   local-basic
```

The corresponding credential record should contain:

```text
Username: navi8or
Password: testpass
```

### Bearer-token repository

Target configuration:

```text
Name:         Local Navi8or Bearer
URL:          https://localhost:8445/
Verify TLS:   Off
Writable PUT: On
MkDir:        On
Delete:       On
Rename:       On
Credential:   local-bearer
```

The corresponding credential record should contain:

```text
Token: navi8or-test-token-12345
```

Use **Repositories → Add Repository**, enter Name and URL, then choose
**Credential**. For port 8443 select **<none>**. For 8444 select `local-basic`
(Basic, username `navi8or`); for 8445 select `local-bearer` (Bearer).
If the record does not exist, choose **+ Add credential...**. Navi8or creates
an encrypted Vault with a confirmed master password, or unlocks an existing
Vault, then lets you choose Basic/Bearer and enter the fields shown above.
Passwords and tokens require masked confirmation. The new credential is selected
automatically. **Unlock Vault...** lists existing records when the Vault is locked.
Esc returns to the previous layer and keeps the repository fields and selection.
The Vault master password unlocks local storage; the credential password/token
authenticates to the server. Only the selected name is saved in repository config;
the credential type determines the authentication method.

## 14. Authentication must apply to every HTTP operation

The selected credential applies consistently to all requests made by that repository instance.

This includes:

- directory LIST requests
- HEAD/STAT requests
- Range GET requests used by F3
- full GET requests used by F5 download
- PUT requests used by F5 upload
- MOVE requests used by F6
- MKCOL requests used by F7
- DELETE requests used by F8

Do not authenticate only the initial directory listing.

A repository that opens successfully but returns `401` when F3 or F5 is used is considered broken.

## 15. Navi8or authentication architecture

Repository configuration should reference a credential rather than embedding secrets directly into the ordinary repository record.

Conceptually:

```text
Repository
    |
    +-- URL
    +-- TLS policy
    +-- capabilities
    +-- credential reference
            |
            v
      CredentialStore
            |
            +-- Basic username/password
            +-- Bearer token
```

The HTTP provider resolves the referenced credential for each operation and
wipes the resolved copy afterward.

The HTTP provider should not know how encrypted credentials are stored.

Likewise, the credential store should not know how libcurl performs HTTP requests.

## 16. libcurl mapping

A Basic credential maps naturally to libcurl options such as:

```c
curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
curl_easy_setopt(curl, CURLOPT_USERNAME, username);
curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
```

Bearer authentication can use libcurl's Bearer support where available:

```c
curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, token);
```

Alternatively, a correctly managed request header can be used:

```text
Authorization: Bearer <token>
```

Do not log passwords or tokens.

Do not copy secrets into status messages, URLs, error messages, or debug output.

## 17. TLS verification

For these self-signed localhost endpoints:

```text
Verify TLS: Off
```

is expected during development.

Navi8or should request additional confirmation before saving or using a repository with TLS verification disabled.

With:

```text
Verify TLS: On
```

opening these repositories should normally fail certificate validation unless the development certificate has been added to the trusted CA store.

Production repositories should use trusted certificates and TLS verification should remain enabled.

## 18. Security notes

These endpoints are deliberately powerful.

When mutation support is enabled, clients can potentially:

- upload files
- create directories
- rename files/directories
- delete files/directories

Keep the test servers bound to:

```text
127.0.0.1
```

Do not expose anonymous DAV to a LAN or public network.

Do not reuse the example password or Bearer token anywhere outside this disposable test environment.

For destructive tests, prefer:

```text
/srv/navi8or-upload-test
```

over the live Navi8or source checkout.

## 19. Current HTTP adapter behavior

Navi8or's generic HTTP repository adapter consumes simple server-generated `<a href>` directory listings.

Ordinary browsing:

- fetches the current directory listing
- does not render remote HTML
- does not execute scripts
- does not recursively fetch subdirectories
- does not download highlighted file bodies
- does not issue one metadata request per visible entry

With the configuration above, nginx HTML rows end with
`</a> DD-Mon-YYYY HH:MM exact-byte-size`, or `-` for a directory size.
Navi8or parses those optional fields from the streamed listing itself. It leaves
missing/invalid fields unknown. Dates are minute-resolution wall clocks without
a timezone; Navi8or interprets them in the client's local zone. With
`autoindex_localtime on`, server and client zones should agree for accurate
absolute timestamps. Explicit HEAD stat uses Last-Modified, which identifies UTC.

nginx's optional `autoindex_format json` produces objects with `name`, `type`,
GMT `mtime`, and file `size`. Supporting this structured format would avoid HTML
column heuristics and timezone ambiguity, but is a future extension, not a
requirement for browsing. This change retains general HTML support and does not
change nginx configuration or add JSON parsing. A future adapter should select
supported JSON by response Content-Type/configuration and retain HTML fallback;
nginx chooses autoindex format through its directive, not an automatic Accept
header negotiation.

F3 explicitly performs bounded remote reads.

F5 explicitly streams complete files between providers.

Explicitly enabled mutation capabilities use:

```text
F5  PUT      upload
F6  MOVE     rename
F7  MKCOL    create directory
F8  DELETE   delete
```

Authentication is a property of the repository/provider request context rather
than a separate browsing implementation. Credentialed repositories require
HTTPS; the provider never enables unrestricted cross-origin authentication and
permits authenticated redirects only to HTTPS.

## 20. Authentication test matrix

Test all three endpoints:

| Operation | Anonymous 8443 | Basic 8444 | Bearer 8445 |
|---|---:|---:|---:|
| Browse | yes | yes | yes |
| F3 Range Viewer | yes | yes | yes |
| F5 Download | yes | yes | yes |
| F5 Upload | yes | yes | yes |
| F6 Rename | yes | yes | yes |
| F7 MkDir | yes | yes | yes |
| F8 Delete | yes | yes | yes |
| Wrong credentials rejected | n/a | yes | yes |
| Missing credentials rejected | n/a | yes | yes |
| TLS verify failure when enabled | yes | yes | yes |

Also verify that credentials configured for one repository are never sent to a different repository instance.
