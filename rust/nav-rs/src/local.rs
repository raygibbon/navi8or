use crate::provider::{
    Capabilities, Entry, EntryKind, ListOptions, Location, LocationInput, Provider, ResourceId,
    ResourceMetadata, ResourceName, WriteOptions, WriteSession,
};
use std::cmp::Ordering;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering as AtomicOrdering};

static WRITE_SESSION_ID: AtomicU64 = AtomicU64::new(1);

#[derive(Debug, Default)]
pub struct LocalProvider;

struct LocalWriteSession {
    file: Option<File>,
    temporary: PathBuf,
    destination: PathBuf,
    overwrite: bool,
    finished: bool,
}

impl Write for LocalWriteSession {
    fn write(&mut self, buffer: &[u8]) -> io::Result<usize> {
        self.file_mut()?.write(buffer)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.file_mut()?.flush()
    }
}

impl WriteSession for LocalWriteSession {
    fn finish(&mut self) -> io::Result<()> {
        if self.finished {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "write session is already finished",
            ));
        }
        self.file_mut()?.flush()?;
        drop(self.file.take());
        if self.overwrite {
            fs::rename(&self.temporary, &self.destination)?;
        } else {
            // Hard-link creation fails atomically if another writer won the name.
            fs::hard_link(&self.temporary, &self.destination)?;
            // Once linked, the destination is committed even if this cleanup fails.
            let _ = fs::remove_file(&self.temporary);
        }
        self.finished = true;
        Ok(())
    }

    fn abort(&mut self) -> io::Result<()> {
        if self.finished {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "cannot abort a finished write session",
            ));
        }
        drop(self.file.take());
        match fs::remove_file(&self.temporary) {
            Ok(()) => Ok(()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
            Err(error) => Err(error),
        }
    }
}

impl LocalWriteSession {
    fn file_mut(&mut self) -> io::Result<&mut File> {
        self.file.as_mut().ok_or_else(|| {
            io::Error::new(io::ErrorKind::BrokenPipe, "write session is no longer open")
        })
    }
}

impl Drop for LocalWriteSession {
    fn drop(&mut self) {
        if !self.finished {
            drop(self.file.take());
            let _ = fs::remove_file(&self.temporary);
        }
    }
}

impl LocalProvider {
    pub fn new() -> Self {
        Self
    }

    fn location_for_path(path: PathBuf) -> Location {
        Location {
            resource: Self::resource_for_path(&path),
            display: path.to_string_lossy().into_owned(),
        }
    }

    fn resource_for_path(path: &Path) -> ResourceId {
        ResourceId::from_provider(path.as_os_str())
    }

    pub(crate) fn path_for_resource(resource: &ResourceId) -> PathBuf {
        PathBuf::from(resource.as_os_str())
    }

    fn path_for_location(location: &Location) -> PathBuf {
        Self::path_for_resource(&location.resource)
    }

    fn metadata_for_path(path: &Path, follow_symlink: bool) -> io::Result<ResourceMetadata> {
        let metadata = if follow_symlink {
            fs::metadata(path)?
        } else {
            fs::symlink_metadata(path)?
        };
        let kind = if metadata.file_type().is_symlink() {
            EntryKind::Symlink
        } else if metadata.is_dir() {
            EntryKind::Directory
        } else if metadata.is_file() {
            EntryKind::File
        } else {
            EntryKind::Other
        };
        Ok(ResourceMetadata {
            kind,
            size: Some(metadata.len()),
            modified: metadata.modified().ok(),
        })
    }
}

