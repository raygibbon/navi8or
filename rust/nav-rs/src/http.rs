//! Blocking HTTP autoindex provider. Resource IDs are absolute URLs, not paths.
use crate::provider::{
    Capabilities, Entry, EntryKind, ListOptions, Location, LocationInput, Provider, ResourceId,
    ResourceMetadata, ResourceName,
};
use base64::Engine;
use std::io::{self, Read};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;
use url::Url;

const LIST_LIMIT: u64 = 4 * 1024 * 1024;
const REDIRECT_LIMIT: usize = 5;

#[derive(Clone, Debug)]
pub enum HttpAuth {
    None,
    Basic { username: String, password: String },
    Bearer(String),
}

pub struct HttpProvider {
    root: Url,
    agent: ureq::Agent,
    auth: HttpAuth,
}

impl HttpProvider {
    pub fn new(base: &str, auth: HttpAuth, tls_verify: bool) -> io::Result<Self> {
        let mut root = parse_url(base)?;
        if root.query().is_some()
            || root.fragment().is_some()
            || root.username() != ""
            || root.password().is_some()
        {
            return Err(invalid(
                "repository URL cannot contain query, fragment, or credentials",
            ));
        }
        if !root.path().ends_with('/') {
            root.set_path(&format!("{}/", root.path()));
        }
        if !matches!(auth, HttpAuth::None) && root.scheme() != "https" {
            return Err(invalid("credentialed repositories require HTTPS"));
        }
        match &auth {
            HttpAuth::Basic { username, .. }
                if username.contains(':') || username.chars().any(char::is_control) =>
            {
                return Err(invalid("invalid Basic username"));
            }
            HttpAuth::Bearer(token)
                if token.is_empty() || !token.bytes().all(|byte| byte.is_ascii_graphic()) =>
            {
                return Err(invalid("invalid Bearer token"));
            }
            _ => {}
        }
        let connector = native_tls::TlsConnector::builder()
            .danger_accept_invalid_certs(!tls_verify)
            .danger_accept_invalid_hostnames(!tls_verify)
            .build()
            .map_err(other)?;
        let agent = ureq::AgentBuilder::new()
            .redirects(0)
            .try_proxy_from_env(true)
            .timeout_connect(Duration::from_secs(5))
            .timeout_read(Duration::from_secs(5))
            .timeout_write(Duration::from_secs(5))
            .tls_connector(Arc::new(connector))
            .build();
        Ok(Self { root, agent, auth })
    }

    pub fn root(&self) -> Location {
        self.to_location(self.root.clone())
    }

    fn scoped(&self, url: Url) -> io::Result<Url> {
        if !matches!(url.scheme(), "http" | "https")
            || url.origin() != self.root.origin()
            || !url.username().is_empty()
            || url.password().is_some()
            || !url.path().starts_with(self.root.path())
            || url.query().is_some()
            || url.fragment().is_some()
        {
            return Err(invalid("HTTP resource is outside repository root"));
        }
        Ok(url)
    }

    fn url_for_resource(&self, resource: &ResourceId) -> io::Result<Url> {
        let text = resource
            .as_os_str()
            .to_str()
            .ok_or_else(|| invalid("invalid HTTP resource"))?;
        self.scoped(parse_url(text)?)
    }

    fn to_location(&self, url: Url) -> Location {
        let display = if url == self.root {
            "/".to_owned()
        } else {
            format!("/{}", &url.path()[self.root.path().len()..])
        };
        Location {
            resource: ResourceId::from_provider(url.as_str()),
            display,
        }
    }

    fn get(&self, start: Url, method: &str) -> io::Result<ureq::Response> {
        self.get_with_cancel(start, method, None)
    }

