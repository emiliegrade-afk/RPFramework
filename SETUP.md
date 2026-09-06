# SETUP — Guide d'installation complet

Ce guide décrit l'installation de **tous** les prérequis nécessaires pour compiler
et charger le plugin **RPFramework** sur un serveur ARK: Survival Ascended (ASA).

## 1. Prérequis

| Outil | Nécessaire | Détail |
|-------|-----------|--------|
| Git | ✅ | Pour cloner AsaApi et versionner le projet |
| Visual Studio 2022 Build Tools | ✅ | Avec le composant MSVC **14.39.33519** (obligatoire pour AsaApi) |
| vcpkg | ✅ | Pour la dépendance `fmt` (utilisée par AsaApi) |
| Serveur ASA + AsaApi | ✅ (test) | Pour charger et tester le plugin |

> ⚠️ **Version du compilateur** : AsaApi exige la version exacte
> **MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.39-17.9)** (= `14.39.33519`).
> Une version plus récente peut produire une DLL incompatible avec le serveur.

## 2. Installer Git

- Télécharger : <https://git-scm.com/download/win>
- Installer avec les options par défaut.

## 3. Installer Visual Studio 2022 Build Tools

1. Télécharger le **Build Tools pour Visual Studio 2022** :
   <https://visualstudio.microsoft.com/fr/downloads/#build-tools-for-visual-studio-2022>
2. Lancer l'installeur et sélectionner la charge de travail **« Développement Desktop en C++ »**.
3. Dans l'onglet **« Composants individuels »**, cocher **impérativement** :
   - **`MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.39-17.9)`**
   - Un **Windows SDK** récent (ex. `Windows 11 SDK` ou `Windows 10 SDK`)
4. Installer.

> 💡 Vérifier que le composant `14.39` est bien présent dans
> `C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\14.39.*\`

## 4. Installer vcpkg

1. Cloner vcpkg dans un dossier stable (ex. `C:\vcpkg`) :
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   ```
2. Intégrer vcpkg à Visual Studio :
   ```powershell
   .\vcpkg integrate install
   ```
3. Le projet `RPFramework.vcxproj` utilise le **mode manifest** (`VcpkgEnableManifest`)
   et `vcpkg.json` : `fmt` sera donc installé automatiquement à la compilation.

## 5. Préparer le projet (setup.ps1)

Depuis le dossier `RPFramework` :

```powershell
.\setup.ps1
```

Ce script :
- vérifie Git et MSVC ;
- clone **AsaApi** dans `extern\AsaApi\` ;
- vérifie la présence d'`AsaApi.lib`.

## 6. Obtenir AsaApi.lib

Le plugin doit se lier à `AsaApi.lib`. Deux options :

### Option A — Compiler AsaApi soi-même
1. Ouvrir `extern\AsaApi\AsaApi.sln` avec Visual Studio.
2. Config **Release** / plateforme **x64**.
3. Compiler.
4. Copier `AsaApi.lib` depuis le dossier de sortie vers `extern\AsaApi\out_lib\`.

### Option B — Télécharger une release
1. Télécharger une release AsaApi :
   <https://ark-server-api.com/resources/asa-server-api.31/>
   (ou <https://github.com/ArkServerApi/AsaApi/releases>).
2. Décompresser.
3. Copier `AsaApi.lib` (dossier `Lib\`) vers `extern\AsaApi\out_lib\` (créer le dossier si besoin).

> Il faut que la version d'AsaApi téléchargée corresponde au code présent dans
> `extern\AsaApi\` (submodule) pour éviter les incompatibilités.

## 7. Compiler le plugin

```powershell
.\build.ps1
```

La sortie se trouve dans `out\` :
- `RPFramework.dll` → le plugin
- `RPFramework.dll.arkapi` → copie pour le rechargement à chaud
- `RPFramework.pdb` → symboles de débogage

## 8. Déployer sur le serveur

1. Créer le dossier du plugin sur le serveur :
   ```
   ShooterGame\Binaries\Win64\ArkApi\Plugins\RPFramework\
   ```
2. Y copier :
   - `RPFramework.dll`
   - `configs\PluginInfo.json`
   - `configs\config.json`
3. (Optionnel) copier `RPFramework.pdb` au même endroit.

Le serveur ASA doit avoir **AsaApi** installé et le
[x64 MSVC Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) à jour.

## 9. Vérifier

Au démarrage du serveur, vérifier dans les logs AsaApi
(`ShooterGame\Win64\logs\ArkApi.log`) les lignes :
- `RPFramework v0.1.0 : chargement...`
- `RPFramework v0.1.0 initialisé (serveur prêt).`

## 10. Rechargement à chaud (optionnel)

Une fois le plugin chargé, déposer `RPFramework.dll.arkapi` dans le dossier du
plugin : AsaApi rechargera automatiquement le plugin et renommera le fichier en
`RPFramework.dll` (configurable dans le `config.json` d'AsaApi).

## 11. Boucle de dev "solo" (serveur dédié local)

Pour itérer rapidement sans te connecter à un serveur distant, installe le tool
**"ARK: Survival Ascended Dedicated Server"** via Steam (gratuit, ~30 GB) puis
configure le chemin :

```powershell
[Environment]::SetEnvironmentVariable('ARKSV_PATH',
  'C:\Program Files (x86)\Steam\steamapps\common\ARK Survival Ascended Dedicated Server',
  'User')
```

Le script `deploy.ps1` se charge ensuite de tout :

```powershell
.\deploy.ps1              # build + deploy + hot reload AsaApi
.\deploy.ps1 -NoBuild      # deploy seul (iteration rapide)
.\deploy.ps1 -Restart      # force un restart complet du serveur
.\deploy.ps1 -WhatIf       # affiche ce qui serait fait, sans rien modifier
```

Le serveur n'a **pas besoin d'être arrêté** : AsaApi détecte le nouveau
`.dll.arkapi` déposé et recharge le plugin à chaud. Lance le serveur dédié
une fois, connecte-toi avec ton client ARK normal (Join ARK → Unofficial
Servers → Local), et itère. Logs serveur :
`%ARKSV_PATH%\ShooterGame\Saved\Logs\ArkApi.log`.
