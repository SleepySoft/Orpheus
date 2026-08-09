# Stop the Orpheus local service (serve.py, port 8000) and clean up run logs.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/stop_orpheus_service.ps1

$ErrorActionPreference = 'SilentlyContinue'
$root = Split-Path $PSScriptRoot -Parent
$stopped = @()

# 1) Stop the process listening on port 8000
$conn = Get-NetTCPConnection -LocalPort 8000 -State Listen | Select-Object -First 1
if ($conn) {
    $procId = $conn.OwningProcess
    Stop-Process -Id $procId -Force
    $stopped += $procId
    Write-Host "Stopped port 8000 listener (PID $procId)."
}

# 2) Fallback: stop any python process running serve.py
Get-CimInstance Win32_Process -Filter "Name like '%python%'" | ForEach-Object {
    if ($_.CommandLine -match 'serve\.py' -and $stopped -notcontains $_.ProcessId) {
        Stop-Process -Id $_.ProcessId -Force
        $stopped += $_.ProcessId
        Write-Host "Stopped serve.py process (PID $($_.ProcessId))."
    }
}

# 3) Remove leftover run logs created when serve.py was started in background
foreach ($log in @('serve_restart.log', 'serve_restart.err.log')) {
    $path = Join-Path $root $log
    if (Test-Path $path) {
        Remove-Item -LiteralPath $path -Force
        Write-Host "Removed $log."
    }
}

# 4) Confirm the port is closed
$alive = Get-NetTCPConnection -LocalPort 8000 -State Listen | Select-Object -First 1
if ($alive) {
    Write-Host "WARNING: port 8000 still listening (PID $($alive.OwningProcess))."
} elseif ($stopped.Count -gt 0) {
    Write-Host "Orpheus service stopped. Restart with: python serve.py"
} else {
    Write-Host "Orpheus service already stopped (nothing on port 8000)."
}
