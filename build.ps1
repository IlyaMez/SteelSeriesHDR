param([switch]$Test, [switch]$SkipGpuTests)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio with Desktop development with C++ and a Windows 10/11 SDK.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools were not found.' }
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
if ($version -notmatch '^(\d+)\.(\d+)\.(\d+)(-[a-zA-Z0-9.]+)?$') { throw 'VERSION must be a semantic version.' }
$versionNumbers = "$($Matches[1]),$($Matches[2]),$($Matches[3]),0"
@"
#pragma once
#define ARENA_VERSION "$version"
#define ARENA_VERSION_W L"$version"
#define ARENA_VERSION_NUM $versionNumbers
"@ | Set-Content -LiteralPath (Join-Path $out 'version.h') -Encoding ascii
# This batch file only sets the compiler environment and builds; it performs no file deletion/moving.
$batch = @"
@echo off
chcp 65001 >nul
call "$devcmd" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "$root"
rc /nologo /I "$out" /fo "$out\app.res" src\app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc /I"$out" src\main.cpp src\capture.cpp src\arena.cpp "$out\app.res" /Fo"$out\\" /Fe"$out\ArenaHDR.exe" /link /MANIFEST:EMBED /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib d3dcompiler.lib windowsapp.lib setupapi.lib hid.lib comctl32.lib shell32.lib user32.lib gdi32.lib advapi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /utf-8 /DNOMINMAX /Isrc tests\tests.cpp /Fo"$out\tests.obj" /Fe"$out\ArenaHDR.Tests.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc tests\startup_tests.cpp /Fo"$out\startup_tests.obj" /Fe"$out\ArenaHDR.StartupTests.exe" /link advapi32.lib
exit /b %errorlevel%
"@
$batchPath = Join-Path $out 'compile.cmd'
[System.IO.File]::WriteAllText($batchPath, $batch, [System.Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $batchPath
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
if ($Test) {
    & (Join-Path $out 'ArenaHDR.Tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    & (Join-Path $out 'ArenaHDR.StartupTests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Startup registration tests failed.' }
    if (-not $SkipGpuTests) {
        $gpuResult = Join-Path $out 'gpu-test.txt'
        $gpuProcess = Start-Process -FilePath (Join-Path $out 'ArenaHDR.exe') -ArgumentList ('--gpu-test "' + $gpuResult + '"') -WindowStyle Hidden -PassThru -Wait
        Get-Content -LiteralPath $gpuResult
        if ($gpuProcess.ExitCode -ne 0) { throw 'GPU pipeline tests failed.' }
    } else { Write-Host 'GPU tests skipped explicitly. Run -Test on a Windows desktop with a D3D11 GPU for full validation.' }
}
Write-Host "Built $out\ArenaHDR.exe"
