# RPFramework — Mode d’emploi

Framework RPG / RP pour **ARK: Survival Ascended**.  
Le plugin fournit le **moteur**. Le modérateur fournit le **contenu** (en jeu via `/mod`, ou dans `config.json`).

---

## 1. En une phrase

Tu charges le plugin sur un dédié AsaApi. Les joueurs se créent un perso RP en chat. Le **premier joueur** qui se connecte devient **OWNER** et peut tout configurer en jeu.

---

## 2. Installation (une fois)

### 2.1 Compiler (dev)

Voir `SETUP.md` pour Git, VS 2022 (MSVC 14.39), vcpkg, AsaApi.

```powershell
.\setup.ps1
.\build.ps1
```

Sortie : `out\RPFramework.dll` et `out\RPFramework.dll.arkapi`.

### 2.2 Déployer sur le serveur

Dossier :

```text
ShooterGame\Binaries\Win64\<AsaApi_x.xx>\ArkApi\Plugins\RPFramework\
```

Y mettre :

- `RPFramework.dll`
- `configs\PluginInfo.json`
- `configs\config.json`

Le serveur **doit** tourner avec AsaApi (loader `AsaApiLoader.exe` à la place de `ArkAscendedServer.exe` — voir `SETUP.md` §11).

### 2.3 Boucle locale

```powershell
[Environment]::SetEnvironmentVariable('ARKSV_PATH',
  'C:\Program Files (x86)\Steam\steamapps\common\ARK Survival Ascended Dedicated Server',
  'User')

.\deploy.ps1              # build + copie + hot reload
.\deploy.ps1 -NoBuild     # copie seule
```

Client ARK : **Join ARK → Unofficial → Local**.

Logs : `%ARKSV_PATH%\ShooterGame\Saved\Logs\ArkApi.log`

---

## 3. Première connexion

1. Le serveur enregistre ton profil (Steam / EOS). Si l’id réseau est illisible, **rien ne se passe** (pid = 0).
2. **Premier joueur = OWNER**, écrit dans `owner.json` dans le dossier du plugin.
3. Tu n’as pas encore de race / métier : le kit de départ **attend** tes choix.
4. Ensuite : `/race` → `/metier` → kit donné (viande cuite, gourde, torche si les blueprints passent).

Promouvoir un autre staff :

```text
/mod player 76561198XXXXXXXXX MODERATOR
/mod player 76561198YYYYYYYYY GM
```

Utilise le **SteamID64** (17 chiffres), pas le pseudo.

---

## 4. Niveaux

| Niveau | Qui | Droit typique |
|--------|-----|----------------|
| PLAYER | Tout le monde | Jouer : race, quêtes, éco, factions |
| MODERATOR | Staff partie | `/mod` : lire / écrire la config live |
| GM | Maître de jeu | + grants économie (moteur) |
| ADMIN | Admin serveur | Créer quêtes / factions (clés baked-in) |
| OWNER | Hôte / 1er joueur | `/framework reload`, tout |

Désactiver le bonus « premier = OWNER » : dans `config.json`, `"security.owner_on_first_join": false`.

---

## 5. Commandes joueur

Tape dans le **chat** (limite 256 caractères). Réponse verte = OK, rouge = erreur.

### Personnage (one-shot : un seul choix)

| Commande | Effet |
|----------|--------|
| `/race list` | Liste les races (`id` + nom) |
| `/race info dwarf` | Traits (buffs / debuffs) |
| `/race select human` | Choisit la race (`elf`, `dwarf`, …) |
| `/metier list` ou `/profession list` | Liste les métiers |
| `/metier info guard` | Traits + nombre d’engrams |
| `/metier select guard` | Choisit le métier |
| `/classe list` / `/class select …` | Classes (off par défaut) |

Après race + métier, le **kit** est composé (commun + race + métier) et donné si chaque item a un `blueprint`.

### Factions

| Commande | Effet |
|----------|--------|
| `/faction list` | Liste |
| `/faction join town` | Rejoint (une faction à la fois) |
| `/faction leave` | Quitte |
| `/faction rep town` | Score + palier, ex. `10 (Amical)` |
| `/faction rank town` | Ton rang |
| `/reputation town` | Pareil que `rep` |
| `/reputation rank town` | Pareil que `rank` |

Exemple fourni : `town`, `thieves_guild` (garde exclue, niveau 5), `forest_keepers` (nains exclus, niveau 3).

La réputation est un entier. Le **palier** (Hai … Exalte) se lit dessus.
Les factions peuvent être `at_war` / `ally` : gagner +40 chez les voleurs
fait perdre 40 à la ville (un seul saut, configuré dans `relations`).

