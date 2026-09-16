use crate::provider::{Capabilities, Entry, EntryKind, Provider};
use std::cmp::Ordering;
use std::fs::{self, File, Metadata, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};

#[derive(Debug, Default)]
pub struct LocalProvider;

impl LocalProvider {
    pub fn new() -> Self {
        Self
    }
}

impl Provider for LocalProvider {
    fn scheme(&self) -> &'static str {
        "local"
    }

    fn display_name(&self) -> &'static str {
        "Local Filesystem"
    }

    fn capabilities(&self) -> Capabilities {
        Capabilities::LIST
            .union(Capabilities::STAT)
            .union(Capabilities::READ)
            .union(Capabilities::WRITE)
            .union(Capabilities::MKDIR)
            .union(Capabilities::DELETE)
            .union(Capabilities::RENAME)
            .union(Capabilities::COPY)
    }

    fn location(&self, input: &Path) -> io::Result<PathBuf> {
        input.canonicalize()
    }

    fn parent(&self, location: &Path) -> io::Result<PathBuf> {
        Ok(location.parent().unwrap_or(location).to_path_buf())
    }

    fn child(&self, location: &Path, name: &str) -> io::Result<PathBuf> {
        Ok(location.join(name))
    }

    fn list(&self, location: &Path, show_hidden: bool) -> io::Result<Vec<Entry>> {
        let mut entries = vec![Entry {
            name: "..".into(),
            resource: location.to_path_buf(),
            kind: EntryKind::Parent,
            size: Some(0),
            modified: None,
        }];

        for candidate in fs::read_dir(location)? {
            let candidate = match candidate {
                Ok(candidate) => candidate,
                Err(_) => continue,
            };
            let name = candidate.file_name().to_string_lossy().into_owned();
            if !show_hidden && name.starts_with('.') {
                continue;
            }
            let metadata = match fs::symlink_metadata(candidate.path()) {
                Ok(metadata) => metadata,
                Err(_) => continue,
            };
            entries.push(Entry {
                name,
                resource: candidate.path(),
                kind: if metadata.is_dir() {
                    EntryKind::Directory
                } else {
                    EntryKind::File
                },
                size: Some(metadata.len()),
                modified: metadata.modified().ok(),
            });
        }

        entries.sort_by(compare_entries);
        Ok(entries)
    }

    fn stat(&self, resource: &Path) -> io::Result<Metadata> {
        fs::symlink_metadata(resource)
    }

    fn open_read(&self, resource: &Path) -> io::Result<Box<dyn Read + Send>> {
        Ok(Box::new(File::open(resource)?))
    }

    fn open_write(&self, resource: &Path, overwrite: bool) -> io::Result<Box<dyn Write + Send>> {
        let file = OpenOptions::new()
            .write(true)
            .create_new(!overwrite)
            .create(overwrite)
            .truncate(overwrite)
            .open(resource)?;
        Ok(Box::new(file))
    }

    fn mkdir(&self, resource: &Path) -> io::Result<()> {
        fs::create_dir(resource)
    }

    fn delete(&self, resource: &Path) -> io::Result<()> {
        if fs::symlink_metadata(resource)?.is_dir() {
            fs::remove_dir(resource)
        } else {
            fs::remove_file(resource)
        }
    }

    fn rename(&self, source: &Path, destination: &Path) -> io::Result<()> {
        fs::rename(source, destination)
    }

    fn copy(&self, source: &Path, destination: &Path) -> io::Result<u64> {
        fs::copy(source, destination)
    }
}

fn compare_entries(left: &Entry, right: &Entry) -> Ordering {
    match (left.kind, right.kind) {
        (EntryKind::Parent, EntryKind::Parent) => Ordering::Equal,
        (EntryKind::Parent, _) => Ordering::Less,
        (_, EntryKind::Parent) => Ordering::Greater,
        (EntryKind::Directory, EntryKind::File) => Ordering::Less,
        (EntryKind::File, EntryKind::Directory) => Ordering::Greater,
        _ => left
            .name
            .to_lowercase()
            .cmp(&right.name.to_lowercase())
            .then_with(|| left.name.cmp(&right.name)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn fixture() -> PathBuf {
        let suffix = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock")
            .as_nanos();
        let root =
            std::env::temp_dir().join(format!("nav-rs-local-{}-{suffix}", std::process::id()));
        fs::create_dir(&root).expect("create fixture");
        root
    }

    #[test]
    fn listing_matches_commander_order_and_hidden_policy() {
        let root = fixture();
        fs::create_dir(root.join("Zulu")).unwrap();
        fs::create_dir(root.join("alpha")).unwrap();
        fs::write(root.join("beta.txt"), b"beta").unwrap();
        fs::write(root.join(".secret"), b"hidden").unwrap();
        let provider = LocalProvider::new();

        let names: Vec<_> = provider
            .list(&root, false)
            .unwrap()
            .into_iter()
            .map(|entry| entry.name)
            .collect();
        assert_eq!(names, ["..", "alpha", "Zulu", "beta.txt"]);
        assert!(
            provider
                .list(&root, true)
                .unwrap()
                .iter()
                .any(|entry| entry.name == ".secret")
        );

        fs::remove_dir_all(root).unwrap();
    }
}
