-- Copy to %APPDATA%/wst/config.lua, or pass --config <path>.
-- This is ordinary Lua: compute values, then return one configuration table.
-- Every setting reloads on save. Invalid saves keep the last working settings.
-- Changing launch settings replaces affected shell sessions, interrupting running commands.
-- Tabs, split layout, the terminal window, and scrollback remain available.
return {
  shell = "powershell", -- "powershell", "cmd", or "nushell" (nu.exe on PATH)
  command = "", -- optional full command line; overrides shell when nonempty
  default_profile = "nushell_dev", -- empty string uses the top-level shell/command
  profiles = {
    powershell = {
      name = "Windows PowerShell",
      shell = "powershell",
      starting_directory = "%USERPROFILE%",
    },
    cmd = {
      name = "Command Prompt",
      shell = "cmd",
      starting_directory = "%USERPROFILE%",
    },
    nushell_dev = {
      name = "Nushell Developer",
      shell = "nushell",
      starting_directory = "%USERPROFILE%",
      -- Adjust the Visual Studio path and installation ID for your machine.
      command = [[powershell.exe -NoLogo -NoProfile -NoExit -Command "& { Import-Module 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'; Enter-VsDevShell 60eeb474 -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'; & nu.exe }"]],
    },
  },
  font_family = "D2KodingLigature Nerd Font Mono", -- hot reloads on save
  font_size = 16, -- shared by all panes; logical pixels (scales with monitor DPI), 8..72
  font_ligatures = true, -- OpenType ligatures when supported by the selected font; hot reloads
  window_opacity = 1.0, -- 0.1..1.0; e.g. 0.9 makes the entire window translucent, including text/header
  columns = 100, -- 20..400; changes resize this window's width
  rows = 30, -- 5..200; changes resize this window's height
  scrollback = 5000, -- per pane, 0..10000 lines; shrinking keeps newest, zero clears
  padding = 12, -- logical pixels, 0..100; keeps window size, recalculates grid
  accent = "#81a1c1",
  -- The complete scheme hot reloads in existing panes and scrollback.
  -- Colors accept '#RRGGBB' strings or RGB integers.
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
  close_on_exit = true, -- close exited panes; last pane closes its tab, last tab the window
  -- Unspecified actions keep their default keyboard shortcuts. Each supplied
  -- action replaces all of its old chords; false or {} disables those chords.
  -- Save to apply without restarting. Invalid saves keep the last valid settings.
  -- Mouse/header controls remain available when an action is disabled.
  keybindings = {
    copy = "Ctrl+Shift+C",
    paste = { "Ctrl+Shift+V", "Shift+Insert" },
    zoom_in = { "Ctrl+Equal", "Ctrl+Shift+Equal" },
    zoom_out = "Ctrl+Minus",
    reset_zoom = "Ctrl+0",
    scroll_line_up = "Alt+Up",
    scroll_line_down = "Alt+Down",
    new_tab = {
      "Ctrl+Shift+T", -- use the current default profile
      { key = "Ctrl+Shift+1", profile = "nushell_dev" },
      { key = "Ctrl+Shift+2", profile = "powershell" },
      { key = "Ctrl+Shift+3", profile = "cmd" },
    },
    close_tab = "Ctrl+Shift+W",
    next_tab = "Ctrl+Tab",
    previous_tab = "Ctrl+Shift+Tab",
    split_horizontal = { -- top/bottom
      "Ctrl+Shift+O", -- inherit the focused pane's profile
      { key = "Ctrl+Alt+O", profile = "nushell_dev" },
    },
    split_vertical = { -- left/right
      "Ctrl+Shift+E", -- inherit the focused pane's profile
      { key = "Ctrl+Alt+E", profile = "nushell_dev" },
    },
    close_pane = "Ctrl+Shift+X",
    next_pane = "Alt+PageDown",
    previous_pane = "Alt+PageUp",
    focus_pane_left = "Ctrl+Alt+Left",
    focus_pane_right = "Ctrl+Alt+Right",
    focus_pane_up = "Ctrl+Alt+Up",
    focus_pane_down = "Ctrl+Alt+Down",
    close_window = "Ctrl+Shift+Q",
    -- scroll_page_up = false, -- disable this action's keyboard shortcuts
    -- scroll_page_down = {}, -- an empty array also disables an action
  },
}
