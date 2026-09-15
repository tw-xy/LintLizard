<#
  CAR_REMOTE flash script  (WCH-LinkE + SDI two-wire)
  NOTE: this OVERWRITES the program currently on the board.
  Usage:  powershell -ExecutionPolicy Bypass -File .\flash.ps1
  ASCII-only on purpose: Windows PowerShell 5.1 mis-reads UTF-8 .ps1 files
  without BOM, which would mangle non-ASCII string literals.
#>
$ErrorActionPreference = 'Stop'

$ProjectDir = $PSScriptRoot
$OpenOcdBin = 'D:\MounRiverStudio\MounRiver_Studio2\resources\app\resources\win32\components\WCH\OpenOCD\OpenOCD\bin'
$OpenOcd    = Join-Path $OpenOcdBin 'openocd.exe'
$Cfg        = Join-Path $OpenOcdBin 'wch-riscv.cfg'
$Elf        = Join-Path $ProjectDir 'build\CAR_REMOTE.elf'

if (-not (Test-Path $Elf)) {
    throw "firmware not found: $Elf  (run build.ps1 first)"
}

Write-Host "OpenOCD  : $OpenOcd"
Write-Host "Config   : $Cfg"
Write-Host "Firmware : $Elf"

# NOTE 1: openocd writes its banner to stderr, so relax ErrorActionPreference
#         around the native call.
# NOTE 2: the string after -c is parsed by Tcl, where "\" is an escape char,
#         so a Windows path must be written with forward slashes.
$ElfTcl = $Elf.Replace('\', '/')
$ErrorActionPreference = 'Continue'
& $OpenOcd -f $Cfg -c "program $ElfTcl verify reset exit"
$rc = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($rc -ne 0) { throw 'openocd flash failed' }

Write-Host 'FLASH OK'