    fn get_with_cancel(
        &self,
        start: Url,
        method: &str,
        cancelled: Option<&AtomicBool>,
    ) -> io::Result<ureq::Response> {
        let mut current = self.scoped(start)?;
        for _ in 0..=REDIRECT_LIMIT {
            if cancelled.is_some_and(|signal| signal.load(Ordering::Relaxed)) {
                return Err(io::ErrorKind::Interrupted.into());
            }
            let mut request = self.agent.request(method, current.as_str());
            match &self.auth {
                HttpAuth::None => {}
                HttpAuth::Basic { username, password } => {
                    let encoded = base64::engine::general_purpose::STANDARD
                        .encode(format!("{username}:{password}"));
                    request = request.set("Authorization", &format!("Basic {encoded}"));
                }
                HttpAuth::Bearer(token) => {
                    request = request.set("Authorization", &format!("Bearer {token}"));
                }
            }
            let result = request.call();
            let response = match result {
                Ok(response) | Err(ureq::Error::Status(_, response)) => response,
                Err(error) => {
                    return Err(io::Error::other(format!("HTTP request failed: {error}")));
                }
            };
            if (300..400).contains(&response.status()) {
                let target = response
                    .header("Location")
                    .ok_or_else(|| invalid("HTTP redirect without Location"))?;
                current = self.scoped(current.join(target).map_err(other)?)?;
                continue;
            }
            if !(200..300).contains(&response.status()) {
                return Err(io::Error::other(format!(
                    "HTTP server returned status {}",
                    response.status()
                )));
            }
            return Ok(response);
        }
        Err(invalid("too many HTTP redirects"))
    }
}

impl Provider for HttpProvider {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }
    fn scheme(&self) -> &'static str {
        "http"
    }
    fn display_name(&self) -> &'static str {
        "HTTP Repository"
    }
    fn capabilities(&self) -> Capabilities {
        Capabilities::LIST
            .union(Capabilities::READ)
            .union(Capabilities::STAT)
    }
    fn background_listing(&self) -> bool {
        true
    }

    fn resolve(&self, input: &LocationInput) -> io::Result<Location> {
        let text = match input {
            LocationInput::Text(s) => s.as_str(),
            LocationInput::LocalPath(s) => {
                s.to_str().ok_or_else(|| invalid("invalid HTTP location"))?
            }
        };
        let mut url = if text.is_empty() {
            self.root.clone()
        } else if let Some(relative) = text.strip_prefix('/') {
            self.root.join(relative).map_err(other)?
        } else {
            self.root.join(text).map_err(other)?
        };
        if !url.path().ends_with('/') {
            url.set_path(&format!("{}/", url.path()));
        }
        Ok(self.to_location(self.scoped(url)?))
    }
    fn location(&self, resource: &ResourceId) -> io::Result<Location> {
        Ok(self.to_location(self.url_for_resource(resource)?))
    }
    fn parent(&self, location: &Location) -> io::Result<Option<Location>> {
        let url = self.url_for_resource(&location.resource)?;
        if url == self.root {
            return Ok(None);
        }
        let parent = url.join("../").map_err(other)?;
        Ok(Some(self.to_location(self.scoped(parent)?)))
    }
    fn child(&self, location: &Location, name: &ResourceName) -> io::Result<Location> {
        let name = name
            .as_os_str()
            .to_str()
            .ok_or_else(|| invalid("HTTP child name is not UTF-8"))?;
        if name.is_empty()
            || name == "."
            || name == ".."
            || name
                .chars()
                .any(|ch| ch.is_control() || ch == '/' || ch == '\\')
        {
            return Err(invalid("invalid HTTP child name"));
        }
        let mut url = self.url_for_resource(&location.resource)?;
        url.path_segments_mut()
            .map_err(|_| invalid("invalid HTTP base URL"))?
            .pop_if_empty()
            .push(name);
        Ok(self.to_location(self.scoped(url)?))
    }
    fn resource_name(&self, resource: &ResourceId) -> io::Result<ResourceName> {
        let url = self.url_for_resource(resource)?;
        let leaf = url
            .path_segments()
            .and_then(|mut segments| segments.next_back())
            .unwrap_or("");
        let name = percent_encoding::percent_decode_str(leaf)
            .decode_utf8()
            .map_err(other)?;
        Ok(ResourceName::Text(name.into_owned()))
    }
    fn list(&self, location: &Location, options: &ListOptions) -> io::Result<Vec<Entry>> {
        self.list_cancellable(location, options, &AtomicBool::new(false))
    }
    fn list_cancellable(
        &self,
        location: &Location,
        _options: &ListOptions,
        cancelled: &AtomicBool,
    ) -> io::Result<Vec<Entry>> {
        if cancelled.load(Ordering::Relaxed) {
            return Err(io::ErrorKind::Interrupted.into());
        }
        let requested = self.url_for_resource(&location.resource)?;
        let response = self.get_with_cancel(requested, "GET", Some(cancelled))?;
        let effective = self.scoped(parse_url(response.get_url())?)?;
        let mut bytes = Vec::new();
        let mut reader = response.into_reader().take(LIST_LIMIT + 1);
        let mut buffer = [0; 16 * 1024];
        loop {
            if cancelled.load(Ordering::Relaxed) {
                return Err(io::ErrorKind::Interrupted.into());
            }
            let count = reader.read(&mut buffer)?;
            if count == 0 {
                break;
            }
            bytes.extend_from_slice(&buffer[..count]);
        }
        if bytes.len() as u64 > LIST_LIMIT {
            return Err(invalid("HTTP listing exceeds 4 MiB"));
        }
        let html = String::from_utf8(bytes).map_err(other)?;
        let mut entries = vec![Entry {
            name: "..".into(),
            resource: location.resource.clone(),
            kind: EntryKind::Parent,
            size: None,
            modified: None,
        }];
        for href in extract_hrefs(&html) {
            let href = html_escape::decode_html_entities(&href);
            if href.is_empty()
                || href.starts_with(['#', '?'])
                || href.contains(['#', '?'])
                || href == ".."
                || href == "../"
            {
                continue;
            }
            let Ok(url) = effective.join(&href) else {
                continue;
            };
            let Ok(url) = self.scoped(url) else {
                continue;
            };
            if url == effective {
                continue;
            }
            let directory = href.ends_with('/');
            let leaf = url
                .path_segments()
                .and_then(|mut segments| segments.rfind(|s| !s.is_empty()))
                .unwrap_or("");
            let Ok(name) = percent_encoding::percent_decode_str(leaf).decode_utf8() else {
                continue;
            };
            if name.is_empty()
                || name == "."
                || name == ".."
                || name
                    .chars()
                    .any(|ch| ch.is_control() || ch == '/' || ch == '\\')
            {
                continue;
            }
            if entries
                .iter()
                .any(|entry| entry.resource.as_os_str() == url.as_str())
            {
                continue;
            }
            entries.push(Entry {
                name: name.into_owned(),
                resource: ResourceId::from_provider(url.as_str()),
                kind: if directory {
                    EntryKind::Directory
                } else {
                    EntryKind::File
                },
                size: None,
                modified: None,
            });
        }
        entries[1..].sort_by(|a, b| {
            b.is_directory()
                .cmp(&a.is_directory())
                .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
        });
        Ok(entries)
    }
    fn stat(&self, resource: &ResourceId) -> io::Result<ResourceMetadata> {
        let url = self.url_for_resource(resource)?;
        let response = self.get(url.clone(), "HEAD")?;
        Ok(ResourceMetadata {
            kind: if response.get_url().ends_with('/') || url.path().ends_with('/') {
                EntryKind::Directory
            } else {
                EntryKind::File
            },
            size: response
                .header("Content-Length")
                .and_then(|v| v.parse().ok()),
            modified: None,
        })
    }
    fn open_read(&self, resource: &ResourceId) -> io::Result<Box<dyn Read + Send>> {
        Ok(self
            .get(self.url_for_resource(resource)?, "GET")?
            .into_reader())
    }
}

