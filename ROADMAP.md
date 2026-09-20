# RPFramework — Roadmap d'exécution

Plan de travail pour transformer le moteur livré (GDD §1–34) en **mod RPG
jouable** (GDD §35–51). Ce document est opérationnel : il découpe le travail en
chantiers parallélisables, avec les contrats d'interface figés à l'avance.

- Spécification : `RPFramework — GDD - Spécification technique v0.1.md`, Partie II
- État du moteur : `README.md`

---

## Principe directeur

**Le DevKit n'est pas sur le chemin critique.** Le prototype entier tourne sur
des assets vanilla (minerai de métal, Refining Forge, lingot, Smithy, épée en
métal) et la config utilise déjà ce format de chemins blueprint. Le DevKit
servira à **reskiner une boucle déjà prouvée**, pas à la construire.

Objectif de la première manche, en une phrase :

> En jeu, forger une épée vanilla fait monter mon niveau de forgeron, et le
> niveau 2 me débloque un nouvel engram.

Le jour où ça marche, l'architecture est validée et le reste n'est plus que du
contenu.

---

## Vue d'ensemble des vagues

```text
VAGUE 1 (4 agents en parallèle — aucun fichier source partagé)
  A1  Clé blueprint canonique + alias d'événements
  A2  PlayerData v4 : progression par métier
  A3  Registre de recettes (data layer)
  A4  Registre d'effets (data layer)
        ↓
VAGUE 2 (2 agents en parallèle)
  B1  Module Progression (dépend de A2)
  B2  Marchands + achat/vente (dépend de A1)
        ↓
VAGUE 3 (1 agent seul — câblage)
  C1  Craft → recette → XP métier → déblocage → engram
        ↓
VAGUE 4 (toi, dans le DevKit)
  D1  Canal commande console + première station custom
```

Dépendances réelles : A1–A4 sont mutuellement indépendants. B1 a besoin du
schéma v4 de A2. B2 a besoin de la convention de clé de A1. C1 a besoin de A1,
A3 et B1.

---

## Prérequis d'infrastructure

### 0. Isolation — un worktree par chantier (OBLIGATOIRE)

C'est le seul point qui rende le parallélisme réellement sûr. Les quatre points
suivants réduisent les conflits de merge ; celui-ci empêche la perte de travail.

Deux agents dans **le même répertoire** produisent deux pannes distinctes, et
les deux ont été observées :

- **Écrasement silencieux.** Le dernier écrivain gagne. Aucun avertissement,
  aucune trace : le travail de l'autre disparaît simplement. Un agent qui
  réécrit un fichier entier efface les modifications faites entre sa lecture et
  son écriture.
- **Interblocage de build.** Deux `MSBuild` sur la même solution écrivent dans
  le même `out\...\vc143.pdb` et échouent avec `error C1041`. Cette erreur ne
  désigne aucun défaut de code, mais elle pousse les agents à « corriger » du
  code sain — et à s'écraser encore davantage en le faisant.

Procédure :

```powershell
# Depuis le worktree principal, travail en cours commité :
.\agent-worktree.ps1 -New A1,A2,A3,A4
```

Chaque agent reçoit alors `..\RPFramework-A1`, sa branche `chantier/A1` et son
propre `out\`. `extern\` et `vcpkg_installed\` sont partagés par jonction, donc
rien n'est dupliqué. **Donne à l'agent le chemin de son worktree, jamais celui
du répertoire principal** : c'est la seule consigne qui compte, un agent ne
devine pas qu'il partage son répertoire.

### 1 à 4. Réduction des conflits de merge (fait)

1. **`tests/TestHarness.h`** — le micro-framework de test est extrait de
   `TestMain.cpp`. Chaque chantier crée **son propre** `tests/Test_<X>.cpp`
   plutôt que d'agrandir `TestMain.cpp`. Plus de conflit de merge sur le
   fichier de tests.
2. **Ancres nommées dans les deux `.vcxproj`** — chaque chantier a sa ligne de
   commentaire (`ANCRE-A1`, `ANCRE-TEST-A3`, …) où insérer ses fichiers. Des
   hunks non adjacents se mergent automatiquement.
3. **Points d'insertion assignés dans `configs/config.json`** — chaque chantier
   ajoute sa section à un endroit distinct du fichier (voir chaque brief).
4. **`core.autocrlf = false`** — sans ça, git réécrit les fins de ligne et
   fabrique des diffs entiers sur des fichiers intacts, ce qui transforme
   chaque merge en conflit artificiel.

Référence actuelle (post-vague 3 / C1 mergé) : `236 tests, 1301 EXPECT, 0 failure`.

---

## Règles communes à tous les chantiers

À rappeler dans chaque prompt d'agent :

- **Langue** : code et commentaires en français, comme le reste du repo.
- **Data-driven** : aucun contenu baked-in. Tout vient de `configs/config.json`.
  Une entrée invalide est **rejetée au chargement** avec un log d'erreur et
  n'apparaît pas dans le Registry (pattern `Character/Registry`).
- **Surface ARK minimale** : tout code dépendant de `API/ARK/Ark.h` vit dans
  `src/Asa/` et expose un stub `#ifdef RPFRAMEWORK_TESTS`, comme
  `Asa/PawnEffects.h`. Le reste doit compiler et se tester sans serveur.
