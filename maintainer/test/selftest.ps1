# selftest.ps1: a quick check of the maintainer tools themselves, before a release (a few seconds, no game started):
# every PowerShell script here parses, every Python script compiles with the installed Python, and every script
# whose param block takes a [string[]] list splits a comma list (powershell -File hands "A,B" over as one string:
# smoke_all and crash_all once ran no game at all that way, 2026-11-06). Prints one line per problem and "selftest
# ok" or "selftest: N problem(s)"; the exit code is the number of problems.
$maint = Split-Path $PSScriptRoot -Parent
$problems = @()
foreach ($f in Get-ChildItem $maint -Recurse -Filter *.ps1) {
  $tokens = $null; $errors = $null
  [void][System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$tokens, [ref]$errors)
  foreach ($e in $errors) { $problems += "$($f.Name):$($e.Extent.StartLineNumber): $($e.Message)" }
  $text = Get-Content -Raw $f.FullName
  $m = [regex]::Match($text, '(?ms)^param\((.*?)\)\s*$')
  if ($m.Success) {
    foreach ($p in [regex]::Matches($m.Groups[1].Value, '\[string\[\]\]\$(\w+)')) {
      $name = $p.Groups[1].Value
      if ($text -notmatch ('\$' + $name + ' = @\(\$' + $name + ' \| ForEach-Object \{ \$_ -split')) {
        $problems += "$($f.Name): -$name is a [string[]] parameter that isn't split at commas"
      }
    }
  }
}
foreach ($f in Get-ChildItem $maint -Recurse -Filter *.py) {
  $out = & python -c "import py_compile, sys; py_compile.compile(sys.argv[1], doraise=True)" $f.FullName 2>&1
  if ($LASTEXITCODE -ne 0) { $problems += "$($f.Name): $out" }
  # The PATH's python may be old (3.8 on the maintainer's PC, which bundle scripts call): its parser catches newer
  # syntax above, not newer library calls (2026-11-26: str.removesuffix stopped `release.py build` for all six).
  $n = 0
  foreach ($line in Get-Content $f.FullName) {
    $n++
    if ($line -match '^\s*#') { continue }
    if ($line -match '\.(removeprefix|removesuffix)\(|\bfunctools\.cache\b|\bimport zoneinfo\b|\bmath\.lcm\(') {
      $problems += "$($f.Name):${n}: needs Python 3.9 or newer ($($Matches[0].Trim('.', '('))): the PATH's python may be 3.8"
    }
  }
}
$problems | ForEach-Object { $_ }
if ($problems.Count) { "selftest: $($problems.Count) problem(s)" } else { "selftest ok" }
exit $problems.Count
