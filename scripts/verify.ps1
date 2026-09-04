#requires -Version 5.1
param(
    [string]$Python = "python",
    [switch]$SkipInstall,
    [switch]$SkipUi
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Invoke-Checked {
    param(
        [string]$Name,
        [scriptblock]$Command
    )

    Write-Host "==> $Name"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Push-Location $root
try {
    Invoke-Checked "Check Python" {
        & $Python -c "import sys; assert sys.version_info[:2] == (3, 12), 'Repository verification requires Python 3.12'; print(sys.version)"
    }

    if (-not $SkipInstall) {
        Invoke-Checked "Install Python dependencies" {
            & $Python -m pip install -e ".\orpheus_core[dev]"
        }
    }

    Invoke-Checked "Build components, runtimes, and C tests" {
        & $Python -m orpheus_core.cli build
    }
    Invoke-Checked "Run CTest" {
        & ctest --test-dir build --output-on-failure
    }
    Invoke-Checked "Run Python tests" {
        & $Python -m pytest orpheus_core/tests/ -q
    }

    if (-not $SkipUi) {
        $env:ORPHEUS_PYTHON = $Python
        Push-Location ui
        try {
            if (-not $SkipInstall) {
                Invoke-Checked "Install UI dependencies" { & npm ci }
                Invoke-Checked "Install Playwright browser" { & npx playwright install chromium }
            }
            Invoke-Checked "Run UI tests" { & npm test -- --watchAll=false }
            Invoke-Checked "Build UI" { & npm run build }
            Invoke-Checked "Run browser workflow tests" { & npm run test:e2e }
        }
        finally {
            Pop-Location
        }
    }
}
finally {
    Pop-Location
}

Write-Host "Orpheus verification completed successfully."