Le forgeron `town_blacksmith` est de faction `town` : prix et droit
de trade suivent ton palier avec la ville. **Hai** ou **Hostile** →
refusé. **Inamical** → plus cher.

Le palier **Hai** a `attack_on_sight`. Un Blueprint garde appelle
`rpf npc hostile town` ; le plugin répond, le DevKit pose l’aggro.

**Pas vu, pas pris** : `rpf crime report theft 0` ne tache pas `town` ;
`rpf crime report theft 1` applique `config.crimes.theft.delta`.
Les portes : `rpf location canenter town_gates` (Hai / Hostile → `denied`).

### Économie

Monnaies d’exemple : `gold`, `gem`, `token_quest` (non transférable).

| Commande | Effet |
|----------|--------|
| `/economy list` | Monnaies |
| `/economy balance gold` | Ton solde |
| `/economy transfer <joueur> gold 10` | Envoie de l’or |

`<joueur>` = SteamID64 (recommandé) ou id interne.

### Quêtes

| Commande | Effet |
|----------|--------|
| `/quetes` ou `/journal` | Journal : quêtes actives + progression |
| `/quetes liste` ou `/quest list` | Toutes les quêtes définies |
| `/quetes disponibles` | Celles que **toi** tu peux prendre |
| `/quetes etat first_hunt` | Progression |
| `/quetes demarrer first_hunt` | Démarre |
| `/quetes terminer first_hunt` | Manuel seulement si `auto_complete` est false |
| `/quetes abandonner first_hunt` | Abandonne |

Alias anglais : `list`, `available`, `status`, `start`, `complete`, `abandon`.

**Quête d’exemple `first_hunt`** : tuer 2 sangliers. Le moteur accepte `boar`, `wild_boar`, etc. Dès que c’est fait, la quête se valide **toute seule** (or + XP + titre). Elle démarre aussi toute seule en rejoignant `town` (journal + `starter_quests`).

### Infos plugin

| Commande | Qui | Effet |
|----------|-----|--------|
| `/framework version` | Tous | Nom + version |
| `/framework reload` | OWNER | Relit `config.json` depuis le disque |

---

## 6. Commandes modérateur (`/mod` ou `/config`)

Niveau **MODERATOR+** (ou OWNER auto). Tout `/mod set` / `kit` / `spawn` / `player` **sauve** `config.json` et **recharge** les registres.

`/mod help` affiche le résumé. `/mod help race|job|faction|quest` détaille une section.  
Alias : `/config …` et `/framework get|set|list|…`.

### Races, métiers, factions, quêtes (OWNER / modo)

Le chat coupe à 256 caractères : on construit **par petits pas**.

```text
/mod race add orc Orc
/mod race trait orc bonus weight multiply 1.2
/mod race trait orc malus speed multiply 0.9
/mod race desc orc Costaud mais lent
/mod race del orc

/mod job add miner Mineur
/mod job trait miner bonus weight multiply 1.15
/mod job trait miner malus speed multiply 0.95
/mod job engram miner add /Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponPike.PrimalItem_WeaponPike
/mod job kit miner add pick 1 /Game/...PrimalItem_WeaponMetalPick
/mod job del miner

/mod faction add town Ville
/mod faction journal town quest_journal /Game/PrimalEarth/CoreBlueprints/Items/Notes/PrimalItem_Note.PrimalItem_Note
/mod faction quest town add first_hunt

/mod quest add first_hunt Premiere chasse
/mod quest desc first_hunt Tuer deux sangliers
/mod quest objective first_hunt kill_boar kill boar 2
/mod quest reward first_hunt currency gold 25
```

Stats reconnues sur le pawn : `health`, `stamina`, `oxygen`, `food`, `water`, `weight`, `melee`, `speed`, `cold` (fortitude / froid), `crafting`.

### Lire / écrire n’importe quelle règle

```text
/mod get character.races.human
/mod set character.classes_enabled true
/mod set character.races.human.name Humain
/mod set character.races.human.spawn_zone tutorial
```

Le chat coupe à ~220 caractères à l’affichage, 256 à la saisie. **Découpe** les gros JSON.

Chemins utiles :

| Chemin | Contenu |
|--------|---------|
| `character.races.<id>` | Une race |
| `character.professions.<id>` | Un métier |
| `character.classes_enabled` | Classes on/off |
| `factions.<id>` | Une faction |
| `quests.<id>` | Une quête |
| `economy.currencies.<id>` | Une monnaie |
| `loadout.common_kit` | Kit commun |
| `world.spawn_zones.<id>` | Coordonnées UE |
| `security.player_levels.<steamid>` | Staff |
| `security.rate_limits.<action>` | Anti-spam |

