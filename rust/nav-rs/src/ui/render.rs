use super::{
    Overlay, UiState, keybar,
    layout::{Layout, Rect},
    menu,
};
use crate::app::{AppState, Pane, SortMode};
use crate::profile::{Role, Theme};
use crate::provider::{Entry, EntryKind};
use crate::terminal::{Cell, Frame, Size};
use std::time::SystemTime;
#[cfg(unix)]
use std::time::UNIX_EPOCH;

pub fn draw(app: &AppState, ui: &UiState, theme: &Theme, size: Size) -> Frame {
    let mut frame = Frame::new(size, theme.cell(Role::Background));
    let layout = Layout::new(size);
    if !layout.usable {
        frame.text(
            0,
            0,
            size.width,
            "Terminal too small",
            theme.cell(Role::Warning),
        );
        return frame;
    }
    draw_menu_bar(&mut frame, ui, theme);
    for index in 0..2 {
        draw_pane(
            &mut frame,
            &app.panes[index],
            index == app.active,
            layout.panes[index],
            layout.body[index],
            theme,
        );
    }
    draw_pull_down(&mut frame, ui, theme);
    draw_overlay(&mut frame, ui, theme);
    frame.text(
        0,
        layout.status.y,
        size.width,
        &format!(" {}", app.status),
        theme.cell(Role::Status),
    );
    draw_keybar(&mut frame, ui, theme, layout.keybar);
    frame
}
fn draw_pane(frame: &mut Frame, pane: &Pane, active: bool, rect: Rect, body: Rect, theme: &Theme) {
    let border = theme.cell(if active {
        Role::BorderActive
    } else {
        Role::Border
    });
    let title = theme.cell(if active {
        Role::PaneTitleActive
    } else {
        Role::PaneTitle
    });
    let right = rect.x + rect.width - 1;
    let bottom = rect.y + rect.height - 1;
    let (vertical, horizontal, top_left, top_right, bottom_left, bottom_right) =
        match theme.frame_style.as_str() {
            "ascii" => ("|", "-", "+", "+", "+", "+"),
            "double" | "combine" | "combine_reverse" => ("║", "═", "╔", "╗", "╚", "╝"),
            "block" => ("█", "█", "█", "█", "█", "█"),
            _ => ("│", "─", "┌", "┐", "└", "┘"),
        };
    for y in rect.y..=bottom {
        frame.text(rect.x, y, 1, vertical, border);
        frame.text(right, y, 1, vertical, border);
    }
    for x in rect.x + 1..right {
        frame.text(x, rect.y, 1, horizontal, border);
        frame.text(x, bottom, 1, horizontal, border);
    }
    frame.text(rect.x, rect.y, 1, top_left, border);
    frame.text(right, rect.y, 1, top_right, border);
    frame.text(rect.x, bottom, 1, bottom_left, border);
    frame.text(right, bottom, 1, bottom_right, border);
    let title_text = format!(" {} ", pane.provider().display_name());
    frame.text(
        rect.x + 1,
        rect.y,
        (title_text.chars().count() as u16).min(rect.width.saturating_sub(2)),
        &title_text,
        title,
    );
    frame.text(
        rect.x + 1,
        rect.y + 1,
        rect.width.saturating_sub(2),
        &tail_path(
            &pane.location.display,
            rect.width.saturating_sub(4) as usize,
        ),
        theme.cell(Role::Path),
    );
    let inner = rect.width.saturating_sub(2);
    let (size_column, date_column) = columns(inner);
    let header = if pane.sort_mode == SortMode::Name {
        "Name ^"
    } else {
        "Name"
    };
    frame.text(
        rect.x + 1,
        rect.y + 2,
        inner,
        header,
        theme.cell(Role::ColumnHeader),
    );
    if let Some(column) = size_column {
        frame.text(
            rect.x + 1 + column,
            rect.y + 2,
            10,
            if pane.sort_mode == SortMode::Size {
                "Size ^"
            } else {
                "Size"
            },
            theme.cell(Role::ColumnHeader),
        );
    }
    if let Some(column) = date_column {
        frame.text(
            rect.x + 1 + column,
            rect.y + 2,
            16,
            if pane.sort_mode == SortMode::Modified {
                "Modified ^"
            } else {
                "Modified"
            },
            theme.cell(Role::ColumnHeader),
        );
    }
    for row in 0..body.height {
        let index = pane.offset + usize::from(row);
        let y = body.y + row;
        let Some(entry) = pane.visible_entry(index) else {
            frame.text(body.x, y, body.width, "", theme.cell(Role::Surface));
            continue;
        };
        let role = if index == pane.selected {
            if active {
                Role::Selection
            } else {
                Role::SelectionInactive
            }
        } else if entry.is_directory() {
            Role::Directory
        } else {
            Role::File
        };
        draw_entry(
            frame,
            entry,
            Rect {
                x: body.x,
                y,
                width: body.width,
                height: 1,
            },
            (size_column, date_column),
            theme.cell(role),
        );
    }
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
    let summary = if active {
        pane.selected_entry()
            .map(|entry| {
                format!(
                    "{}  {}",
                    entry.name,
                    if entry.is_directory() {
                        "<DIR>".into()
                    } else {
                        format_size(entry.size)
                    }
                )
            })
            .unwrap_or_default()
    } else {
        format!("{files} files, {directories} dirs")
    };
    frame.text(
        rect.x + 1,
        bottom - 1,
        inner,
        &summary,
        theme.cell(Role::Status),
    );
}
fn columns(width: u16) -> (Option<u16>, Option<u16>) {
    let date = if width >= 54 { Some(width - 17) } else { None };
    let size = if width >= 28 {
        Some(date.map_or(width - 11, |date| date - 12))
    } else {
        None
    };
    (size, date)
}
fn draw_entry(
    frame: &mut Frame,
    entry: &Entry,
    rect: Rect,
    columns: (Option<u16>, Option<u16>),
    style: Cell,
) {
    let (x, y, width) = (rect.x, rect.y, rect.width);
    let (size_column, date_column) = columns;
    let glyph = match entry.kind {
        EntryKind::Parent => "↰",
        EntryKind::Directory => "◆",
        _ => " ",
    };
    let suffix = if entry.is_directory() { "/" } else { "" };
    let name_width = size_column.unwrap_or(width).saturating_sub(2) as usize;
    frame.text(
        x,
        y,
        width,
        &format!("{glyph}{}{}", clipped(&entry.name, name_width), suffix),
        style,
    );
    if let Some(column) = size_column
        && !entry.is_directory()
    {
        let value = format_size(entry.size);
        frame.text(x + column, y, 10, &format!("{:>10}", value), style);
    }
    if let Some(column) = date_column {
        frame.text(x + column, y, 16, &format_modified(entry.modified), style);
    }
}
fn draw_keybar(frame: &mut Frame, ui: &UiState, theme: &Theme, rect: Rect) {
    let items = keybar::items(&ui.keys);
    for (index, item) in items.iter().enumerate() {
        let start = rect.x + (u32::from(rect.width) * index as u32 / 10) as u16;
        let end = rect.x + (u32::from(rect.width) * (index as u32 + 1) / 10) as u16;
        let width = end.saturating_sub(start);
        let key = format!("F{}", item.number);
        let role = if item.enabled {
            Role::KeybarKey
        } else {
            Role::KeybarDisabled
        };
        frame.text(start, rect.y, width, &key, theme.cell(role));
        if width > key.len() as u16 {
            frame.text(
                start + key.len() as u16,
                rect.y,
                width - key.len() as u16,
                item.label,
                theme.cell(if item.enabled {
                    Role::Keybar
                } else {
                    Role::KeybarDisabled
                }),
            );
        }
    }
}
fn heading_x(index: usize) -> u16 {
    menu::MENUS
        .iter()
        .take(index)
        .map(|m| m.heading.len() as u16 + 2)
        .sum()
}
fn draw_menu_bar(frame: &mut Frame, ui: &UiState, theme: &Theme) {
    for (index, menu) in menu::MENUS.iter().enumerate() {
        let x = heading_x(index);
        frame.text(
            x,
            0,
            frame.size().width.saturating_sub(x),
            &format!(" {} ", menu.heading),
            theme.cell(
                if ui
                    .menu
                    .as_ref()
                    .is_some_and(|state| state.active_major == index)
                {
                    Role::MenuSelected
                } else {
                    Role::Menu
                },
            ),
        );
    }
}
fn draw_pull_down(frame: &mut Frame, ui: &UiState, theme: &Theme) {
    let Some(state) = &ui.menu else {
        return;
    };
    let menu = &menu::MENUS[state.active_major];
    let width = menu
        .items
        .iter()
        .map(|item| item.label.len())
        .max()
        .unwrap_or(0) as u16
        + 4;
    let x = heading_x(state.active_major).min(frame.size().width.saturating_sub(width));
    let width = width.min(frame.size().width.saturating_sub(x));
    if width < 2 {
        return;
    }
    frame.text(
        x,
        1,
        width,
        &format!("┌{}┐", "─".repeat(width.saturating_sub(2) as usize)),
        theme.cell(Role::Menu),
    );
    for (index, item) in menu.items.iter().enumerate() {
        let y = index as u16 + 2;
        if y >= frame.size().height.saturating_sub(3) {
            break;
        }
        let role = if index == state.active_item[state.active_major] {
            Role::MenuSelected
        } else if item.command.is_some_and(crate::app::Command::available) {
            Role::Menu
        } else {
            Role::TextDim
        };
        let content = if item.command.is_none() {
            format!("├{}┤", "─".repeat(width.saturating_sub(2) as usize))
        } else {
            format!(
                "│ {:width$}│",
                item.label,
                width = width.saturating_sub(3) as usize
            )
        };
        frame.text(x, y, width, &content, theme.cell(role));
    }
    let bottom = menu.items.len() as u16 + 2;
    if bottom < frame.size().height.saturating_sub(2) {
        frame.text(
            x,
            bottom,
            width,
            &format!("└{}┘", "─".repeat(width.saturating_sub(2) as usize)),
            theme.cell(Role::Menu),
        );
    }
}
fn draw_overlay(frame: &mut Frame, ui: &UiState, theme: &Theme) {
    let lines = match ui.overlay {
        Overlay::None => return,
        Overlay::Help => menu::help_lines(&ui.keys),
    };
    let size = frame.size();
    let width = size.width.saturating_sub(4).min(58);
    let x = (size.width - width) / 2;
    let y = size.height.saturating_sub(lines.len() as u16) / 2;
    for (index, line) in lines.iter().enumerate() {
        frame.text(
            x,
            y + index as u16,
            width,
            line,
            theme.cell(if index == 0 {
                Role::MenuSelected
            } else {
                Role::Menu
            }),
        );
    }
}
fn clipped(value: &str, width: usize) -> String {
    value
        .chars()
        .filter(|ch| !ch.is_control())
        .take(width)
        .collect()
}
fn tail_path(path: &str, width: usize) -> String {
    if path.chars().count() <= width {
        return format!(" {path}");
    }
    if width < 4 {
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
fn format_size(size: Option<u64>) -> String {
    let Some(bytes) = size else { return "-".into() };
    let units = ["B", "KB", "MB", "GB", "TB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1024.0 && unit < 4 {
        value /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{bytes} B")
    } else {
        format!("{value:.1} {}", units[unit])
    }
}
fn format_modified(time: Option<SystemTime>) -> String {
    let Some(time) = time else { return "-".into() };
    #[cfg(unix)]
    {
        let seconds = time
            .duration_since(UNIX_EPOCH)
            .map(|span| span.as_secs() as libc::time_t)
            .unwrap_or(0);
        let mut local = std::mem::MaybeUninit::<libc::tm>::uninit();
        if unsafe { libc::localtime_r(&seconds, local.as_mut_ptr()) }.is_null() {
            return "-".into();
        }
        let local = unsafe { local.assume_init() };
        let mut output = [0u8; 32];
        let length = unsafe {
            libc::strftime(
                output.as_mut_ptr().cast(),
                output.len(),
                c"%Y-%m-%d %H:%M".as_ptr(),
                &local,
            )
        };
        String::from_utf8_lossy(&output[..length]).into_owned()
    }
    #[cfg(not(unix))]
    {
        let _ = time;
        "-".into()
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::{LocalProvider, Pane};
    use std::sync::Arc;
    #[test]
    fn active_and_inactive_titles_differ() {
        let cwd = std::env::current_dir().unwrap();
        let panes = [
            Pane::open(
                Arc::new(LocalProvider::new()),
                cwd.clone().into_os_string(),
                false,
            )
            .unwrap(),
            Pane::open(Arc::new(LocalProvider::new()), cwd.into_os_string(), false).unwrap(),
        ];
        let app = AppState::new(panes);
        let frame = draw(
            &app,
            &UiState::new(Default::default()),
            &Theme::bundled("solar-dark").unwrap(),
            Size {
                width: 100,
                height: 30,
            },
        );
        assert_ne!(frame.cells()[101].foreground, frame.cells()[153].foreground);
        assert_eq!(frame.cells()[130].character, '─');
    }
}
