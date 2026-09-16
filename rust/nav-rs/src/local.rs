use crate::provider::{
    Capabilities, Entry, EntryKind, ListOptions, Location, Provider, ResourceId, ResourceMetadata,
};
use std::cmp::Ordering;
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};

#[derive(Debug, Default)]
pub struct LocalProvider;

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
        ResourceId::from_provider(path.to_string_lossy().into_owned())
    }

    fn path_for_resource(resource: &ResourceId) -> PathBuf {
        PathBuf::from(resource.as_str())
    }

    fn path_for_location(location: &Location) -> PathBuf {
        Self::path_for_resource(&location.resource)
    }

    fn metadata_for_path(path: &Path) -> io::Result<ResourceMetadata> {
        let metadata = fs::symlink_metadata(path)?;
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

    fn resolve(&self, input: &str) -> io::Result<Location> {
        Ok(Self::location_for_path(Path::new(input).canonicalize()?))
    }

    fn parent(&self, location: &Location) -> io::Result<Option<Location>> {
        let path = Self::path_for_location(location);
        Ok(path
            .parent()
            .filter(|parent| *parent != path)
            .map(|parent| Self::location_for_path(parent.to_path_buf())))
    }

    fn child(&self, location: &Location, name: &str) -> io::Result<Location> {
        Ok(Self::location_for_path(
            Self::path_for_location(location).join(name),
        ))
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
            let metadata = match Self::metadata_for_path(&path) {
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
        Self::metadata_for_path(&Self::path_for_resource(resource))
    }

    fn open_read(&self, resource: &ResourceId) -> io::Result<Box<dyn Read + Send>> {
        Ok(Box::new(File::open(Self::path_for_resource(resource))?))
    }

    fn open_write(
        &self,
        resource: &ResourceId,
        overwrite: bool,
    ) -> io::Result<Box<dyn Write + Send>> {
        let file = OpenOptions::new()
            .write(true)
            .create_new(!overwrite)
            .create(overwrite)
            .truncate(overwrite)
            .open(Self::path_for_resource(resource))?;
        Ok(Box::new(file))
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
        let location = provider.resolve(root.to_str().unwrap()).unwrap();
        assert_eq!(
            location.display,
            root.canonicalize().unwrap().to_string_lossy()
        );

        let child = provider.child(&location, "child").unwrap();
        assert_eq!(child.display, root.join("child").to_string_lossy());
        let child = provider.resolve(&child.display).unwrap();
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
        let location = provider.resolve(root.to_str().unwrap()).unwrap();

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
}
