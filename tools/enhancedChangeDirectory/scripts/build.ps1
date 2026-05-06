$ErrorActionPreference = 'Stop'

$ToolName = if ($env:TOOL_NAME) { $env:TOOL_NAME } else { 'tcd' }
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
if (Test-Path "$OutDir/shell") {
    Get-ChildItem -Path "$OutDir/shell" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

$Cc = if ($env:CC) { $env:CC } else { 'gcc' }
if (-not (Get-Command $Cc -ErrorAction SilentlyContinue)) {
    throw "no C compiler found (looked for '$Cc'). Install MinGW-w64 or set `$env:CC."
}

$sources = @(
    'src/term.c','src/input.c','src/fs.c','src/json.c',
    'src/config.c','src/ui.c','src/main.c'
)

Write-Host "Building $ToolName ($OsLabel/$Arch) with $Cc, version=$Version commit=$Commit"

$buildInfoPath = "$OutDir/_build_info.h"
$buildInfo = @"
#define TCD_VERSION "$Version"
#define TCD_COMMIT "$Commit"
"@
Set-Content -Path $buildInfoPath -Value $buildInfo -Encoding ascii

$gccArgs = @(
    '-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-format-truncation',
    '-std=gnu99',
    '-include',$buildInfoPath,
    '-o',"$OutDir/$Bin"
) + $sources + @('-lshell32','-lole32','-luuid')

& $Cc @gccArgs
if ($LASTEXITCODE -ne 0) { throw "$Cc build failed (exit $LASTEXITCODE)" }

Remove-Item -Force $buildInfoPath -ErrorAction SilentlyContinue

Copy-Item README.md           "$OutDir/" -ErrorAction SilentlyContinue
Copy-Item config.example.json "$OutDir/" -ErrorAction SilentlyContinue
Copy-Item config.json         "$OutDir/" -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Force -Path "$OutDir/shell" | Out-Null
if ($OsLabel -eq 'windows') {
    Copy-Item shell/tcd.bat "$OutDir/shell/" -ErrorAction SilentlyContinue
} else {
    Copy-Item shell/tcd.sh  "$OutDir/shell/" -ErrorAction SilentlyContinue
}

Write-Host "Done."
Get-ChildItem $OutDir | Format-Table -AutoSize
