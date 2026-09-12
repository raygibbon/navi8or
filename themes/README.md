# Navi8or themes

All shipped themes use Navi8or's semantic format-2 schema while preserving the
intent of their original palettes.

Included:
- Navi8or Classic
- Amber CRT
- Phosphor
- Commander
- Carbon
- Slate
- Paper
- Nordic
- Violet Night
- Monochrome
- DOS VGA
- Workbench
- CDE
- Solar Dark
- Solar Light

Themes use `[theme]`, semantic `[ui.*]` role tables, and optional `[symbols]`.
The small `[ui] style = "modern" | "classic"` profile selects Navi8or-owned
chrome; colours remain role based. Frame appearance is configured separately by
`[ui.frame]` and does not become an arbitrary geometry language.

Note: these files assume Navi8or accepts the same named colour vocabulary
as the supplied Classic TDX-derived theme. See
[`../docs/THEME_FORMAT.md`](../docs/THEME_FORMAT.md) for roles, fallbacks, and
format-1 compatibility.
