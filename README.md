# wst

A small Windows terminal emulator written in Glosso. It runs PowerShell,
Command Prompt, or Nushell through Windows ConPTY, uses Glosso's standard
`Gui` module for windowing, input, and rendering, and uses Clay for the
surrounding interface layout. Its
configuration is a Lua program that returns a table. Tabs and nested split
panes keep independent shell sessions and scrollback in one window.

Requires **64-bit Windows 10 version 1809 or later**, a Windows x64 Glosso
compiler on `PATH`, and the selected shell installed. PowerShell and cmd use
`powershell.exe -NoLogo` and `cmd.exe /d`; Nushell uses `nu.exe` from `PATH`.
Lua 5.5 and the vendored Clay source are linked statically, so no separate Lua
or Clay DLL is needed. The initial build compiles those dependency sources into
the local `build/cache` directory. The executable embeds Microsoft's x64 ConPTY
library and console host to support terminal graphics. When the first shell
starts, wst extracts the signed files to a versioned directory under
`%LOCALAPPDATA%\wst\conpty`. If that cache is unavailable, wst tries
`%LOCALAPPDATA%\Temp\wst\conpty`. Later launches reuse matching files.
wst falls back to the Windows system ConPTY if it cannot load the bundled pair.

Application logic is Glosso, including ConPTY workers, Windows API bindings,
installed-font discovery, and Clay layout construction. Windows services are
called directly through Glosso's foreign-function interface. A small native
presentation adapter keeps the standard `Gui` renderer compatible with Windows
window opacity. Clay, Lua, and `Gui` retain their own native dependencies.

Build and launch with the installed Glosso compiler:

```powershell
glosso compile first.glo --cache-dir=build/cache
.\build\wst.exe
```

`first.glo` follows the compile-time build-script pattern used by `fls`,
`glofmt`, and `glolsp`: it uses `Compiler.Build_Profile` and `compile_file`,
then suppresses its own executable output. The default task builds an optimized
GUI executable. All build automation is Glosso; no PowerShell scripts are needed.

The build uses Git to patch a temporary copy of older Glosso SDKs when needed.
The memory patch makes `Gui` upload text geometry in batches and allocate
Direct3D scene resources on first use. The GPU patch adds adapter selection
to the Windows `Gui` renderer. When the selected SDK lacks emoji presentation
properties, the build applies the `std/Unicode` portion of `unicode-emoji.patch`.

To update a sibling Glosso checkout that lacks the emoji APIs, apply the
full patch from wst. It includes the pinned data, generator changes, and tests.

```powershell
git -C ../glosso apply (Resolve-Path vendor/glosso/unicode-emoji.patch).Path
glosso compile first.glo --std=../glosso/std --cache-dir=build/cache -- test
```

To use GPU selection in other Glosso applications, run the following from
the wst project directory. Older SDKs that still allocate 3D scene resources
at startup need `vendor/glosso/gui-memory.patch` applied first.

```powershell
git -C ../glosso apply (Resolve-Path vendor/glosso/gui-gpu.patch).Path
$env:GLOSSO_STD = (Resolve-Path ../glosso/std).Path
```

With a fixed SDK selected, you can also compile just the executable:

```powershell
glosso compile src/main.glo -O3 -o build/wst.exe --cache-dir build/cache --linker-arg /subsystem:windows
```

`glosso compile first.glo --cache-dir=build/cache -- build --console` builds
the same output path with a diagnostic console.
The default build opens only the terminal window. All generated binaries and
compiler caches belong in `build/`.

Copy `wst.exe` alone to install it. Include `LICENSE` and `THIRD_PARTY_NOTICES`
when redistributing a build.

Tasks follow the compiler's `--` separator:

```powershell
glosso compile first.glo --cache-dir=build/cache -- help
glosso compile first.glo --cache-dir=build/cache -- run --profile nushell_dev
glosso compile first.glo --cache-dir=build/cache -- test
```

