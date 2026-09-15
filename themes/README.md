# Navi8or UI profiles

The four shipped profiles are:

- `classic-dos.toml`: Classic DOS chrome and blue palette.
- `solar-dark.toml`: the unchanged Modern default.
- `solar-light.toml`: Modern light palette.
- `monochrome.toml`: monochrome presentation.

Run `nav -i themes/classic-dos.toml`, or copy any file to your own location and
edit the explicit layout, panes, viewer, shortcut and primary key defaults.
Modern syntax is `[profile]`, `[colors.role]` and command-to-key `[keys]`.
Legacy `[theme]` and `[ui.*]` palette syntax remains supported for compatibility.
External `[editor]` command settings belong in normal `nav.toml`, not UI profiles.
See [profile configuration](../docs/THEME_FORMAT.md).

Use Options → Preferences to preview changes. Save/Save As creates a personal
profile through the platform config-directory abstraction. These four templates
are protected from normal saves. See [editing profiles](../docs/PROFILES.md).
