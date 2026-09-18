//! Windows backend seam for the existing Navi8or/Termbox2 console implementation.
//! No terminal framework is linked into the Rust core.
use super::{Event, Frame, Size};
use std::io;
use std::time::Duration;
pub struct WindowsTerminal;
impl WindowsTerminal {
    pub fn start() -> io::Result<Self> {
        Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "Rust Windows terminal backend is pending the Navi8or console port",
        ))
    }
    pub fn size(&self) -> io::Result<Size> {
        Err(io::ErrorKind::Unsupported.into())
    }
    pub fn poll(&mut self, _timeout: Duration) -> io::Result<Option<Event>> {
        Err(io::ErrorKind::Unsupported.into())
    }
    pub fn present(&mut self, _frame: &Frame) -> io::Result<()> {
        Err(io::ErrorKind::Unsupported.into())
    }
    pub fn suspend<T>(&mut self, _operation: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
        Err(io::ErrorKind::Unsupported.into())
    }
}
