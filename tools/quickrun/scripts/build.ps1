$ErrorActionPreference = 'Stop'

$ToolName = if ($env:TOOL_NAME) { $env:TOOL_NAME } else { 'quickrun' }
$OsLabel  = if ($env:OS_LABEL)  { $env:OS_LABEL }  else { 'windows' }
$Arch     = if ($env:ARCH)      { $env:ARCH }      else { 'x64' }
$Version  = if ($env:VERSION)   { $env:VERSION }   else { 'dev' }

$Commit = (& git rev-parse --short HEAD 2>$null)
if (-not $Commit) { $Commit = 'unknown' }

$Bin = "$ToolName.exe"
$OutDir = 'build'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-ChildItem -Path $OutDir -File -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

$Cc = if ($env:CC) { $env:CC } else { 'gcc' }
if (-not (Get-Command $Cc -ErrorAction SilentlyContinue)) {
    throw "no C compiler found (looked for '$Cc'). Install MinGW-w64 or set `$env:CC."
}

$UiohookDir = 'vendor/uiohook'
if (-not (Test-Path "$UiohookDir/src")) {
    throw "$UiohookDir is missing or empty. Run: git submodule update --init --recursive"
}

$buildInfoPath = "$OutDir/_build_info.h"
$buildInfo = @"
#define QUICKRUN_VERSION "$Version"
#define QUICKRUN_COMMIT "$Commit"
"@
Set-Content -Path $buildInfoPath -Value $buildInfo -Encoding ascii

# Embed icon.ico as the app/tray icon resource via windres.
$iconObj = $null
if (Test-Path 'icon.ico') {
    Write-Host 'Embedding icon from icon.ico'
    Copy-Item 'icon.ico' "$OutDir/_app_icon.ico" -Force
    Set-Content -Path "$OutDir/_app_icon.rc" -Value '1 ICON "_app_icon.ico"' -Encoding ascii
    Push-Location $OutDir
    try {
        & windres _app_icon.rc -O coff -o _app_icon.res
        if ($LASTEXITCODE -ne 0) { throw "windres failed" }
    } finally { Pop-Location }
    $iconObj = "$OutDir/_app_icon.res"
} else {
    Write-Warning 'icon.ico not found - building without app icon'
}

$ourSources = @(
    'src/main.c','src/config.c','src/json.c',
    'src/keyspec.c','src/action.c','src/log.c','src/tray.c',
    'src/autostart.c'
)

$uiohookSources = @(
    "$UiohookDir/src/logger.c",
    "$UiohookDir/src/windows/input_helper.c",
    "$UiohookDir/src/windows/input_hook.c",
    "$UiohookDir/src/windows/post_event.c",
    "$UiohookDir/src/windows/system_properties.c"
)

$includes = @(
    "-I$UiohookDir/include",
    "-I$UiohookDir/src",
    "-I$UiohookDir/src/windows"
)

$libs = @('-mwindows','-luser32','-ladvapi32','-lshell32')

Write-Host "Building $ToolName ($OsLabel/$Arch) with $Cc, version=$Version commit=$Commit"

$gccArgs = @(
    '-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-format-truncation',
    '-std=gnu99'
) + $includes + @(
    '-include',$buildInfoPath,
    '-o',"$OutDir/$Bin"
) + $ourSources + $uiohookSources
if ($iconObj) { $gccArgs += $iconObj }
$gccArgs += $libs

& $Cc @gccArgs
if ($LASTEXITCODE -ne 0) { throw "$Cc build failed (exit $LASTEXITCODE)" }

Remove-Item -Force $buildInfoPath, "$OutDir/_app_icon.ico", "$OutDir/_app_icon.rc", "$OutDir/_app_icon.res" -ErrorAction SilentlyContinue

Copy-Item README.md           "$OutDir/" -ErrorAction SilentlyContinue
Copy-Item config.example.json "$OutDir/" -ErrorAction SilentlyContinue
Copy-Item config.json         "$OutDir/" -ErrorAction SilentlyContinue

Write-Host "Done."
Get-ChildItem $OutDir | Format-Table -AutoSize
