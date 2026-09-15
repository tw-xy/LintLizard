<#
  CAR_REMOTE serial logger
  Records COM4 (USART1 debug output) to a timestamped file under .\logs\

  Usage:
     powershell -ExecutionPolicy Bypass -File .\log.ps1                 # 60 s
     powershell -ExecutionPolicy Bypass -File .\log.ps1 -Seconds 120
     powershell -ExecutionPolicy Bypass -File .\log.ps1 -Port COM5

  ASCII-only on purpose (Windows PowerShell 5.1 mangles UTF-8 .ps1 without BOM).
#>
param(
    [int]$Seconds = 60,
    [string]$Port = 'COM4',
    [int]$Baud = 115200
)

$ErrorActionPreference = 'Stop'

$ProjectDir = $PSScriptRoot
$LogDir     = Join-Path $ProjectDir 'logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$LogFile = Join-Path $LogDir ('serial_{0:yyyyMMdd_HHmmss}.log' -f (Get-Date))

Write-Host "Port     : $Port @ $Baud"
Write-Host "Log file : $LogFile"
Write-Host "Duration : $Seconds s  (Ctrl+C to stop early)"

$serial = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 500
$serial.Open()

$writer = New-Object System.IO.StreamWriter($LogFile, $false, [System.Text.Encoding]::UTF8)
$sw    = [Diagnostics.Stopwatch]::StartNew()
$buf   = ''
$lines = 0

try {
    while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
        Start-Sleep -Milliseconds 50
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -gt 0) {
            $buf += $chunk
            while (($i = $buf.IndexOf("`n")) -ge 0) {
                $line = $buf.Substring(0, $i).TrimEnd("`r")
                $buf  = $buf.Substring($i + 1)
                if ($line.Length -gt 0) {
                    $writer.WriteLine(('{0,8:F3}  {1}' -f $sw.Elapsed.TotalSeconds, $line))
                    $lines++
                    if ($line -like '*[stat]*') { Write-Host $line }
                }
            }
        }
    }
}
finally {
    $writer.Flush()
    $writer.Close()
    $serial.Close()
}

Write-Host "Saved $lines lines -> $LogFile"
