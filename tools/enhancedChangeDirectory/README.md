# tcd — TGN CD
<img src="enhancedChangeDirectory.png" alt="tcd" width="160">

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
shell/{tcd.bat | tcd.sh}      # bat on Windows, sh on POSIX
config.json                   drop-in starter (Dracula theme)
config.example.json           full reference, every option documented
```

| Shell | One-time |
|---|---|
| **Windows (cmd.exe)** | add `<path>\shell\tcd.bat` is on PATH; type `tcd` |
| **bash / zsh**        | add `source <path>/shell/tcd.sh` to `~/.bashrc` / `~/.zshrc` |


## Usage
```sh
tcd                  # navigate from current directory
tcd /some/path       # start somewhere specific
tcd work             # bookmark `work`, unless ./work folder exists (folder wins)
tcd @work            # always the bookmark — never the folder
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
| `Ctrl+S`             | save current directory as a bookmark |


## Config
1. POSIX: `~/.config/tcd/config.json` (or `$XDG_CONFIG_HOME/tcd/config.json`)
2. Windows: `<tcd-binary-dir>/config.json` (or `%APPDATA%\tcd\config.json`)

Run `tcd --config` to view the resolved config file (path + contents) in a styled panel. For every config option, see [config.example.json](config.example.json).
