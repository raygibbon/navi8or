//! Navi8or presentation and interaction state, separate from terminal I/O.
pub mod keybar;
pub mod layout;
pub mod menu;
pub mod render;
use crate::app::Command;
use crate::keys::{Context, Keymap};
use crate::profile::Profile;
use crate::terminal::{Event, Key};
use crate::{AppState, viewer_bridge};
use std::io;
use std::time::Duration;
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Overlay {
    None,
    Help,
}
#[derive(Clone, Debug)]
pub struct UiState {
    pub overlay: Overlay,
    pub keys: Keymap,
    pub menu: Option<menu::MenuState>,
    pub saved_menu: menu::MenuState,
    pub remember_menu: bool,
}
impl UiState {
    pub fn new(keys: Keymap) -> Self {
        Self {
            overlay: Overlay::None,
            keys,
            menu: None,
            saved_menu: menu::MenuState::default(),
            remember_menu: true,
        }
    }
    pub fn context(&self) -> Context {
        if self.menu.is_some() {
            Context::Menu
        } else if self.overlay == Overlay::Help {
            Context::Help
        } else {
            Context::Panel
        }
    }
    pub fn command(&mut self, key: Key) -> Option<Command> {
        self.keys.resolve(self.context(), key)
    }
    pub fn dispatch_overlay(&mut self, command: Command) -> bool {
        match command {
            Command::Help => {
                self.overlay = if self.overlay == Overlay::Help {
                    Overlay::None
                } else {
                    Overlay::Help
                };
                self.keys.reset();
                true
            }
            Command::Menu => {
                self.menu = Some(if self.remember_menu {
                    self.saved_menu.clone()
                } else {
                    menu::MenuState::default()
                });
                self.keys.reset();
                true
            }
            Command::CloseOverlay => {
                self.overlay = Overlay::None;
                if let Some(menu) = self.menu.take() {
                    self.saved_menu = menu;
                }
                self.keys.reset();
                true
            }
            _ => false,
        }
    }
    pub fn on_event(&mut self, event: Event) -> Option<Command> {
        match event {
            Event::Key(key) => {
                let command = self.command(key);
                if let Some(menu) = &mut self.menu {
                    if command == Some(Command::CloseOverlay) {
                        self.dispatch_overlay(Command::CloseOverlay);
                        return None;
                    }
                    let chosen = menu.act(command, key);
                    if let Some(chosen) = chosen {
                        self.dispatch_overlay(Command::CloseOverlay);
                        return Some(chosen);
                    }
                    return None;
                }
                command
            }
            Event::Resize(_) => {
                self.keys.reset();
                None
            }
        }
    }
}

pub fn run(app: &mut AppState, profile: Profile) -> io::Result<()> {
    let mut terminal = crate::terminal::Terminal::start()?;
    let mut ui = UiState::new(profile.keymap.clone());
    ui.remember_menu = profile.menu_remember_position;
    let mut size = terminal.size()?;
    let mut redraw = true;
    while app.running {
        if redraw {
            let layout = layout::Layout::new(size);
            app.set_viewport_rows(usize::from(layout.body[0].height).max(1));
            terminal.present(&render::draw(app, &ui, &profile.theme, size))?;
            redraw = false;
        }
        if let Some(event) = terminal.poll(Duration::from_millis(100))? {
            match event {
                Event::Resize(new_size) => {
                    size = new_size;
                    ui.on_event(event);
                    redraw = true;
                }
                Event::Key(_) => {
                    let command = ui.on_event(event);
                    redraw = true;
                    if let Some(command) = command
                        && !ui.dispatch_overlay(command)
                    {
                        app.dispatch(command);
                        if let Some(request) = app.take_viewer_request() {
                            let result = terminal.suspend(|| viewer_bridge::launch(&request));
                            app.status = match result {
                                Ok(status) if status.success() => "Viewer closed".into(),
                                Ok(status) => format!("Viewer helper exited with {status}"),
                                Err(error) => format!("Viewer unavailable: {error}"),
                            };
                        }
                    }
                }
            }
        }
        redraw |= app.service_background_work();
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::terminal::{KeyCode, Modifiers};
    #[test]
    fn menu_is_state_not_overlay_and_restores_panel() {
        let mut ui = UiState::new(Keymap::default());
        assert!(ui.dispatch_overlay(Command::Menu));
        assert_eq!(ui.overlay, Overlay::None);
        assert_eq!(ui.context(), Context::Menu);
        assert_eq!(ui.menu.as_ref().unwrap().active_major, 0);
        ui.on_event(Event::Key(Key::plain(KeyCode::Right)));
        assert_eq!(ui.menu.as_ref().unwrap().active_major, 1);
        ui.on_event(Event::Key(Key::plain(KeyCode::Escape)));
        assert_eq!(ui.context(), Context::Panel);
        assert!(ui.menu.is_none());
        ui.dispatch_overlay(Command::Menu);
        assert_eq!(ui.menu.as_ref().unwrap().active_major, 1);
    }
    #[test]
    fn commands_and_overlays_are_terminal_independent() {
        let mut ui = UiState::new(Keymap::default());
        assert_eq!(ui.command(Key::plain(KeyCode::F(1))), Some(Command::Help));
        assert!(ui.dispatch_overlay(Command::Help));
        assert_eq!(ui.overlay, Overlay::Help);
        assert_eq!(
            ui.command(Key::plain(KeyCode::Escape)),
            Some(Command::CloseOverlay)
        );
        assert!(ui.dispatch_overlay(Command::CloseOverlay));
        assert_eq!(ui.overlay, Overlay::None);
        assert_eq!(
            ui.command(Key {
                code: KeyCode::Char('n'),
                modifiers: Modifiers {
                    ctrl: true,
                    ..Modifiers::default()
                }
            }),
            None
        );
        assert_eq!(
            ui.command(Key::plain(KeyCode::Char('r'))),
            Some(Command::Refresh)
        );
    }
}
