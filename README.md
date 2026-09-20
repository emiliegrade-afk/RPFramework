# RPFramework

Framework **RPG / RP générique** pour serveurs **ARK: Survival Ascended**, et
socle d'un mod RPG médiéval-fantastique.

> **« Construire le moteur une seule fois. Laisser chaque serveur construire son propre monde au-dessus. »**

Le plugin fournit le **moteur et les règles**. Le créateur du serveur fournit le **contenu** (races, métiers, classes, factions, quêtes, kits, économie) via la configuration, sans recompiler le code C++.

- **Technologie** : C++ / plugin serveur ASA (via [AsaApi](https://github.com/ArkServerApi/AsaApi))
- **Fonctionnement** : 100 % côté serveur ; le contenu visuel viendra d'un mod DevKit
- **Spécification** : `RPFramework — GDD - Spécification technique v0.1.md`
  — Partie I (§1–34) le moteur, Partie II (§35–51) la couche gameplay RPG
- **Plan de travail** : `ROADMAP.md` (chantiers parallélisables)

## Statut actuel

**Moteur livré** : phases 0 à 9 (GDD §30). **Vagues 1–5 livrées** (clé
blueprint, PlayerData v4, recettes, effets, progression métier, marchands,
câblage craft→XP→engram, canal console `rpf`, compétences, buffs ARK,
cuisine/alchimie, standing / relations / prix marchand).
**279 tests, 1549 EXPECT, 0 failure.**
**Reste :** crime à témoin + accès lieux + gardes hostiles (vague 6 / E2–E3,
voir `ROADMAP.md`) ; UI DevKit / PNJ (phase 20).

## Architecture : C++ vs DevKit

Décision structurante, documentée en GDD §36 : **un mod ASA ne peut pas
embarquer de C++.** Le DevKit ne compile pas de code — les mods sont
*content-only* (Blueprints et assets), le cook passe par CurseForge et le
serveur ARK ne charge pas de DLL de mod. Le seul moyen d'exécuter du code natif
côté serveur est une DLL AsaApi, donc ce repo.

Conséquences pratiques :

- **C++ = cerveau** : règles, progression, effets, économie, déblocages,
  transactions, persistance, audit.
- **DevKit = corps** : ateliers, objets, ressources, recettes, nourriture,
  potions, visuels, UI, PNJ.
- AsaApi est **côté serveur uniquement** : les joueurs, consoles incluses,
  n'installent que le mod CurseForge.
- Le coût d'AsaApi est la maintenance aux patchs ARK. On le contient en gardant
  la surface ARK minimale : **toute dépendance à `API/ARK/Ark.h` vit dans
  `src/Asa/`** et expose un stub `#ifdef RPFRAMEWORK_TESTS` (modèle
  `Asa/PawnEffects.h`), pour que le reste se teste hors serveur.
- Pour le lien mod → plugin, préférer une **commande console** (ne dépend
  d'aucun offset, survit aux patchs) plutôt qu'un hook. Les hooks restent
  réservés au gameplay vanilla. Voir GDD §48.

### Phase 1 — Core
- cycle de vie du plugin (`Plugin_Init` / `Plugin_Unload`) piloté par `rpframework::core::PluginContext`
- journalisation typée (`LogInfo` / `LogWarn` / `LogError` / `LogDebug`) via spdlog/AsaApi
- configuration centralisée, thread-safe : `Root()` / `Get()` renvoient une copie sous lock
- chemins filesystem centralisés (plugin, config, logs, audit)
- versioning du framework + version de schéma de config

### Phase 2 — Security (GDD §16-19)
- **Permissions** : 6 niveaux (`PLAYER` < `MODERATOR` < `GM` < `ADMIN` < `OWNER` < `SYSTEM`), clés baked-in, surchargeable via config. Clé inconnue → OWNER (fail-closed). `SYSTEM` clampé à OWNER pour un joueur.
- **RateLimiter** : sliding window par (joueur, action). Appliqué **après** les validations métier (id inconnu, déjà choisi, etc.)
- **Validator** : validation composable (NotEmpty, MaxLength, InRange, OneOf, ids alnum/`_`/`-` max 64)
- **AuditLog** : ring buffer mémoire + flush JSON-lines vers `logs/audit.log`, rotation size/count. `Recent()` survit au flush.
- **Identité** : FNV-1a 64-bit sur `GetUniqueNetIdAsString` uniquement. Échec → `PlayerId == 0`, le hook ignore. **Pas de fallback pointeur.**

### Phase 3 — Data (GDD §20)
- `PlayerData` (identité, character, progression, réputation, titres, unlocks, wallets, quêtes, flag kit)
- `PlayerStore` : fichier par joueur, écriture atomique, backups rotatifs, recovery si corruption, `recursive_mutex` + `ExclusiveLock` sur le cycle Load-mutate-Save
- `save_dir` refuse `..` et UNC
- Hook `HandleNewPlayer` → load/create (nom via `GetCharacterName`) ; `Logout` → save (skip si pid == 0)

### Phase 4 — Character (GDD §4-8)
- `Race`, `Profession`, `CharClass` data-driven (`config.character.*`)
- `SelectionCondition` (level, races/métiers/classes requis/exclus, `min_reputation`)
- `SelectRace` pose `Race.faction` + `initialReputation` ; refus si la faction actuelle exclut la race
- `EffectiveStats` : combine race+prof+class
- Sélection one-shot, pipeline permission → rate-limit → conditions → save → audit → kit

### Phase 5 — Loadouts (GDD §9)
- `Item { id, quantity, quality, extras }` — un `blueprint` JSON racine est copié dans `extras.blueprint`
- `Composer` : fusion commun + race + métier + classe
- `Distributor` : flag `starterKitDelivered` idempotent, puis `GiveItem` ASA si blueprint (ou `id` chemin `/Game/...`)
- `config.json` livre des blueprints vanilla (viande cuite, gourde, torche). Sans blueprint : kit RP-only.

### Phase 6 — Factions (GDD §13)
- `Faction` / `Rank` data-driven, zéro faction baked-in
- `Join` / `Leave`, `Get/Set/ModifyReputation`, `GetCurrentRank` (exige `data.faction == factionId`)
- Exclusions race/métier/classe, une faction à la fois
- Phase 6b (E1) : paliers de standing, matrice `relations` (un saut),
  marchands `faction` + multiplicateur / refus de trade
- Phase 6c (E2/E3, à faire) : crime à témoin (« pas vu, pas pris »),
  `CanEnter` / `IsHostileTo` pour lieux et gardes

### Phase 7 — Economy (GDD §14)
- Devises data-driven, wallets `int64`, soldes négatifs rejetés au load
- `Add` / `Subtract` / `Transfer` / `Grant` / `Reward` — toute mutation passe par `Wallet`
- Transfert : charge la cible avant débit ; SteamID ≥ 16 digits → `MakePlayerId`
- Historique V1 = audit JSONL (pas de livre dédié)

### Phase 8 — Quest Engine (GDD §10-12)
- Définitions data-driven, cycle start / progress / abandon / complete
- Récompenses : monnaie, XP (peut monter `level` via `xpPerLevel` sans jamais le baisser), réputation, titres, unlocks, items en attente
- Hooks monde : kill / tame / craft / harvest. Matching souple (`wild_boar` → `boar`, `*` / `any`, craft `item`, collect `harvest`)

### Phase 9 — Interface V1 (GDD §22)
- Commandes chat joueur : `/quest` `/quetes` `/race` `/profession` `/metier` `/class` `/classe` `/faction` `/reputation` `/economy` `/marchand` `/skill` `/competence`
- Commandes **modérateur** (`/mod` ou `/config`, niveau MODERATOR+) : `get` / `set` n’importe quel chemin de config, `list`, `kit add|clear`, `spawn`, `player`, `grant`, `rep` — persisté dans `config.json` et appliqué à chaud
- Erreurs en `FColorList::Red` ; `HandleCommand` avec le niveau réel du joueur
- Façade `rpframework::api::GetPlayerInfo` (DTO pour chat / RCON / futur AI Bridge)

### Branchement AsaApi
- `AShooterGameMode.BeginPlay()` → log « serveur prêt »
- `AShooterPlayerController.ServerSendChatMessage_Impl()` → Security **avant** l'envoi (longueur, permission, rate-limit)
- `AShooterGameMode.HandleNewPlayer_Implementation()` → profil + kit + GiveItem + catch-up quêtes
- `AShooterGameMode.Logout()` → save (skip pid 0)
- `APrimalDinoCharacter.Die(...)` → `ReportKill`
- `APrimalDinoCharacter.TameDino(...)` → `ReportTame`
- `AShooterPlayerController.ServerCraftItem_Implementation(...)` → `AllowCraft` avant le vanilla, puis quête `ReportCraft` **et** pipeline RPG (`Crafting::OnItemCrafted` : XP métier, level up, engrams)
- `AShooterPlayerController.HarvestedElement(...)` → `ReportGameplay` `{blueprint, slug, harvest}` si ressources données
- `AShooterPlayerController.ServerRequestInventoryUseItem_Implementation(...)` → `OnItemUsed` (plat / potion → effet RPG)

### Tests
- Projet `tests/RPFramework.Tests.vcxproj` (console, Release|x64)
- Compiler via `RPFramework.sln` (le `.vcxproj` tests seul n'a pas `SolutionDir`)
- Micro-framework dans `tests/TestHarness.h` : un nouveau chantier crée **son
  propre** `tests/Test_<Chantier>.cpp` plutôt que d'agrandir `TestMain.cpp`.
  `main()` exécute tous les tests enregistrés, quel que soit le fichier.
- Les deux `.vcxproj` contiennent des **ancres nommées** (`ANCRE-A1`,
  `ANCRE-TEST-A3`, …) : chaque chantier insère ses fichiers à son ancre, ce qui
  évite les conflits de merge entre agents travaillant en parallèle.
- Premier joueur connecté = OWNER (`security.owner_on_first_join`, persisté dans `owner.json`)
- Stats race/métier (buffs + debuffs) appliquées au pawn ; engrams de métier débloqués à la sélection
- Journal de quête donné à l’adhésion faction ; quêtes `starter_quests` auto-démarrées et auto-validées
- Spawn de race : `SelectRace` téléporte une fois (`spawnApplied`) vers `world.spawn_zones.{race}`
- Stock marchand runtime persisté dans `merchant_stock.json` (un `stock: 0` catalogue reste illimité)
- Compétences : `/skill unlock` dépense `skillPoints`, pose les recettes, applique l’effet lié
- Effets : `Apply` pose le buff ARK (`APrimalBuff::StaticAddBuff`), cooldown persisté, pas de tick C++
- Canal mod : commande console `rpf` (GDD §48 / D1)
- Cuisine : marmite / mortier, recettes herboriste, consommable → effet à l'utilisation (pas au craft)
- **279 tests, 1549 EXPECT, 0 failure** (`out\tests\RPFramework.Tests.exe`)

## Structure

```text
RPFramework/
├── RPFramework.sln              Solution VS (plugin + tests)
├── RPFramework.vcxproj          Plugin DLL x64, Release, C++20
├── tests/
│   ├── RPFramework.Tests.vcxproj
│   ├── TestHarness.h            Micro-framework (EXPECT / TEST / registre)
│   ├── TestMain.cpp             main() + tests du moteur
│   └── Test_*.cpp               Un fichier par chantier
├── src/
│   ├── Main.cpp                 Glue AsaApi (hooks + commandes chat)
│   ├── Asa/                     Identité UniqueNetId + hooks monde
│   ├── Api/                     Façade query (GetPlayerInfo)
│   ├── Core/                    Phase 1
│   ├── Security/                Phase 2
│   ├── Data/                    Phase 3
│   ├── Character/               Phase 4
│   ├── Loadout/                 Phase 5 (+ AsaDeliver GiveItem)
│   ├── Faction/                 Phase 6
│   ├── Economy/                 Phase 7 (+ Merchant, chantier B2)
│   ├── Quest/                   Phase 8 + routeur commandes Phase 9
│   ├── Crafting/                Recettes, ateliers, pipeline craft→XP (A3, C1)
│   ├── Effects/                 Buffs / debuffs + Apply ARK (A4, phase 18)
│   ├── Progression/             XP métier + dépense de compétences (B1, phase 17)
│   └── Mod/                     Canal console `rpf` (D1)
├── configs/
│   ├── PluginInfo.json
│   └── config.json
├── extern/AsaApi/
├── setup.ps1
├── build.ps1
├── ROADMAP.md
└── SETUP.md
```

## Démarrage rapide

1. Prérequis : VS 2022 Build Tools (MSVC 14.39.33519), Git, vcpkg — voir `SETUP.md`
2. `./setup.ps1` — clone AsaApi
3. Compiler AsaApi la première fois :
   ```powershell
   $msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
   & $msbuild extern\AsaApi\AsaApi.sln /p:Configuration=Release /p:Platform=x64 /m
   ```
4. `./build.ps1` — compile le plugin ET les tests
5. Lancer les tests : `out\tests\RPFramework.Tests.exe`
6. **Pour tester sur un serveur ASA local** (boucle de dev) :
   ```powershell
   # Une fois : set le chemin du serveur dédié ASA (installer le tool Steam
   # "ARK: Survival Ascended Dedicated Server" au préalable)
   [Environment]::SetEnvironmentVariable('ARKSV_PATH', 'C:\…\ARK Survival Ascended Dedicated Server', 'User')

   # Build + deploy + hot reload AsaApi (le serveur n'a pas besoin d'être arrêté)
   .\deploy.ps1

   # Lancer le serveur une fois :
   & "$env:ARKSV_PATH\ShooterGame\Binaries\Win64\ArkAscendedServer.exe"
   # ou via Steam : "ARK: Survival Ascended Dedicated Server" → Jouer
   # Client ARK : Join ARK → Unofficial Servers → filtre Local

   # Itération rapide (skip rebuild) :
   .\deploy.ps1 -NoBuild

   # Forcer un restart complet du serveur (par défaut, AsaApi recharge à chaud) :
   .\deploy.ps1 -Restart
   ```

## Pipeline Security (référence Phase 2/4)

```cpp
#include "Security/Permissions.h"
#include "Security/RateLimiter.h"
#include "Security/Validator.h"
#include "Security/AuditLog.h"

bool HandleRaceSelect(PlayerId player, Level playerLevel, const std::string& raceId)
{
    if (!Permissions::Check(playerLevel, "race.select")) {
        AuditLog::LogDenied("race.select", player, "permission");
        return false;
    }
    if (!RateLimiter::Allow(player, "race.select")) {
        AuditLog::LogDenied("race.select", player, "rate_limit",
            {{"retry_in_sec", RateLimiter::SecondsUntilNext(player, "race.select")}});
        return false;
    }
    auto v = Validator::All({
        Validator::NotEmpty(raceId, "race_id"),
        Validator::MaxLength(raceId, 64, "race_id"),
    });
    if (!v.valid) {
        AuditLog::LogDenied("race.select", player, "validation", v.errorContext);
        return false;
    }
    AuditLog::Log("race.select", player, {{"race_id", raceId}});
    return true;
}
```

## Phases (numérotation GDD §30)

| Phase | Contenu | Statut |
|-------|---------|--------|
| 0 | Environnement (VS 2022, AsaApi, vcpkg) | ✅ |
| 1 | Core | ✅ |
| 2 | Security Core | ✅ |
| 3 | Player Data | ✅ |
| 4 | Character | ✅ |
| 5 | Loadouts (compose + GiveItem si blueprint) | ✅ |
| 6 | Factions | ✅ |
| 6b | Standing, relations, prix marchand (E1) | ✅ |
| 7 | Economy (historique = audit JSONL) | ✅ |
| 8 | Quest Engine + hooks monde | ✅ |
| 9 | Interface V1 (commandes chat) | ✅ |
| 10 | AI Bridge | ⏳ hors V1 — `GetPlayerInfo` prêt |
| 11 | DevKit (UI, PNJ) | ⏳ voir phases 16 et 20 |

### Couche gameplay RPG (GDD §50, détail dans `ROADMAP.md`)

| Phase | Contenu | Statut |
|-------|---------|--------|
| 12 | Fondations : clé blueprint, PlayerData v4, recettes, effets (données) | ✅ |
| 13 | Progression métier (XP, niveaux, points de compétence) | ✅ |
| 14 | Câblage de l'artisanat (craft → XP → déblocage → engram) | ✅ |
| 15 | Économie jouable (marchands, achat / vente) | ✅ |
| 16 | Canal mod ↔ plugin (`rpf`) | ✅ |
| 17 | Arbre de compétences (dépense de points, prérequis, recettes) | ✅ |
| 18 | Effets appliqués (buffs ARK, cooldowns, stacking) | ✅ |
| 19 | Cuisine et alchimie (consommables → effets) | ✅ |
| 20 | UI RPG et PNJ | ⬜ |

### Dettes techniques connues (GDD §49)

1. ~~Entités identifiées par nom **localisé** dans `Asa/WorldHooks.cpp`~~ —
   **réglé (A1)** : clé blueprint canonique + alias slug.
2. ~~Niveau global dérivé de la courbe métier dans `Quest/Engine.cpp`~~ —
   **réglé (B1)** : XP métier séparée du niveau global.
3. Aucune boucle de tick dans le plugin — conditionne la conception des effets :
   les durées doivent venir de buffs ARK, pas d'un timer C++.
4. ~~Hook de récolte sans granularité (`"harvest"` constant)~~ —
   **réglé** : `HarvestedElement` envoie `{blueprint, slug, harvest}`. Le
   type exact via `FAttachedInstancedHarvestingElement` n'est toujours pas lu.
5. `Utf8ToFString` dupliqué dans `Loadout/AsaDeliver.cpp` — à mutualiser dans
   `Asa/`.
6. Application de stats permanente dans `Asa/PawnEffects.cpp` — inutilisable
   pour des effets temporaires.
