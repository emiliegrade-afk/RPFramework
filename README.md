# RPFramework

Framework **RPG / RP générique** pour serveurs **ARK: Survival Ascended**.

> **« Construire le moteur une seule fois. Laisser chaque serveur construire son propre monde au-dessus. »**

Le plugin fournit le **moteur et les règles**. Le créateur du serveur fournit le **contenu** (races, métiers, classes, factions, quêtes, kits, économie) via la configuration, sans recompiler le code C++.

- **Technologie** : C++ / plugin serveur ASA (via [AsaApi](https://github.com/ArkServerApi/AsaApi))
- **Fonctionnement** : 100 % côté serveur, sans DevKit en V1
- **Spécification** : voir `RPFramework — GDD - Spécification technique v0.1.md`

## Statut actuel

**Phases 1 à 7 livrées, testées et auditées :**

### Phase 1 — Core (socle modulaire)
- cycle de vie du plugin (`Plugin_Init` / `Plugin_Unload`) piloté par `rpframework::core::PluginContext`
- journalisation typée (`LogInfo` / `LogWarn` / `LogError` / `LogDebug`) via spdlog/AsaApi
- configuration centralisée, thread-safe, à accès par chemin-point
- chemins filesystem centralisés (plugin, config, logs, audit)
- versioning du framework + version de schéma de config

### Phase 2 — Security (socle de sécurité, GDD §16-19)
- **Permissions** : 6 niveaux (`PLAYER` < `MODERATOR` < `GM` < `ADMIN` < `OWNER` < `SYSTEM`), ~30 clés baked-in, surchargeable via config
- **RateLimiter** : sliding window par (joueur, action), configurable par action
- **Validator** : validation composable (NotEmpty, MaxLength, InRange, OneOf, …)
- **AuditLog** : ring buffer mémoire + flush JSON-lines vers `logs/audit.log`, avec **rotation** (size + count configurables)

### Phase 3 — Data (persistance joueur, GDD §20)
- `PlayerData` struct (identity, character, progression, reputation, titres, loadout flag)
- `PlayerStore` : fichier par joueur, écriture atomique (.tmp + rename), backups rotatifs (.bak.1 → .bak.N), **recovery depuis backup** si fichier principal corrompu (avec mise en quarantaine `.corrupt.<timestamp>`), migration versionnée
- Hook `AShooterGameMode_HandleNewPlayer` → load/create
- Hook `AShooterGameMode_Logout` → save

### Phase 4a — Character (GDD §4-8)
- `Race`, `Profession`, `CharClass` data-driven, chargées depuis `config.character.*`
- `SelectionCondition` (level, races/professions/classes requises/exclues, `min_reputation` par faction)
- `Registry` : Get/Has/List, `ClassesEnabled` (système de classes désactivable)
- `EffectiveStats` : combine race+prof+class avec convention "1er Multiply → init 1.0"
- `SelectRace/Profession/Class` : pipeline permission → rate limit → load (auto-création) → one-shot → conditions → save → audit

### Phase 4b — Loadout (GDD §9)
- `Item { id, quantity, quality, extras }` + JSON parse
- `Composer` : fusion **commun + race + profession + class** (additions qtés, dernier quality wins)
- `Distributor` : distribution idempotente (`starterKitDelivered` flag), audit traçable
- Intégré dans `HandleNewPlayer` : un nouveau joueur reçoit son kit à la première connexion

### Phase 5 — Factions (GDD §13)
- `Faction { id, name, ranks[], joinCondition, excludedRaces/Professions/Classes, initialReputation }` + `Rank { id, minReputation, benefits }` data-driven, chargées depuis `config.factions.*`
- `Registry` : Get/Has/List, **zéro faction baked-in** (cohérent avec Character)
- Réutilise `Character::SelectionCondition` pour les conditions d'adhésion (dépendance à sens unique Faction → Character, pas de cycle)
- `Reputation` API : `Get/Set/ModifyReputation` + `GetCurrentRank` (le rang le plus élevé dont `min_reputation` est atteint)
- `Join/Leave` : pipeline Security complet (permission → rate limit → conditions → exclusions → save → audit). **One faction at a time** par joueur (modèle Phase 5)
- Statuts `JoinStatus` typés : `Success / AlreadyInFaction / AlreadyInAnotherFaction / UnknownFaction / ConditionNotMet / RaceExcluded / ProfessionExcluded / ClassExcluded / RateLimited / PermissionDenied / PlayerDataUnavailable`
- Audits : `faction.join`, `faction.leave`, `faction.reputation.modify`, `faction.reputation.set`
- Permissions baked-in : `faction.view` (PLAYER), `faction.join` (PLAYER), `faction.leave` (PLAYER), `faction.create` (ADMIN), `faction.modify_reputation` (GM)
- Rate limits baked-in : `faction.join` (3/h), `faction.leave` (3/h)

### Phase 6 — Economy (GDD §14)
- `Currency { id, name, symbol, maxBalance, transferable }` data-driven, chargées depuis `config.economy.currencies.*` (zéro monnaie baked-in)
- `Registry` : Get/Has/List
- `Wallet` API centrale : `GetBalance`, `Add`, `Subtract`, `Transfer`, `Grant`, `Reward` — **toute modification de solde DOIT passer par là** (GDD §14 : « le code ne doit pas modifier arbitrairement le solde »)
- Soldes stockés dans `PlayerData.wallets` (map<currency_id, int64>), persistés automatiquement
- Migration `v1 → v2` : ajout de la section `economy` aux fichiers joueurs existants
- Statuts `TxStatus` typés : `Success / UnknownCurrency / InvalidAmount / InsufficientFunds / WouldExceedMax / CurrencyNotTransferable / PlayerDataUnavailable / RateLimited / PermissionDenied`
- Transfert peer-to-peer avec **gestion d'erreur défensive** : si la cible ne peut pas recevoir (max, save), l'émetteur est remboursé. Échec partiel logué en `economy.transfer_partial`
- Audits : `economy.add`, `economy.subtract`, `economy.transfer`, `economy.grant`, `economy.reward`, + `*_denied` sur refus
- Permissions baked-in : `economy.view` (PLAYER), `economy.transfer` (PLAYER), `economy.grant` (GM), `economy.add/subtract/reward` (SYSTEM)
- Rate limits baked-in : `economy.transfer` (10/h), `economy.grant` (50/h)

### Phase 7 — Quest Engine (GDD §10-12)
- définitions data-driven : métadonnées, source, prérequis, niveau et objectifs
- registre thread-safe avec chargement/rechargement depuis `config.quests.*`
- cycle de vie serveur : démarrer, abandonner, progresser et terminer une quête
- progression persistée par joueur, objectifs obligatoires et récompenses idempotentes
- récompenses intégrées : monnaie, XP, réputation et titres
- audits `quest.start`, `quest.progress`, `quest.abandon` et `quest.complete`
- événements serveur typés : kill, collecte, tame, craft, livraison, exploration,
  interaction, progression métier/réputation et quête terminée

### Branchement AsaApi (4 hooks actifs)
- `AShooterGameMode_BeginPlay` → log "serveur prêt"
- `AShooterPlayerController_ServerSendChatMessage_Impl` → pipeline Security (chat)
- `AShooterGameMode_HandleNewPlayer_Implementation` → load/create PlayerData + distrib kit
- `AShooterGameMode_Logout` → save
- `ExtractPlayerId` : FNV-1a 64-bit sur `GetUniqueNetIdAsString`, fallback `reinterpret_cast<pc>`

### Tests
- Projet séparé `tests/RPFramework.Tests.vcxproj` (console, Release|x64)
- **106 tests, 535 EXPECT, 0 failure, exit 0**
- Couverture par phase :
  - **Phase 1-2 (Core + Security)** : 24 tests (Permissions, RateLimiter, Validator, AuditLog, PluginContext, identity hashing)
  - **Phase 3 (Data)** : 7 PlayerStore (round-trip, recovery, migration, identity, backup floor) + 10 tests `Audit_*` (Config/Paths/Version round-trip, Security accessors, Migration v0→current + v2→v3, PlayerData round-trip avec unlocks)
  - **Phase 4a (Character)** : 16 tests (Registry, Select, Stats, edge cases : reputation, cross-refs, classes_enabled, robustness)
  - **Phase 4b (Loadout)** : 4 tests (Item, Merge, Compose, Distributor)
  - **Phase 5 (Faction)** : 10 tests (Registry, Reputation default/persist, Join success/already/unknown/condition/race, Leave, GetCurrentRank)
  - **Phase 6 (Economy)** : 14 tests (Registry, GetBalance default, Add success/unknown/invalid, Subtract success/insufficient, Transfer success/non-transferable/self, Grant, Reward, WouldExceedMax, Migration v1→v3)
  - **Phase 7 (Quest)** : 14 tests (Registry load + dédup, Engine progress+reward, Events routing, Start condition, Completion any/expired/invalid-rewards, Item reward delivery, **Per-id confirm**, **Batched xp+title+item+unlock**, Available filtering)
  - **Phase 8 (Interface)** : 4 tests Commands (routeur core actions, livraison multi-modules, admin reload requires OWNER, **/reputation rep+rank**)
  - **Intégration** : 1 end-to-end (new player → select → loadout → audit)
  - **Robustesse** : 5 (config missing/corrupt, file recovery, delete nonexistent, log before init)
  - **Total** : 24 + 7 + 10 + 16 + 4 + 10 + 14 + 14 + 4 + 1 + 5 = **106** ✓

## Structure

```text
RPFramework/
├── RPFramework.sln              Solution VS (plugin + tests)
├── RPFramework.vcxproj          Plugin DLL x64, Release, C++20
├── tests/
│   ├── RPFramework.Tests.vcxproj    Console app x64
│   └── TestMain.cpp            (102 tests, 485 EXPECT)
├── src/
│   ├── Main.cpp                 Glue AsaApi : 4 hooks
│   ├── Core/                    Phase 1
│   │   ├── Version.h / .cpp
│   │   ├── Paths.h / .cpp
│   │   ├── Logger.h / .cpp
│   │   ├── Config.h / .cpp
│   │   └── PluginContext.h / .cpp
│   ├── Security/                Phase 2
│   │   ├── Types.h
│   │   ├── Permissions.h / .cpp
│   │   ├── RateLimiter.h / .cpp
│   │   ├── Validator.h / .cpp
│   │   └── AuditLog.h / .cpp
│   ├── Data/                    Phase 3
│   │   ├── PlayerData.h / .cpp
│   │   ├── PlayerStore.h / .cpp
│   │   └── Migration.h / .cpp
│   ├── Character/               Phase 4a
│   │   ├── Character.h
│   │   ├── Definitions.h / .cpp
│   │   ├── Registry.h / .cpp
│   │   ├── Stats.h / .cpp
│   │   └── Select.h / .cpp
│   ├── Loadout/                 Phase 4b
│   │   ├── Item.h / .cpp
│   │   ├── Compose.h / .cpp
│   │   └── Distribute.h / .cpp
│   └── Faction/                 Phase 5
│       ├── Definitions.h / .cpp
│       ├── Registry.h / .cpp
│       ├── Reputation.h / .cpp
│       └── Join.h / .cpp
│   └── Economy/                 Phase 6
│       ├── Definitions.h / .cpp
│       ├── Registry.h / .cpp
│       └── Wallet.h / .cpp
├── configs/
│   ├── PluginInfo.json          Métadonnées lues par AsaApi
│   └── config.json              character.*, loadout.*, security.* surchargeables
├── extern/AsaApi/               API serveur ASA + AsaApi.dll
├── setup.ps1
├── build.ps1
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
5. Déployer `out\RPFramework.dll` + `configs\*` dans `ShooterGame\Binaries\Win64\ArkApi\Plugins\RPFramework\`
6. Lancer les tests : `out\tests\RPFramework.Tests.exe`

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

## Prochaines phases (voir GDD)

| Phase | Contenu | Statut |
|-------|---------|--------|
| 1 | Core complet | ✅ |
| 2 | Security Core | ✅ |
| 3 | Player Data (persistance, backups, migration, récupération) | ✅ |
| 4 | Character + Loadout (races, métiers, classes, kits) | ✅ |
| 5 | Factions (réputation, rangs, conditions) | ✅ |
| 6 | Economy (monnaie, transactions, grants) | ✅ |
| 7 | Quest Engine | ✅ |
| 8 | Interface V1 (chat commands `/race`, `/metier` visibles) | ✅ |
| 9 | AI Bridge | ⏳ |
| 10 | DevKit futur | ⏳ |
