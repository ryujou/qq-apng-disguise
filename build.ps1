$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    python -m PyInstaller --noconfirm --onefile --windowed --collect-all tkinterdnd2 --exclude-module numpy --icon assets/icon.ico --add-data 'assets/icon.ico;assets' --name QQ-APNG-Disguise apng_disguise.py
    if ($LASTEXITCODE -ne 0) { throw 'PyInstaller build failed' }
} finally {
    Pop-Location
}