fn extract_hrefs(html: &str) -> Vec<String> {
    let mut found = Vec::new();
    let mut rest = html;
    while let Some(start) = rest.find('<') {
        rest = &rest[start + 1..];
        if !rest.starts_with(['a', 'A'])
            || !rest
                .as_bytes()
                .get(1)
                .is_some_and(|ch| ch.is_ascii_whitespace())
        {
            continue;
        }
        let Some(end) = rest.find('>') else {
            break;
        };
        let tag = &rest[1..end];
        let mut scan = tag;
        while !scan.is_empty() {
            scan = scan.trim_start_matches(|ch: char| ch.is_ascii_whitespace());
            let key_end = scan
                .find(|ch: char| ch.is_ascii_whitespace() || ch == '=')
                .unwrap_or(scan.len());
            if key_end == 0 {
                scan = &scan[1..];
                continue;
            }
            let key = &scan[..key_end];
            scan = &scan[key_end..];
            scan = scan.trim_start();
            if !scan.starts_with('=') {
                continue;
            }
            scan = scan[1..].trim_start();
            let (value, next) = if let Some(quote @ ('\'' | '"')) = scan.chars().next() {
                let tail = &scan[1..];
                let Some(close) = tail.find(quote) else {
                    break;
                };
                (&tail[..close], &tail[close + 1..])
            } else {
                let end = scan.find(char::is_whitespace).unwrap_or(scan.len());
                (&scan[..end], &scan[end..])
            };
            if key.eq_ignore_ascii_case("href") {
                found.push(value.to_owned());
                break;
            }
            scan = next;
        }
        rest = &rest[end + 1..];
    }
    found
}

