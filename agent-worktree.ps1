# ============================================================================
# RPFramework - Isolation des chantiers parallelises
#
# Probleme resolu : plusieurs agents travaillant dans LE MEME repertoire
# s'ecrasent mutuellement (dernier ecrivain gagne, silencieusement) et leurs
# builds concurrents se battent pour le meme vc143.pdb (error C1041).
#
# Solution : un worktree git par chantier. Chaque agent a son propre
# repertoire, sa propre branche et son propre dossier out\, donc :
#   - aucune ecriture croisee sur les sources, les .vcxproj ou config.json ;
#   - aucune collision de build ;
#   - un diff propre par chantier, mergeable et annulable.
#
# extern\AsaApi (~1,2 Go) et vcpkg_installed sont ignores par git : ils sont
# partages avec le worktree principal via une jonction de repertoire, pour ne
# pas les dupliquer.
#
# Usage :
#   .\agent-worktree.ps1 -New A1              cree ..\RPFramework-A1
#   .\agent-worktree.ps1 -New A1,A2,A3,A4     cree les quatre d'un coup
#   .\agent-worktree.ps1 -List
#   .\agent-worktree.ps1 -Remove A1
# ============================================================================
[CmdletBinding()]
param(
    [string[]] $New,
    [switch]   $List,
    [string[]] $Remove,
    # Branche de depart des nouveaux worktrees (defaut : HEAD courant).
    [string]   $From
)

$ErrorActionPreference = "Stop"
$repo = $PSScriptRoot
$parent = Split-Path $repo -Parent
$shared = @("extern", "vcpkg_installed")

function Assert-Git {
    if (-not (Test-Path (Join-Path $repo ".git"))) {
        Write-Error "Pas de depot git dans $repo."
        exit 1
    }
}

function Assert-CleanTree {
    $dirty = & git -C $repo status --porcelain
    if ($dirty) {
        Write-Host ""
        Write-Host "Le worktree principal a des modifications non commitees." -ForegroundColor Yellow
        Write-Host "Commite-les d'abord : les nouveaux worktrees partent de HEAD," -ForegroundColor Yellow
        Write-Host "donc tout travail non commite ne sera PAS visible par les agents." -ForegroundColor Yellow
        Write-Host ""
        $answer = Read-Host "Continuer quand meme ? (o/N)"
        if ($answer -ne "o") { exit 1 }
    }
}

function Link-Shared([string] $target) {
    foreach ($dir in $shared) {
        $src = Join-Path $repo $dir
        if (-not (Test-Path $src)) { continue }
        $dst = Join-Path $target $dir
        if (Test-Path $dst) { continue }
        New-Item -ItemType Junction -Path $dst -Target $src | Out-Null
        Write-Host "  jonction $dir -> $src" -ForegroundColor DarkGray
    }
}

Assert-Git

if ($List) {
    & git -C $repo worktree list
    exit 0
}

if ($Remove) {
    foreach ($id in $Remove) {
        $path = Join-Path $parent "RPFramework-$id"
        # Retirer les jonctions AVANT de supprimer, pour ne jamais risquer
        # une suppression recursive dans extern\ du worktree principal.
        foreach ($dir in $shared) {
            $j = Join-Path $path $dir
            if (Test-Path $j) {
                [System.IO.Directory]::Delete($j, $false)
                Write-Host "  jonction $dir retiree" -ForegroundColor DarkGray
            }
        }
        & git -C $repo worktree remove $path --force
        Write-Host "Worktree $id supprime (la branche chantier/$id est conservee)." -ForegroundColor Green
    }
    exit 0
}

if (-not $New) {
    Write-Host "Usage : .\agent-worktree.ps1 -New A1[,A2,...] | -List | -Remove A1"
    exit 0
}

Assert-CleanTree
$base = if ($From) { $From } else { (& git -C $repo rev-parse --abbrev-ref HEAD).Trim() }

foreach ($id in $New) {
    $branch = "chantier/$id"
    $path = Join-Path $parent "RPFramework-$id"

    if (Test-Path $path) {
        Write-Host "$path existe deja, ignore." -ForegroundColor Yellow
        continue
    }

    Write-Host ""
    Write-Host "=== Chantier $id ===" -ForegroundColor Cyan
    & git -C $repo worktree add -b $branch $path $base
    if ($LASTEXITCODE -ne 0) { Write-Error "Creation du worktree $id echouee."; exit 1 }
    Link-Shared $path
    Write-Host "Pret : $path (branche $branch, depuis $base)" -ForegroundColor Green
}

Write-Host ""
Write-Host "Donne a chaque agent le chemin de SON worktree, jamais celui du" -ForegroundColor Cyan
Write-Host "repertoire principal. Fusion ensuite depuis le principal :" -ForegroundColor Cyan
Write-Host "  git merge --no-ff chantier/A2   (puis A1, A3, A4)" -ForegroundColor Cyan
Write-Host "  .\build.ps1 ; .\out\tests\RPFramework.Tests.exe" -ForegroundColor Cyan