- **Sécurité** : toute mutation joueur passe par le pipeline existant
  permission → rate limit → validation → load → mutation → save → audit.
  Ne jamais écrire dans `PlayerData.wallets` directement : passer par
  `Economy::Wallet`.
- **Persistance** : un changement de forme de `PlayerData` exige un bump de
  `kPlayerDataSchemaVersion` **et** une migration (voir `Data/Migration.h`).
- **Tests** : nouveau fichier `tests/Test_<Chantier>.cpp` incluant
  `TestHarness.h`. Aucune régression tolérée.
- **Build** : `.\build.ps1` puis `out\tests\RPFramework.Tests.exe`. Les deux
  doivent passer. Passer par `build.ps1` et **jamais** par `MSBuild` en direct :
  le script prend un verrou nommé qui sérialise les builds concurrents. Sans
  lui, deux builds simultanés échouent sur `error C1041` (base de données du
  programme verrouillée) — une erreur d'environnement, jamais de code.
- **En cas d'échec de build inexpliqué** : vérifier d'abord
  `Get-Process MSBuild,cl`. Si des processus tiers compilent, attendre — ne pas
  modifier du code en réaction à `C1041`, `MSB8027` ou un `.pdb` verrouillé.
- **Périmètre** : ne toucher **que** les fichiers listés dans le brief. Si un
  autre fichier semble devoir changer, le signaler dans le rapport final au
  lieu de le modifier.

---

# VAGUE 1

## A1 — Clé blueprint canonique + alias d'événements

**Pourquoi** : les entités sont aujourd'hui identifiées par un slug de nom
**localisé** (`GetItemName`, `GetDescriptiveName`), dépendant de la langue du
client et du préfixe de qualité. Inutilisable comme clé de recette. Voir GDD
§39 et dette n°1.

**Piège à ne pas reproduire** : remplacer purement le slug casse les quêtes
existantes. `Quest/Match.h` ne découpe que sur `_`, donc dans un chemin complet
`boar` n'est plus un token isolé et la quête `first_hunt` (`entity: "boar"`)
cesse de progresser. La solution est d'**ajouter** le blueprint comme clé
stricte et de **garder** le slug comme alias.

**Contrat figé** :

```cpp
// src/Asa/Blueprints.h   (n'inclut RIEN d'ARK ; forward-declare UObjectBase)
namespace rpframework::asa
{
    // Normalise un chemin brut vers le format de config.json :
    //   "Blueprint'/Game/X.Default__X_C'" → "/Game/X.X"
    // Pure, sans dépendance ARK : compilée dans le plugin ET dans les tests.
    std::string NormalizeBlueprintPath(std::string_view raw);

#ifdef RPFRAMEWORK_TESTS
    inline std::string BlueprintPathOf(class UObjectBase*) { return {}; }
#else
    std::string BlueprintPathOf(class UObjectBase* object);
#endif
}
```

```cpp
// src/Quest/Events.h — surcharge ; l'ancienne signature est conservée et
// délègue avec un alias unique.
// Convention : aliases[0] == chemin blueprint quand il est résolvable.
int ReportGameplay(PlayerId player, std::string_view type,
                   const std::vector<std::string>& aliases, int amount = 1,
                   std::vector<EventNotice>* notices = nullptr);
```

