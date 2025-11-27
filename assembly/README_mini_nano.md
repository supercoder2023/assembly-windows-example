mini_nano - tiny terminal editor

This is a small POSIX terminal text editor written in C (`mini_nano.c`). It is intended for quick edits in a terminal (Linux / WSL / Cygwin). It uses `termios` and ANSI escape codes.

Build

On WSL / Linux / Cygwin / MSYS2 with GCC:

```bash
# from assembly/ directory
gcc mini_nano.c -o mini_nano
```

On Windows native (PowerShell) this source uses POSIX APIs and won't compile without a compatibility layer (Cygwin/MSYS2/PDCurses adaptations). Use WSL or MSYS2 for best results.

Run

```bash
# open a file
./mini_nano myfile.txt

# or start empty
./mini_nano
```

Keys

- Ctrl-S : Save (prompts for filename the first time)
- Ctrl-O : Open file (prompt for filename)
- Ctrl-X : Exit (prompts to save if modified)
- Arrow keys : Move cursor
- Backspace / Delete : remove characters
- Enter : New line

Notes

This is an educational, minimal editor — not a full-featured nano. It supports basic edit/save/open operations.
