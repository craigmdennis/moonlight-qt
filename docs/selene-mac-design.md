# Selene for Mac: reskin of moonlight-qt

Status: working draft, 2026-11-08. Choices marked "my call" were made without the user's confirmation.

## What this is

A restyle of upstream Moonlight PC (moonlight-qt, Qt Quick/QML, GPLv3) to match the Selene tvOS home experience in `~/Projects/moonlight-tvos`, following the macOS Human Interface Guidelines including the touch guidance added on 2026-10-28. The streaming core (moonlight-common-c, FFmpeg, SDL, OpenSSL) stays as upstream. Every setting in `SettingsView.qml` stays available.

## Source of the touch rules

Fetched from Apple's HIG JSON (`https://developer.apple.com/tutorials/data/design/human-interface-guidelines/<page>.json`; the HTML pages need JavaScript). Pages: designing-for-macos, gestures, pointing-devices (all changed 2026-10-28).

1. Touch is secondary to keyboard and pointer. Keep control size and layout the same for touch; enlarge tap targets (hit areas) beyond the visible bounds instead of enlarging controls.
2. Small or dense controls may expand temporarily on touch, stay expanded while held and briefly after release.
3. Never make touch the only way to reach a feature. Everything works from keyboard and pointer; every hover-only feature gets a touch equivalent.
4. Touch equivalents on Mac: tap is click, double tap is open, touch-and-hold is context menu, touch-and-hold then drag is drag, one-finger slide scrolls.
5. Hidden (hover-revealed) controls: reveal on a tap near their location, hide when the person finishes or returns to the pointer. A tap on the hidden control's area reveals it without activating it; the next tap activates. Keep essential controls reachable through a short context menu.
6. Use standard framework controls where possible; they size their own tap targets.

## Mapping tvOS to Mac

| tvOS (Selene) | Mac |
|---|---|
| One portrait tile per computer, game art as backdrop | Adaptive grid of tiles in a resizable window; tile click or tap = Play (wake first if offline), double click not needed |
| Status line under the tile (Online, Waking, Sleeping, In use...) | Same text from `StatusLine` rules, as a secondary label under the name |
| Hold for options menu | Context menu (right click, touch-and-hold, Menu key): Play, Wake, Sleep, Test Network, Rename, Details, Remove |
| Sleep keeps its menu slot but is disabled when offline or in use | Same, disabled item with state text |
| Add a computer: alert with subnet prefill | Toolbar button and Cmd-N open a sheet; subnet prefill from the Mac's interface |
| Pairing PIN screen | Sheet with large PIN and Cancel |
| Settings drawer (4 rows plus link to system Settings) | Moonlight's existing full settings page, restyled, opened with Cmd-, |
| Steam last-played art and wallpaper from Apollo endpoints | New C++ in `computermodel` and `nvhttp` (see Work items) |

## Work items

1. Build upstream unchanged on this Mac (needs Qt 6, installed with Homebrew).
2. Restyle `app/gui`: `PcView.qml` tiles, dialogs, `main.qml` chrome, `SettingsView.qml` grouping. Use the Qt `macOS` style (`QtQuickControls2MacOSStyle` ships in Moonlight 6.2.0).
3. Add Apollo support to the C++ layer: `/moonlight/steam` activity and art, wallpaper, sleep (`/moonlight/sleep`), in-use fields. Mirrors `HomeModel.swift` and `MoonlightKit` in moonlight-tvos.
4. Touch behaviour per the rules above: hit-area padding on tiles and toolbar items, context menu on touch-and-hold (upstream already uses `onPressAndHold`), reveal-then-activate for any hover control.
5. Security and performance pass after the reskin (candidates: pairing certificate storage in Keychain, hardened runtime and notarization, current OpenSSL and FFmpeg, startup time, decode CPU). Priority not yet chosen by the user.

## Non-goals

- iOS: keeps its storyboards.
- Replacing the streaming engine or porting `HomeModel` to a new Mac-native target (tried and abandoned).

## Open decisions

- Priority of performance versus security work (user has not chosen).

## Status (2026-11-08, branch selene-mac)

Built and compiling, not yet reviewed on screen by the user:

- Tile: portrait card, tint, badge (Online, Offline, Not Paired, In Use, spinner), state line, 8 pt larger hit area.
- Apollo: Steam line (`/moonlight/steam`), Sleep PC (`/moonlight/sleep`, 4 s spinner then 20 s Offline hold), wallpaper backdrop (`/moonlight/wallpaper`, validated as an image, capped at 16 MB, cached one hour).
- Native macOS Qt style with dark colour scheme forced (Qt 6.8+); pairing sheet with large PIN digits; Add PC prefilled with the Mac's private subnet.
- Settings: all seven groups kept; sky-blue titles removed.

Design calls:

- The state line sits under each tile, not in one line above the row, because a pointer or finger has no single focused tile the way the Apple TV remote does.
- Settings stay one page opened with the toolbar button and Cmd-comma, replacing the tvOS drawer plus system Settings split.
- Touch: the only hover-only content is tooltips, which the guidance says need no touch equivalent. Context menu opens on touch-and-hold. Controls keep their size.

Not started: Keychain storage for the pairing identity, notarization and hardened runtime, startup and decode profiling.
