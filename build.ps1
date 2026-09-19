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
$programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
$programFiles    = [Environment]::GetFolderPath('ProgramFiles')
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
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
# Fallback sans vswhere (Build Tools seul, chemin connu).
if (-not $msbuild) {
    $fallbacks = @(
        (Join-Path $programFilesX86 "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path $programFiles    "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path $programFiles    "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe")
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) { $msbuild = $candidate; break }
    }
}
if (-not $msbuild) {
    Write-Error "MSBuild introuvable. Installez Visual Studio 2022 Build Tools."
    exit 1
}

Write-Host "MSBuild : $msbuild" -ForegroundColor Cyan

# --- Worktree : vcpkg partage depuis le depot principal ---------------------
# Chaque worktree a son propre chemin absolu, ce qui pousse vcpkg a relancer
# `install` (et echouer hors Developer Command Prompt). On reutilise le
# vcpkg_installed deja peuple du depot principal.
$msbuildProps = @()
if ($PSScriptRoot -match 'RPFramework-[A-Z0-9]+$') {
    $mainRoot = Join-Path (Split-Path $PSScriptRoot -Parent) "RPFramework"
    $sharedVcpkg = Join-Path $mainRoot "vcpkg_installed\x64-windows-static-md\"
    if (Test-Path $sharedVcpkg) {
        $msbuildProps += "/p:VcpkgManifestRoot=$mainRoot"
        $msbuildProps += "/p:VcpkgInstalledDir=$sharedVcpkg"
        Write-Host "Worktree detecte : vcpkg partage depuis $mainRoot" -ForegroundColor DarkGray
    }
}

# --- Verrou de build --------------------------------------------------------
# Deux MSBuild simultanes sur la meme solution ecrivent dans le meme
# vc143.pdb et echouent avec "error C1041 : impossible d'ouvrir la base de
# donnees du programme". Ce n'est PAS une erreur de code, mais elle pousse a
# "corriger" du code sain. Le mutex nomme serialise les builds concurrents.
#
# Ce verrou ne remplace PAS l'isolation : plusieurs agents travaillant en
# parallele doivent avoir un worktree git chacun (voir ROADMAP.md).
$mutex = New-Object System.Threading.Mutex($false, "Global\RPFrameworkBuild")
$acquired = $false
try {
    if (-not $mutex.WaitOne(0)) {
        Write-Host "Un autre build est en cours : attente du verrou..." -ForegroundColor Yellow
        if (-not $mutex.WaitOne([TimeSpan]::FromMinutes(15))) {
            Write-Error "Verrou de build non obtenu apres 15 min. Un build est bloque ? (Get-Process MSBuild)"
            exit 1
        }
    }
    $acquired = $true

    # --- Compilation --------------------------------------------------------
    & $msbuild (Join-Path $PSScriptRoot "RPFramework.sln") /p:Configuration=Release /p:Platform=x64 /m @msbuildProps

    if ($LASTEXITCODE -ne 0) {
        Write-Error "La compilation a echoue (code $LASTEXITCODE)."
        exit $LASTEXITCODE
    }
}
finally {
    if ($acquired) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}

Write-Host ""
Write-Host "=== Build termine ===" -ForegroundColor Green
Write-Host "Sortie : $(Join-Path $PSScriptRoot 'out')" -ForegroundColor Green
Write-Host "Fichiers generes : RPFramework.dll et RPFramework.dll.arkapi" -ForegroundColor Green
