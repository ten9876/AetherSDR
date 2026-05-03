<#
.SYNOPSIS
    Stage app-local MSVC runtime DLLs for the Windows installer.

.DESCRIPTION
    Finds the x64 Microsoft.VC*.CRT redist directory from the active Visual
    Studio/MSVC environment and copies its DLLs to a staging directory for
    Inno Setup. Prefer VCToolsRedistDir because GitHub Actions initializes it
    through ilammy/msvc-dev-cmd before packaging.
#>

[CmdletBinding()]
param(
    [string]$OutputDir = "installer-runtime"
)

$ErrorActionPreference = "Stop"

function Get-MsvcRuntimeVersion {
    param([System.IO.DirectoryInfo]$Directory)

    if ($Directory.FullName -match "\\MSVC\\([^\\]+)\\x64\\Microsoft\.VC[^\\]+\.CRT$") {
        $versionText = $Matches[1]
        $parsed = $null
        if ([System.Version]::TryParse($versionText, [ref]$parsed)) {
            return $parsed
        }
    }

    return [System.Version]::new(0, 0)
}

function Get-CrtDirsFromRedistRoot {
    param([string]$RedistRoot)

    if ([string]::IsNullOrWhiteSpace($RedistRoot)) {
        return @()
    }

    $x64Root = Join-Path $RedistRoot "x64"
    if (-not (Test-Path -LiteralPath $x64Root)) {
        return @()
    }

    return @(Get-ChildItem -LiteralPath $x64Root -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue)
}

$candidateDirs = @()

if ($env:VCToolsRedistDir) {
    $candidateDirs += Get-CrtDirsFromRedistRoot -RedistRoot $env:VCToolsRedistDir
}

if (-not $candidateDirs) {
    $vsRoots = @(
        "${env:ProgramFiles}\Microsoft Visual Studio",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

    foreach ($root in $vsRoots) {
        $candidateDirs += Get-ChildItem -LiteralPath $root -Recurse -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\VC\\Redist\\MSVC\\[^\\]+\\x64\\Microsoft\.VC[^\\]+\.CRT$" }
    }
}

$runtimeDir = $candidateDirs |
    Sort-Object @{ Expression = { Get-MsvcRuntimeVersion -Directory $_ }; Descending = $true }, FullName -Descending |
    Select-Object -First 1

if (-not $runtimeDir) {
    throw "Could not find an x64 Microsoft.VC*.CRT runtime directory. Make sure the MSVC redist components are installed."
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null

$runtimeDlls = Get-ChildItem -LiteralPath $runtimeDir.FullName -File -Filter "*.dll"
if (-not $runtimeDlls) {
    throw "No runtime DLLs found in $($runtimeDir.FullName)."
}

foreach ($dll in $runtimeDlls) {
    Copy-Item -LiteralPath $dll.FullName -Destination $resolvedOutputDir -Force
}

Write-Host "Staged MSVC runtime from $($runtimeDir.FullName)"
Get-ChildItem -LiteralPath $resolvedOutputDir -File -Filter "*.dll" |
    Sort-Object Name |
    ForEach-Object { Write-Host "  $($_.Name)" }
