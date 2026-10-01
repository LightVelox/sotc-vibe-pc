param(
    [Parameter(Mandatory = $true)][string]$State,
    [string]$Output = "build/private-play",
    [switch]$DisableMotionBlur
)

$ErrorActionPreference = "Stop"
$workspace = Split-Path -Parent $PSScriptRoot
$source = Join-Path $workspace "build/port/bin"
$statePath = (Get-Item -LiteralPath $State).FullName
$target = [IO.Path]::GetFullPath((Join-Path $workspace $Output))
if ($target.TrimEnd('\') -eq $source.TrimEnd('\')) {
    throw "Choose a private output directory separate from build/port/bin."
}
New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item -LiteralPath (Join-Path $source "sotc.exe") -Destination $target
Get-ChildItem -LiteralPath $source -Filter "*.dll" | Copy-Item -Destination $target
$config = Join-Path $target "sotc.ini"
if (!(Test-Path -LiteralPath $config)) {
    Copy-Item -LiteralPath (Join-Path $source "sotc.ini") -Destination $config
}
$cards = Join-Path $target "memcards"
if (!(Test-Path -LiteralPath $cards)) {
    Copy-Item -LiteralPath (Join-Path $source "memcards") -Destination $cards -Recurse
}
$privateState = Join-Path $target "input.state"
if ($statePath -ne $privateState) {
    Copy-Item -LiteralPath $statePath -Destination $privateState
}
$launch = New-Object Diagnostics.ProcessStartInfo
$launch.FileName = Join-Path $target "sotc.exe"
$launch.WorkingDirectory = $workspace
$launch.UseShellExecute = $false
foreach ($key in @("SOTC_CAMERA_TEST", "SOTC_CAMERA_TRACE", "SOTC_PROFILE", "SOTC_TIMELINE", "SOTC_TRACE_CALLS",
                   "PS2X_STATE_SAVE_AT", "PS2X_FRAME_TIMES", "PS2X_SCREENSHOT_FIELDS", "PS2X_GS_RECORD",
                   "PS2X_GS_GPU_PROF", "PS2X_VU1_TRACE", "PS2X_VU1_VERIFY", "PS2X_PAD_SCRIPT")) {
    $launch.EnvironmentVariables.Remove($key)
}
$launch.EnvironmentVariables["PS2X_STATE_LOAD_AT"] = "1:" + $privateState.Replace('\', '/')
$launch.EnvironmentVariables["PS2X_GS_SHADER_CACHE_DIR"] = Join-Path $workspace "build/performance/shader-cache"
$launch.EnvironmentVariables["PS2X_FRAME_HASH"] = "0"
if ($DisableMotionBlur) {
    $launch.EnvironmentVariables["SOTC_DISABLE_MOTION_BLUR"] = "1"
}
$game = [Diagnostics.Process]::Start($launch)
Write-Output ("Private game process " + $game.Id + "; saves and cards: " + $target)
