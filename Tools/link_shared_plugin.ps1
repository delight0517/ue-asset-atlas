param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectFile,
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
$sharedPlugin = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$uproject = (Resolve-Path -LiteralPath $ProjectFile).Path
$projectRoot = Split-Path -Parent $uproject
$pluginLink = Join-Path $projectRoot 'Plugins\BlackLionAssetArchive'
$descriptor = Join-Path $sharedPlugin 'BlackLionAssetArchive.uplugin'
$data = Get-Content -LiteralPath $uproject -Raw | ConvertFrom-Json
if ([string]$data.EngineAssociation -ne '5.8') { throw 'This shared plugin source is currently pinned to Unreal Engine 5.8.' }
if (-not (Test-Path -LiteralPath $descriptor)) { throw 'Canonical shared plugin descriptor is missing.' }
$running = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('UnrealEditor.exe', 'UnrealEditor-Cmd.exe') -and $_.CommandLine -and $_.CommandLine.Contains($uproject, [System.StringComparison]::OrdinalIgnoreCase)
}
if ($running) { throw "Close this exact Unreal project normally before linking the plugin (PID $($running.ProcessId))." }

$existingLink = Test-Path -LiteralPath $pluginLink
$linkIsCorrect = $false
if ($existingLink) {
    $item = Get-Item -LiteralPath $pluginLink -Force
    if ($item.LinkType -eq 'Junction') {
        $linkIsCorrect = ([IO.Path]::GetFullPath([string]$item.Target).TrimEnd('\') -ieq $sharedPlugin.TrimEnd('\'))
    }
    if (-not $linkIsCorrect) { throw 'Project Plugins\BlackLionAssetArchive already exists and is not the canonical junction; no overwrite was attempted.' }
}
$pluginRows = @($data.Plugins)
$row = $pluginRows | Where-Object { $_.Name -eq 'BlackLionAssetArchive' } | Select-Object -First 1
$needsUprojectChange = -not $row -or -not $row.Enabled
if (-not $Apply) {
    [pscustomobject]@{ mode='PREVIEW_ONLY'; project=$uproject; shared_source=$sharedPlugin; junction_needed=(-not $linkIsCorrect); uproject_enable_needed=$needsUprojectChange }
    return
}

New-Item -ItemType Directory -Path (Join-Path $projectRoot 'Plugins') -Force | Out-Null
if (-not $linkIsCorrect) { New-Item -ItemType Junction -Path $pluginLink -Target $sharedPlugin | Out-Null }
if (-not $data.Plugins) { $data | Add-Member -MemberType NoteProperty -Name Plugins -Value @() }
$pluginRows = @($data.Plugins)
$row = $pluginRows | Where-Object { $_.Name -eq 'BlackLionAssetArchive' } | Select-Object -First 1
if ($row) { $row.Enabled = $true }
else { $pluginRows += [pscustomobject]@{ Name='BlackLionAssetArchive'; Enabled=$true } }
$data.Plugins = $pluginRows
$temporary = $uproject + '.archive-tmp'
$serialized = ConvertTo-Json -InputObject $data -Depth 100
[IO.File]::WriteAllText($temporary, $serialized, [Text.UTF8Encoding]::new($false))
$null = Get-Content -LiteralPath $temporary -Raw | ConvertFrom-Json
[IO.File]::Replace($temporary, $uproject, $null)

$inbox = Join-Path $projectRoot 'AssetArchive_Inbox'
New-Item -ItemType Directory -Path $inbox -Force | Out-Null
$projectIdentity = Join-Path $inbox 'project.json'
if (-not (Test-Path -LiteralPath $projectIdentity)) {
    $projectId = [regex]::Replace([IO.Path]::GetFileNameWithoutExtension($uproject), '[^A-Za-z0-9_-]+', '_').Trim('_')
    $identity = [ordered]@{ schema='blacklion.asset-archive.project.v1'; project_id=$projectId; display_name=$data.Description; units='centimeters'; up_axis='Z'; ground_plane='XY'; approved_source_roots=@() } | ConvertTo-Json
    [IO.File]::WriteAllText($projectIdentity, $identity, [Text.UTF8Encoding]::new($false))
}
[pscustomobject]@{ mode='APPLIED'; project=$uproject; shared_source=$sharedPlugin; junction=(Get-Item -LiteralPath $pluginLink -Force).Target; project_id=(Get-Content -LiteralPath $projectIdentity -Raw | ConvertFrom-Json).project_id; user_assets_copied=$false }
