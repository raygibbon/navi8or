//! Navi8or-owned cell terminal. Platform I/O stops at this boundary.
pub mod input;
#[cfg(unix)]
mod unix;
#[cfg(windows)]
mod windows;

pub use input::{Event, Key, KeyCode, Modifiers};
use std::io;
use std::time::Duration;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Size {
    pub width: u16,
    pub height: u16,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Color(pub u8);
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct Attributes {
    pub bold: bool,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Cell {
    pub character: char,
    pub foreground: Color,
    pub background: Color,
    pub attributes: Attributes,
}
impl Default for Cell {
    fn default() -> Self {
        Self {
            character: ' ',
            foreground: Color(7),
            background: Color(0),
            attributes: Attributes::default(),
        }
    }
}

pub struct Frame {
    size: Size,
    cells: Vec<Cell>,
}
impl Frame {
    pub fn new(size: Size, fill: Cell) -> Self {
        Self {
            size,
            cells: vec![fill; usize::from(size.width) * usize::from(size.height)],
        }
    }
    pub fn size(&self) -> Size {
        self.size
    }
    pub fn clear(&mut self, fill: Cell) {
        self.cells.fill(fill);
    }
    pub fn put(&mut self, x: u16, y: u16, cell: Cell) {
        if x < self.size.width && y < self.size.height {
            self.cells[usize::from(y) * usize::from(self.size.width) + usize::from(x)] = cell;
        }
    }
    pub fn text(&mut self, x: u16, y: u16, width: u16, value: &str, style: Cell) {
        let end = x.saturating_add(width).min(self.size.width);
        let mut column = x;
        for ch in value.chars().filter(|ch| !ch.is_control()) {
            let span = unicode_width::UnicodeWidthChar::width(ch).unwrap_or(0);
            if span == 0 || usize::from(column) + span > usize::from(end) {
                continue;
            }
            self.put(
                column,
                y,
                Cell {
                    character: ch,
                    ..style
                },
            );
            if span == 2 {
                self.put(
                    column + 1,
                    y,
                    Cell {
                        character: '\0',
                        ..style
                    },
                );
            }
            column += span as u16;
        }
        while column < end {
            self.put(column, y, style);
            column += 1;
        }
    }
    pub fn cells(&self) -> &[Cell] {
        &self.cells
    }
}

#[cfg(unix)]
type Platform = unix::UnixTerminal;
#[cfg(windows)]
type Platform = windows::WindowsTerminal;
pub struct Terminal {
    platform: Platform,
}
impl Terminal {
    pub fn start() -> io::Result<Self> {
        Ok(Self {
            platform: Platform::start()?,
        })
    }
    pub fn size(&self) -> io::Result<Size> {
        self.platform.size()
    }
    pub fn poll(&mut self, timeout: Duration) -> io::Result<Option<Event>> {
        self.platform.poll(timeout)
    }
    pub fn present(&mut self, frame: &Frame) -> io::Result<()> {
        self.platform.present(frame)
    }
    pub fn suspend<T>(&mut self, operation: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
        self.platform.suspend(operation)
    }
}
