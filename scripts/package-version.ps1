# Shared by the packager and its version-input regression test. No filesystem side effects.
function Get-PackageVersion([string]$CMakeSource) {
    $declarations = [regex]::Matches($CMakeSource, '(?m)^[\t ]*set\(BANAN_VERSION[^\r\n]*\r?$')
    if ($declarations.Count -ne 1 -or
        $declarations[0].Value -cnotmatch '^[\t ]*set\(BANAN_VERSION "(1\.0\.0(?:-dev\.[1-9][0-9]*)?)"\)[\t ]*\r?$') {
        throw 'Expected exactly one version declaration: 1.0.0 or 1.0.0-dev.N (positive N, no leading zero).'
    }
    return $Matches[1]
}
