local modifier = "Ctrl+Alt+"
return {
  keybindings = {
    copy = modifier .. "C",
    paste = { modifier .. "V", "Alt+Insert" },
    zoom_out = "Ctrl+Minus",
    reset_zoom = false,
    scroll_line_up = "Alt+Up",
    scroll_line_down = "Alt+Down",
    close_window = "Ctrl+Shift+Q",
  },
}