fn parse_url(value: &str) -> io::Result<Url> {
    let url = Url::parse(value).map_err(other)?;
    if !matches!(url.scheme(), "http" | "https") {
        return Err(invalid("repository must use HTTP or HTTPS"));
    }
    Ok(url)
}
fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, message)
}
fn other(error: impl std::error::Error + Send + Sync + 'static) -> io::Error {
    io::Error::other(error)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;
    use std::thread;

    #[test]
    fn listing_uses_authoritative_hrefs_and_streams_file() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = thread::spawn(move || {
            for _ in 0..2 {
                let (mut stream, _) = listener.accept().unwrap();
                let mut request = [0; 1024];
                let count = stream.read(&mut request).unwrap();
                let request = String::from_utf8_lossy(&request[..count]);
                let body = if request.starts_with("GET /repo/a%20b.txt") {
                    "remote bytes"
                } else {
                    "<a href=\"a%20b.txt\">Wrong display</a><a href=\"dir/\">Directory</a><a href=\"/outside\">Outside</a>"
                };
                write!(
                    stream,
                    "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
                    body.len()
                )
                .unwrap();
            }
        });
        let provider =
            HttpProvider::new(&format!("http://{address}/repo/"), HttpAuth::None, true).unwrap();
        let root = provider.root();
        let entries = provider.list(&root, &ListOptions::default()).unwrap();
        assert_eq!(entries.len(), 3);
        let file = entries
            .iter()
            .find(|entry| entry.name == "a b.txt")
            .unwrap();
        assert_eq!(
            file.resource.as_os_str().to_str().unwrap(),
            format!("http://{address}/repo/a%20b.txt")
        );
        let destination = std::env::temp_dir().join(format!(
            "nav-http-copy-{}-{}",
            std::process::id(),
            address.port()
        ));
        let local = crate::local::LocalProvider::new();
        let target = ResourceId::from_provider(destination.as_os_str());
        let mut writer = local
            .open_write(&target, crate::provider::WriteOptions::default())
            .unwrap();
        let mut reader = provider.open_read(&file.resource).unwrap();
        let result = crate::transfer::copy_stream(
            reader.as_mut(),
            writer.as_mut(),
            &std::sync::atomic::AtomicBool::new(false),
            |_| {},
        )
        .unwrap();
        assert_eq!(result, crate::transfer::TransferOutcome::Completed(12));
        assert!(matches!(
            writer.finish(),
            crate::provider::FinishOutcome::Committed
        ));
        assert_eq!(std::fs::read(&destination).unwrap(), b"remote bytes");
        std::fs::remove_file(destination).unwrap();
        server.join().unwrap();
    }

    #[test]
    fn href_extraction_handles_quotes_and_entities() {
        assert_eq!(
            extract_hrefs("<a class=x href='a&amp;b'>a</a><A HREF=dir/>d</A>"),
            ["a&amp;b", "dir/"]
        );
    }

    #[test]
    fn status_and_redirect_scope_errors_are_reported() {
        for (status, headers, expected) in [
            ("404 Not Found", "", "404"),
            (
                "302 Found",
                "Location: /outside\r\n",
                "outside repository root",
            ),
        ] {
            let listener = TcpListener::bind("127.0.0.1:0").unwrap();
            let address = listener.local_addr().unwrap();
            let response = format!(
                "HTTP/1.1 {status}\r\n{headers}Content-Length: 0\r\nConnection: close\r\n\r\n"
            );
            let server = thread::spawn(move || {
                let (mut stream, _) = listener.accept().unwrap();
                let mut request = [0; 1024];
                let _ = stream.read(&mut request);
                stream.write_all(response.as_bytes()).unwrap();
            });
            let provider =
                HttpProvider::new(&format!("http://{address}/repo/"), HttpAuth::None, true)
                    .unwrap();
            let error = provider
                .list(&provider.root(), &ListOptions::default())
                .unwrap_err();
            assert!(error.to_string().contains(expected), "{error}");
            server.join().unwrap();
        }
    }

    #[test]
    fn basic_and_bearer_require_https() {
        assert!(
            HttpProvider::new(
                "http://localhost/repo/",
                HttpAuth::Basic {
                    username: "u".into(),
                    password: "p".into()
                },
                true
            )
            .is_err()
        );
        assert!(
            HttpProvider::new(
                "http://localhost/repo/",
                HttpAuth::Bearer("secret".into()),
                true
            )
            .is_err()
        );
    }

    #[test]
    fn navigation_stays_beneath_repository_root() {
        let provider =
            HttpProvider::new("https://example.invalid/repo/", HttpAuth::None, true).unwrap();
        let root = provider.root();
        let child = provider
            .child(&root, &ResourceName::Text("sp ace".into()))
            .unwrap();
        assert!(
            child
                .resource
                .as_os_str()
                .to_str()
                .unwrap()
                .ends_with("/repo/sp%20ace"),
            "{}",
            child.resource
        );
        let subdir = provider
            .resolve(&LocationInput::Text("/dir/".into()))
            .unwrap();
        assert_eq!(subdir.display, "/dir/");
        assert_eq!(provider.parent(&subdir).unwrap(), Some(root.clone()));
        assert_eq!(provider.parent(&root).unwrap(), None);
        assert!(
            provider
                .resolve(&LocationInput::Text(
                    "https://example.invalid/outside/".into()
                ))
                .is_err()
        );
        assert!(
            provider
                .scoped(Url::parse("https://user:secret@example.invalid/repo/file").unwrap())
                .is_err()
        );
        assert!(
            provider
                .resolve(&LocationInput::Text(
                    "https://user:secret@example.invalid/repo/dir/".into()
                ))
                .is_err()
        );
    }

    #[test]
    fn truncated_body_is_not_a_successful_download() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut request = [0; 1024];
            let _ = stream.read(&mut request);
            stream
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\nshort",
                )
                .unwrap();
        });
        let provider =
            HttpProvider::new(&format!("http://{address}/repo/"), HttpAuth::None, true).unwrap();
        let file = ResourceId::from_provider(format!("http://{address}/repo/file"));
        let mut bytes = Vec::new();
        assert!(
            provider
                .open_read(&file)
                .unwrap()
                .read_to_end(&mut bytes)
                .is_err()
        );
        server.join().unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn basic_and_bearer_headers_reach_https_server() {
        use std::process::Command;
        let root = std::env::temp_dir().join(format!("nav-http-auth-test-{}", std::process::id()));
        std::fs::create_dir_all(&root).unwrap();
        let cert = root.join("cert.pem");
        let key = root.join("key.pem");
        let identity = root.join("identity.p12");
        assert!(
            Command::new("openssl")
                .args([
                    "req",
                    "-x509",
                    "-newkey",
                    "rsa:2048",
                    "-nodes",
                    "-days",
                    "1",
                    "-subj",
                    "/CN=localhost",
                    "-keyout"
                ])
                .arg(&key)
                .arg("-out")
                .arg(&cert)
                .output()
                .unwrap()
                .status
                .success()
        );
        assert!(
            Command::new("openssl")
                .args(["pkcs12", "-export", "-inkey"])
                .arg(&key)
                .arg("-in")
                .arg(&cert)
                .arg("-out")
                .arg(&identity)
                .args(["-passout", "pass:test"])
                .output()
                .unwrap()
                .status
                .success()
        );
        let identity =
            native_tls::Identity::from_pkcs12(&std::fs::read(identity).unwrap(), "test").unwrap();
        let acceptor = native_tls::TlsAcceptor::new(identity).unwrap();
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = thread::spawn(move || {
            let mut headers = Vec::new();
            for _ in 0..2 {
                let (socket, _) = listener.accept().unwrap();
                let mut stream = acceptor.accept(socket).unwrap();
                let mut request = [0; 4096];
                let count = stream.read(&mut request).unwrap();
                headers.push(String::from_utf8_lossy(&request[..count]).into_owned());
                stream
                    .write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                    .unwrap();
            }
            headers
        });
        for auth in [
            HttpAuth::Basic {
                username: "fixture-user".into(),
                password: "fixture-pass".into(),
            },
            HttpAuth::Bearer("fixture-token".into()),
        ] {
            let provider = HttpProvider::new(
                &format!("https://localhost:{}/repo/", address.port()),
                auth,
                false,
            )
            .unwrap();
            provider
                .list(&provider.root(), &ListOptions::default())
                .unwrap();
        }
        let headers = server.join().unwrap();
        assert!(
            headers[0].contains("Authorization: Basic Zml4dHVyZS11c2VyOmZpeHR1cmUtcGFzcw==\r\n")
        );
        assert!(headers[1].contains("Authorization: Bearer fixture-token\r\n"));
        std::fs::remove_dir_all(root).unwrap();
    }
}