impl Provider for LocalProvider {
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

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
    }

    fn resolve(&self, input: &LocationInput) -> io::Result<Location> {
        let path = match input {
            LocationInput::Text(value) => Path::new(value),
            LocationInput::LocalPath(value) => Path::new(value),
        };
        let absolute = if path.is_absolute() {
            path.to_path_buf()
        } else {
            std::env::current_dir()?.join(path)
        };
        Ok(Self::location_for_path(normalize_lexically(&absolute)))
    }

    fn location(&self, resource: &ResourceId) -> io::Result<Location> {
        Ok(Self::location_for_path(Self::path_for_resource(resource)))
    }

    fn parent(&self, location: &Location) -> io::Result<Option<Location>> {
        let path = Self::path_for_location(location);
        Ok(path
            .parent()
            .filter(|parent| *parent != path)
            .map(|parent| Self::location_for_path(parent.to_path_buf())))
    }

    fn child(&self, location: &Location, name: &ResourceName) -> io::Result<Location> {
        Ok(Self::location_for_path(
            Self::path_for_location(location).join(name.as_os_str()),
        ))
    }

    fn resource_name(&self, resource: &ResourceId) -> io::Result<ResourceName> {
        Self::path_for_resource(resource)
            .file_name()
            .map(|name| ResourceName::Native(name.to_os_string()))
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "resource has no leaf name"))
    }

    fn list(&self, location: &Location, options: &ListOptions) -> io::Result<Vec<Entry>> {
        let path = Self::path_for_location(location);
        let mut entries = vec![Entry {
            name: "..".into(),
            resource: location.resource.clone(),
            kind: EntryKind::Parent,
            size: None,
            modified: None,
        }];

        for candidate in fs::read_dir(path)? {
            let candidate = match candidate {
                Ok(candidate) => candidate,
                Err(_) => continue,
            };
            let name = candidate.file_name().to_string_lossy().into_owned();
            if !options.show_hidden && name.starts_with('.') {
                continue;
            }
            let path = candidate.path();
            let metadata = match Self::metadata_for_path(&path, false) {
                Ok(metadata) => metadata,
                Err(_) => continue,
            };
            entries.push(Entry {
                name,
                resource: Self::resource_for_path(&path),
                kind: metadata.kind,
                size: metadata.size,
                modified: metadata.modified,
            });
        }

        entries.sort_by(compare_entries);
        Ok(entries)
    }

    fn stat(&self, resource: &ResourceId) -> io::Result<ResourceMetadata> {
        Self::metadata_for_path(&Self::path_for_resource(resource), false)
    }

    fn stat_target(&self, resource: &ResourceId) -> io::Result<ResourceMetadata> {
        Self::metadata_for_path(&Self::path_for_resource(resource), true)
    }

    fn open_read(&self, resource: &ResourceId) -> io::Result<Box<dyn Read + Send>> {
        Ok(Box::new(File::open(Self::path_for_resource(resource))?))
    }

    fn open_write(
        &self,
        resource: &ResourceId,
        options: WriteOptions,
    ) -> io::Result<Box<dyn WriteSession>> {
        let destination = Self::path_for_resource(resource);
        if !options.overwrite && destination.exists() {
            return Err(io::Error::new(
                io::ErrorKind::AlreadyExists,
                "destination already exists",
            ));
        }
        let parent = destination.parent().ok_or_else(|| {
            io::Error::new(io::ErrorKind::InvalidInput, "destination has no parent")
        })?;
        let leaf = destination.file_name().ok_or_else(|| {
            io::Error::new(io::ErrorKind::InvalidInput, "destination has no leaf name")
        })?;
        let mut temporary_name = leaf.to_os_string();
        temporary_name.push(format!(
            ".nav-part-{}-{}",
            std::process::id(),
            WRITE_SESSION_ID.fetch_add(1, AtomicOrdering::Relaxed)
        ));
        let temporary = parent.join(temporary_name);
        let file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary)?;
        Ok(Box::new(LocalWriteSession {
            file: Some(file),
            temporary,
            destination,
            overwrite: options.overwrite,
            finished: false,
        }))
    }

    fn mkdir(&self, resource: &ResourceId) -> io::Result<()> {
        fs::create_dir(Self::path_for_resource(resource))
    }

    fn delete(&self, resource: &ResourceId) -> io::Result<()> {
        let path = Self::path_for_resource(resource);
        if fs::symlink_metadata(&path)?.is_dir() {
            fs::remove_dir(path)
        } else {
            fs::remove_file(path)
        }
    }

    fn rename(&self, source: &ResourceId, destination: &ResourceId) -> io::Result<()> {
        fs::rename(
            Self::path_for_resource(source),
            Self::path_for_resource(destination),
        )
    }
}

fn normalize_lexically(path: &Path) -> PathBuf {
    use std::path::Component;

    let mut normalized = PathBuf::new();
    for component in path.components() {
        match component {
            Component::Prefix(prefix) => normalized.push(prefix.as_os_str()),
            Component::RootDir => normalized.push(component.as_os_str()),
            Component::CurDir => {}
            Component::ParentDir => {
                normalized.pop();
            }
            Component::Normal(part) => normalized.push(part),
        }
    }
    normalized
}

fn compare_entries(left: &Entry, right: &Entry) -> Ordering {
    match (left.kind, right.kind) {
        (EntryKind::Parent, EntryKind::Parent) => Ordering::Equal,
        (EntryKind::Parent, _) => Ordering::Less,
        (_, EntryKind::Parent) => Ordering::Greater,
        (EntryKind::Directory, EntryKind::Directory) => compare_names(left, right),
        (EntryKind::Directory, _) => Ordering::Less,
        (_, EntryKind::Directory) => Ordering::Greater,
        _ => compare_names(left, right),
    }
}

