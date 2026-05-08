# TgnTools
<img src="tgntools.png" alt="tgntools">

Personal mono-repo for cross-platform productivity, automation, and developer tools.
Each tool lives in `tools/<name>/` and is independently buildable into Windows / macOS / Linux executables on demand via GitHub Actions.

## Layout

```
tools/
  <tool-name>/
    src/                  source code
    scripts/
      build.sh            POSIX build (Linux + macOS)
      build.ps1           Windows build (PowerShell)
    build/                build output (gitignored)
    tool.yml              manifest: language, runtime, metadata
    README.md             tool-specific docs

.github/workflows/
  build.yml               manual-trigger build pipeline

docs/
  adding-tools.md         how to add a new tool
```

## Building

### On GitHub (release builds)

1. Go to **Actions** → **Build Tool** → **Run workflow**.
2. Pick the branch, enter the tool name (e.g., `hello`), optionally a version label.
3. The workflow builds on `windows-latest`, `ubuntu-latest`, and `macos-latest` in parallel and uploads three artifacts:
   - `<tool>-windows-x64.zip`
   - `<tool>-linux-x64.zip`
   - `<tool>-macos-arm64.zip`

Each downloaded zip contains the binary, the tool's README, and any config templates the build script copies in.

