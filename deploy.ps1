# ============================================================================
# RPFramework - Deploy script
#
# Build + copie le plugin sur un serveur ASA local pour test "solo" (dev).
# Le serveur n'a PAS besoin d'être arrêté : AsaApi recharge automatiquement
# le plugin à la détection du fichier .arkapi déposé dans son dossier.
#
# Prérequis :
#   - Visual Studio 2022 Build Tools + vcpkg (voir SETUP.md)
#   - Serveur dédié ASA installé (Steam tool "ARK: Survival Ascended
#     Dedicated Server") et AsaApi installé dessus
#   - Variable d'env ARKSV_PATH pointant vers la racine du serveur, OU
#     modification du $DefaultServerPath ci-dessous
#
# Usage :
#   .\deploy.ps1                  # build + deploy (le plus courant)
#   .\deploy.ps1 -NoBuild         # deploy sans rebuild (itération rapide)
#   .\deploy.ps1 -Restart         # force un restart du serveur après deploy
#   .\deploy.ps1 -WhatIf          # affiche ce qui serait fait, sans toucher
#
# Hot reload AsaApi : déposer *.dll.arkapi suffit. Le serveur charge la
# nouvelle DLL et renomme le fichier en *.dll sans interruption de service.
# ============================================================================
param(
    [switch]$NoBuild,
    [switch]$Restart,
    [switch]$WhatIf
)

$ErrorActionPreference = "Stop"

$ScriptDir    = $PSScriptRoot
$PluginName   = "RPFramework"
$ServerExeName = "ArkAscendedServer"

# --- 1. Résoudre le chemin du serveur ASA ----------------------------------
$DefaultServerPath = "C:\Program Files (x86)\Steam\steamapps\common\ARK Survival Ascended Dedicated Server"
$ServerPath        = $env:ARKSV_PATH
if (-not $ServerPath) { $ServerPath = $DefaultServerPath }

# --- 2. Banner -------------------------------------------------------------
Write-Host ""
Write-Host "=== RPFramework : deploy ===" -ForegroundColor Cyan
Write-Host "Plugin     : $PluginName" -ForegroundColor Cyan
Write-Host "Cible      : $ServerPath" -ForegroundColor Cyan
if ($WhatIf) { Write-Host "Mode       : WHAT-IF (rien ne sera modifie)" -ForegroundColor Magenta }
Write-Host ""

# --- 3. Pré-flight checks --------------------------------------------------
if (-not (Test-Path $ServerPath)) {
    Write-Error "Serveur ASA introuvable : '$ServerPath'."
    Write-Error "Solutions :"
    Write-Error "  - Installe 'ARK: Survival Ascended Dedicated Server' via Steam"
    Write-Error "  - Set la variable d'env :`n`t`t[Environment]::SetEnvironmentVariable('ARKSV_PATH', 'D:\…\ARK Survival Ascended Dedicated Server', 'User')"
    Write-Error "  - Edite `$DefaultServerPath dans deploy.ps1"
    exit 1
}

$PluginsDir        = Join-Path $ServerPath "ShooterGame\Binaries\Win64\ArkApi\Plugins\$PluginName"
$DeployConfigsDir  = Join-Path $PluginsDir "configs"
$AsaApiDir         = Join-Path $ServerPath "ShooterGame\Binaries\Win64\ArkApi"
$AsaApiDll         = Join-Path $AsaApiDir "AsaApi.dll"
$ServerExe         = Join-Path $ServerPath "ShooterGame\Binaries\Win64\$ServerExeName.exe"

if (-not (Test-Path $AsaApiDll)) {
    Write-Warning "AsaApi.dll absent : '$AsaApiDll'."
    Write-Warning "Le plugin ne se chargera pas sans AsaApi. Installe-le : https://ark-server-api.com"
    if (-not $WhatIf) {
        $choice = Read-Host "Continuer quand même ? (o/N)"
        if ($choice -ne "o") { exit 1 }
    }
}

# --- 4. Build (optionnel) --------------------------------------------------
if (-not $NoBuild) {
    Write-Host "[1/3] Build..." -ForegroundColor Yellow
    if ($WhatIf) {
        Write-Host "  (what-if) build.ps1 non lance" -ForegroundColor Magenta
    } else {
        & (Join-Path $ScriptDir "build.ps1")
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Build echoue (code $LASTEXITCODE). Deploy annule."
            exit $LASTEXITCODE
        }
    }
} else {
    Write-Host "[1/3] Build : SKIP (-NoBuild)" -ForegroundColor DarkGray
}