### Listes

```text
/mod list races
/mod list professions
/mod list classes
/mod list factions
/mod list quests
/mod list currencies
/mod list kit
```

### Kit de départ

```text
/mod kit add torch 1 /Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponTorch.PrimalItem_WeaponTorch
/mod kit clear
```

Sans `blueprint`, l’item est RP-only (flag + audit, **pas** d’objet dans l’inventaire).

### Spawn de race

`SelectRace` pose `spawnApplied` et appelle `ApplyWorldEffects` avec **Spawn + Stats**.
Le pawn est téléporté **une seule fois** vers `world.spawn_zones.<race.spawn_zone>`
(`x` / `y` / `z`, `yaw` optionnel). Un relog ou un `SelectProfession` ne
re-téléporte pas : le flag `spawnApplied` reste vrai, seuls les stats pawn
sont réappliqués.

`/mod spawn` sert à **éditer** les zones dans `config.json` (staff). Ce n’est
pas le véhicule de playtest — la téléportation joue au moment de la sélection
de race.

### Staff, or, réputation

```text
/mod player 76561198000000000 GM
/mod grant 76561198000000000 gold 100
/mod rep 76561198000000000 town 50
```

---

## Canal console rpf (mod DevKit)

Canal **Mod → Plugin** (GDD §48). Une commande console dédiée, enregistrée
via `AsaApi::GetCommands().AddConsoleCommand` — **aucun offset ARK**, donc
elle survit aux patchs. Les hooks vanilla (kill / craft / harvest) ne
passent pas par ici.

Le plugin est prêt **avant** le mod. Tant que le DevKit n’existe pas, le
chemin blueprint de la station custom est un placeholder dans la config ;
le canal `rpf` fonctionne déjà (test : `rpf ping`).

### Côté Blueprint (serveur)

Ne jamais faire confiance au client : le C++ re-valide permissions, ids et
chemins. `ExecuteConsoleCommand` doit tourner **sur le serveur** (buff
invisible / singleton monde), pas dans un widget client seul.

```text
Execute Console Command
Command : rpf ping
```

Exemples :

```text
rpf ping
rpf race list
rpf race select human
rpf job list
rpf job select blacksmith
rpf player status
rpf auth <code>
rpf mod get character.races.human
rpf mod get "/Game/Mods/RPFramework/Stations/MedievalForge.MedievalForge"
```

En Blueprint, un chemin avec espaces ou un JSON path se met entre
**guillemets doubles**. Le plugin tokenize de façon robuste (`"` et `\"`).

Réponse : message serveur `[RPFramework] …` (vert = ok, rouge = erreur) +
ligne dans `ArkApi.log`.

### Sous-commandes D1

| Commande | Qui | Effet |
|----------|-----|--------|
| `rpf ping` | Tous | `RPFramework OK` — test du canal |
| `rpf race list` | Tous | Liste les races config |
| `rpf race select <id>` | Tous (`race.select`) | Équivalent `/race select <id>` |
| `rpf job list` | Tous | Liste les métiers (`metier` / `profession` alias) |
| `rpf job select <id>` | Tous (`profession.select`) | Équivalent `/metier select <id>` |
| `rpf player status` | Tous | JSON `race` / `profession` / `level` / `job_level` (widgets) |
| `rpf mod get <json.path>` | MODERATOR+ ou session `auth` | Lecture config live |
| `rpf crime report <id> <0\|1>` | Tous | Crime à témoin (E2). `0` = pas vu, `1` = vu |
| `rpf location canenter <id>` | Tous | Accès lieu (E3). `allowed` / `denied` |
| `rpf npc hostile <faction>` | Tous | `hostile` si palier `attack_on_sight` |
| `rpf auth <code>` | Tous (rate-limité) | Valide `security.admin_code` |

Toute sous-commande **mutante** (`select`, et tout `rpf mod` hors `get`)
exige une permission serveur. Un Blueprint client ne contourne rien.

### Auth admin

Dans `config.json` :

```json
"security": {
    "admin_code": "un-secret-long"
}
```

Vide = auth **désactivée** (échec fermé). Comparaison **constant-time** ;
le code n’est jamais écrit dans l’audit ni dans la réponse. Succès →
session ADMIN **en mémoire** (perdue au reload / déco plugin), pas dans
`player_levels`. 5 essais / 60 s (`rpf.auth`).

