<#
  CAR_REMOTE build script (same settings as MounRiver's obj/Release configuration)

  Usage:  powershell -ExecutionPolicy Bypass -File .\build.ps1
  Output: build\CAR_REMOTE.elf / .hex / .bin / .map

  SDK path resolution order (folder that directly contains Core / Peripheral / Ld / Startup):
    1. environment variable CH32V307_SDK  (may point either at ...\EXAM or at ...\EXAM\SRC)
    2. bundled sub-folder .\sdk  (committed with this repo -> clone & build works anywhere)
    3. file sdk_path.txt in this folder (one line, e.g. D:\WCH_CH32V307_EVT\extracted\EVT\EXAM)
    4. built-in default below

  ASCII-only on purpose: Windows PowerShell 5.1 mis-reads UTF-8 .ps1 files
  without BOM and would mangle non-ASCII string literals.
#>
$ErrorActionPreference = 'Stop'

$ProjectDir = $PSScriptRoot
$ToolBin    = 'D:\MounRiverStudio\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin'

function Resolve-SrcRoot([string]$path) {
    if (-not $path) { return $null }
    if (Test-Path (Join-Path $path 'Peripheral\src')) { return $path }          # already Core/Peripheral/...
    if (Test-Path (Join-Path $path 'SRC\Peripheral\src')) { return (Join-Path $path 'SRC') }
    return $null
}

$SrcRoot = Resolve-SrcRoot $env:CH32V307_SDK
if (-not $SrcRoot) { $SrcRoot = Resolve-SrcRoot (Join-Path $ProjectDir 'sdk') }
if (-not $SrcRoot) {
    $sdkFile = Join-Path $ProjectDir 'sdk_path.txt'
    if (Test-Path $sdkFile) { $SrcRoot = Resolve-SrcRoot (Get-Content $sdkFile -First 1).Trim() }
}
if (-not $SrcRoot) { $SrcRoot = Resolve-SrcRoot 'D:\WCH_CH32V307_EVT\extracted\EVT\EXAM' }
if (-not $SrcRoot) {
    throw "WCH SDK not found. Set CH32V307_SDK, keep .\sdk, or create sdk_path.txt."
}
Write-Host "SDK      : $SrcRoot"

$CC      = Join-Path $ToolBin 'riscv-none-embed-gcc.exe'
$OBJCOPY = Join-Path $ToolBin 'riscv-none-embed-objcopy.exe'
$SIZE    = Join-Path $ToolBin 'riscv-none-embed-size.exe'

$Target   = 'CAR_REMOTE'
$BuildDir = Join-Path $ProjectDir 'build'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Arch = @('-march=rv32i', '-mabi=ilp32')

$IncDirs = @(
    (Join-Path $ProjectDir 'User'),
    (Join-Path $ProjectDir 'Bsp'),
    (Join-Path $ProjectDir 'App'),
    (Join-Path $SrcRoot 'Core'),
    (Join-Path $SrcRoot 'Peripheral\inc')
)
$IncFlags = $IncDirs | ForEach-Object { "-I$_" }

$CFlags = $Arch + @('-Os', '-g', '-std=gnu99', '-fsigned-char',
                    '-ffunction-sections', '-fdata-sections', '-fno-common',
                    '-Wall', '-Wunused', '-Wuninitialized') + $IncFlags

$Sources = @()
$Sources += (Get-ChildItem (Join-Path $SrcRoot 'Peripheral\src') -Filter *.c | ForEach-Object { $_.FullName })
$Sources += (Join-Path $SrcRoot 'Core\core_riscv.c')
$Sources += (Join-Path $SrcRoot 'Startup\startup_ch32v30x_D8C.S')
$Sources += (Get-ChildItem (Join-Path $ProjectDir 'User') -Filter *.c | ForEach-Object { $_.FullName })
$Sources += (Get-ChildItem (Join-Path $ProjectDir 'Bsp')  -Filter *.c | ForEach-Object { $_.FullName })
$Sources += (Get-ChildItem (Join-Path $ProjectDir 'App')  -Filter *.c | ForEach-Object { $_.FullName })

$Objects = @()
foreach ($src in $Sources)
{
    $obj = Join-Path $BuildDir ([IO.Path]::GetFileNameWithoutExtension($src) + '.o')
    Write-Host ("CC   " + [IO.Path]::GetFileName($src))
    & $CC @CFlags -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
    $Objects += $obj
}

$Elf = Join-Path $BuildDir "$Target.elf"
$Map = Join-Path $BuildDir "$Target.map"
$LdFlags = $Arch + @('-nostartfiles', '--specs=nano.specs', '--specs=nosys.specs',
                     '-Wl,--gc-sections', "-Wl,-Map=$Map", '-Wl,--print-memory-usage',
                     '-T', (Join-Path $SrcRoot 'Ld\Link.ld'))

Write-Host 'LD   CAR_REMOTE.elf'
& $CC @LdFlags $Objects -o $Elf
if ($LASTEXITCODE -ne 0) { throw 'link failed' }

& $OBJCOPY -O ihex   $Elf (Join-Path $BuildDir "$Target.hex")
& $OBJCOPY -O binary $Elf (Join-Path $BuildDir "$Target.bin")
& $SIZE $Elf

Write-Host ("OK -> " + $Elf)
