use std::fs::Metadata;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::time::SystemTime;

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
    pub const COPY: Self = Self(1 << 7);

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
}

#[derive(Clone, Debug)]
pub struct Entry {
    pub name: String,
    pub resource: PathBuf,
    pub kind: EntryKind,
    pub size: Option<u64>,
    pub modified: Option<SystemTime>,
}

impl Entry {
    pub fn is_directory(&self) -> bool {
        matches!(self.kind, EntryKind::Parent | EntryKind::Directory)
    }
}

/// Synchronous provider operations used by the local core.
///
/// Future network providers will execute these operations behind the jobs
/// boundary rather than on the UI thread. The trait does not expose a runtime
/// or couple application state to Tokio.
pub trait Provider: Send + Sync {
    fn scheme(&self) -> &'static str;
    fn display_name(&self) -> &'static str;
    fn capabilities(&self) -> Capabilities;

    fn location(&self, input: &Path) -> io::Result<PathBuf>;
    fn parent(&self, location: &Path) -> io::Result<PathBuf>;
    fn child(&self, location: &Path, name: &str) -> io::Result<PathBuf>;
    fn list(&self, location: &Path, show_hidden: bool) -> io::Result<Vec<Entry>>;

    fn stat(&self, _resource: &Path) -> io::Result<Metadata> {
        Err(unsupported("stat"))
    }

    fn open_read(&self, _resource: &Path) -> io::Result<Box<dyn Read + Send>> {
        Err(unsupported("read"))
    }

    fn open_write(&self, _resource: &Path, _overwrite: bool) -> io::Result<Box<dyn Write + Send>> {
        Err(unsupported("write"))
    }

    fn mkdir(&self, _resource: &Path) -> io::Result<()> {
        Err(unsupported("mkdir"))
    }

    fn delete(&self, _resource: &Path) -> io::Result<()> {
        Err(unsupported("delete"))
    }

    fn rename(&self, _source: &Path, _destination: &Path) -> io::Result<()> {
        Err(unsupported("rename"))
    }

    fn copy(&self, _source: &Path, _destination: &Path) -> io::Result<u64> {
        Err(unsupported("copy"))
    }
}

fn unsupported(operation: &str) -> io::Error {
    io::Error::new(
        io::ErrorKind::Unsupported,
        format!("provider does not support {operation}"),
    )
}
