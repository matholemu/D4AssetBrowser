<#
    fix-paths-after-move.ps1 — re-root the absolute paths a project folder move left behind.

    Moving the project does not move what is INSIDE it that is recorded as an absolute path.
    Two kinds of stale path survive a move and neither announces itself:

      1. The app's own settings (QSettings .ini). paths/d4dataDir and paths/tactKeysPath are
         absolute. After a move they still point at the OLD copy, so the app keeps working —
         by reading the old folder. Delete that folder and it breaks, and the symptom is
         "no d4data" / "encrypted content cannot be decoded", not "you moved the project".

      2. CMake's cache. build\release\CMakeCache.txt records the source directory it was
         configured from. Building from the new location fails outright until it is cleared.

    What this does NOT delete: build\release\data. That holds the d4data checkout, the CASC
    index and every built cache — gigabytes that would otherwise be re-downloaded and rebuilt.
    Only CMakeCache.txt and CMakeFiles\ go, which is all CMake needs to reconfigure.

    gameDir is deliberately left alone: the game install is not inside the project and did not
    move with it.
#>
[CmdletBinding()]
param([switch]$WhatIfOnly)

$ErrorActionPreference = 'Stop'
$root    = $PSScriptRoot
$rootFwd = ($root -replace '\\', '/').TrimEnd('/')

Write-Host ""
Write-Host "Project root: $root" -ForegroundColor Cyan
Write-Host ""

# ── 1. settings ─────────────────────────────────────────────────────────────────────────────
# Keyed on the RELATIVE tail each setting should have, so this works for any sibling project
# (FOX/POE2/DI) without editing: the tail is a property of the layout, not of the old path.
$targets = @(
    @{ Key = 'd4dataDir';    Tail = 'build/release/data/d4data' },
    @{ Key = 'tactKeysPath'; Tail = '.Resources' }
)

$inis = @()
$dataDir = Join-Path $root 'build\release\data'
if (Test-Path -LiteralPath $dataDir) {
    $inis = Get-ChildItem -LiteralPath $dataDir -Filter *.ini -Recurse -Depth 1 -ErrorAction SilentlyContinue
}
if (-not $inis) { Write-Host "  (no settings .ini found under build\release\data)" -ForegroundColor Yellow }

$changed = 0
foreach ($ini in $inis) {
    # Read as BYTES and preserve the byte-order mark exactly as found. Re-encoding a settings
    # file is how a stray character ends up on its first line.
    $bytes = [System.IO.File]::ReadAllBytes($ini.FullName)
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $text   = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($hasBom) { $text = $text.Substring(1) }

    $before = $text
    foreach ($t in $targets) {
        $want = "$rootFwd/$($t.Tail)"
        $rx   = "(?m)^(" + [regex]::Escape($t.Key) + "=)(.*)$"
        $m    = [regex]::Match($text, $rx)
        if (-not $m.Success) { continue }
        $old = $m.Groups[2].Value.Trim()
        if ($old -ieq $want) {
            Write-Host ("  ok      {0} already points here" -f $t.Key) -ForegroundColor DarkGray
            continue
        }
        if (-not (Test-Path -LiteralPath ($want -replace '/', '\'))) {
            Write-Host ("  SKIP    {0}: {1} does not exist - not pointing a setting at nothing" -f $t.Key, $want) -ForegroundColor Yellow
            continue
        }
        $text = [regex]::Replace($text, $rx, { param($x) $x.Groups[1].Value + $want })
        Write-Host ("  fixed   {0}" -f $t.Key) -ForegroundColor Green
        Write-Host ("            was: {0}" -f $old) -ForegroundColor DarkGray
        Write-Host ("            now: {0}" -f $want) -ForegroundColor DarkGray
        $changed++
    }

    if ($text -ne $before -and -not $WhatIfOnly) {
        $enc = New-Object System.Text.UTF8Encoding($hasBom)
        [System.IO.File]::WriteAllText($ini.FullName, $text, $enc)
        Write-Host ("  wrote   {0}" -f $ini.FullName) -ForegroundColor Green
    }
}

# ── 2. CMake caches ─────────────────────────────────────────────────────────────────────────
Write-Host ""
$cacheRoots = @(
    (Join-Path $root 'build\release'),
    (Join-Path $root 'tools\d4shader\build'),
    (Join-Path $root 'tools\d4cloth\build')
)
$cleared = 0
foreach ($b in $cacheRoots) {
    $cache = Join-Path $b 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache)) { continue }
    $home_ = (Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' |
              Select-Object -First 1).Matches.Groups[1].Value
    if ($home_ -and ($home_ -replace '\\','/').TrimEnd('/') -ieq $rootFwd) {
        Write-Host ("  ok      cache in {0} already points here" -f $b) -ForegroundColor DarkGray
        continue
    }
    Write-Host ("  stale   {0}" -f $cache) -ForegroundColor Green
    Write-Host ("            configured from: {0}" -f $home_) -ForegroundColor DarkGray
    if (-not $WhatIfOnly) {
        Remove-Item -LiteralPath $cache -Force
        $cf = Join-Path $b 'CMakeFiles'
        if (Test-Path -LiteralPath $cf) { Remove-Item -LiteralPath $cf -Recurse -Force }
        Write-Host  "            removed CMakeCache.txt + CMakeFiles\ (build\release\data untouched)" -ForegroundColor DarkGray
    }
    $cleared++
}

Write-Host ""
Write-Host ("Settings repointed: {0}    CMake caches cleared: {1}" -f $changed, $cleared) -ForegroundColor Cyan
if ($WhatIfOnly) { Write-Host "DRY RUN - nothing was written." -ForegroundColor Yellow }
Write-Host ""
exit 0
