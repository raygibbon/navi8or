# Navi8or UI design

Navi8or is a keyboard-first terminal resource navigator. Its defining shape is
two persistent panes: each has a provider title, a separately clipped location,
shared Full-view columns or dense Brief content, and a secondary summary. A
compact application menu, one global status row, and the function-key bar frame
that workflow.

The design follows a few constraints:

- Selection is the strongest visual signal. Active and inactive selection use
  separate semantic roles without changing text origin or column geometry.
- Modern presentation minimizes chrome: one subtle pane divider, no enclosing
  pane boxes, no extra heading rule, flat menu/key/status surfaces, and modest
  transient frames.
- Modern pane titles use friendly provider or repository names without fixed
  left/right badges. Active title colour and the active selection preserve
  focus; Classic may retain its `[L]`/`[R]` cues.
- Paths, column headings, pane summaries, idle status, and Viewer metadata are
  deliberately subdued. An active filter is shown in its pane summary without
  consuming another row.
- Classic presentation keeps the same commands and navigation while restoring
  the heading separator and allowing stronger frames, spacing, shadows, and
  retro palettes.
- Themes assign colours to semantic roles. The two-value `ui.style` profile
  chooses chrome; it is not a widget-layout language.
- Paths truncate from the leading side so their useful tail remains visible.
  Full headings and body rows share one column calculation, and selection never
  moves those columns.
- Resize is ordinary state. Flexible names shrink first, metadata disappears
  only when necessary, and short terminals retain the menu, minimal pane body,
  summary, status, and keybar when space permits.
- The keybar keeps stable segment geometry while accenting only operations that
  the current selection and provider capabilities can perform.
- Keybar colour communicates capability state, never arbitrary per-command or
  pane-focus emphasis. Available tokens use one key role, labels use the base
  bar role, and unavailable tokens and labels share one disabled role.
- F6 remains labelled `Move`: local panes genuinely support pane-to-pane move,
  while the same unchanged command performs an in-place rename where required
  by providers such as HTTP.
- Symbols are limited to ordinary terminal, Unicode, and CP437-safe characters;
  the interface never depends on Nerd Fonts, Powerline, or other custom fonts.

TDX and Navi8or are sibling applications: TDX is an editor, while Navi8or is a
resource navigator and file manager. Both value terminal-first, keyboard-first,
fast, low-dependency, predictable controls. Navi8or retains provenance for
adapted TDX/TDE mechanics, owns its UI API and visual language, and may
selectively exchange proven ideas or implementations without requiring the two
interfaces to remain visually identical.
