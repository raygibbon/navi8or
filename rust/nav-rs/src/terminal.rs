use crate::{AppState, Command, Entry, EntryKind, Pane, SortMode, viewer_bridge};
use crossterm::cursor::{Hide, MoveTo, Show};
use crossterm::event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};
use crossterm::style::{
    Attribute, Color, Print, ResetColor, SetAttribute, SetBackgroundColor, SetForegroundColor,
};
use crossterm::terminal::{
    self, Clear, ClearType, EnterAlternateScreen, LeaveAlternateScreen, disable_raw_mode,
    enable_raw_mode,
};
use crossterm::{execute, queue};
use std::io::{self, Stdout, Write};
use std::time::Duration;

const DOS_BLUE: Color = Color::DarkBlue;
const DOS_CYAN: Color = Color::Cyan;

pub fn run(app: &mut AppState) -> io::Result<()> {
    let mut terminal = Terminal::start()?;
    let (width, height) = terminal::size()?;
    app.resize(width, height);
    let mut redraw = true;

    while app.running {
        if redraw {
            terminal.draw(app)?;
            redraw = false;
        }
        if event::poll(Duration::from_millis(100))? {
            match event::read()? {
                Event::Key(key) if key.kind != KeyEventKind::Release => {
                    if let Some(command) = command_for_key(key) {
                        app.dispatch(command);
                        if let Some(request) = app.take_viewer_request() {
                            let result = terminal.suspended(|| viewer_bridge::launch(&request));
                            app.status = match result {
                                Ok(status) if status.success() => "Viewer closed".into(),
                                Ok(status) => format!("Viewer helper exited with {status}"),
                                Err(error) => format!("Unable to open Viewer: {error}"),
                            };
                        }
                        redraw = true;
                    }
                }
                Event::Resize(width, height) => {
                    app.resize(width, height);
                    redraw = true;
                }
                _ => {}
            }
        }
        redraw |= app.service_background_work();
    }
    Ok(())
}

fn command_for_key(key: KeyEvent) -> Option<Command> {
    let control = key.modifiers.contains(KeyModifiers::CONTROL);
    let alt = key.modifiers.contains(KeyModifiers::ALT);
    match key.code {
        KeyCode::F(10) => Some(Command::Quit),
        KeyCode::F(3) => Some(Command::View),
        KeyCode::F(5) => Some(Command::Copy),
        KeyCode::Esc => Some(Command::CancelJob),
        KeyCode::Char('q' | 'Q') if control => Some(Command::Quit),
        KeyCode::Tab | KeyCode::BackTab => Some(Command::SwitchPane),
        KeyCode::Up => Some(Command::Up),
        KeyCode::Down => Some(Command::Down),
        KeyCode::Home => Some(Command::Home),
        KeyCode::End => Some(Command::End),
        KeyCode::PageUp => Some(Command::PageUp),
        KeyCode::PageDown => Some(Command::PageDown),
        KeyCode::Enter => Some(Command::Open),
        KeyCode::Backspace => Some(Command::Parent),
        KeyCode::Left if alt => Some(Command::HistoryBack),
        KeyCode::Right if alt => Some(Command::HistoryForward),
        KeyCode::Char('u' | 'U') if control => Some(Command::SwapPanes),
        KeyCode::Char('r' | 'R') if control => Some(Command::Refresh),
        _ => None,
    }
}

struct Terminal {
    output: Stdout,
    restored: bool,
}

impl Terminal {
    fn start() -> io::Result<Self> {
        let output = io::stdout();
        let mut terminal = Self {
            output,
            restored: true,
        };
        terminal.resume()?;
        Ok(terminal)
    }

    fn resume(&mut self) -> io::Result<()> {
        enable_raw_mode()?;
        if let Err(error) = execute!(self.output, EnterAlternateScreen, Hide) {
            let _ = disable_raw_mode();
            return Err(error);
        }
        self.restored = false;
        Ok(())
    }

