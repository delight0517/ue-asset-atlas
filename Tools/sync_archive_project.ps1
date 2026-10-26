param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectFile
)

$ErrorActionPreference = 'Stop'
$uproject = (Resolve-Path -LiteralPath $ProjectFile).Path
$projectRoot = Split-Path -Parent $uproject
$projectJson = Join-Path $projectRoot 'AssetArchive_Inbox\project.json'
$pluginRoot = Join-Path $projectRoot 'Plugins\BlackLionAssetArchive'
$pluginTools = Join-Path $pluginRoot 'Tools'
$uprojectData = Get-Content -LiteralPath $uproject -Raw | ConvertFrom-Json
$engineVersion = [string]$uprojectData.EngineAssociation
$engineCmd = "C:\Program Files\Epic Games\UE_$engineVersion\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$script = Join-Path $pluginTools 'import_archive_assets.py'
$logDir = Join-Path $projectRoot 'Saved\BlackLionAssetArchive'
$logPath = Join-Path $logDir 'sync.log'

if (-not (Test-Path -LiteralPath $projectJson) -or -not (Test-Path -LiteralPath $pluginRoot) -or -not (Test-Path -LiteralPath $script) -or -not (Test-Path -LiteralPath $engineCmd)) {
    throw 'Project identity, shared plugin link, UE commandlet, or archive importer is missing.'
}
$running = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('UnrealEditor.exe', 'UnrealEditor-Cmd.exe') -and $_.CommandLine -and $_.CommandLine.Contains($uproject, [System.StringComparison]::OrdinalIgnoreCase)
}
if ($running) {
    throw "Target project is open in an Unreal process (PID $($running.ProcessId)); save and close it normally before syncing."
}
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$oldRoot = $env:BLACKLION_ARCHIVE_PROJECT_ROOT
try {
    $env:BLACKLION_ARCHIVE_PROJECT_ROOT = $projectRoot
    & $engineCmd $uproject "-run=pythonscript" "-script=$script" -unattended -nop4 -NullRHI -nosplash -stdout -FullStdOutLogOutput *> $logPath
    $exitCode = $LASTEXITCODE
} finally {
    $env:BLACKLION_ARCHIVE_PROJECT_ROOT = $oldRoot
}
Select-String -Path $logPath -Pattern 'Archive group |Archive intake |catalog written|LogPython: Error|LogPythonScriptCommandlet: Error|Success -' | ForEach-Object { $_.Line }
if ($exitCode -ne 0) { throw "Asset Archive sync failed with exit code $exitCode. Full log: $logPath" }
