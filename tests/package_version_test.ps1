$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../scripts/package-version.ps1')
foreach ($newline in @("`n", "`r`n")) {
    $text = 'cmake_minimum_required(VERSION 3.25)' + $newline + 'set(BANAN_VERSION "1.0.0")' + $newline
    if ((Get-PackageVersion $text) -cne '1.0.0') { throw 'Multiline version parsing failed.' }
}
foreach ($version in @('1.0.0', '1.0.0-dev.1', '1.0.0-dev.9', '1.0.0-dev.123')) {
    $actual = Get-PackageVersion ('set(BANAN_VERSION "' + $version + '")')
    if ($actual -cne $version) { throw "Accepted version changed: $version" }
}
foreach ($version in @('', '0.4.16', '1.0.1', 'v1.0.0', '1.0.0-dev.0', '1.0.0-dev.01',
        '1.0.0-dev.-1', '1.0.0-dev.1.extra', '1.0.0-rc.1', '1.0.0+local', '1.0.0-dev.1junk')) {
    $rejected = $false
    try { $null = Get-PackageVersion ('set(BANAN_VERSION "' + $version + '")') } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid version accepted: $version" }
}
foreach ($text in @('# set(BANAN_VERSION "1.0.0")',
        "set(BANAN_VERSION `"1.0.0`")`nset(BANAN_VERSION `"1.0.0-dev.9`")",
        "set(BANAN_VERSION `"1.0.0`")`nset(BANAN_VERSION `"invalid`")",
        'set(BANAN_VERSION "1.0.0") trailing-command')) {
    $rejected = $false
    try { $null = Get-PackageVersion $text } catch { $rejected = $true }
    if (-not $rejected) { throw 'Ambiguous or inactive version declaration accepted.' }
}
Write-Output 'Package version tests passed: stable, development and rejected/ambiguous declarations.'