    fn suspended<T>(&mut self, operation: impl FnOnce() -> io::Result<T>) -> io::Result<T> {
        self.restore()?;
        let operation_result = operation();
        let resume_result = self.resume();
        match (operation_result, resume_result) {
            (_, Err(error)) => Err(error),
            (result, Ok(())) => result,
        }
    }

    fn draw(&mut self, app: &mut AppState) -> io::Result<()> {
        let (width, height) = terminal::size()?;
        app.resize(width, height);
        queue!(
            self.output,
            SetBackgroundColor(DOS_BLUE),
            SetForegroundColor(Color::White),
            Clear(ClearType::All)
        )?;
        if width < 20 || height < 8 {
            text(
                &mut self.output,
                0,
                0,
                width,
                "Terminal too small",
                Style::Menu,
            )?;
            return self.output.flush();
        }

        let layout = Layout::new(width, height);
        text(
            &mut self.output,
            0,
            0,
            width,
            " File  Mark  Commands  Options  Right ",
            Style::Menu,
        )?;

        for index in 0..2 {
            draw_pane(
                &mut self.output,
                &app.panes[index],
                index == app.active,
                layout.pane_x[index],
                layout.pane_width[index],
                &layout,
            )?;
        }
        for row in layout.title_row..=layout.summary_row {
            text(&mut self.output, layout.divider, row, 1, "│", Style::Border)?;
        }
        text(
            &mut self.output,
            0,
            layout.status_row,
            width,
            &format!(" {}", app.status),
            Style::Status,
        )?;
        draw_key_bar(&mut self.output, layout.key_bar_row, width)?;
        queue!(
            self.output,
            ResetColor,
            SetAttribute(Attribute::Reset),
            Hide
        )?;
        self.output.flush()
    }

    fn restore(&mut self) -> io::Result<()> {
        if self.restored {
            return Ok(());
        }
        let screen_result = execute!(
            self.output,
            ResetColor,
            SetAttribute(Attribute::Reset),
            Show,
            LeaveAlternateScreen
        );
        let raw_result = disable_raw_mode();
        self.restored = screen_result.is_ok() && raw_result.is_ok();
        screen_result.and(raw_result)
    }
}

impl Drop for Terminal {
    fn drop(&mut self) {
        let _ = self.restore();
    }
}

struct Layout {
    divider: u16,
    pane_x: [u16; 2],
    pane_width: [u16; 2],
    title_row: u16,
    location_row: u16,
    header_row: u16,
    body_top: u16,
    body_height: u16,
    summary_row: u16,
    status_row: u16,
    key_bar_row: u16,
}

impl Layout {
    fn new(width: u16, height: u16) -> Self {
        let divider = width / 2;
        let right_x = divider + 1;
        Self {
            divider,
            pane_x: [0, right_x],
            pane_width: [divider, width.saturating_sub(right_x)],
            title_row: 1,
            location_row: 2,
            header_row: 3,
            body_top: 4,
            body_height: height.saturating_sub(7),
            summary_row: height - 3,
            status_row: height - 2,
            key_bar_row: height - 1,
        }
    }
}

