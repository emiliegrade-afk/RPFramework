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
    & $msbuild (Join-Path $PSScriptRoot "RPFramework.sln") /p:Configuration=Release /p:Platform=x64 /m

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
