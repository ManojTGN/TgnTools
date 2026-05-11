# quickrun
<img src="quickrun.png" alt="quickrun">

Cross-platform global-hotkey daemon. Press a shortcut anywhere, run a command. One C binary, JSON config, runs on Windows / macOS / Linux via libuiohook.

## Install
Build:

```sh
bash scripts/build.sh        # POSIX
.\scripts\build.ps1           # Windows
```

`build/` will contain:
```
quickrun(.exe)
config.json                drop-in starter
config.example.json        full reference
README.md
```

Add the build folder to your `PATH`, then run `quickrun` to start the daemon.

## Config

1. `<quickrun-binary-dir>/config.json`
2. POSIX: `~/.config/quickrun/config.json`
3. Windows: `%APPDATA%\quickrun\config.json`

Run `quickrun --config` to view the resolved config in a styled panel.

### Action types
| Action | Fields | What it does |
|---|---|---|
| `run`  | `command` (string), `args` (optional string array) | Spawns the command as a child process. No shell — args are passed verbatim. |
| `open` | `target` (string) | Opens with the OS default handler: ShellExecute (Windows), `open` (macOS), `xdg-open` (Linux). Use this for URLs and file paths. |

### Key spec syntax
`modifier+modifier+key`. Modifiers: `ctrl`, `alt`, `shift`, `meta` (Win/Cmd). Keys: letters, digits, `f1`–`f12`, `space`, `tab`, `enter`, `esc`, arrow keys, etc.

Examples: `ctrl+alt+t`, `meta+space`, `shift+f5`, `ctrl+;`.

## CLI
```
quickrun [OPTIONS]
  --config              print resolved config (path + contents) in a styled panel
  --install-autostart   register quickrun to start at user login
  --uninstall-autostart remove the autostart entry
  --version, -V         print version
  --help, -h            show this help
```

## Build

quickrun depends on [libuiohook](https://github.com/kwhat/libuiohook) for the platform-specific hotkey hooks. The library is vendored as a git submodule under `vendor/uiohook/` (TBD — submodule not yet added). The build scripts compile the vendored sources alongside quickrun's own and produce a single static binary.

Without libuiohook present, the build fails fast with a clear error.