fn draw_pane(
    output: &mut Stdout,
    pane: &Pane,
    active: bool,
    x: u16,
    width: u16,
    layout: &Layout,
) -> io::Result<()> {
    text(
        output,
        x,
        layout.title_row,
        width,
        &format!(" {}", pane.provider().display_name()),
        if active {
            Style::ActiveTitle
        } else {
            Style::Title
        },
    )?;
    text(
        output,
        x,
        layout.location_row,
        width,
        &path_text(&pane.location.display, width.saturating_sub(2) as usize),
        Style::Path,
    )?;
    let name_header = if pane.sort_mode == SortMode::Name {
        " Name ^"
    } else {
        " Name"
    };
    text(
        output,
        x,
        layout.header_row,
        width,
        name_header,
        Style::Header,
    )?;
    if width >= 28 {
        let size_header = if pane.sort_mode == SortMode::Size {
            "Size ^"
        } else {
            "Size"
        };
        text(
            output,
            x + width - 12,
            layout.header_row,
            11,
            size_header,
            Style::Header,
        )?;
    }

    for row in 0..layout.body_height {
        let index = pane.offset + usize::from(row);
        let Some(entry) = pane.visible_entry(index) else {
            text(output, x, layout.body_top + row, width, "", Style::File)?;
            continue;
        };
        let selected = index == pane.selected;
        let style = if selected && active {
            Style::Selection
        } else if selected {
            Style::InactiveSelection
        } else if entry.is_directory() {
            Style::Directory
        } else {
            Style::File
        };
        draw_entry(output, entry, x, layout.body_top + row, width, style)?;
    }

    let mut summary = if active {
        pane.selected_entry()
            .map(|entry| match entry.kind {
                EntryKind::Parent | EntryKind::Directory => format!(" {}  <DIR>", entry.name),
                EntryKind::File | EntryKind::Symlink | EntryKind::Other => {
                    format!(" {}  {}", entry.name, format_size(entry.size))
                }
            })
            .unwrap_or_default()
    } else {
        let directories = pane
            .entries()
            .iter()
            .filter(|entry| entry.kind == EntryKind::Directory)
            .count();
        let files = pane
            .entries()
            .iter()
            .filter(|entry| {
                matches!(
                    entry.kind,
                    EntryKind::File | EntryKind::Symlink | EntryKind::Other
                )
            })
            .count();
        format!(" {files} files, {directories} dirs")
    };
    if !pane.filter.is_empty() {
        summary.push_str(&format!("  |  Filter: {}", pane.filter));
    }
    text(
        output,
        x,
        layout.summary_row,
        width,
        &summary,
        Style::Summary,
    )
}

fn draw_entry(
    output: &mut Stdout,
    entry: &Entry,
    x: u16,
    y: u16,
    width: u16,
    style: Style,
) -> io::Result<()> {
    let glyph = match entry.kind {
        EntryKind::Parent => "↰",
        EntryKind::Directory => "◆",
        EntryKind::File | EntryKind::Symlink | EntryKind::Other => " ",
    };
    let suffix = if entry.is_directory() { "/" } else { "" };
    let size = if !entry.is_directory() && width >= 28 {
        format_size(entry.size)
    } else {
        String::new()
    };
    let available = usize::from(width.saturating_sub(2));
    let name_width = if size.is_empty() {
        available
    } else {
        available.saturating_sub(12)
    };
    let mut line = format!(
        "{glyph}{}{}",
        clipped(&entry.name, name_width.saturating_sub(1)),
        suffix
    );
    if !size.is_empty() {
        let used = line.chars().count();
        line.extend(std::iter::repeat_n(
            ' ',
            available.saturating_sub(used + size.chars().count()),
        ));
        line.push_str(&size);
    }
    text(output, x, y, width, &line, style)
}

fn draw_key_bar(output: &mut Stdout, row: u16, width: u16) -> io::Result<()> {
    const KEYS: [(&str, &str); 9] = [
        ("F1", "Help"),
        ("F2", "Menu"),
        ("F3", "View"),
        ("F4", "Edit"),
        ("F5", "Copy"),
        ("F6", "Move"),
        ("F7", "MkDir"),
        ("F8", "Delete"),
        ("F10", "Quit"),
    ];
    let mut x = 0;
    for (index, (key, label)) in KEYS.iter().enumerate() {
        if x >= width {
            break;
        }
        let remaining = width - x;
        let segment = remaining / (KEYS.len() - index) as u16;
        let key_width = (*key).len() as u16;
        text(output, x, row, key_width.min(segment), key, Style::Key)?;
        if segment > key_width {
            text(
                output,
                x + key_width,
                row,
                segment - key_width,
                label,
                Style::KeyLabel,
            )?;
        }
        x += segment;
    }
    if x < width {
        text(output, x, row, width - x, "", Style::KeyLabel)?;
    }
    Ok(())
}