**Détail qui fait échouer silencieusement si on le rate** : `Quest/Events.cpp`
teste la correspondance, puis `AddProgress` **re-teste** de son côté
(`Quest/Engine.cpp`). Il faut donc mémoriser *quel* alias a matché et
transmettre **celui-là** à `AddProgress`.

**Fichiers** :
- créer `src/Asa/Blueprints.h`, `src/Asa/BlueprintPath.cpp` (pur),
  `src/Asa/Blueprints.cpp` (ARK, plugin uniquement)
- créer `tests/Test_Blueprints.cpp`
- modifier `src/Quest/Events.h`, `src/Quest/Events.cpp`
- modifier `src/Asa/WorldHooks.cpp` : les hooks kill / tame / craft construisent
  `{ BlueprintPathOf(obj), slugExistant }`. `ItemSlug` et `CharacterSlug` ne
  changent pas, ils descendent en deuxième position.
- ancres : `ANCRE-A1`, `ANCRE-A1-H` (plugin), `ANCRE-TEST-A1` +
  `BlueprintPath.cpp` dans le projet de tests

**Hors périmètre** : ne pas toucher `Hook_..._HarvestedElement` (la granularité
de récolte est la dette n°4, elle demande un autre hook et la boucle
d'artisanat n'en a pas besoin). Ne pas modifier `Quest/Match.h`.

**Réserve à vérifier** : `extern/AsaApi/` doit être présent pour connaître la
signature exacte de `GetFullName`. Si `AsaApi::GetApiUtils().GetBlueprint()`
existe dans `IApiUtils.h`, l'utiliser plutôt que de refaire la manipulation,
puis normaliser via `NormalizeBlueprintPath`.

**Terminé quand** : `Test_Blueprints.cpp` couvre la normalisation (wrapper
`Blueprint'…'`, `Default__`, suffixe `_C`, entrée vide) et prouve la
non-régression — `ReportGameplay(id, "kill", {"/Game/PrimalEarth/Dinos/Boar/Boar_Character_BP.Boar_Character_BP", "boar"})`
fait toujours avancer `first_hunt`.

---

## A2 — PlayerData v4 : progression par métier

**Pourquoi** : il n'existe qu'un couple `level` / `xp` global, alimenté
uniquement par les récompenses de quête. Aucune XP de métier. Voir GDD §38.

**Contrat figé** :

```cpp
// src/Data/PlayerData.h
struct ProfessionProgression
{
    std::string              professionId;
    int                      level = 1;
    int                      xp = 0;
    int                      skillPoints = 0;
    std::vector<std::string> unlockedSkills;
    std::vector<std::string> unlockedRecipes;

    nlohmann::json ToJson() const;
    static ProfessionProgression FromJson(const std::string& id,
                                          const nlohmann::json& j);
};

// dans PlayerData : profession_id → progression
std::unordered_map<std::string, ProfessionProgression> professions;
```

Sérialisation : nouvelle section `professions` dans le JSON, au même niveau que
`progression` / `reputation` / `titles`. Omise si vide, comme les autres.

**Contraintes non négociables** :
- `kPlayerDataSchemaVersion` **et** `kCurrentSchemaVersion` passent à `4` ;
- migration `Migrate_v3_to_v4` ajoutée à la chaîne de `Migrate()` : si
  `character.profession` est non vide, créer l'entrée correspondante en y
  recopiant `level` et `xp` existants, pour ne perdre aucun joueur ;
- le champ `profession` (string) est **conservé tel quel** : il est lu par
  `Character/Stats.cpp`, `Character/Select.cpp` et `Asa/PawnEffects.cpp`. Le
  supprimer casse les stats de pawn.
- un JSON corrompu ou des valeurs négatives doivent être rejetés comme
  aujourd'hui (`level < 1 || xp < 0`).

**Fichiers** :
- modifier `src/Data/PlayerData.h`, `src/Data/PlayerData.cpp`,
  `src/Data/Migration.h`, `src/Data/Migration.cpp`
- créer `tests/Test_PlayerDataV4.cpp`
- ancre : `ANCRE-TEST-A2`

**Hors périmètre** : **aucune logique de gain d'XP** (c'est B1). Ce chantier ne
livre que la forme de la donnée et sa migration. Ne pas toucher
`Quest/Engine.cpp`.

**Terminé quand** : un fichier joueur v3 existant se charge, se migre en v4 en
conservant son métier, son niveau et son XP, et se resauvegarde sans perte.
Round-trip `ToJson` / `FromJson` testé avec plusieurs métiers.

