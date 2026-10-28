# Packages a previously built executable; build.ps1 -Test first.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(-[a-zA-Z0-9.]+)?$') { throw 'Invalid VERSION.' }
$exe = Join-Path $root 'build/ArenaHDR.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Run ./build.ps1 -Test before packaging.' }
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $version) { throw 'Executable version does not match VERSION. Rebuild first.' }
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$name = "ArenaHDR-$version-windows-x64"
$zipPath = Join-Path $dist "$name.zip"
# Create the archive directly from an allowlist, never from the build directory.
# A random staging ZIP avoids deleting/reusing a directory with unrelated files.
$temporary = Join-Path $dist ([Guid]::NewGuid().ToString() + '.zip')
$files = [ordered]@{
    'ArenaHDR.exe' = $exe
    'README.md' = (Join-Path $root 'README.md')
    'assets/screenshot.jpg' = (Join-Path $root 'assets/screenshot.jpg')
    'THIRD_PARTY.md' = (Join-Path $root 'THIRD_PARTY.md')
    'CHANGELOG.md' = (Join-Path $root 'CHANGELOG.md')
    'PERFORMANCE.md' = (Join-Path $root 'PERFORMANCE.md')
    'CONTRIBUTING.md' = (Join-Path $root 'CONTRIBUTING.md')
    'docs/RELEASING.md' = (Join-Path $root 'docs/RELEASING.md')
}
if (Test-Path -LiteralPath (Join-Path $root 'LICENSE')) { $files['LICENSE'] = Join-Path $root 'LICENSE' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
try {
    $archive = [System.IO.Compression.ZipFile]::Open($temporary, 'Create')
    try {
        foreach ($file in $files.GetEnumerator()) {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.Value, $file.Key, 'Optimal') | Out-Null
        }
    } finally { $archive.Dispose() }
    $archive = [System.IO.Compression.ZipFile]::OpenRead($temporary)
    try {
        $actual = @($archive.Entries | ForEach-Object { $_.FullName })
        if (Compare-Object @($files.Keys) $actual) { throw 'Package contains unexpected files.' }
        if ($archive.Entries | Where-Object { $_.Length -eq 0 }) { throw 'Package contains an empty file.' }
        foreach ($entry in $archive.Entries) {
            $stream = $entry.Open(); $sha = [System.Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
            finally { $sha.Dispose(); $stream.Dispose() }
            if ($hash -ne (Get-FileHash -LiteralPath $files[$entry.FullName] -Algorithm SHA256).Hash) { throw "Archive verification failed: $($entry.FullName)" }
        }
    } finally { $archive.Dispose() }
    Move-Item -LiteralPath $temporary -Destination $zipPath -Force
    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name.zip" | Set-Content -LiteralPath ($zipPath + '.sha256') -Encoding ascii
    Write-Host "Verified package: $zipPath"
    Write-Host "SHA256: $hash"
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
