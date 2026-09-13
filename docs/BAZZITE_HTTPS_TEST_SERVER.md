# Navi8or local HTTPS test server on Bazzite

This setup provides local HTTPS endpoints for developing and testing Navi8or's HTTP repository provider on Bazzite.

It mirrors the Debian test fixture:

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

This is for **local development only**. All servers are bound to `127.0.0.1`. Do not expose the writable endpoints to a LAN or the public Internet.

Navi8or does not depend on nginx or on any of the paths below.

## 1. Install the Bazzite host packages

Bazzite is image-based, so do not use `dnf install` for host packages.

For this fixture nginx needs to run as a host system service, so layer the small set of required Fedora packages:

```sh
rpm-ostree install nginx httpd-tools policycoreutils-python-utils
```

Reboot into the new deployment:

```sh
systemctl reboot
```

After reboot, verify the tools are available:

```sh
nginx -v
htpasswd -h 2>&1 | head
openssl version
semanage --help >/dev/null
```

If `openssl` is not already available, install it through Homebrew rather than layering another package:

```sh
brew install openssl
```

### Optional Navi8or build dependencies

For Navi8or itself, prefer Homebrew development libraries rather than layering development RPMs into Bazzite:

```sh
brew install curl libsodium pkgconf
```

The Navi8or Makefile should discover Homebrew curl/libsodium automatically. Do not hardcode Homebrew Cellar versions.

## 2. Prepare a disposable test tree

On Bazzite, prefer a disposable tree under `/srv` instead of bind-mounting the live checkout from your home directory. This avoids SELinux labels from the home tree becoming part of the nginx test setup.

Create a writable fixture:

```sh
sudo mkdir -p /srv/navi8or-upload-test
sudo chown -R nginx:nginx /srv/navi8or-upload-test
sudo chmod 0755 /srv/navi8or-upload-test
```

Give nginx an SELinux label that permits writes:

```sh
sudo chcon -R -t httpd_sys_rw_content_t /srv/navi8or-upload-test
```

Seed it with some files:

```sh
printf 'hello from Navi8or\n' | sudo tee /srv/navi8or-upload-test/hello.txt >/dev/null

sudo mkdir -p /srv/navi8or-upload-test/subdir
printf 'nested file\n' | sudo tee /srv/navi8or-upload-test/subdir/nested.txt >/dev/null

sudo chown -R nginx:nginx /srv/navi8or-upload-test
sudo chcon -R -t httpd_sys_rw_content_t /srv/navi8or-upload-test
```

If you want to test against a copy of the Navi8or checkout, copy it into the disposable tree rather than serving the live worktree:

```sh
sudo cp -a "$HOME/git/navi8or/." /srv/navi8or-upload-test/
sudo chown -R nginx:nginx /srv/navi8or-upload-test
sudo chcon -R -t httpd_sys_rw_content_t /srv/navi8or-upload-test
```

Adjust the source path if your checkout lives elsewhere.

### Optional live-checkout mode

A bind mount of the live checkout can be used, but Fedora SELinux normally labels files in your home directory as user content. That needs additional SELinux policy changes.

For normal Navi8or testing, the disposable `/srv` tree above is the recommended Bazzite setup.

## 3. Allow the development ports through SELinux

nginx runs in the Fedora `httpd_t` SELinux domain. Check which ports are already allowed:

```sh
sudo semanage port -l | grep '^http_port_t'
```

Ensure TCP ports `8443`, `8444`, and `8445` are assigned to `http_port_t`.

Add only ports that are missing:

```sh
sudo semanage port -a -t http_port_t -p tcp 8443
sudo semanage port -a -t http_port_t -p tcp 8444
sudo semanage port -a -t http_port_t -p tcp 8445
```

If a command reports that a port is already defined, inspect the existing assignment rather than blindly changing it:

```sh
sudo semanage port -l | grep -E '8443|8444|8445'
```

No firewall rule is required because the nginx listeners below bind only to `127.0.0.1`.

## 4. Create a localhost TLS certificate

Create the certificate directory:

```sh
sudo mkdir -p /etc/nginx/navi8or-test
```

