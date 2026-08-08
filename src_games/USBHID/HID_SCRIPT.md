# USBHID Script Format

Scripts are plain text files on the SD card, run with **Run Script** from the
USBHID main menu. Files are browsed like the bootloader (directories
navigable, **A** opens a file, **B** back).

The USBHID device presents as a single composite HID interface: **mouse**,
**keyboard** and **gamepad** report IDs are always enumerated; a script only
uses the reports it needs.

## Rules

- One instruction per line.
- Plain lines are typed on the host **character by character, with no Enter
  at the end** — a line of text is exactly the characters it contains.
- Lines starting with `##` are comments and are ignored.
- Lines starting with `#` are commands.
- Everything is case-insensitive except literal text and `#KEY` keys (which
  follow the rules below).
- 8.3 file names only (e.g. `DEMO.TXT`).

### Escapes inside text lines

| Escape | Meaning                 |
|--------|-------------------------|
| `\n`   | Enter key               |
| `\t`   | Tab key                 |
| `\\`   | a literal backslash     |
| `\#`   | a literal `#` (so a text line can start with `#`) |

So `Hello\n` types the letters `Hello` and then presses Enter.

## Commands

### `#DELAY <ms>`
Wait the given number of milliseconds before continuing.

### `#BEEP [ms]`
Beep for `ms` milliseconds (default 100).

### `#LED <0|1|2|3>`
Set the board LEDs: `0` both off, `1` LED 0, `2` LED 1, `3` both.

### `#MSG <text>`
Show `<text>` on the device OLED. It stays on screen until the next script
action that uses the screen replaces it (a later `#MSG`) or the script ends —
`#DELAY`, typing, mouse actions and such do not clear it. The device shows its
own status (running file, pause hint, abort hint) at the bottom of the OLED so
it never displaces your message.

### `#PAUSE`
Pause the script until button **A** is pressed on the device. The script's
on-screen message stays as-is; the device shows the pause hint in its status
area.

### `#SPEED <ms>`
Set the delay between typed characters (default 20 ms).

### `#STOP`
End the script.

### `#KEY <modifier+modifier+key>`
Press a key combination for a short time (about 30 ms), then release.

Examples:

```
#KEY CTRL+C
#KEY ALT+TAB
#KEY CTRL+SHIFT+ESC
#KEY F5
#KEY ENTER
```

- Single letters are typed **without shift**: `CTRL+C` is control+c, not
  control+shift+c.
- Modifiers: `CTRL` (or `CONTROL`), `SHIFT`, `ALT`, `GUI` (or `WIN`).
- Named keys: `ESC`, `ENTER`, `RETURN`, `TAB`, `BACKSPACE`, `DELETE`,
  `INSERT`, `HOME`, `END`, `PAGEUP`, `PAGEDOWN`, `UP`, `DOWN`, `LEFT`,
  `RIGHT`, `SPACE`, `F1`..`F12`.
- Symbols use the US layout (`#KEY !`, `#KEY ?`, `#KEY +`, `#KEY {`, ...).
- The key is automatically released, so no key can be left stuck.

### `#MOVE <dx> <dy>`
Move the mouse relative by `dx`,`dy` pixels (signed, -127..127).

### `#CLICK [LEFT|RIGHT|MIDDLE] [DOUBLE]`
Click the given button (default LEFT). `DOUBLE` performs a double-click.

### `#PRESS <btn>` / `#RELEASE <btn>`
Hold a mouse button down (`#PRESS LEFT`), then release it later
(`#RELEASE LEFT`) — used for dragging.

### `#SCROLL <n>`
Scroll the mouse wheel by `n` notches (signed).

## Aborting

Press **B** on the device at any time to abort the script. Aborting releases
every key and mouse button (a stuck modifier can never happen even if the
script stops mid-line).

## Example

```
## Lock the workstation
#MSG Locking - Ctrl+Alt+Delete
#DELAY 2000
#KEY CTRL+ALT+DELETE
#DELAY 1500
#MSG Type password and press Enter
#SPEED 30
my-password\n
```
