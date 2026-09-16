use std::any::Any;
use std::ffi::{OsStr, OsString};
use std::fmt;
use std::io::{self, Read, Write};
use std::time::SystemTime;

/// Opaque identity meaningful only to the provider that created it.
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct ResourceId(OsString);

impl ResourceId {
    pub(crate) fn from_provider(value: impl Into<OsString>) -> Self {
        Self(value.into())
    }

    pub fn as_os_str(&self) -> &OsStr {
        &self.0
    }
}

impl fmt::Display for ResourceId {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.to_string_lossy().fmt(formatter)
    }
}

/// Provider identity and user-facing presentation are deliberately separate.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Location {
    pub resource: ResourceId,
    pub display: String,
}

/// Provider features are queried by commands before work is started.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Capabilities(u16);

impl Capabilities {
    pub const LIST: Self = Self(1 << 0);
    pub const STAT: Self = Self(1 << 1);
    pub const READ: Self = Self(1 << 2);
    pub const WRITE: Self = Self(1 << 3);
    pub const MKDIR: Self = Self(1 << 4);
    pub const DELETE: Self = Self(1 << 5);
    pub const RENAME: Self = Self(1 << 6);

    pub const fn empty() -> Self {
        Self(0)
    }

    pub const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    pub const fn contains(self, requested: Self) -> bool {
        self.0 & requested.0 == requested.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EntryKind {
    Parent,
    Directory,
    File,
    Symlink,
    Other,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ResourceMetadata {
    pub kind: EntryKind,
    pub size: Option<u64>,
    pub modified: Option<SystemTime>,
}

#[derive(Clone, Debug)]
pub struct Entry {
    pub name: String,
    pub resource: ResourceId,
    pub kind: EntryKind,
    pub size: Option<u64>,
    pub modified: Option<SystemTime>,
}

impl Entry {
    pub fn is_directory(&self) -> bool {
        matches!(self.kind, EntryKind::Parent | EntryKind::Directory)
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ListOptions {
    pub show_hidden: bool,
}

/// Provider-neutral resource operations.
///
/// Blocking providers will eventually run behind a jobs boundary. The trait
/// intentionally exposes neither local filesystem types nor an async runtime.
pub trait Provider: Any + Send + Sync {
    fn as_any(&self) -> &dyn Any;
    fn scheme(&self) -> &'static str;
    fn display_name(&self) -> &'static str;
    fn capabilities(&self) -> Capabilities;

    fn resolve(&self, input: &str) -> io::Result<Location>;
    fn location(&self, resource: &ResourceId) -> io::Result<Location>;
    fn parent(&self, location: &Location) -> io::Result<Option<Location>>;
    fn child(&self, location: &Location, name: &str) -> io::Result<Location>;
    fn list(&self, location: &Location, options: &ListOptions) -> io::Result<Vec<Entry>>;

    fn stat(&self, _resource: &ResourceId) -> io::Result<ResourceMetadata> {
        Err(unsupported("stat"))
    }

    fn open_read(&self, _resource: &ResourceId) -> io::Result<Box<dyn Read + Send>> {
        Err(unsupported("read"))
    }

    fn open_write(
        &self,
        _resource: &ResourceId,
        _overwrite: bool,
    ) -> io::Result<Box<dyn Write + Send>> {
        Err(unsupported("write"))
    }

    fn mkdir(&self, _resource: &ResourceId) -> io::Result<()> {
        Err(unsupported("mkdir"))
    }

    fn delete(&self, _resource: &ResourceId) -> io::Result<()> {
        Err(unsupported("delete"))
    }

    fn rename(&self, _source: &ResourceId, _destination: &ResourceId) -> io::Result<()> {
        Err(unsupported("rename"))
    }
}

fn unsupported(operation: &str) -> io::Error {
    io::Error::new(
        io::ErrorKind::Unsupported,
        format!("provider does not support {operation}"),
    )
}
