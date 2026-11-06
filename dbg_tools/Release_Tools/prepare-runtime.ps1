param(
    [string]$ReleaseWorkRoot,
    [string]$ToolSnapshotId = 'auto-changelog-2.6.1-local01'
)
$ErrorActionPreference = 'Stop'
$ReleaseBaseline = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tool-baseline.json') -Raw | ConvertFrom-Json
if ($ToolSnapshotId -ne $ReleaseBaseline.snapshotId) { throw 'Tool snapshot does not match baseline.' }
$ReleaseArgs = @((Join-Path $PSScriptRoot 'generate_release_notes.py'), '--prepare-runtime')
if ($ReleaseWorkRoot) { $ReleaseArgs += @('--runtime-root', $ReleaseWorkRoot) }
& python @ReleaseArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
