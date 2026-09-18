use crate::app::Command;
use crate::keys::{Context, Keymap};
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FunctionItem {
    pub number: u8,
    pub label: &'static str,
    pub binding: Option<String>,
    pub enabled: bool,
}
const DEFAULT_LABELS: [&str; 10] = [
    "Help", "Menu", "View", "Edit", "Copy", "Move", "MkDir", "Delete", "—", "Quit",
];
fn command_label(command: Command) -> &'static str {
    match command {
        Command::Help => "Help",
        Command::Menu => "Menu",
        Command::View => "View",
        Command::Edit => "Edit",
        Command::Move => "Move",
        Command::MkDir => "MkDir",
        Command::Delete => "Delete",
        Command::Copy => "Copy",
        Command::Quit => "Quit",
        Command::Open => "Open",
        Command::Parent => "Parent",
        Command::Refresh => "Refresh",
        Command::SwitchPane => "Switch",
        Command::SwapPanes => "Swap",
        Command::HistoryBack => "Back",
        Command::HistoryForward => "Forward",
        Command::ToggleHidden => "Hidden",
        Command::SortName => "Name",
        Command::SortSize => "Size",
        Command::SortModified => "Date",
        Command::Up => "Up",
        Command::Down => "Down",
        Command::Home => "Home",
        Command::End => "End",
        Command::PageUp => "PgUp",
        Command::PageDown => "PgDn",
        Command::CancelJob => "Cancel",
        Command::CloseOverlay => "Close",
        _ => "—",
    }
}
pub fn items(map: &Keymap) -> [FunctionItem; 10] {
    std::array::from_fn(|index| {
        let number = (index + 1) as u8;
        let bound = map.function(Context::Panel, number);
        FunctionItem {
            number,
            label: bound.map_or(DEFAULT_LABELS[index], command_label),
            binding: bound.and_then(|command| map.label(Context::Panel, command)),
            enabled: bound.is_some_and(Command::available),
        }
    })
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_ten_slots_and_disabled_features() {
        let items = items(&Keymap::default());
        assert_eq!(items.len(), 10);
        assert_eq!(items[3].label, "Edit");
        for number in [4, 6, 7, 8, 9] {
            assert!(!items[(number - 1) as usize].enabled);
        }
        assert!(items[0].enabled && items[9].enabled);
    }
    #[test]
    fn remapped_function_key_shows_its_actual_action() {
        let mut map = Keymap::default();
        map.bind(Context::Panel, "F4", Command::Copy).unwrap();
        let slots = items(&map);
        assert!(slots[3].enabled);
        assert_eq!(slots[3].label, "Copy");
    }
}
