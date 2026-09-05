# ============================================================================
# RPFramework - Script de compilation du plugin (Phase 0)
# Necessite : AsaApi.lib present dans extern\AsaApi\out_lib\ (voir setup.ps1).
# ============================================================================
$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "=== RPFramework : build ===" -ForegroundColor Cyan

# --- Verification d'AsaApi.lib ---------------------------------------------
$libPath = Join-Path $PSScriptRoot "extern\AsaApi\out_lib\AsaApi.lib"
if (-not (Test-Path $libPath)) {
    Write-Error "AsaApi.lib introuvable dans extern\AsaApi\out_lib\. Lancez setup.ps1 puis compilez/telechargez AsaApi.lib."
    exit 1
}

# --- Localisation de MSBuild ------------------------------------------------
$msbuild = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if ($vsPath) {
        $candidate = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
        if (Test-Path $candidate) { $msbuild = $candidate }
    }
}
if (-not $msbuild) {
    $cmd = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($cmd) { $msbuild = $cmd.Source }
}
if (-not $msbuild) {
    Write-Error "MSBuild introuvable. Installez Visual Studio 2022 Build Tools."
    exit 1
}

Write-Host "MSBuild : $msbuild" -ForegroundColor Cyan

# --- Compilation ------------------------------------------------------------
& $msbuild (Join-Path $PSScriptRoot "RPFramework.sln") /p:Configuration=Release /p:Platform=x64 /m

if ($LASTEXITCODE -ne 0) {
    Write-Error "La compilation a echoue (code $LASTEXITCODE)."
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "=== Build termine ===" -ForegroundColor Green
Write-Host "Sortie : $(Join-Path $PSScriptRoot 'out')" -ForegroundColor Green
Write-Host "Fichiers generes : RPFramework.dll et RPFramework.dll.arkapi" -ForegroundColor Green