---

## A3 — Registre de recettes (data layer)

**Pourquoi** : les recettes sont le cœur de la boucle d'artisanat. Voir GDD §40.

**Pattern à copier** : `Character/Definitions.{h,cpp}` + `Character/Registry.{h,cpp}`.
Même structure, même style de validation, même comportement de rejet.

**Schéma de config figé** — section `"crafting"` à insérer dans
`configs/config.json` **immédiatement après le bloc `"quests"`** (donc entre
`quests` et `character`) :

```json
"crafting": {
  "stations": {
    "refining_forge": {
      "name": "Fonderie",
      "blueprint": "/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_Forge.PrimalItemStructure_Forge",
      "professions": ["blacksmith"]
    },
    "smithy": {
      "name": "Forge",
      "blueprint": "/Game/PrimalEarth/CoreBlueprints/Items/Structures/Misc/PrimalItemStructure_AnvilBench.PrimalItemStructure_AnvilBench",
      "professions": ["blacksmith"]
    }
  },
  "recipes": {
    "iron_ingot": {
      "name": "Lingot de fer",
      "station": "refining_forge",
      "profession": "blacksmith",
      "min_level": 1,
      "required_skill": "",
      "craft_time_sec": 0,
      "xp": 5,
      "output": {
        "blueprint": "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot",
        "quantity": 1
      },
      "ingredients": [
        { "blueprint": "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalOre.PrimalItemResource_MetalOre", "quantity": 2 }
      ]
    },
    "iron_sword": {
      "name": "Épée de fer",
      "station": "smithy",
      "profession": "blacksmith",
      "min_level": 1,
      "xp": 35,
      "output": {
        "blueprint": "/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword",
        "quantity": 1
      },
      "ingredients": [
        { "blueprint": "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_MetalIngot.PrimalItemResource_MetalIngot", "quantity": 8 }
      ]
    }
  }
}
```

**Contrat figé** — l'API dont le chantier C1 aura besoin :

```cpp
// src/Crafting/Registry.h
namespace rpframework::crafting
{
    void Load();                                 // depuis Core::Config
    std::optional<Recipe>  GetRecipe(const std::string& id);
    // Lookup par clé stricte du produit. Format = Asa::NormalizeBlueprintPath.
    std::optional<Recipe>  FindByOutputBlueprint(const std::string& blueprint);
    std::vector<Recipe>    ListRecipes();
    std::vector<Recipe>    ListForProfession(const std::string& professionId);
    std::optional<Station> GetStation(const std::string& id);
    bool HasRecipe(const std::string& id);
}
```