The script locates its project from `first.glo`, so it also works when invoked
by absolute path from another working directory. Tests are discovered and run
in filename order, with failures propagated to the compiler's exit status.

Create a binary release archive with:

```powershell
glosso compile first.glo --cache-dir=build/cache -- release --version 0.1.0
```

This builds a fresh executable and creates
`build/releases/wst-0.1.0-windows-x64.zip`. The archive contains `wst.exe`,
`LICENSE`, `THIRD_PARTY_NOTICES`, `README.md`,
and `config.example.lua` at its root. The version labels the archive; omitting it uses `dev`. The example
configuration remains an example until you copy it to your configuration path.
ZIP creation uses a deterministic Glosso ZIP32 writer with stored entries and
atomic replacement, without an external archive program.

The application icon is embedded in `wst.exe`; `Gui` uses it for the window and
taskbar, and Windows Explorer uses it for the executable. The icon includes
16–256 px variants for different display scales, with no separate runtime asset
files required. Edit `assets/wst.svg` and regenerate its ICO and PNG with:

```powershell
glosso compile first.glo --cache-dir=build/cache -- icon
```

The task compiles a native Glosso SVG rasterizer using the standard image
library, then writes `assets/wst.ico` and `assets/wst.png`. For a preview that
preserves the current assets, add `--output-dir build/icon-preview`. Rebuild
the application after changing the icon.

wst uses an original compact title rail with rounded active tabs, a small `>_`
mark, and a profile menu beside the new-tab button. Its colors follow the Lua
background, foreground, accent and palette. Thin pane separators retain the
same wider drag targets; scrollbar thumbs stay inside their existing gutters.
The controls and smaller proportional tab font stay the same size when terminal
text is zoomed. At narrow widths, zoom controls remain available in the menu.

Click `+` to open the default profile, or the chevron to choose a profile, split
a pane, close a tab, or change zoom. Profile labels are sorted alphabetically.
The menu supports arrow keys, Enter, Escape,
and wheel scrolling. Wheel over the tab rail to switch tabs; Ctrl+wheel still
zooms. Drag empty header space to move the window, double-click it to maximize
or restore, and use the `>_` mark, right-click on the header, or unbound Alt+Space
for the Windows system menu. Standard resize borders and window shortcuts remain.