fn format_size(size: Option<u64>) -> String {
    let Some(bytes) = size else {
        return "-".into();
    };
    const UNITS: [&str; 5] = ["B", "KB", "MB", "GB", "TB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1024.0 && unit + 1 < UNITS.len() {
        value /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{bytes} B")
    } else if value < 10.0 || (value < 100.0 && value.fract() >= 0.05) {
        format!("{value:.1} {}", UNITS[unit])
    } else {
        format!("{value:.0} {}", UNITS[unit])
    }
}

fn path_text(path: &str, width: usize) -> String {
    let count = path.chars().count();
    if count <= width {
        return format!(" {path}");
    }
    if width <= 4 {
        return clipped(path, width);
    }
    let tail: String = path
        .chars()
        .rev()
        .take(width - 3)
        .collect::<Vec<_>>()
        .into_iter()
        .rev()
        .collect();
    format!("...{tail}")
}

fn clipped(value: &str, width: usize) -> String {
    value
        .chars()
        .filter(|character| !character.is_control())
        .take(width)
        .collect()
}

#[derive(Clone, Copy)]
enum Style {
    Menu,
    ActiveTitle,
    Title,
    Path,
    Header,
    Border,
    Directory,
    File,
    Selection,
    InactiveSelection,
    Summary,
    Status,
    Key,
    KeyLabel,
}

fn text(
    output: &mut Stdout,
    x: u16,
    y: u16,
    width: u16,
    value: &str,
    style: Style,
) -> io::Result<()> {
    if width == 0 {
        return Ok(());
    }
    let (foreground, background, attribute) = match style {
        Style::Menu => (Color::Black, Color::Grey, Attribute::Bold),
        Style::ActiveTitle => (Color::Yellow, DOS_BLUE, Attribute::Bold),
        Style::Title => (Color::Grey, DOS_BLUE, Attribute::Reset),
        Style::Path => (Color::White, DOS_BLUE, Attribute::Reset),
        Style::Header => (Color::Black, DOS_CYAN, Attribute::Bold),
        Style::Border => (DOS_CYAN, DOS_BLUE, Attribute::Reset),
        Style::Directory => (Color::Yellow, DOS_BLUE, Attribute::Reset),
        Style::File => (Color::White, DOS_BLUE, Attribute::Reset),
        Style::Selection => (Color::Black, DOS_CYAN, Attribute::Bold),
        Style::InactiveSelection => (Color::Black, Color::Grey, Attribute::Reset),
        Style::Summary | Style::Status => (Color::Black, Color::Grey, Attribute::Reset),
        Style::Key => (Color::White, Color::Black, Attribute::Bold),
        Style::KeyLabel => (Color::Black, DOS_CYAN, Attribute::Reset),
    };
    let visible = clipped(value, usize::from(width));
    let padding = usize::from(width).saturating_sub(visible.chars().count());
    queue!(
        output,
        MoveTo(x, y),
        SetForegroundColor(foreground),
        SetBackgroundColor(background),
        SetAttribute(attribute),
        Print(visible),
        Print(" ".repeat(padding))
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn key_mapping_covers_first_milestone_controls() {
        let plain = KeyModifiers::NONE;
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::Tab, plain)),
            Some(Command::SwitchPane)
        );
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::Enter, plain)),
            Some(Command::Open)
        );
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::F(10), plain)),
            Some(Command::Quit)
        );
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::F(3), plain)),
            Some(Command::View)
        );
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::F(5), plain)),
            Some(Command::Copy)
        );
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::Esc, plain)),
            Some(Command::CancelJob)
        );
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::Char('q'), KeyModifiers::CONTROL)),
            Some(Command::Quit)
        );
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::Left, KeyModifiers::ALT)),
            Some(Command::HistoryBack)
        );
        assert_eq!(
            command_for_key(KeyEvent::new(KeyCode::Right, KeyModifiers::ALT)),
            Some(Command::HistoryForward)
        );
    }

    #[test]
    fn long_paths_keep_the_useful_tail() {
        assert_eq!(path_text("/one/two/three/four", 12), "...hree/four");
    }
}
