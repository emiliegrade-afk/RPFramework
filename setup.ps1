# ============================================================================
# RPFramework - Script d'initialisation de l'environnement (Phase 0)
# Vérifie les prérequis et clone la dépendance AsaApi.
# ============================================================================
$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "=== RPFramework : setup de l'environnement ===" -ForegroundColor Cyan

# --- 1. Git -----------------------------------------------------------------
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "Git est requis mais introuvable. Installez-le : https://git-scm.com/"
    exit 1
}
Write-Host "[OK] Git detecte" -ForegroundColor Green

# --- 2. Compilateur MSVC ----------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
}
if ($vsPath) {
    Write-Host "[OK] Outillage C++ MSVC detecte : $vsPath" -ForegroundColor Green
} else {
    Write-Warning "Outillage C++ MSVC introuvable."
    Write-Host "  Installez Visual Studio 2022 Build Tools avec le composant :" -ForegroundColor Yellow
    Write-Host "    'MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.39-17.9)'" -ForegroundColor Yellow
    Write-Host "  (cette version exacte est requise par AsaApi)." -ForegroundColor Yellow
}

# --- 3. Clonage d'AsaApi ----------------------------------------------------
$asaDir = Join-Path $PSScriptRoot "extern\AsaApi"
if (-not (Test-Path $asaDir)) {
    Write-Host "Clonage d'AsaApi dans extern\AsaApi ..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path (Split-Path $asaDir) | Out-Null
    git clone --depth 1 https://github.com/ArkServerApi/AsaApi.git $asaDir
    Write-Host "[OK] AsaApi clone" -ForegroundColor Green
} else {
    Write-Host "[OK] AsaApi deja present" -ForegroundColor Green
}

# --- 4. AsaApi.lib ----------------------------------------------------------
$libPath = Join-Path $asaDir "out_lib\AsaApi.lib"
if (-not (Test-Path $libPath)) {
    Write-Host "" -ForegroundColor Yellow
    Write-Host "AsaApi.lib introuvable. Deux options :" -ForegroundColor Yellow
    Write-Host "  1. Compilez AsaApi : ouvrez extern\AsaApi\AsaApi.sln et compilez en Release|x64," -ForegroundColor Yellow
    Write-Host "     puis copiez AsaApi.lib dans extern\AsaApi\out_lib\" -ForegroundColor Yellow
    Write-Host "  2. Telechargez une release AsaApi depuis https://ark-server-api.com/resources/asa-server-api.31/" -ForegroundColor Yellow
    Write-Host "     et copiez le AsaApi.lib du dossier Lib\ dans extern\AsaApi\out_lib\" -ForegroundColor Yellow
} else {
    Write-Host "[OK] AsaApi.lib present" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Setup termine ===" -ForegroundColor Cyan
Write-Host "Consultez SETUP.md pour l'installation complete (VS 2022, vcpkg, deploiement)." -ForegroundColor Cyan