**Règles de validation** (rejet + log, la recette n'entre pas au Registry) :
`output.blueprint` non vide, `output.quantity > 0`, `station` référençant une
station déclarée, `profession` non vide, `min_level >= 1`, `xp >= 0`,
`craft_time_sec >= 0`, chaque ingrédient avec `blueprint` non vide et
`quantity > 0`. Les chemins blueprint sont stockés normalisés.

**Fichiers** :
- créer `src/Crafting/Definitions.h`, `src/Crafting/Definitions.cpp`,
  `src/Crafting/Registry.h`, `src/Crafting/Registry.cpp`
- créer `tests/Test_Crafting.cpp`
- modifier `configs/config.json` (section `crafting` uniquement, à l'endroit
  indiqué)
- ancres : `ANCRE-A3`, `ANCRE-A3-H` (plugin), `ANCRE-SRC-A3`,
  `ANCRE-TEST-A3` (tests)

**Hors périmètre** : aucun branchement sur un hook, aucune consommation de
ressources, aucun octroi d'XP. Ce chantier ne livre que les données et leur
validation. Ne pas toucher `Asa/`, `Quest/`, `Data/`. Ne pas appeler
`Asa::NormalizeBlueprintPath` (livré par A1, pas encore mergé) : dupliquer
temporairement une normalisation locale minimale et **le signaler dans le
rapport final** pour que C1 mutualise.

**Terminé quand** : `Test_Crafting.cpp` couvre le chargement depuis un JSON en
mémoire, le rejet de chaque règle de validation, le lookup par produit, et le
filtrage par métier.

---

## A4 — Registre d'effets (data layer)

**Pourquoi** : on veut un **moteur d'effets unique**, pas un système de buff par
source (nourriture, potion, poison, métier, skill). Voir GDD §44.

**Contrainte de conception à respecter absolument** : le plugin n'a **aucune
boucle de tick** — ni timer, ni thread, ni horloge. Les durées et l'expiration
ne sont donc **pas** gérées en C++ : un effet référence un **buff ARK**
(`buff_blueprint`) qui expire seul. Le C++ décide de l'octroi, des conditions,
du cooldown et du stacking.

**Schéma de config figé** — section `"effects"` à insérer dans
`configs/config.json` **immédiatement après le bloc `"character"`** (donc entre
`character` et `factions`) :

```json
"effects": {
  "hunter_stew": {
    "name": "Ragoût du chasseur",
    "type": "buff",
    "buff_blueprint": "",
    "duration_sec": 600,
    "stacking": "refresh",
    "max_stacks": 1,
    "cooldown_sec": 0,
    "modifiers": [
      { "target": "stamina", "op": "add", "value": 20.0 },
      { "target": "speed",   "op": "multiply", "value": 0.95 }
    ],
    "conditions": { "min_level": 1 }
  }
}
```

**Contrat figé** :

```cpp
// src/Effects/Definitions.h
enum class EffectType   : std::uint8_t { Buff = 0, Debuff = 1 };
enum class StackingMode : std::uint8_t { None = 0, Refresh = 1, Stack = 2 };

struct Effect
{
    std::string id;
    std::string name;
    EffectType  type = EffectType::Buff;
    std::string buffBlueprint;              // vide = effet purement logique
    int         durationSeconds = 0;        // 0 = permanent tant que la source dure
    StackingMode stacking = StackingMode::Refresh;
    int         maxStacks = 1;
    int         cooldownSeconds = 0;
    std::vector<character::StatModifier> modifiers;   // réutilise l'existant
    character::SelectionCondition        conditions;  // réutilise l'existant
};

// src/Effects/Registry.h — Load / GetEffect / ListEffects / HasEffect
```

Réutiliser `character::StatModifier` et `character::SelectionCondition` plutôt
que de redéfinir des types équivalents.

**Règles de validation** : `id` non vide, `type` dans {buff, debuff},
`stacking` dans {none, refresh, stack}, `max_stacks >= 1`,
`duration_sec >= 0`, `cooldown_sec >= 0`, au moins un modifier **ou** un
`buff_blueprint` non vide.

**Fichiers** :
- créer `src/Effects/Definitions.h`, `src/Effects/Definitions.cpp`,
  `src/Effects/Registry.h`, `src/Effects/Registry.cpp`
- créer `tests/Test_Effects.cpp`
- modifier `configs/config.json` (section `effects` uniquement, à l'endroit
  indiqué)
- ancres : `ANCRE-A4`, `ANCRE-A4-H` (plugin), `ANCRE-SRC-A4`,
  `ANCRE-TEST-A4` (tests)

**Hors périmètre** : **aucune application** d'effet, aucun appel ARK, aucune
modification de `Asa/PawnEffects.cpp`. L'application est la phase 18. Ne pas
introduire de timer, de thread ni de `steady_clock` pour gérer l'expiration.

**Terminé quand** : `Test_Effects.cpp` couvre le parsing complet, chaque règle
de rejet, et les trois modes de stacking.

---

# VAGUE 2

## B1 — Module Progression (dépend de A2)

**Pourquoi** : donner de l'XP de métier et faire monter les niveaux. Voir GDD
§38 et phase 13.

**Dette à démêler au passage (n°2)** : dans `Quest/Engine.cpp`, le `level`
global du joueur est calculé par `1 + xp / profession.xpPerLevel`. « Niveau du
joueur » et « courbe du métier » sont donc le même chiffre. Le niveau global
doit cesser de dépendre de la courbe du métier ; à la place, la progression
métier vit dans `PlayerData.professions`. Ne jamais faire **baisser** un niveau.

**Contrat figé** :

```cpp
// src/Progression/Professions.h
namespace rpframework::progression
{
    struct XpResult
    {
        bool        ok = false;
        int         level = 1;
        int         previousLevel = 1;
        int         xp = 0;
        int         skillPointsGained = 0;
        std::string message;

        bool LeveledUp() const { return level > previousLevel; }
    };

    XpResult AddProfessionXp(PlayerId player, std::string_view professionId,
                             int amount, std::string_view reason);
    int GetProfessionLevel(PlayerId player, std::string_view professionId);
    int GetProfessionXp(PlayerId player, std::string_view professionId);
}
```

Courbe : `Profession.xpPerLevel` et `Profession.maxLevel`, déjà data-driven.
Points de compétence : un nombre configurable par niveau gagné, via une clé de
config (`character.professions.<id>.skill_points_per_level`, défaut 1).

Pipeline obligatoire : validation (métier connu, `amount > 0`) → load → mutation
→ save → audit (`progression.xp`). Overflow `int` à garder sous contrôle comme
le fait déjà `Quest/Engine.cpp`.

**Fichiers** : créer `src/Progression/Professions.{h,cpp}` et
`tests/Test_Progression.cpp` ; modifier `src/Quest/Engine.cpp` (démêlage du
niveau global uniquement). Ancres `ANCRE-B1`, `ANCRE-B1-H`, `ANCRE-SRC-B1`,
`ANCRE-TEST-B1`.

**Hors périmètre** : pas de déblocage d'engram (c'est C1), pas d'arbre de
compétences (phase 17), pas de hook.

