# make-windows.ps1 - builds the launcher for Windows and packages it as a portable folder and zip:
#   ConkerLauncher.exe with Qt (windeployqt) and libarchive's DLLs beside it
#   setup-windows.exe   the build kit (live-and-recompiled's launcher branch, tools/package.sh)
#   zig\                Zig 0.16.0, the compiler the kit builds the game with
#   licenses\           every licence text and the credits
# Nothing of the game is in it: players build the game from their own disc on the first run, and everything it makes
# (game\, mods\, launcher.ini) stays in this folder.
#
#   tools\make-windows.ps1 -Kit <folder with setup-windows.exe> [-Vcpkg <vcpkg root>]
# Needs: CMake, Visual Studio 2022 (MSVC), Qt 6 for MSVC (qmake / windeployqt on PATH), vcpkg with libarchive:x64-windows.
# Output: dist\ConkerRecompedReloaded-windows-x64\ and dist\ConkerRecompedReloaded-windows-x64.zip
param(
    [Parameter(Mandatory = $true)][string]$Kit,
    [string]$Vcpkg = $env:VCPKG_INSTALLATION_ROOT
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $PSScriptRoot
$build = Join-Path $here 'build-windows'
$out = Join-Path $here 'dist'
$stage = Join-Path $out 'ConkerRecompedReloaded-windows-x64'
$cache = Join-Path $here 'packaging\cache'

# the compiler, as the kit pins it (live-and-recompiled tools/toolchains.cmake)
$zigArchive = 'zig-x86_64-windows-0.16.0.zip'
$zigUrl = "https://ziglang.org/download/0.16.0/$zigArchive"
$zigSha256 = '68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e'

if (-not (Test-Path (Join-Path $Kit 'setup-windows.exe'))) { throw "no setup-windows.exe in $Kit" }
if (-not $Vcpkg) { throw 'vcpkg not found: pass -Vcpkg or set VCPKG_INSTALLATION_ROOT' }

Write-Host '== libarchive (vcpkg)'
& (Join-Path $Vcpkg 'vcpkg.exe') install libarchive:x64-windows
if ($LASTEXITCODE) { throw 'vcpkg install failed' }

Write-Host '== the launcher'
cmake -S $here -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
if ($LASTEXITCODE) { throw 'configure failed' }
cmake --build $build --config Release --target ConkerLauncher
if ($LASTEXITCODE) { throw 'build failed' }

Write-Host '== the compiler'
New-Item -ItemType Directory -Force $cache | Out-Null
$zipPath = Join-Path $cache $zigArchive
if (-not (Test-Path $zipPath) -or (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLower() -ne $zigSha256) {
    Invoke-WebRequest $zigUrl -OutFile $zipPath
    if ((Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLower() -ne $zigSha256) { throw "$zigArchive checksum differs" }
}

Write-Host '== folder'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $build 'Release\ConkerLauncher.exe') $stage
Copy-Item (Join-Path $Kit 'setup-windows.exe') $stage
windeployqt --release --no-translations --no-system-d3d-compiler (Join-Path $stage 'ConkerLauncher.exe')
if ($LASTEXITCODE) { throw 'windeployqt failed' }
# offscreen: scripted --install and the smoke test, without a display
$qtPlugins = (& qmake -query QT_INSTALL_PLUGINS).Trim()
New-Item -ItemType Directory -Force (Join-Path $stage 'platforms') | Out-Null
Copy-Item (Join-Path $qtPlugins 'platforms\qoffscreen.dll') (Join-Path $stage 'platforms')
# libarchive and the compression libraries it uses
Copy-Item (Join-Path $Vcpkg 'installed\x64-windows\bin\*.dll') $stage
# the compiler: the archive's single top folder becomes zig\
$unzip = Join-Path $cache 'zig-unzip'
if (Test-Path $unzip) { Remove-Item -Recurse -Force $unzip }
Expand-Archive $zipPath -DestinationPath $unzip
Move-Item (Get-ChildItem $unzip -Directory | Select-Object -First 1).FullName (Join-Path $stage 'zig')
Remove-Item -Recurse -Force $unzip

Write-Host '== licences'
$lic = Join-Path $stage 'licenses'
foreach ($d in 'live-and-recompiled', 'zig', 'tomlplusplus', 'miniupnpc', 'qt', 'vcpkg') {
    New-Item -ItemType Directory -Force (Join-Path $lic $d) | Out-Null
}
Copy-Item (Join-Path $here 'CREDITS.md') $lic
Copy-Item (Join-Path $Kit 'LICENSE') (Join-Path $lic 'live-and-recompiled\LICENSE (GPL-3.0)')
Copy-Item (Join-Path $Kit 'THIRD_PARTY.txt') (Join-Path $lic 'live-and-recompiled')
Copy-Item (Join-Path $stage 'zig\LICENSE') (Join-Path $lic 'zig\LICENSE (MIT)')
Copy-Item (Join-Path $build '_deps\tomlplusplus-src\LICENSE') (Join-Path $lic 'tomlplusplus\LICENSE (MIT)')
Copy-Item (Join-Path $build '_deps\miniupnpc-src\miniupnpc\LICENSE') (Join-Path $lic 'miniupnpc\LICENSE (BSD-3-Clause)')
$qtVersion = (& qmake -query QT_VERSION).Trim()
Set-Content (Join-Path $lic 'qt\README.txt') "Qt $qtVersion (The Qt Company and contributors), used under the LGPL-3.0, linked dynamically (the Qt6*.dll files). Source: https://download.qt.io - licence: https://www.gnu.org/licenses/lgpl-3.0.txt"
# each vcpkg package's copyright file (libarchive and its compression libraries)
Get-ChildItem (Join-Path $Vcpkg 'installed\x64-windows\share') -Directory | ForEach-Object {
    $c = Join-Path $_.FullName 'copyright'
    if (Test-Path $c) { Copy-Item $c (Join-Path $lic "vcpkg\$($_.Name).txt") }
}

Write-Host '== zip'
$zip = Join-Path $out 'ConkerRecompedReloaded-windows-x64.zip'
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Get-Item $zip | Select-Object Name, Length