The design draws on general interface patterns documented by
[Alacritty](https://alacritty.org/config-alacritty.html),
[Ghostty](https://ghostty.org/docs/config/reference),
[Rio](https://rioterm.com/docs/config#navigation),
[Kitty](https://sw.kovidgoyal.net/kitty/conf/#tab-bar), and
[WezTerm](https://wezterm.org/config/appearance.html#tab-bar-appearance-colors).
The chrome geometry, artwork, and drawing implementation are original to wst.

Choose a shell or provide a complete command line:

```powershell
.\build\wst.exe --shell cmd
.\build\wst.exe --shell nushell
.\build\wst.exe --discrete-gpu
.\build\wst.exe --command 'powershell.exe -NoLogo -NoProfile'
.\build\wst.exe --config .\my-config.lua --hold
.\build\wst.exe --help
```

Supported options are `--shell powershell|cmd|nushell`, `--command <command>`,
`--profile <id>`, `--config <path>`, `--hold`, `--discrete-gpu`, `--smoke-test`, and `--help`. A nonempty custom
command overrides the selected shell. `--hold` keeps the window open after
the child exits.

By default, wst prefers the minimum-power GPU, usually the integrated GPU on
a hybrid laptop. Pass `--discrete-gpu` to prefer the high-performance GPU for
that launch. If the preferred adapter is unavailable, the renderer tries other
available hardware and can fall back to software rendering. Other Glosso
applications using the patched Windows `Gui` can request the same preference
by passing `Gui.Window_Flags.High_Performance_Gpu` to `Gui.create_window`.
The smoke test prints the adapter that created the Direct3D device.

If a graphics or window update fails during a session, wst attempts to
recreate the window at its previous position and size. Running shells, tabs,
and scrollback survive a successful recovery. A failed recovery or another
failure within ten seconds displays the error before the terminal closes.

Copy [config.example.lua](config.example.lua) to `%APPDATA%\wst\config.lua`
to customize the terminal. This single file contains the
general settings, shell profiles, and all keyboard shortcuts. You can also
use it directly with `--config .\config.example.lua`. An explicit `--config` path
takes priority; otherwise an existing `./config.lua` takes precedence over
`%APPDATA%\wst\config.lua`. The user path comes from
`File_System/Directory_Paths.get_config_dir()` joined with `wst/config.lua`.
The application does not create configuration files or their directories.
Invalid configuration prints a diagnostic and preserves the previous
settings. Known field names, types, numeric ranges, and RGB colors are
validated before any settings are committed.

A minimal configuration can contain:

```lua
local base_size = 16
return {
  shell = "powershell",
  font_family = "Cascadia Mono",
  font_size = base_size + 2,
  font_ligatures = true,
  window_opacity = 1.0,
  columns = 100,
  rows = 30,
  scrollback = 5000,
  padding = 12,
  foreground = "#d8dee9",
  background = "#11151c",
  accent = 0x81a1c1,
  cursor = "#e5c07b",
  close_on_exit = true,
}
```

The example file documents every field and its allowed range, including the
optional full `command` string. Color values accept `"#RRGGBB"` strings or RGB
integers. Configuration files execute as ordinary local Lua programs; use
configuration you trust.

The supplied color scheme is enabled in [config.example.lua](config.example.lua).
Complete schemes use nested Lua tables:

```lua
return {
  colors = {
    primary = { foreground = "#bdbdbd", background = "#080808" },
    cursor = { text = "#080808", cursor = "#9e9e9e" },
    selection = { text = "#080808", background = "#b2ceee" },
    normal = {
      black = "#323437", red = "#ff5454", green = "#8cc85f", yellow = "#e3c78a",
      blue = "#80a0ff", magenta = "#cf87e8", cyan = "#79dac8", white = "#c6c6c6",
    },
    bright = {
      black = "#949494", red = "#ff5189", green = "#36c692", yellow = "#c6c684",
      blue = "#74b2ff", magenta = "#ae81ff", cyan = "#85dc85", white = "#e4e4e4",
    },
  },
}
```

Each group and color is optional; omitted values use the defaults. `normal`
and `bright` configure ANSI palette indices 0–7 and 8–15 respectively.
The top-level `foreground`, `background`, and `cursor` fields remain supported
and override the corresponding nested values when both are supplied. Unknown
groups, color names, or invalid values reject the configuration.

Named profiles let you keep ordinary and developer shells in the same window.
Each profile has a map key used by `default_profile`, `--profile`, and shortcuts:

```lua
return {
  default_profile = "nushell_dev",
  profiles = {
    powershell = { name = "Windows PowerShell", shell = "powershell" },
    cmd = { name = "Command Prompt", shell = "cmd" },
    nushell_dev = {
      name = "Nushell Developer",
      shell = "nushell",
      starting_directory = "%USERPROFILE%",
      command = [[powershell.exe -NoLogo -NoProfile -NoExit -Command "& { Import-Module 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'; Enter-VsDevShell 60eeb474 -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'; & nu.exe }"]],
    },
  },
  keybindings = {
    new_tab = {
      "Ctrl+Shift+T",
      { key = "Ctrl+Shift+1", profile = "nushell_dev" },
      { key = "Ctrl+Shift+2", profile = "powershell" },
      { key = "Ctrl+Shift+3", profile = "cmd" },
    },
    split_vertical = {
      "Ctrl+Shift+E",
      { key = "Ctrl+Alt+E", profile = "nushell_dev" },
    },
  },
}
```

[config.example.lua](config.example.lua) enables the Nushell Developer profile
by default and includes ordinary PowerShell/cmd profiles alongside the general
terminal settings. Its shortcuts open developer, PowerShell, and cmd tabs with
`Ctrl+Shift+1`, `Ctrl+Shift+2`, and `Ctrl+Shift+3`; `Ctrl+Alt+E` and `Ctrl+Alt+O`
open developer splits. Launch the complete configuration directly:

```powershell
.\build\wst.exe --config .\config.example.lua
```

Update the Visual Studio path and installation ID for your installation.
The example imports Visual Studio's developer environment in PowerShell,
then starts Nushell with that inherited environment. Lua `[[...]]` strings
preserve Windows backslashes and quotes. `%ENVIRONMENT_VARIABLE%` references
expand in `command` and `starting_directory` before launch. A nonempty profile
command overrides its `shell`; an empty directory inherits the application's
working directory. Profile appearance uses the global font and color settings.

Up to 32 profiles may be defined. `name`, `shell`, `command`, and
`starting_directory` are optional; their defaults are the profile ID,
`"powershell"`, `""`, and `""`. An empty or omitted `default_profile` uses the
top-level `shell` / `command`. Unqualified new-tab shortcuts and the `+` button
use the default profile, as does the first tab at startup. Unqualified split shortcuts inherit the focused pane's
profile. A `{ key = "...", profile = "..." }` entry selects a profile explicitly
for `new_tab`, `split_horizontal`, or `split_vertical`. These entries can be
mixed with ordinary chord strings in the same action's array. Other actions
accept the existing chord syntax. Unknown profiles reject the whole config.

Profile definitions and shortcuts hot reload. Changing a profile's command,
shell, or starting directory replaces only sessions using that profile;
changing its display name keeps them running. Changing `default_profile`
affects future tabs. Removing a profile leaves its existing sessions running
with their last settings, while preventing new launches of the removed profile.
Failed replacement launches preserve all sessions and the last valid config.
`--profile` selects a default for that window. `--shell` and `--command` select
the legacy launch settings instead and clear the profile override. Unrelated
saves preserve these overrides. Removing a profile definition also clears a
command-line default that refers to that removed profile.

Change keyboard shortcuts through `keybindings` in the returned Lua table:

```lua
return {
  keybindings = {
    copy = "Ctrl+Shift+C",
    paste = { "Ctrl+Shift+V", "Shift+Insert" },
    zoom_in = { "Ctrl+Equal", "Ctrl+Shift+Equal" },
    zoom_out = "Ctrl+Minus",
    reset_zoom = "Ctrl+0",
    scroll_line_up = "Alt+Up",
    scroll_line_down = "Alt+Down",
    new_tab = "Ctrl+Shift+T",
    close_tab = "Ctrl+Shift+W",
    next_tab = "Ctrl+Tab",
    previous_tab = "Ctrl+Shift+Tab",
    split_horizontal = "Ctrl+Shift+O",
    split_vertical = "Ctrl+Shift+E",
    close_pane = "Ctrl+Shift+X",
    close_window = "Ctrl+Shift+Q",
    -- scroll_page_up = false,
    -- scroll_page_down = {},
  },
}
```

An action accepts one chord string, an array of chord strings, or `false`/`{}`
to disable its keyboard shortcuts. Supplying an action replaces all of that
action's previous chords; unspecified actions retain their defaults. For
example, the `zoom_in` override above removes the default keypad-plus chords.
Mouse gestures, scrollbar controls, and header buttons remain available.
The combined table supports up to 128 chords. Every Lua configuration field
reloads on save, usually within 250–500 ms. Removing an override restores its
default. Changes apply together after validation; a failed reload keeps the
last working settings and shell sessions. Tab and pane shortcuts use the same
reload behavior, including arrays of chords and disabling with `false` or `{}`.

Use the tab bar or `new_tab` to open a tab, and click a tab or use `next_tab`
and `previous_tab` to switch. `split_horizontal` divides the focused pane into
top and bottom panes; `split_vertical` creates left and right panes. Splits
can be nested. Drag the divider between panes to resize them; the pointer
changes to a resize cursor over each divider. Pane sizes update while dragging,
with minimum sizes enforced, and their proportions survive window resizing,
zoom, and config reloads. Escape or losing mouse capture ends a resize.
Click a pane, cycle with `next_pane` / `previous_pane`, or use
the directional focus actions to choose where typing, selection, and scrolling
go. Every pane runs its own shell and continues processing output in the
background. New tabs use the current default profile; splits inherit the
focused pane's profile unless their binding names another one.

The window supports up to 16 tabs and 32 panes in total. `close_pane` closes
the focused pane; closing its last pane closes the tab. `close_tab` closes
every pane in that tab. Closing the last tab closes the window. These actions
end the affected shell sessions and interrupt their running commands.

Font changes keep the window size fixed unless the same save also changes
`columns` or `rows`, and update the shell's row and column count.
Manual zoom is preserved when the configured `font_size` is unchanged;
editing that value sets the current font size and the new zoom-reset size.
`font_ligatures` enables OpenType ligatures by default and hot reloads on save.
Choose a font with programming ligatures, such as Cascadia Code or
D2KodingLigature Nerd Font Mono; fonts without them retain their ordinary glyphs.
Ligatures combine compatible runs of single-cell ASCII text while preserving
the terminal grid, original text, and cell-based cursor and selection behavior.
Set `font_ligatures = false` to draw each character separately.

wst uses Segoe UI Emoji for color glyphs such as 🏠 and ⏰. Both occupy two
terminal columns. Symbols such as ❤ use their text appearance by default and
switch to color with the emoji presentation selector (❤️). The solid left and
right Powerline separators fill the cell height with any terminal font.

`window_opacity` controls transparency and hot reloads on save. Its range is
`0.1` to `1.0`; the default `1.0` is fully opaque, and `0.9` gives 90% opacity.
It applies to the entire window, including text, cursor, and header. Mouse and
keyboard input keep working normally at every supported opacity.
Default text and background colors update in existing text and scrollback.
ANSI colors and indexed colors 0–15 also follow the configured palette in
existing text and scrollback. Explicit RGB colors and xterm indexed colors
16–255 retain their values. Cursor and selection colors update immediately.

| Setting | Reload behavior |
| --- | --- |
| `keybindings`, fonts, `font_ligatures`, colors | Update the current window and every pane, including default and ANSI palette colors in existing text |
| `window_opacity` | Update the whole window immediately, including text and header; `1.0` restores full opacity |
| `columns`, `rows` | Resize the corresponding window axis to the requested grid size, subject to the window minimum |
| `padding` | Update spacing within the current window size and recalculate the grid |
| `scrollback` | Apply the new limit to every pane immediately, retaining the newest lines; zero clears history |
| `close_on_exit` | Close each exited pane when enabled, including a pane held after its shell has already exited |
| `shell`, `command` | Replace unnamed legacy sessions if the effective command changes, retaining tabs, pane layout, and scrollback |
| `profiles` | Replace sessions using a changed launch definition; display-name changes apply without restarting |
| `default_profile` | Choose the profile for future tabs without replacing existing sessions |

A shell replacement closes the previous console session, interrupting its
running commands, and starts a fresh session with a cleared screen. Its working
directory and shell variables are reset. Replacements are prepared before
switching; a launch failure leaves the previous sessions running. A nonempty
`command` takes precedence over `shell`.
Changing an ignored shell name alone does not restart that custom command.

Unrelated saves preserve manual zoom, manual window resizing, and command-line
overrides (`--shell`, `--command`, `--profile`, `--hold`). Changing the corresponding Lua
value takes precedence over that override. Shell and command are treated as a
pair, so changing either applies both values from the Lua file.

The application watches the configuration path selected at startup. If no
configuration exists, it watches `%APPDATA%\wst\config.lua` for creation,
or the explicit `--config` path when supplied. Deleting the file keeps the
last valid settings; recreating it reloads them. The selected
path remains fixed for that window, including when editors save by replacing
the file.

The whole Lua file is validated on each reload. Invalid or incomplete saves
leave the previous settings active and show a red `!` in the zoom control.
Click that indicator to view the full error; saving never opens a dialog
automatically. Correcting and saving the file applies the new settings.

| Action name | Effect |
| --- | --- |
| `copy` | Copy selected text |
| `paste` | Paste clipboard text |
| `zoom_in` | Increase terminal font size |
| `zoom_out` | Decrease terminal font size |
| `reset_zoom` | Restore the configured font size |
| `scroll_page_up` | Move one page into older output |
| `scroll_page_down` | Move one page toward live output |
| `scroll_top` | Show the oldest retained output |
| `scroll_bottom` | Return to live output |
| `scroll_line_up` | Move one line into older output |
| `scroll_line_down` | Move one line toward live output |
| `close_window` | Close the terminal window and all its shell sessions |
| `new_tab` | Open a tab with a new shell |
| `close_tab` | Close the active tab and all its panes |
| `next_tab`, `previous_tab` | Switch tabs, wrapping at either end |
| `split_horizontal` | Split the focused pane into top and bottom panes |
| `split_vertical` | Split the focused pane into left and right panes |
| `close_pane` | Close the focused pane |
| `next_pane`, `previous_pane` | Cycle focus through the active tab's panes |
| `focus_pane_left`, `focus_pane_right` | Focus a pane in the indicated direction |
| `focus_pane_up`, `focus_pane_down` | Focus a pane above or below |

Chords use `Ctrl` (or `Control`), `Shift`, and `Alt`, joined with `+`, followed
by a key. Key and modifier names are case-insensitive. Modifiers match exactly:
`Ctrl+C` and `Ctrl+Shift+C` are separate chords. Explicit bindings can also use
unmodified or Shift-only keys, such as `F1`, `A`, or `Shift+A`; those keys are
then consumed by the terminal. Unbound keys pass through to the shell.

Supported key names include:

- `A`–`Z`, `0`–`9`, and `F1`–`F12`.
- `Enter`/`Return`, `Tab`, `Escape`/`Esc`, `Backspace`, and `Space`.
- `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `PageUp`/`PgUp`,
  `PageDown`/`PgDn`/`PgDown`, `Insert`/`Ins`, and `Delete`/`Del`.
- `Equal`/`Plus`/`OemPlus`/`=`, `Minus`/`OemMinus`/`-`, `Comma`, `Period`,
  `Slash`, `Backslash`, `Semicolon`, `Quote`, `Backtick`, `LeftBracket`, and
  `RightBracket`. Punctuation names also accept their unshifted literal
  characters; named keys avoid Lua string-escaping concerns. `Ctrl++` is an
  alias for `Ctrl+Equal`.
- `Num0`–`Num9`, `NumAdd`, `NumSubtract`, `NumMultiply`, `NumDivide`, and
  `NumDecimal`; the corresponding `Numpad...` names are aliases.

These names identify Windows virtual keys. Aliases such as `Plus` do not add
Shift automatically: use `Ctrl+Shift+Equal` for that exact chord. Alt+F4 is
reserved for Windows' close action. Unknown actions/keys, malformed chords,
and conflicting chords are rejected before configuration is applied.

The default controls are:

| Input | Action |
| --- | --- |
| Drag with the left mouse button | Select terminal text |
| Ctrl+Shift+C | Copy selection |
| Ctrl+Shift+V, Shift+Insert, or right click | Paste clipboard text |
| Ctrl+plus / Ctrl+=, or the header zoom + button | Zoom in |
| Ctrl+minus, or the header zoom - button | Zoom out |
| Ctrl+0, or click the zoom percentage | Reset to the configured font size |
| Ctrl+mouse wheel | Zoom in or out |
| Mouse wheel / touchpad | Scroll history using Windows' scroll setting |
| Drag the right scrollbar thumb | Move through scrollback |
| Click above / below the scrollbar thumb | Scroll by a page |
| Shift+PageUp / Shift+PageDown | Scroll by a page |
| Shift+Home / Shift+End | Go to oldest history / live output |
| Ctrl+Shift+T | Open a tab |
| Ctrl+Shift+W | Close the active tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Switch to the next / previous tab |
| Ctrl+Shift+O | Split the focused pane horizontally (top/bottom) |
| Ctrl+Shift+E | Split the focused pane vertically (left/right) |
| Ctrl+Shift+X | Close the focused pane |
| Alt+PageDown / Alt+PageUp | Focus the next / previous pane |
| Ctrl+Alt+Arrow | Focus a pane in that direction |

Zoom changes the terminal font from 8 to 72 logical pixels and updates the
row and column count of every pane while keeping the window size fixed. Zoom
is shared by all tabs and panes; `font_size` in the Lua configuration sets the
initial/reset size. Each pane retains its own scrollback when zooming or
resizing.

### Inline images

wst supports static images sent using the Kitty graphics protocol's direct
transfer mode (`t=d`). Clients can stream PNG, RGB, or RGBA data in chunks,
optionally compressed with zlib. Images can be placed at the cursor, scaled to
cells, cropped, layered with text, and deleted by ID. Each pane has its own
image store.

With `kitten` installed, run this inside wst.

```sh
kitten icat --transfer-mode=stream image.png
```

To try it from PowerShell without installing an image viewer, run these two
lines to draw a blue square four columns wide.

```powershell
$esc = [string][char]27
[Console]::Write("${esc}_Ga=T,f=24,s=1,v=1,c=4,q=2;AID/${esc}\")
```

The decoded RGBA data can occupy up to 64 MiB per image and 96 MiB per screen.
The renderer can show up to 512 distinct images across a window's panes at
once. File and shared-memory transfers, Unicode placeholders, relative
placements, indexed-color image data, and animation are unsupported.
[Kitty graphics protocol reference](https://sw.kovidgoyal.net/kitty/graphics-protocol/)

Run the checks from PowerShell:

```powershell
glosso compile first.glo --cache-dir=build/cache -- test
```

The Glosso task rebuilds the GUI application, compiles and runs each
`tests/*_test.glo` program, and runs the application smoke test with both GPU
preferences. Tests cover the
terminal parser and grid, Clay layout, Lua configuration, and the Windows
platform integration. Test executables are written under `build/`.

To save an offscreen rendering preview while running the end-to-end smoke test:

```powershell
.\build\wst.exe --smoke-test --capture build/preview.bmp
```

This is a deliberately small emulator. It does not reflow existing text;
narrower views clip historical lines
without discarding their hidden columns. Scrollback is limited to 10,000 lines.
Cell widths and emoji presentation use the character properties in Glosso’s `Unicode` module.
Grapheme cluster shaping, bidirectional text, and IME composition are not implemented.
The parser supports common VT sequences, colors, cursor movement, and screen
updates, with the implementation and regression cases in `src/terminal.glo`
and `tests/terminal_test.glo` defining its current scope.

Original contributions to wst are [MIT licensed](LICENSE). Third-party materials
retain their upstream copyrights and license terms. Complete notices for st,
Clay, Glosso (including `Gui`), Lua, stb_truetype, LLVM compiler-rt builtins,
Microsoft's ConPTY host, and Unicode character data are included in
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES) and accompany binary releases.
The upstream st and Clay license files also remain in their source
directories. The st notice is retained for the supplied reference sources and
st-derived portions of wst.
