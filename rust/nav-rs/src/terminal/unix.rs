use super::{Cell, Event, Frame, Size, input::Parser};
use std::io::{self, Write};
use std::os::fd::AsRawFd;
use std::time::Duration;

pub struct UnixTerminal {
    original: libc::termios,
    parser: Parser,
    active: bool,
    size: Size,
    output: io::Stdout,
}
impl UnixTerminal {
    pub fn start() -> io::Result<Self> {
        let fd = io::stdin().as_raw_fd();
        let mut original = std::mem::MaybeUninit::<libc::termios>::uninit();
        // SAFETY: tcgetattr initializes the termios value on success.
        if unsafe { libc::tcgetattr(fd, original.as_mut_ptr()) } != 0 {
            return Err(io::Error::last_os_error());
        }
        let original = unsafe { original.assume_init() };
        let mut terminal = Self {
            original,
            parser: Parser::default(),
            active: false,
            size: Size::default(),
            output: io::stdout(),
        };
        terminal.resume()?;
        terminal.size = terminal.size()?;
        Ok(terminal)
    }
    fn resume(&mut self) -> io::Result<()> {
        let mut raw = self.original;
        // SAFETY: raw is a valid termios value and stdin is a terminal.
        unsafe {
            libc::cfmakeraw(&mut raw);
        }
        if unsafe { libc::tcsetattr(io::stdin().as_raw_fd(), libc::TCSAFLUSH, &raw) } != 0 {
            return Err(io::Error::last_os_error());
        }
        if let Err(error) = self
            .output
            .write_all(b"\x1b[?1049h\x1b[?25l\x1b[?2004h")
            .and_then(|()| self.output.flush())
        {
            let _ = unsafe {
                libc::tcsetattr(io::stdin().as_raw_fd(), libc::TCSAFLUSH, &self.original)
            };
            return Err(error);
        }
        self.active = true;
        Ok(())
    }
    fn restore(&mut self) -> io::Result<()> {
        if !self.active {
            return Ok(());
        }
        let screen = self
            .output
            .write_all(b"\x1b[0m\x1b[?25h\x1b[?2004l\x1b[?1049l")
            .and_then(|()| self.output.flush());
        let raw =
            if unsafe { libc::tcsetattr(io::stdin().as_raw_fd(), libc::TCSAFLUSH, &self.original) }
                != 0
            {
                Err(io::Error::last_os_error())
            } else {
                Ok(())
            };
        self.active = raw.is_err();
        screen.and(raw)
    }
    pub fn size(&self) -> io::Result<Size> {
        let mut winsize = std::mem::MaybeUninit::<libc::winsize>::zeroed();
        if unsafe {
            libc::ioctl(
                io::stdout().as_raw_fd(),
                libc::TIOCGWINSZ,
                winsize.as_mut_ptr(),
            )
        } != 0
        {
            return Err(io::Error::last_os_error());
        }
        let winsize = unsafe { winsize.assume_init() };
        Ok(Size {
            width: winsize.ws_col.max(1),
            height: winsize.ws_row.max(1),
        })
    }
    pub fn poll(&mut self, timeout: Duration) -> io::Result<Option<Event>> {
        let size = self.size()?;
        if size != self.size {
            self.size = size;
            return Ok(Some(Event::Resize(size)));
        }
        if let Some(key) = self.parser.next(false) {
            return Ok(Some(Event::Key(key)));
        }
        let fd = io::stdin().as_raw_fd();
        let mut descriptor = libc::pollfd {
            fd,
            events: libc::POLLIN,
            revents: 0,
        };
        let wait = if self.parser.pending_escape() {
            timeout.min(Duration::from_millis(35))
        } else {
            timeout
        };
        let result = unsafe {
            libc::poll(
                &mut descriptor,
                1,
                wait.as_millis().min(i32::MAX as u128) as i32,
            )
        };
        if result < 0 {
            let error = io::Error::last_os_error();
            return if error.kind() == io::ErrorKind::Interrupted {
                Ok(None)
            } else {
                Err(error)
            };
        }
        if result == 0 {
            return Ok(self.parser.next(true).map(Event::Key));
        }
        let mut bytes = [0u8; 256];
        let count = unsafe { libc::read(fd, bytes.as_mut_ptr().cast(), bytes.len()) };
        if count < 0 {
            return Err(io::Error::last_os_error());
        }
        if count > 0 {
            self.parser.push(&bytes[..count as usize]);
        }
        Ok(self.parser.next(false).map(Event::Key))
    }
    pub fn present(&mut self, frame: &Frame) -> io::Result<()> {
        let mut output = String::with_capacity(frame.cells().len() * 2);
        output.push_str("\x1b[H");
        let mut previous: Option<Cell> = None;
        for y in 0..frame.size().height {
            output.push_str(&format!("\x1b[{};1H", y + 1));
            for x in 0..frame.size().width {
                let cell = frame.cells()
                    [usize::from(y) * usize::from(frame.size().width) + usize::from(x)];
                if cell.character == '\0' {
                    continue;
                }
                if previous.is_none_or(|old| {
                    old.foreground != cell.foreground
                        || old.background != cell.background
                        || old.attributes != cell.attributes
                }) {
                    output.push_str(&format!(
                        "\x1b[0;{};38;5;{};48;5;{}m",
                        if cell.attributes.bold { 1 } else { 22 },
                        cell.foreground.0,
                        cell.background.0
                    ));
                    previous = Some(cell);
                }
                output.push(cell.character);
            }
        }
        output.push_str("\x1b[?25l");
        self.output.write_all(output.as_bytes())?;
        self.output.flush()
    }
    pub fn suspend<T>(&mut self, operation: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
        self.restore()?;
        let result = operation();
        let resumed = self.resume();
        match resumed {
            Err(error) => Err(error),
            Ok(()) => result,
        }
    }
}
impl Drop for UnixTerminal {
    fn drop(&mut self) {
        let _ = self.restore();
    }
}