# --- 5. Vérifier les artefacts --------------------------------------------
$BuiltDll    = Join-Path $ScriptDir "out\$PluginName.dll"
$BuiltArkapi = Join-Path $ScriptDir "out\$PluginName.dll.arkapi"
$BuiltPdb    = Join-Path $ScriptDir "out\$PluginName.pdb"
$SourceCfgs  = Join-Path $ScriptDir "configs"

if (-not $WhatIf) {
    foreach ($f in @($BuiltDll, $BuiltArkapi)) {
        if (-not (Test-Path $f)) {
            Write-Error "Artefact manquant : $f. Lance d'abord .\build.ps1"
            exit 1
        }
    }
}

# --- 6. Deploy -------------------------------------------------------------
Write-Host "[2/3] Deploy vers $PluginsDir ..." -ForegroundColor Yellow
if ($WhatIf) {
    Write-Host "  (what-if) mkdir + Copy-Item non executes" -ForegroundColor Magenta
} else {
    New-Item -ItemType Directory -Force -Path $PluginsDir       | Out-Null
    New-Item -ItemType Directory -Force -Path $DeployConfigsDir | Out-Null

    Copy-Item -Path $BuiltDll        -Destination $PluginsDir -Force
    Copy-Item -Path $BuiltArkapi     -Destination $PluginsDir -Force
    if (Test-Path $BuiltPdb) {
        Copy-Item -Path $BuiltPdb   -Destination $PluginsDir -Force
    }
    Copy-Item -Path (Join-Path $SourceCfgs "*") `
               -Destination $DeployConfigsDir -Recurse -Force

    Write-Host "  OK : $PluginName.dll (+.arkapi, .pdb) et configs/*" -ForegroundColor Green
}

# --- 7. Restart / Hot reload ----------------------------------------------
$ServerProc = Get-Process -Name $ServerExeName -ErrorAction SilentlyContinue

Write-Host "[3/3] Etat du serveur :" -ForegroundColor Yellow
if ($ServerProc) {
    Write-Host "  En cours (PID $($ServerProc.Id))." -ForegroundColor Cyan
    if ($Restart) {
        if ($WhatIf) {
            Write-Host "  (what-if) Stop-Process + relance non executes" -ForegroundColor Magenta
        } else {
            Write-Host "  -Restart demande : kill du serveur..." -ForegroundColor Yellow
            Stop-Process -Id $ServerProc.Id -Force
            # Attend qu'il meure vraiment
            $ServerProc.WaitForExit(15000) | Out-Null
            Write-Host "  Relance..." -ForegroundColor Yellow
            Start-Process -FilePath $ServerExe -WorkingDirectory (Split-Path $ServerExe -Parent)
            Write-Host "  Serveur relance." -ForegroundColor Green
        }
    } else {
        Write-Host "  AsaApi va recharger automatiquement le plugin a chaud" -ForegroundColor Green
        Write-Host "  (le .dll.arkapi depose declenche le reload)." -ForegroundColor Green
        Write-Host ""
        Write-Host "  Si le reload n'a pas lieu, restart manuel :" -ForegroundColor Yellow
        Write-Host "    Stop-Process -Name $ServerExeName -Force" -ForegroundColor Yellow
        Write-Host "    Start-Process '$ServerExe'" -ForegroundColor Yellow
    }
} else {
    Write-Host "  Serveur NON demarre." -ForegroundColor Cyan
    Write-Host "  Pour tester : lance le serveur puis connecte-toi avec ton client ARK." -ForegroundColor Cyan
    Write-Host ""
    Write-Host "    '$ServerExe'" -ForegroundColor Yellow
    Write-Host "    ou via Steam : 'ARK: Survival Ascended Dedicated Server' -> Jouer" -ForegroundColor Yellow
    Write-Host "    Client ARK : Join ARK -> Unofficial Servers -> filtre Local" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Deploy termine ===" -ForegroundColor Green
Write-Host "Logs serveur : $ServerPath\ShooterGame\Saved\Logs\ArkApi.log" -ForegroundColor DarkGray