Create a self-signed certificate valid for `localhost` and `127.0.0.1`:

```sh
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

Protect the private key:

```sh
sudo chmod 0600 /etc/nginx/navi8or-test/key.pem
```

This certificate is for development only.

With Navi8or TLS verification enabled, the certificate should fail verification unless it has been explicitly trusted. With `Verify TLS` disabled, Navi8or should connect only after its normal insecure-TLS confirmation.

Navi8or must never disable TLS verification globally.

## 5. Create the Basic-auth test account

Fedora/Bazzite provides `htpasswd` through the `httpd-tools` package.

Create the password file:

```sh
sudo htpasswd -c /etc/nginx/navi8or-test/htpasswd navi8or
```

For this disposable development environment you can use:

```text
Username: navi8or
Password: testpass
```

Do not reuse a real password.

## 6. Configure nginx on Bazzite

Fedora/Bazzite nginx uses `/etc/nginx/conf.d/` rather than Debian's `sites-available` / `sites-enabled` layout.

Create:

```text
/etc/nginx/conf.d/navi8or-test.conf
```

with:

```nginx
# Development-only Bearer token:
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

    root /srv/navi8or-upload-test;

    autoindex on;
    autoindex_exact_size on;
    autoindex_localtime on;

    location / {
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

    root /srv/navi8or-upload-test;

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

    root /srv/navi8or-upload-test;

    autoindex on;
    autoindex_exact_size on;
    autoindex_localtime on;

    location / {
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

All three listeners are deliberately restricted to `127.0.0.1`.

## 7. Verify DAV support

The mutation fixture needs nginx's HTTP DAV module.

Check the Fedora nginx build:

```sh
nginx -V 2>&1 | grep -- --with-http_dav_module
```

You should see `--with-http_dav_module`.

If that option is missing, do not continue with PUT/MOVE/MKCOL/DELETE testing until nginx has DAV support.

## 8. Validate and start nginx

Validate the configuration:

```sh
sudo nginx -t
```

Enable and start nginx:

```sh
sudo systemctl enable --now nginx
```

After later config edits, reload it with:

```sh
sudo nginx -t && sudo systemctl reload nginx
```

Check status:

```sh
systemctl status nginx --no-pager
```

If SELinux blocks nginx, inspect recent denials:

```sh
sudo ausearch -m AVC -ts recent
```

Do not disable SELinux to make the fixture work.

## 9. Test the anonymous endpoint

Directory listing:

```sh
curl -k https://localhost:8443/
```

Range read:

```sh
curl -k \
  -H 'Range: bytes=0-65535' \
  -D - \
  https://localhost:8443/hello.txt \
  -o /tmp/navi8or-range-test.bin
```

For an object large enough to require a range, expect `206 Partial Content` when nginx honours the request.

## 10. Test HTTP Basic authentication

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

Range reads must also authenticate:

```sh
curl -k \
  -u navi8or:testpass \
  -H 'Range: bytes=0-65535' \
  https://localhost:8444/hello.txt \
  -o /tmp/navi8or-basic-range.bin
```

## 11. Test Bearer authentication

Without the token:

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

Range reads must also carry the token:

```sh
curl -k \
  -H 'Authorization: Bearer navi8or-test-token-12345' \
  -H 'Range: bytes=0-65535' \
  https://localhost:8445/hello.txt \
  -o /tmp/navi8or-bearer-range.bin
```

## 12. Create large Viewer fixtures

Do not commit these files to Git.

Create a 5 MiB fixture:

```sh
sudo dd if=/dev/zero \
  of=/srv/navi8or-upload-test/http-5mb.bin \
  bs=1M \
  count=5
```

Create a sparse 100 MiB fixture:

```sh
sudo truncate -s 100M /srv/navi8or-upload-test/http-100mb.bin
```

Create a large line-oriented file:

```sh
awk 'BEGIN {
    for (i = 0; i < 1000000; i++)
        printf "line %08d remote range test data\n", i
}' | sudo tee /srv/navi8or-upload-test/http-large-lines.txt >/dev/null
```

Restore ownership and SELinux labels:

```sh
sudo chown -R nginx:nginx /srv/navi8or-upload-test
sudo chcon -R -t httpd_sys_rw_content_t /srv/navi8or-upload-test
```

## 13. Test F3 range-backed viewing

Open one of the large fixtures with F3 and exercise:

- PageDown
- PageUp
- End
- search for a late line

Navi8or's HTTP Viewer should use bounded Range requests instead of downloading the entire object before displaying it.

Watch nginx requests:

```sh
sudo tail -f /var/log/nginx/access.log
```

Browsing or merely highlighting a file must not fetch that file's body.

## 14. Test DAV mutations with curl

### PUT

```sh
printf 'hello from Navi8or\n' > /tmp/navi8or-put.txt

curl -k \
  -T /tmp/navi8or-put.txt \
  https://localhost:8443/navi8or-put.txt
```

Basic:

```sh
curl -k \
  -u navi8or:testpass \
  -T /tmp/navi8or-put.txt \
  https://localhost:8444/navi8or-put.txt
```

Bearer:

```sh
curl -k \
  -H 'Authorization: Bearer navi8or-test-token-12345' \
  -T /tmp/navi8or-put.txt \
  https://localhost:8445/navi8or-put.txt
```

### MKCOL

```sh
curl -k \
  -X MKCOL \
  https://localhost:8443/test-folder/
```

### MOVE

```sh
curl -k \
  -X MOVE \
  -H 'Destination: https://localhost:8443/navi8or-renamed.txt' \
  -H 'Overwrite: F' \
  https://localhost:8443/navi8or-put.txt
```

### DELETE

```sh
curl -k \
  -X DELETE \
  https://localhost:8443/navi8or-renamed.txt
```

For Basic or Bearer endpoints, add the corresponding credentials/header.

## 15. Navi8or repository configurations

### Anonymous

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

### Basic

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

Credential record:

```text
Username: navi8or
Password: testpass
```

### Bearer

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

Credential record:

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

## 16. Authentication test matrix

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

## 17. Bazzite-specific troubleshooting

### nginx cannot read or write `/srv/navi8or-upload-test`

Check ownership:

```sh
ls -ldZ /srv/navi8or-upload-test
```

Expected ownership should include the nginx user, and the SELinux type should be:

```text
httpd_sys_rw_content_t
```

Repair it:

```sh
sudo chown -R nginx:nginx /srv/navi8or-upload-test
sudo chcon -R -t httpd_sys_rw_content_t /srv/navi8or-upload-test
```

### nginx cannot bind 8444 or 8445

Check SELinux port mappings:

```sh
sudo semanage port -l | grep -E 'http_port_t|8443|8444|8445'
```

Add only missing ports to `http_port_t`.

### Config file is ignored

On Bazzite/Fedora the fixture belongs in:

```text
/etc/nginx/conf.d/navi8or-test.conf
```

There is no Debian-style `sites-enabled` symlink step.

Check the loaded configuration:

```sh
sudo nginx -T | less
```

### Inspect nginx logs

```sh
sudo journalctl -u nginx -n 100 --no-pager
sudo tail -n 100 /var/log/nginx/error.log
```

## 18. Clean up

Stop nginx if it was installed only for this fixture:

```sh
sudo systemctl disable --now nginx
```

Remove the test configuration and data:

```sh
sudo rm -f /etc/nginx/conf.d/navi8or-test.conf
sudo rm -rf /etc/nginx/navi8or-test
sudo rm -rf /srv/navi8or-upload-test
```

If you no longer want the layered Bazzite packages:

```sh
rpm-ostree uninstall nginx httpd-tools policycoreutils-python-utils
```

Then reboot:

```sh
systemctl reboot
```

Package layering is intentionally kept small because unnecessary layered packages can complicate future Bazzite updates.

## 19. Security notes

These endpoints are deliberately powerful.

Keep the listeners bound to:

```text
127.0.0.1
```

Do not expose anonymous DAV to a LAN or the public Internet.

Do not reuse the example password or Bearer token outside this disposable development environment.

Do not disable SELinux globally.

Do not disable TLS verification globally inside Navi8or.

Use the disposable `/srv/navi8or-upload-test` tree for destructive testing rather than your live source checkout.
