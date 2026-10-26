param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectFile,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
$Project = Get-Content -LiteralPath $ProjectFile -Raw | ConvertFrom-Json
if ($Project.EngineAssociation -ne '5.8') {
    throw "This QA runner targets UE 5.8; project association is '$($Project.EngineAssociation)'."
}
if (Get-Process | Where-Object { $_.ProcessName -match '^UnrealEditor' }) {
    throw 'An Unreal Editor process is already running. Close it normally before headless QA; the runner will not interrupt a user session.'
}

$BuildTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $BuildTool) -or !(Test-Path -LiteralPath $EditorCmd)) {
    throw "UE 5.8 tools were not found under '$EngineRoot'."
}

$QaDirectory = Join-Path (Split-Path -Parent $ProjectFile) 'Saved\BlackLionAssetArchive\qa'
New-Item -ItemType Directory -Force -Path $QaDirectory | Out-Null
$BuildLog = Join-Path $QaDirectory 'archive-build.log'
$AutomationLog = Join-Path $QaDirectory 'archive-automation.log'
$CommandOutput = Join-Path $QaDirectory 'archive-command-output.log'

if (!$SkipBuild) {
	$TargetName = "$([IO.Path]::GetFileNameWithoutExtension($ProjectFile))Editor"
	& $BuildTool $TargetName 'Win64' 'Development' "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE *> $BuildLog
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $BuildLog -Tail 80
        throw "UE build failed with exit code $LASTEXITCODE. See '$BuildLog'."
    }
}

$AutomationArgs = @(
    $ProjectFile,
    '-unattended', '-nop4', '-NullRHI', '-NoSound', '-NoSplash',
    '-ExecCmds=Automation RunTests BlackLion.AssetArchive; Quit',
    '-TestExit=Automation Test Queue Empty',
    "-abslog=$AutomationLog"
)
& $EditorCmd @AutomationArgs *> $CommandOutput
$AutomationExit = $LASTEXITCODE
if (!(Test-Path -LiteralPath $AutomationLog)) {
    Get-Content -LiteralPath $CommandOutput -Tail 80
    throw "Unreal did not produce the expected automation log (exit $AutomationExit)."
}

$LogText = Get-Content -LiteralPath $AutomationLog -Raw
$ExpectedTests = @(
    'BlackLion.AssetArchive.Paging.LargeCatalogNoLossOrDuplicates',
    'BlackLion.AssetArchive.Preview.AdaptiveQualityRespectsAssetBounds'
)
foreach ($TestName in $ExpectedTests) {
    if ($LogText -notmatch [regex]::Escape("Result={Success} Name={$($TestName.Split('.')[-1])}")) {
        Get-Content -LiteralPath $AutomationLog -Tail 100
        throw "Expected automation test did not pass: $TestName"
    }
}
if ($AutomationExit -ne 0 -or $LogText -notmatch 'TEST COMPLETE\. EXIT CODE: 0') {
    Get-Content -LiteralPath $AutomationLog -Tail 100
    throw "Unreal automation failed with process exit code $AutomationExit."
}

Write-Output "ARCHIVE_QA_OK engine=5.8 tests=$($ExpectedTests.Count) build_skipped=$([bool]$SkipBuild)"
Write-Output "BUILD_LOG=$BuildLog"
Write-Output "AUTOMATION_LOG=$AutomationLog"
