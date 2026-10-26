param(
    [Parameter(Mandatory = $true)] [string]$ProjectFile,
    [Parameter(Mandatory = $true)] [string]$ManifestPath,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$uproject = (Resolve-Path -LiteralPath $ProjectFile).Path
$manifest = (Resolve-Path -LiteralPath $ManifestPath).Path
$projectRoot = Split-Path -Parent $uproject
$projectJson = Join-Path $projectRoot 'AssetArchive_Inbox\project.json'
$catalogPath = Join-Path $projectRoot 'Saved\BlackLionAssetArchive\catalog.json'
$engineCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = Join-Path $PSScriptRoot 'apply_scene_layout.py'
$validator = Join-Path $PSScriptRoot 'scene_layout.py'
$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCommand) { throw 'Python 3 is required for local scene-layout preflight; Unreal has not been started.' }
$python = $pythonCommand.Source
$qaDir = Join-Path (Split-Path -Parent $manifest) 'ue-qa'
$logPath = Join-Path $qaDir 'scene-layout-commandlet.log'
$commandOutput = Join-Path $qaDir 'scene-layout-command-output.log'

foreach ($path in @($projectJson, $catalogPath, $engineCmd, $script, $validator)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required project, catalog or UE tool is missing: $path" }
}
$projectConfig = Get-Content -LiteralPath $projectJson -Raw | ConvertFrom-Json
$catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
$layout = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($layout.schema -ne 'blacklion.scene-layout.v1' -or $layout.project_id -ne $projectConfig.project_id -or $layout.project_id -ne $catalog.project_id) {
    throw 'Scene layout identity does not match both this project and its local catalog.'
}
if ($layout.target_level -notlike '/Game/Developer/AIPlacementQA/*') {
    throw 'This runner only writes under /Game/Developer/AIPlacementQA/. Production levels are never modified by this QA command.'
}
if ($layout.units -ne 'cm' -or $layout.up_axis -ne 'Z' -or $layout.ground_plane -ne 'XY') {
    throw 'Expected centimeters, Z-up and XY ground plane.'
}
$preflight = & $python $validator --manifest $manifest --project $projectJson --catalog $catalogPath
if ($LASTEXITCODE -ne 0) { $preflight; throw 'Scene layout or reference-shot preflight rejected the request before Unreal was started.' }
$running = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('UnrealEditor.exe', 'UnrealEditor-Cmd.exe') -and $_.CommandLine -and $_.CommandLine.Contains($uproject, [System.StringComparison]::OrdinalIgnoreCase)
}
if ($running) { throw "Target Unreal Editor is open (PID $($running.ProcessId)); preserve that session and retry only after it closes normally." }

New-Item -ItemType Directory -Path $qaDir -Force | Out-Null
$oldManifest = $env:BL_SCENE_LAYOUT_MANIFEST
$oldProject = $env:BL_SCENE_LAYOUT_PROJECT
$oldCatalog = $env:BL_SCENE_LAYOUT_CATALOG
try {
    $env:BL_SCENE_LAYOUT_MANIFEST = $manifest
    $env:BL_SCENE_LAYOUT_PROJECT = $uproject
    $env:BL_SCENE_LAYOUT_CATALOG = $catalogPath
    & $engineCmd $uproject '-run=pythonscript' "-script=$script" -unattended -nop4 -NullRHI -NoSound -NoSplash "-abslog=$logPath" *> $commandOutput
    $exitCode = $LASTEXITCODE
} finally {
    $env:BL_SCENE_LAYOUT_MANIFEST = $oldManifest
    $env:BL_SCENE_LAYOUT_PROJECT = $oldProject
    $env:BL_SCENE_LAYOUT_CATALOG = $oldCatalog
}

$reportPath = Join-Path (Split-Path -Parent $manifest) 'scene-layout-report.json'
$logText = if (Test-Path -LiteralPath $logPath) { Get-Content -LiteralPath $logPath -Raw } else { '' }
if ($exitCode -ne 0 -or $logText -notmatch 'SCENE_LAYOUT_APPLIED' -or -not (Test-Path -LiteralPath $reportPath)) {
    if (Test-Path -LiteralPath $commandOutput) { Get-Content -LiteralPath $commandOutput -Tail 60 }
    if (Test-Path -LiteralPath $logPath) { Get-Content -LiteralPath $logPath -Tail 100 }
    throw "Scene layout commandlet failed (exit $exitCode). See $logPath"
}
Get-Content -LiteralPath $reportPath -Raw
Write-Output "SCENE_LAYOUT_QA_OK log=$logPath report=$reportPath"
