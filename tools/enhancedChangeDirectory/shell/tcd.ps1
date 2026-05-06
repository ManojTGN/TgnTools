# tcd shell wrapper for PowerShell.
#
# Resolution order for the binary:
#   1. $env:TCD_BIN if set
#   2. tcd.exe in this script's parent directory (i.e. <shell-dir>\..\tcd.exe)
#   3. tcd.exe on PATH
#
# Install: dot-source this file from your $PROFILE:
#   . C:\path\to\tcd.ps1

$Global:TcdShellDir = $PSScriptRoot

function tcd {
    [CmdletBinding()]
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]] $Arguments
    )

    $bin = $env:TCD_BIN

    if (-not $bin -and $Global:TcdShellDir) {
        $candidate = Join-Path (Split-Path -Parent $Global:TcdShellDir) 'tcd.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $bin = $candidate
        }
    }

    if (-not $bin) {
        $cmd = Get-Command -Name 'tcd' -CommandType Application -ErrorAction SilentlyContinue |
               Where-Object { $_.Source -like '*.exe' } |
               Select-Object -First 1
        if (-not $cmd) {
            Write-Error 'tcd: tcd.exe not found; set $env:TCD_BIN to its full path.'
            return
        }
        $bin = $cmd.Source
    }

    $target = (& $bin @Arguments | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { return }
    if ($target) {
        if (Test-Path -LiteralPath $target -PathType Container) {
            Set-Location -LiteralPath $target
        } else {
            Write-Output $target
        }
    }
}
