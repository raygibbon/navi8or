use crate::terminal::Size;
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Rect {
    pub x: u16,
    pub y: u16,
    pub width: u16,
    pub height: u16,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Layout {
    pub menu: Rect,
    pub panes: [Rect; 2],
    pub body: [Rect; 2],
    pub status: Rect,
    pub keybar: Rect,
    pub usable: bool,
}
impl Layout {
    pub fn new(size: Size) -> Self {
        let usable = size.width >= 24 && size.height >= 9;
        let divider = size.width / 2;
        let left = Rect {
            x: 0,
            y: 1,
            width: divider,
            height: size.height.saturating_sub(3),
        };
        let right = Rect {
            x: divider,
            y: 1,
            width: size.width.saturating_sub(divider),
            height: size.height.saturating_sub(3),
        };
        let body = [left, right].map(|pane| Rect {
            x: pane.x.saturating_add(1),
            y: pane.y.saturating_add(3),
            width: pane.width.saturating_sub(2),
            height: pane.height.saturating_sub(5),
        });
        Self {
            menu: Rect {
                x: 0,
                y: 0,
                width: size.width,
                height: 1,
            },
            panes: [left, right],
            body,
            status: Rect {
                x: 0,
                y: size.height.saturating_sub(2),
                width: size.width,
                height: 1,
            },
            keybar: Rect {
                x: 0,
                y: size.height.saturating_sub(1),
                width: size.width,
                height: 1,
            },
            usable,
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn sizes_and_body_rects() {
        for (width, height) in [(100, 30), (60, 15), (24, 9), (12, 5)] {
            let layout = Layout::new(Size { width, height });
            assert_eq!(layout.panes[0].width + layout.panes[1].width, width);
            if layout.usable {
                assert!(
                    layout
                        .body
                        .iter()
                        .all(|body| body.height > 0 && body.y + body.height < height - 2)
                );
            }
        }
    }
}