### Protocole DevKit (station custom)

1. Compiler / déployer le plugin (`.\build.ps1` puis `.\deploy.ps1`).
2. En jeu, depuis un Blueprint **serveur** : `ExecuteConsoleCommand("rpf ping")`.
   Attendu : `[RPFramework] RPFramework OK`.
3. Créer **une** structure (fonderie médiévale). Noter le chemin UGC
   `/Game/Mods/<TonMod>/…` (figé par le nom du projet dans le menu UGC).
4. Déclarer la station dans `config.crafting.stations` avec ce blueprint.
5. Vérifier la lecture : `rpf mod get crafting.stations.<id>` (modo / auth).
6. Widgets Phase 20 : `rpf player status` pour hydrater race / métier / niveau.

Hors périmètre D1 : widgets UMG, HTTP, panel web. Les hooks vanilla restent
inchangés.

---

## 7. Scénario de playtest (30 min)

1. Déploie, join le local.
2. `/framework version` — le plugin répond.
3. `/race list` puis `/race select human`.
4. `/metier select guard` — tu dois recevoir le kit.
5. `/faction join town` — tu reçois le **journal** (note) et `first_hunt` démarre toute seule.
6. Tue 2 sangliers — la quête se **valide toute seule** (or + XP + titre).
7. `/economy balance gold` — tu dois avoir 25.
8. `/quetes etat first_hunt` — doit afficher `terminee`.

Côté modérateur (toi, OWNER) :

```text
/mod list quests
/mod set quests.first_hunt.rewards.0.amount 50
```

(Le 2ᵉ set dépend de la forme JSON ; si ça échoue, édite la quête par petits champs ou recharge un JSON préparé.)

---

## 8. Ce que le moteur fait tout seul

| Événement ARK | Quête |
|---------------|--------|
| Mort d’un dino (toi = killer) | `kill` + slug (`wild_boar` → objectif `boar`) |
| Apprivoisement | `tame` |
| Craft | `craft` + nom d’objet (ou objectif `item` = n’importe quoi) |
| Récolte | `collect` + `harvest` (générique) |

Les recettes **dans** `config.crafting.recipes` sont fail-closed : si
`AllowCraft` refuse (métier, niveau, skill), le vanilla n’est pas appelé
et il n’y a pas d’XP / quête. Les crafts vanilla **hors** registry restent
libres (V1).

Stats race / métier / rang : appliquées au **pawn** (PV, poids, vitesse, froid, craft, etc.) au join et après un choix.  
Engrams du métier : débloqués à `/metier select`.  
`Rank.benefits.unlocks` : ajoutés une fois au profil.  
Journal de faction : item donné **une fois** à l’adhésion ; quêtes de `starter_quests` auto-démarrées et auto-validées.

---

## 9. Fichiers sur le serveur

```text
ArkApi/Plugins/RPFramework/
  RPFramework.dll
  PluginInfo.json
  config.json          ← contenu live (aussi modifié par /mod)
  owner.json           ← id du premier OWNER
  players/<id>.json    ← fiches joueurs
  logs/audit.log       ← historique (éco, staff, quêtes)
  logs/framework.log
```

Ne commite pas `owner.json` ni `players/` s’ils contiennent des parties réelles.

---

## 10. Limites (V1)

- Pas d’UI UMG, pas de PNJ, pas d’IA.
- Message chat ≤ 256 caractères.
- Spawn de race : **V2 / DevKit** (pas en V1).
- Craft / harvest : les quêtes fines marchent mieux en `kill` / `tame` pour l’instant.
- Identité = UniqueNetId seulement (pas de fallback pointeur).

Hors V1 : DevKit, portraits, dialogues, Game Master IA.

---

## 11. Dépannage

| Symptôme | Cause probable |
|----------|----------------|
| Aucune commande ne répond | Plugin pas chargé / AsaApi loader absent |
| Tout est ignoré (pas de profil) | UniqueNetId vide (pid 0) |
| `/mod` = permission refusée | Pas OWNER / pas dans `player_levels` |
| Kit flag OK mais inventaire vide | Blueprint faux ou chemin UE invalide |
| `first_hunt` ne avance pas | Mauvais slug ; essaie `/quetes etat` après un kill |
| `/framework reload` refusé | Réservé OWNER |
| Joueur téléporté dans le vide | Spawn 0,0,0 évité ; vérifie tes X/Y/Z |

---

## 12. Où aller ensuite

- Spec : `RPFramework — GDD - Spécification technique v0.1.md`
- Install compile : `SETUP.md`
- Aperçu technique : `README.md`