fn compare_names(left: &Entry, right: &Entry) -> Ordering {
    left.name
        .to_lowercase()
        .cmp(&right.name.to_lowercase())
        .then_with(|| left.name.cmp(&right.name))
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
    fn resources_resolve_and_parent_and_child_stay_provider_owned() {
        let root = fixture();
        fs::create_dir(root.join("child")).unwrap();
        let provider = LocalProvider::new();
        let location = provider
            .resolve(&LocationInput::LocalPath(root.as_os_str().to_os_string()))
            .unwrap();
        assert_eq!(
            location.display,
            root.canonicalize().unwrap().to_string_lossy()
        );

        let child = provider
            .child(&location, &ResourceName::Text("child".into()))
            .unwrap();
        assert_eq!(child.display, root.join("child").to_string_lossy());
        let child = provider
            .resolve(&LocationInput::Text(child.display.clone()))
            .unwrap();
        assert_eq!(provider.parent(&child).unwrap(), Some(location));

        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn listing_uses_navi8or_metadata_and_commander_order() {
        let root = fixture();
        fs::create_dir(root.join("Zulu")).unwrap();
        fs::create_dir(root.join("alpha")).unwrap();
        fs::write(root.join("beta.txt"), b"beta").unwrap();
        fs::write(root.join(".secret"), b"hidden").unwrap();
        let provider = LocalProvider::new();
        let location = provider
            .resolve(&LocationInput::LocalPath(root.as_os_str().to_os_string()))
            .unwrap();

        let entries = provider.list(&location, &ListOptions::default()).unwrap();
        let names: Vec<_> = entries.iter().map(|entry| entry.name.as_str()).collect();
        assert_eq!(names, ["..", "alpha", "Zulu", "beta.txt"]);
        let file = entries
            .iter()
            .find(|entry| entry.name == "beta.txt")
            .unwrap();
        assert_eq!(file.kind, EntryKind::File);
        assert_eq!(file.size, Some(4));
        assert_eq!(provider.stat(&file.resource).unwrap().kind, EntryKind::File);

        assert!(
            provider
                .list(&location, &ListOptions { show_hidden: true })
                .unwrap()
                .iter()
                .any(|entry| entry.name == ".secret")
        );

        fs::remove_dir_all(root).unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn resource_identity_preserves_non_utf8_names() {
        use std::os::unix::ffi::OsStringExt;

        let root = fixture();
        let name = std::ffi::OsString::from_vec(b"non-utf8-\xff".to_vec());
        let path = root.join(&name);
        fs::write(&path, b"identity").unwrap();
        let provider = LocalProvider::new();
        let location = provider
            .resolve(&LocationInput::LocalPath(root.as_os_str().to_os_string()))
            .unwrap();
        let entries = provider.list(&location, &ListOptions::default()).unwrap();
        let entry = entries
            .iter()
            .find(|entry| LocalProvider::path_for_resource(&entry.resource) == path)
            .expect("lossless resource identity");
        assert_eq!(
            LocalProvider::path_for_resource(&entry.resource).as_os_str(),
            path.as_os_str()
        );

        fs::remove_dir_all(root).unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn lexical_resolution_does_not_dereference_symlinks() {
        use std::os::unix::fs::symlink;

        let root = fixture();
        let real = root.join("real");
        let link = root.join("link");
        fs::create_dir(&real).unwrap();
        symlink(&real, &link).unwrap();
        let provider = LocalProvider::new();
        let location = provider
            .resolve(&LocationInput::LocalPath(link.as_os_str().to_os_string()))
            .unwrap();
        assert_eq!(LocalProvider::path_for_resource(&location.resource), link);

        let parent = provider
            .resolve(&LocationInput::LocalPath(
                root.join("link/..").into_os_string(),
            ))
            .unwrap();
        assert_eq!(LocalProvider::path_for_resource(&parent.resource), root);
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn local_streams_read_and_write_exact_bytes() {
        let root = fixture();
        let source = root.join("source.bin");
        let destination = root.join("destination.bin");
        let bytes = b"provider-neutral stream data";
        fs::write(&source, bytes).unwrap();
        let provider = LocalProvider::new();
        let source = ResourceId::from_provider(source.into_os_string());
        let destination = ResourceId::from_provider(destination.into_os_string());

        let mut reader = provider.open_read(&source).unwrap();
        let mut copied = Vec::new();
        reader.read_to_end(&mut copied).unwrap();
        assert_eq!(copied, bytes);

        let mut writer = provider
            .open_write(
                &destination,
                WriteOptions {
                    overwrite: false,
                    total: Some(bytes.len() as u64),
                },
            )
            .unwrap();
        writer.write_all(&copied).unwrap();
        writer.finish().unwrap();
        assert_eq!(
            fs::read(LocalProvider::path_for_resource(&destination)).unwrap(),
            bytes
        );

        let aborted = ResourceId::from_provider(root.join("aborted.bin").into_os_string());
        let mut writer = provider
            .open_write(&aborted, WriteOptions::default())
            .unwrap();
        writer.write_all(b"partial").unwrap();
        writer.abort().unwrap();
        assert!(!LocalProvider::path_for_resource(&aborted).exists());

        let raced = ResourceId::from_provider(root.join("raced.bin").into_os_string());
        let mut writer = provider
            .open_write(&raced, WriteOptions::default())
            .unwrap();
        writer.write_all(b"incoming").unwrap();
        fs::write(LocalProvider::path_for_resource(&raced), b"winner").unwrap();
        assert_eq!(
            writer.finish().unwrap_err().kind(),
            io::ErrorKind::AlreadyExists
        );
        writer.abort().unwrap();
        assert_eq!(
            fs::read(LocalProvider::path_for_resource(&raced)).unwrap(),
            b"winner"
        );
        fs::remove_dir_all(root).unwrap();
    }
}
