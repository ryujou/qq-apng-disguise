param([string]$Toolchain = '')
$ErrorActionPreference = 'Stop'
if ($Toolchain) { $env:PATH = "$Toolchain;$env:PATH" }
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force ../dist | Out-Null
    windres app.rc -O coff -o ../dist/cpp-res.o
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }
    g++ main.cpp ../dist/cpp-res.o -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows -static -s -o ../dist/QQ-APNG-Disguise-CPP.exe -lwindowscodecs -lole32 -luuid -lcomdlg32 -lcomctl32 -lshell32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed' }
} finally {
    Pop-Location
}
