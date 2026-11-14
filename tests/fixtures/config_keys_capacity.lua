local prefixes = { "", "Shift+", "Ctrl+", "Ctrl+Shift+", "Alt+", "Alt+Shift+", "Ctrl+Alt+", "Ctrl+Shift+Alt+" }
local chords = {}
for _, prefix in ipairs(prefixes) do
  for letter = 65, 90 do
    if #chords < 128 then chords[#chords + 1] = prefix .. string.char(letter) end
  end
end
local bindings = { copy = chords }
for _, action in ipairs({ "paste", "zoom_in", "zoom_out", "reset_zoom", "scroll_page_up", "scroll_page_down", "scroll_top", "scroll_bottom", "scroll_line_up", "scroll_line_down", "close_window", "new_tab", "close_tab", "next_tab", "previous_tab", "split_horizontal", "split_vertical", "close_pane", "next_pane", "previous_pane", "focus_pane_left", "focus_pane_right", "focus_pane_up", "focus_pane_down" }) do
  bindings[action] = false
end
return { keybindings = bindings }
