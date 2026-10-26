# Packages only the stable/development runtime and public documentation. Never installs or launches the game.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $PSScriptRoot 'package-version.ps1')

function Get-SafePath([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -match '[*?\[\]]' -or $Value.StartsWith('\\')) {
        throw 'Use a local filesystem path without wildcards or device/UNC prefixes.'
    }
    if (-not [IO.Path]::IsPathRooted($Value)) { $Value = Join-Path (Get-Location).Path $Value }
    $full = [IO.Path]::GetFullPath($Value)
    if ($full -notmatch '^[A-Za-z]:\\') {
        throw 'Use a local drive-absolute filesystem path; network and device paths are not allowed.'
    }
    if ($full.Substring(2).Contains(':')) { throw 'Alternate data streams are not package paths.' }
    $part = $full
    while ($part) {
        if (Test-Path -LiteralPath $part) {
            $item = Get-Item -LiteralPath $part -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Reparse points are not allowed in package paths.'
            }
        }
        $part = [IO.Path]::GetDirectoryName($part)
    }
    return $full.TrimEnd('\', '/')
}

function Get-FileRecord([string]$Path, [string]$RelativePath) {
    return [ordered]@{
        path = $RelativePath
        bytes = (Get-Item -LiteralPath $Path).Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$source = Get-SafePath (Join-Path $PSScriptRoot '..')
$build = Get-SafePath $BuildDirectory
$output = Get-SafePath $OutputDirectory
foreach ($protected in @($source, $build)) {
    if ($protected -eq $output -or $protected.StartsWith($output + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Output must not be a source/build directory or one of its ancestors.'
    }
}
if ($output -eq [IO.Path]::GetPathRoot($output).TrimEnd('\')) { throw 'Output must not be a drive root.' }

$cmake = Get-Content -LiteralPath (Join-Path $source 'CMakeLists.txt') -Raw
$version = Get-PackageVersion $cmake
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
if ($cache -notmatch '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)\r?$') { throw 'Not a configured build.' }
if ((Get-SafePath $Matches[1].Trim()) -ne $source) { throw 'Build belongs to a different source directory.' }
if ($cache -notmatch '(?m)^KM_DUMPER:BOOL=OFF\r?$') { throw 'Only normal builds with KM_DUMPER=OFF may be packaged.' }

$inputs = [ordered]@{
    'winmm.dll' = (Join-Path $build 'Release/winmm.dll')
    'README.md' = (Join-Path $source 'README.md')
    'README.ru.md' = (Join-Path $source 'README.ru.md')
    'CHANGELOG.md' = (Join-Path $source 'CHANGELOG.md')
    'LICENSE' = (Join-Path $source 'LICENSE')
    'THIRD-PARTY-NOTICES.txt' = (Join-Path $source 'THIRD-PARTY-NOTICES.txt')
    'assets/fonts/Inter-OFL.txt' = (Join-Path $source 'assets/fonts/Inter-OFL.txt')
    'docs/VERIFICATION.md' = (Join-Path $source 'docs/VERIFICATION.md')
}
$records = @()
foreach ($name in $inputs.Keys) {
    $path = Get-SafePath $inputs[$name]
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing required package input: $name" }
    $records += Get-FileRecord $path $name
}
$dllText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($inputs['winmm.dll']))
if (-not $dllText.Contains($version + [char]0)) { throw 'Release DLL does not contain the package version; rebuild it first.' }
$commit = & git -C $source rev-parse --verify HEAD
if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'Cannot identify the source commit.' }
# Refuse to label uncommitted distribution/build inputs with an earlier source commit.
$changes = & git -C $source status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0 -or $changes) { throw 'Commit the source and package inputs before packaging.' }

$stem = "BananMenu-$version-win64"
$zipPath = Join-Path $output ($stem + '.zip')
$manifestPath = Join-Path $output ($stem + '.manifest.json')
foreach ($target in @($zipPath, $manifestPath)) {
    $null = Get-SafePath $target
    if (Test-Path -LiteralPath $target) {
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw 'A package target is not a regular file.' }
        if (-not $Force) { throw 'Package output already exists; use -Force explicitly to overwrite.' }
    }
}
$null = [IO.Directory]::CreateDirectory($output)
$mode = if ($Force) { [IO.FileMode]::Create } else { [IO.FileMode]::CreateNew }
$zipStream = [IO.File]::Open($zipPath, $mode, [IO.FileAccess]::Write, [IO.FileShare]::None)
$archive = [IO.Compression.ZipArchive]::new($zipStream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in $inputs.Keys) {
        $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset]::Parse('2026-10-24T00:00:00+00:00')
        $destination = $entry.Open()
        $sourceStream = [IO.File]::OpenRead($inputs[$name])
        try { $sourceStream.CopyTo($destination) } finally { $sourceStream.Dispose(); $destination.Dispose() }
    }
} finally { $archive.Dispose() }

$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if ($archive.Entries.Count -ne $inputs.Count) { throw 'Archive entry count does not match the allowlist.' }
    $seen = @{}
    foreach ($entry in $archive.Entries) {
        if (-not $inputs.Contains($entry.FullName) -or $seen.ContainsKey($entry.FullName)) {
            throw 'Unexpected or duplicate archive entry.'
        }
        $seen[$entry.FullName] = $true
        $record = $records | Where-Object { $_.path -ceq $entry.FullName }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
        finally { $sha.Dispose(); $stream.Dispose() }
        if ($entry.Length -ne $record.bytes -or $hash -ne $record.sha256) {
            throw "Archive content mismatch: $($entry.FullName)"
        }
    }
} finally { $archive.Dispose() }

$manifest = [ordered]@{
    version = $version
    sourceCommit = $commit
    files = $records
    archive = (Get-FileRecord $zipPath ($stem + '.zip'))
}
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output "Created $stem.zip and $stem.manifest.json; verified all $($inputs.Count) allowlisted entries."
