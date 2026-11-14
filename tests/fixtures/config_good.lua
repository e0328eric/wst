local size = 10 * 2
return {
  shell = "cmd", command = "cmd.exe /d", font_family = "Consolas",
  font_size = size, font_ligatures = false, columns = 120, rows = 40, scrollback = 1234, padding = 8,
  foreground = "#aBcDeF", background = 0x102030,
  accent = "#aabbcc", cursor = 0xffcc00, close_on_exit = false,
}
