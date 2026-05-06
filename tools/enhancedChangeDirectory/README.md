# tcd — TGN CD

A terminal directory navigator in pure C. Browse, hit Ctrl+Enter, your shell `cd`s there. Windows / macOS / Linux. No third-party deps.

## Install

Build:
```sh
bash scripts/build.sh        # POSIX
.\scripts\build.ps1           # Windows
```

`build/` will contain:
```
tcd(.exe)
shell/{tcd.bat, tcd.ps1, tcd.sh}
config.json            drop-in starter (Dracula theme)
config.example.json    full reference, every option documented
```

Add `build/shell/` to your `PATH`. The wrappers auto-detect `tcd(.exe)` one folder up — no `TCD_BIN` needed if you keep the layout. Then activate the function for your shell:

| Shell | One-time |
|---|---|
| **cmd.exe**     | nothing — `tcd.bat` is on PATH |
| **PowerShell**  | add `. <path>\shell\tcd.ps1` to `$PROFILE` (`notepad $PROFILE`) |
| **bash / zsh**  | add `source <path>/shell/tcd.sh` to `~/.bashrc` / `~/.zshrc` |

> Override the binary location with `TCD_BIN` (POSIX) or `$env:TCD_BIN` (PowerShell) if you don't want the parent-folder layout.

## Usage

```sh
tcd                  # navigate from current directory
tcd /some/path       # start somewhere specific
tcd --help           # full reference
```

| Key | Action |
|---|---|
| `↑` `↓`              | move |
| `Enter` / `→`        | descend into folder |
| `Backspace` / `←`    | parent (or delete last filter char) |
| type letters/digits  | filter (substring, case-insensitive) |
| `Tab`                | drives / locations picker |
| `Ctrl+Enter`         | **commit** — your shell `cd`s |
| `Ctrl+Shift+Enter`   | commit **and** open the chosen folder in the OS file manager |
| `Esc`                | cancel |
| `Ctrl+H`             | toggle hidden files |
| `Ctrl+U`             | clear filter |


## Config

On Windows, Ctrl+Enter is recognized natively by the console. Most Linux/macOS terminals can't tell Ctrl+Enter from plain Enter — rebind `commit` in config (e.g. `["ctrl+g"]`, `["f2"]`).

1. `--config FILE` (CLI override)
2. **`<tcd-binary-dir>/config.json`** — portable mode: drop a `config.json` next to `tcd.exe` and it travels with the binary
3. POSIX: `~/.config/tcd/config.json` (or `$XDG_CONFIG_HOME/tcd/config.json`)
4. Windows: `%APPDATA%\tcd\config.json`

The build ships with `config.json` already next to `tcd.exe`, so out of the box you get the Dracula theme — no setup required. To customize, edit that file directly, or copy it to `~/.config/tcd/` (`%APPDATA%\tcd\` on Windows) for system-wide settings shared across multiple binaries.

Run `tcd --print-config-path` to see which file tcd is loading. Run `tcd --list-themes` to see built-in theme names. For every config option, see `config.example.json`.

Customize:

- **theme** — pick a named preset, or override individual colors. See [Themes](#themes).
- **show_index**, **show_hidden**, **show_size**, **per_page**, **sort**, **wrap_navigation**
- **keys** — every action: `up`, `down`, `enter`, `back`, `commit`, `commit_explore`, `cancel`, `drives`, `top`, `bottom`, `page_up`, `page_down`, `toggle_hidden`, `clear_filter`. Each takes a string or array of strings; modifier prefixes `ctrl+`, `alt+`, `shift+`.

### Themes

Two fields work together: `theme` picks the active theme by name, `themes` is an array of theme definitions you can extend.

**Built-in theme names** (always available, no config entry needed):

| Name | Style |
|---|---|
| `default`         | terminal palette (no colors hardcoded) |
| `dracula`         | classic dark purple/pink |
| `nord`            | cool arctic blue/cyan |
| `solarized-dark`  | low-contrast teal/yellow |
| `gruvbox-dark`    | warm retro green/orange |
| `tokyo-night`     | dark blue/purple |
| `monokai`         | classic green/pink |

Full schema: see [config.example.json](config.example.json).