---

## B2 — Marchands et achat/vente (dépend de A1)

**Pourquoi** : donner une raison d'utiliser l'argent. Voir GDD §46 et phase 15.

**Schéma de config** — section `"merchants"` à insérer **immédiatement après le
bloc `"economy"`** (donc entre `economy` et `quests`) :

```json
"merchants": {
  "town_blacksmith": {
    "name": "Forgeron de la ville",
    "currency": "gold",
    "conditions": { "min_level": 1 },
    "sells": [ { "blueprint": "/Game/…", "price": 120, "stock": 0 } ],
    "buys":  [ { "blueprint": "/Game/…", "price": 30 } ]
  }
}
```

`stock: 0` = illimité. Les prix sont en entier, dans la monnaie déclarée.

**Contrat** : `Economy/Merchant.{h,cpp}` avec un `TxResult` réutilisant
`economy::TxStatus`, et une transaction atomique : vérifier conditions, stock et
solde **avant** tout débit, puis passer par `Economy::Wallet` (jamais par
`PlayerData.wallets`), puis auditer (`economy.merchant.buy` / `.sell`).

Interface joueur : commande chat `/marchand` (`list`, `info <id>`,
`acheter <id> <item> [qty]`, `vendre <id> <item> [qty]`), enregistrée dans
`Main.cpp` sur le modèle des 14 commandes existantes, avec une clé de
permission et un rate limit déclarés en config.

**Fichiers** : créer `src/Economy/Merchant.{h,cpp}`, `tests/Test_Merchant.cpp` ;
modifier `configs/config.json` et `src/Main.cpp`. Ancres `ANCRE-B2`,
`ANCRE-B2-H`, `ANCRE-SRC-B2`, `ANCRE-TEST-B2`.

**Hors périmètre** : pas de PNJ, pas de boutique joueur, pas d'UI.

---

# VAGUE 3

## C1 — Câblage de la boucle (agent seul) — ✅ fait

Dépendait de A1, A3 et B1. Mergé dans `main` (`chantier/C1`).

Enchaînement livré : craft détecté (hook, clé blueprint de A1) → lookup
`Crafting::FindByOutputBlueprint` → vérification des conditions (métier,
niveau, skill) → `Progression::AddProfessionXp` → si montée de niveau,
évaluation des recettes désormais accessibles → `Loadout::TryUnlockEngrams` →
message joueur via `asa::Tell`.

