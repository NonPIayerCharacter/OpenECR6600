param(
    [string]$ReleaseWorkRoot,
    [string]$ToolSnapshotId = 'auto-changelog-2.6.1-local01',
    [Parameter(Mandatory = $true)][string]$PreviousTag,
    [Parameter(Mandatory = $true)][string]$TargetTag,
    [string]$ContentPath,
    [string]$CoveragePath,
    [string[]]$Important,
    [string]$ImportantFilePath,
    [string]$Version,
    [ValidateRange(10, 1800)][int]$TimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
$ReleaseBaseline = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tool-baseline.json') -Raw | ConvertFrom-Json
if ($ToolSnapshotId -ne $ReleaseBaseline.snapshotId) { throw 'Tool snapshot does not match baseline.' }
$ReleaseArgs = @((Join-Path $PSScriptRoot 'generate_release_notes.py'),
    '--from', $PreviousTag, '--to', $TargetTag, '--timeout', [string]$TimeoutSeconds)
if ($ReleaseWorkRoot) { $ReleaseArgs += @('--runtime-root', $ReleaseWorkRoot) }
if ($ContentPath) { $ReleaseArgs += @('--content', $ContentPath) }
if ($CoveragePath) { $ReleaseArgs += @('--coverage', $CoveragePath) }
if ($ImportantFilePath) { $ReleaseArgs += @('--important-file', $ImportantFilePath) }
if ($Version) { $ReleaseArgs += @('--version', $Version) }
foreach ($ReleaseId in $Important) { $ReleaseArgs += @('--important', $ReleaseId) }
Write-Host 'V1.6: output is now Release_Tools/release; ReleaseWorkRoot selects runtime only.'
& python @ReleaseArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
