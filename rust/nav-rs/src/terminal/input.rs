//! Terminal byte parsing. No application commands or UI policy live here.
use super::Size;
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Hash)]
pub struct Modifiers {
    pub ctrl: bool,
    pub alt: bool,
    pub shift: bool,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub enum KeyCode {
    Char(char),
    F(u8),
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Insert,
    Delete,
    Enter,
    Tab,
    Backspace,
    Escape,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub struct Key {
    pub code: KeyCode,
    pub modifiers: Modifiers,
}
impl Key {
    pub const fn plain(code: KeyCode) -> Self {
        Self {
            code,
            modifiers: Modifiers {
                ctrl: false,
                alt: false,
                shift: false,
            },
        }
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Event {
    Key(Key),
    Resize(Size),
}

#[derive(Default)]
pub struct Parser {
    buffer: Vec<u8>,
}
impl Parser {
    pub fn push(&mut self, bytes: &[u8]) {
        self.buffer.extend_from_slice(bytes);
    }
    pub fn next(&mut self, escape_timeout: bool) -> Option<Key> {
        if self.buffer.is_empty() {
            return None;
        }
        let (key, used) = parse(&self.buffer, escape_timeout)?;
        self.buffer.drain(..used);
        Some(key)
    }
    pub fn pending_escape(&self) -> bool {
        self.buffer.first() == Some(&0x1b)
    }
}
fn parse(bytes: &[u8], timed_out: bool) -> Option<(Key, usize)> {
    let first = *bytes.first()?;
    if first == 0x1b {
        if bytes.len() == 1 {
            return timed_out.then_some((Key::plain(KeyCode::Escape), 1));
        }
        if bytes[1] == b'[' {
            if let Some(end) = bytes[2..].iter().position(|b| (0x40..=0x7e).contains(b)) {
                let end = end + 2;
                if let Some(key) = csi(&bytes[2..=end]) {
                    return Some((key, end + 1));
                }
                return Some((Key::plain(KeyCode::Escape), 1));
            }
            return timed_out.then_some((Key::plain(KeyCode::Escape), 1));
        }
        if bytes[1] == b'O' {
            if bytes.len() < 3 {
                return timed_out.then_some((Key::plain(KeyCode::Escape), 1));
            }
            let code = match bytes[2] {
                b'P' => KeyCode::F(1),
                b'Q' => KeyCode::F(2),
                b'R' => KeyCode::F(3),
                b'S' => KeyCode::F(4),
                b'A' => KeyCode::Up,
                b'B' => KeyCode::Down,
                b'C' => KeyCode::Right,
                b'D' => KeyCode::Left,
                b'H' => KeyCode::Home,
                b'F' => KeyCode::End,
                _ => KeyCode::Escape,
            };
            return Some((Key::plain(code), 3));
        }
        if let Some((mut key, count)) = parse(&bytes[1..], timed_out) {
            key.modifiers.alt = true;
            return Some((key, count + 1));
        }
        return timed_out.then_some((Key::plain(KeyCode::Escape), 1));
    }
    let code = match first {
        0 => {
            return Some((
                Key {
                    code: KeyCode::Char(' '),
                    modifiers: Modifiers {
                        ctrl: true,
                        ..Modifiers::default()
                    },
                },
                1,
            ));
        }
        0x1c => {
            return Some((
                Key {
                    code: KeyCode::Char('\\'),
                    modifiers: Modifiers {
                        ctrl: true,
                        ..Modifiers::default()
                    },
                },
                1,
            ));
        }
        b'\r' | b'\n' => KeyCode::Enter,
        b'\t' => KeyCode::Tab,
        0x7f | 0x08 => KeyCode::Backspace,
        1..=26 => {
            return Some((
                Key {
                    code: KeyCode::Char(char::from(b'a' + first - 1)),
                    modifiers: Modifiers {
                        ctrl: true,
                        ..Modifiers::default()
                    },
                },
                1,
            ));
        }
        _ => {
            let text = std::str::from_utf8(bytes)
                .ok()
                .or_else(|| std::str::from_utf8(&bytes[..bytes.len().min(4)]).ok())?;
            let ch = text.chars().next()?;
            return Some((Key::plain(KeyCode::Char(ch)), ch.len_utf8()));
        }
    };
    Some((Key::plain(code), 1))
}
fn csi(sequence: &[u8]) -> Option<Key> {
    let (&final_byte, parameters) = sequence.split_last()?;
    let params = std::str::from_utf8(parameters).ok()?;
    let mut parts = params.split(';');
    let number = parts.next().unwrap_or("").parse::<u16>().unwrap_or(1);
    let modifier = parts
        .next()
        .and_then(|part| part.parse::<u8>().ok())
        .unwrap_or(1)
        .saturating_sub(1);
    let code = match final_byte {
        b'A' => KeyCode::Up,
        b'B' => KeyCode::Down,
        b'C' => KeyCode::Right,
        b'D' => KeyCode::Left,
        b'H' => KeyCode::Home,
        b'F' => KeyCode::End,
        b'Z' => KeyCode::Tab,
        b'~' => match number {
            1 | 7 => KeyCode::Home,
            4 | 8 => KeyCode::End,
            2 => KeyCode::Insert,
            3 => KeyCode::Delete,
            5 => KeyCode::PageUp,
            6 => KeyCode::PageDown,
            11..=15 => KeyCode::F((number - 10) as u8),
            17..=21 => KeyCode::F((number - 11) as u8),
            23 | 24 => KeyCode::F((number - 12) as u8),
            _ => return None,
        },
        _ => return None,
    };
    Some(Key {
        code,
        modifiers: Modifiers {
            ctrl: modifier & 4 != 0,
            alt: modifier & 2 != 0,
            shift: modifier & 1 != 0 || final_byte == b'Z',
        },
    })
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn parses_utf8_modifiers_and_function_keys() {
        let mut parser = Parser::default();
        parser.push("é".as_bytes());
        assert_eq!(parser.next(false), Some(Key::plain(KeyCode::Char('é'))));
        parser.push(b"\x1b[1;5A\x1b[15~\x1c");
        assert_eq!(
            parser.next(false),
            Some(Key {
                code: KeyCode::Up,
                modifiers: Modifiers {
                    ctrl: true,
                    ..Modifiers::default()
                }
            })
        );
        assert_eq!(parser.next(false), Some(Key::plain(KeyCode::F(5))));
        assert_eq!(
            parser.next(false),
            Some(Key {
                code: KeyCode::Char('\\'),
                modifiers: Modifiers {
                    ctrl: true,
                    ..Modifiers::default()
                }
            })
        );
        parser.push(b"\x1b");
        assert_eq!(parser.next(false), None);
        assert_eq!(parser.next(true), Some(Key::plain(KeyCode::Escape)));
    }
}