Point d'entrée testable : `Crafting::OnItemCrafted` / `CollectAccessibleEngrams`
(`src/Crafting/Pipeline.*`). Filet anti-triche (bloquer l'original du hook) :
non implémenté en V1 ; le verrou est en amont (`TryUnlockEngrams`).

**Critère de réussite, en jeu** : forger une épée vanilla fait monter l'XP
forgeron ; au niveau 2, l'engram `metal_pick` (config) est débloqué.
(`xp_per_level` forgeron = 1500 → ~43 épées à 35 XP pour le niveau 2.)

---

# VAGUE 4 — DevKit (pas un agent)

## D1 — Canal commande console + première station custom

1. Côté plugin : enregistrer une commande console dédiée au mod (GDD §48). Ce
   canal **ne dépend d'aucun offset**, donc il survit aux patchs ARK. Les hooks
   restent réservés au gameplay vanilla.
2. Côté DevKit : **une seule** structure custom (la fonderie médiévale), et
   rien d'autre. L'objectif n'est pas le contenu mais de vérifier que le plugin
   la reconnaît par son chemin blueprint et que le canal fonctionne. C'est le
   seul vrai risque technique DevKit↔C++ du projet.

À noter pendant l'installation du DevKit : le chemin de sortie du mod
(`/Game/Mods/<TonMod>/…`) est figé par le nom donné au projet dans l'UGC menu,
et c'est ce préfixe que la config devra utiliser.

Le véhicule standard côté mod est un **singleton monde** qui applique un **buff
invisible** à chaque joueur au spawn : il peut écouter des touches, ajouter des
entrées au menu multi-use et ouvrir des widgets. C'est la porte d'entrée de
l'UI RPG, du marchand et des effets.

---

# Comment lancer les agents

## Étape 1 — préparer les worktrees

Le travail en cours doit être commité : les worktrees partent de `HEAD`, donc
tout ce qui n'est pas commité sera invisible pour les agents.

```powershell
git add -A ; git commit -m "Base de la vague 1"
.\agent-worktree.ps1 -New A1,A2,A3,A4
```

## Étape 2 — un prompt par agent, avec SON chemin

Le chemin du worktree est la première ligne du prompt et la seule protection
contre l'écrasement mutuel. Un agent à qui on donne le répertoire principal
travaillera dedans, quoi que dise le reste du prompt.

```text
Repo : c:\Users\emili\OneDrive\Bureau\RPFramework-A1
       (worktree dédié, branche chantier/A1 — ne travaille QUE dans ce
        répertoire, jamais dans ...\Bureau\RPFramework\RPFramework)

Lis ROADMAP.md et applique intégralement le chantier « A1 — Clé blueprint
canonique + alias d'événements », y compris la section « Règles communes à
tous les chantiers ».

Contexte de spécification : GDD Partie II, §39 et dette technique n°1.

Respecte strictement le contrat d'interface figé dans le brief : d'autres
agents travaillent en parallèle sur A2, A3 et A4 et dépendent de ces
signatures. Ne touche aucun fichier hors de la liste du brief.

Termine par : .\build.ps1 puis out\tests\RPFramework.Tests.exe, les deux
doivent passer sans régression (référence : 195 tests, 1038 EXPECT). Commite
ensuite sur ta branche. Rapporte le nombre de tests final et tout écart au
brief.
```

## Étape 3 — fusionner

Ordre **A2 → A1 → A3 → A4** : A2 ne partage aucun fichier, A1 ne touche pas la
config. Build et tests après *chaque* merge, pas seulement à la fin — sinon on
ne sait plus quel merge a cassé quoi.

```powershell
git merge --no-ff chantier/A2
.\build.ps1 ; .\out\tests\RPFramework.Tests.exe
# … puis A1, A3, A4 de la même façon
```

En cas de conflit sur un `.vcxproj`, garder **les deux** blocs d'ancres : les
insertions de chantiers différents sont additives, jamais concurrentes.

## Ce qu'il ne faut pas faire

Les deux erreurs commises lors de la première tentative de vague 1 :

- **Lancer les agents dans le répertoire principal.** Ils se sont écrasés
  mutuellement et leurs builds concurrents se sont bloqués sur le même `.pdb`.
  Le travail perdu n'est pas signalé : il disparaît.
- **Intervenir soi-même dans le répertoire pendant que les agents tournent.**
  L'orchestrateur est un écrivain concurrent comme les autres. Pendant une
  vague, on observe (`git status`, `Get-Process MSBuild`) et on ne modifie
  rien.

---

# Suivi

| Chantier | Vague | Dépend de | Statut |
|----------|-------|-----------|--------|
| Infra (harness + ancres + worktrees) | 0 | — | ✅ fait |
| A1 Clé blueprint + alias | 1 | — | ✅ fait |
| A2 PlayerData v4 | 1 | — | ✅ fait |
| A3 Registre de recettes | 1 | — | ✅ fait |
| A4 Registre d'effets | 1 | — | ✅ fait |
| B1 Progression métier | 2 | A2 | ✅ fait |
| B2 Marchands | 2 | A1 | ✅ fait |
| C1 Câblage de la boucle | 3 | A1, A3, B1 | ✅ fait |
| D1 Canal mod + station | 4 | C1 | ⬜ |
