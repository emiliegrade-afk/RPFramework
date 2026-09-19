# RPFramework
## Framework RPG / RP générique pour ARK: Survival Ascended

**Version :** 0.1  
**Statut :** Spécification initiale validée — implémentation V1 en cours (voir §34)  
**Cible :** ARK: Survival Ascended  
**Technologie principale :** C++ / plugin serveur ASA (AsaApi)  
**Environnement de développement :** Visual Studio 2022 (Build Tools / MSVC) et Cursor  
**DevKit :** NON requis pour la V1  
**IA :** Architecture prévue, implémentation ultérieure

---

# 1. Vision du projet

RPFramework est un **framework RPG/RP générique destiné aux serveurs ARK: Survival Ascended**.

Le framework ne doit être lié à aucun univers, lore, serveur, race, métier ou système de jeu particulier.

Le principe fondamental est :

> **Le plugin fournit le moteur et les règles. Le créateur du serveur fournit le contenu.**

Chaque administrateur doit pouvoir créer son propre système RPG à partir du framework sans modifier le code source du plugin.

Le framework doit fonctionner initialement **100 % côté serveur**, sans nécessiter de mod créé avec le DevKit.

Une couche visuelle DevKit pourra être ajoutée ultérieurement sans devoir réécrire le cœur du framework.

---

# 2. Principes fondamentaux

## 2.1 Plugin générique

Aucune donnée spécifique à un serveur ne doit être codée en dur.

Le plugin ne doit pas supposer :

- quelles races existent ;
- quels métiers existent ;
- quelles classes existent ;
- quelles factions existent ;
- quel est le lore ;
- quelles quêtes existent ;
- quelle monnaie est utilisée ;
- quels objets constituent les kits ;
- combien de races ou métiers sont disponibles.

Toutes ces informations doivent être configurables.

---

## 2.2 Séparation moteur / contenu

### Le code C++

Gère :

- règles ;
- logique ;
- validation ;
- sécurité ;
- progression ;
- sauvegarde ;
- exécution des actions ;
- gestion des joueurs ;
- quêtes ;
- métiers ;
- races ;
- factions ;
- économie ;
- kits ;
- permissions.

### La configuration

Définit :

- races ;
- métiers ;
- classes ;
- factions ;
- quêtes ;
- récompenses ;
- kits ;
- conditions ;
- progression ;
- paramètres du serveur.

Le propriétaire du serveur peut donc modifier son univers sans recompiler le plugin.

---

# 3. Architecture générale

```text
RPFramework
│
├── Core
│   ├── Plugin lifecycle
│   ├── Configuration
│   ├── Logging
│   └── Versioning
│
├── Character
│   ├── Player
│   ├── Race
│   ├── Profession
│   ├── Class
│   └── Progression
│
├── Quest
│   ├── Quest definitions
│   ├── Objectives
│   ├── Conditions
│   └── Rewards
│
├── Faction
│   ├── Factions
│   ├── Reputation
│   └── Ranks
│
├── Economy
│   ├── Currency
│   ├── Transactions
│   └── Rewards
│
├── Loadout
│   ├── Race kits
│   ├── Profession kits
│   ├── Class kits
│   └── Combined starter kits
│
├── Security
│   ├── Permissions
│   ├── Validation
│   ├── Rate limiting
│   ├── Anti-abuse
│   └── Audit logging
│
├── Data
│   ├── Player persistence
│   ├── Backups
│   └── Migration
│
└── AI
    ├── AI Manager
    ├── AI Provider
    ├── AI Context
    ├── AI Tools
    └── AI Audit
```

---

# 4. Gestion des personnages

Chaque joueur dispose d'un profil RP persistant.

Les données prévues comprennent notamment :

- identité du personnage ;
- race ;
- métier ;
- classe ;
- spécialisation si configurée ;
- progression ;
- expérience ;
- réputation ;
- faction ;
- titres ;
- données économiques nécessaires au framework ;
- progression des quêtes.

Les données doivent être conservées après :

- déconnexion ;
- reconnexion ;
- redémarrage du serveur.

---

# 5. Système de races

Le framework doit permettre au propriétaire du serveur de créer autant de races que nécessaire.

Exemples uniquement illustratifs :

- Humain ;
- Elfe ;
- Nain ;
- Orc ;
- Vampire.

Ces exemples ne constituent **pas une liste imposée par le framework**.

## Une race doit pouvoir définir

- nom ;
- description ;
- lore ;
- bonus ;
- malus ;
- statistiques ;
- multiplicateurs ;
- compétences ou capacités supportées ;
- restrictions ;
- zone de départ ;
- faction associée ;
- réputation initiale ;
- équipement de départ ;
- conditions de sélection.

Une race peut être totalement différente selon le serveur.

---

# 6. Système de métiers

Le propriétaire du serveur doit pouvoir créer ses propres métiers.

Exemples illustratifs :

- Forgeron ;
- Herboriste ;
- Chasseur ;
- Mineur ;
- Tavernier ;
- Garde ;
- Médecin ;
- Ingénieur.

Le framework n'impose aucune liste.

## Un métier doit pouvoir définir

- nom ;
- description ;
- lore ;
- niveaux ;
- expérience ;
- progression ;
- bonus ;
- restrictions ;
- compétences ;
- spécialisations ;
- prérequis ;
- équipement de départ ;
- récompenses ;
- interactions avec les quêtes ;
- relations avec les factions.

---

# 7. Classes

Le framework prévoit également un système de classes configurable.

Le propriétaire peut décider :

- d'utiliser des classes ;
- de ne pas en utiliser ;
- du nombre de classes ;
- des classes disponibles ;
- de leurs statistiques ;
- de leurs bonus/malus ;
- de leurs restrictions ;
- de leurs équipements.

Le système doit rester indépendant des races et métiers.

---

# 8. Combinaison Race + Métier + Classe

Les différents éléments peuvent contribuer au profil final du personnage.

Exemple :

```text
Race
+
Métier
+
Classe
+
Faction éventuelle
        ↓
Profil du personnage
        ↓
Starter Loadout
```

Le système doit pouvoir déterminer automatiquement le kit final du joueur à partir des éléments configurés.

---

# 9. Système de kits de départ

Le propriétaire du serveur doit pouvoir définir exactement ce qu'un joueur reçoit.

Les kits doivent pouvoir être composés à partir de plusieurs sources :

```text
Kit commun
+
Kit de race
+
Kit de métier
+
Kit de classe
+
éventuellement autres éléments configurés
        ↓
Kit final
```

Exemple conceptuel :

```text
Race : Nain
→ Pioche
→ Ressources

Métier : Forgeron
→ Marteau
→ Lingots

Classe : Guerrier
→ Arme
→ Bouclier
```

Le plugin distribue le résultat final.

En V1, la distribution dans le monde ASA passe par `AShooterPlayerController::GiveItem` lorsqu'un chemin Blueprint est connu : champ JSON `blueprint` (racine ou `extras.blueprint`), ou un `id` contenant `/`. Les identifiants RP-only (ex. `bread`, `torch`) posent le flag `starterKitDelivered` et restent audités, mais ne créent pas d'objet jeu tant qu'aucun blueprint n'est configuré.

Les objets, quantités et conditions doivent être configurables.

---

# 10. Système de quêtes

Le framework doit intégrer un **Quest Engine configurable**.

Le propriétaire du serveur doit pouvoir créer ses propres quêtes sans modifier le C++.

Une quête doit pouvoir définir :

- identifiant unique ;
- nom ;
- description ;
- lore ;
- donneur ou source de quête ;
- prérequis ;
- niveau requis ;
- race requise ou interdite ;
- métier requis ou interdit ;
- classe requise ou interdite ;
- faction ;
- réputation ;
- objectifs ;
- progression ;
- récompenses ;
- quêtes préalables ;
- état de la quête.

---

# 11. Objectifs de quêtes

Le moteur doit être conçu pour supporter différents types d'objectifs.

Les types validés comme objectifs du framework comprennent notamment :

- collecte ;
- élimination ;
- apprivoisement ;
- fabrication ;
- livraison ;
- exploration / déplacement vers une zone ;
- interaction ;
- progression de métier ;
- progression de réputation ;
- accomplissement d'une autre quête ;
- objectifs temporisés.

Le moteur doit également permettre de combiner plusieurs objectifs.

Exemple conceptuel :

```text
Objectif A
ET
Objectif B

OU

Objectif C
```

---

# 12. Récompenses de quêtes

Une quête peut attribuer plusieurs types de récompenses configurables :

- expérience ;
- monnaie ;
- réputation ;
- objets ;
- progression ;
- titre ;
- déblocage d'éléments du framework.

Les récompenses doivent être validées côté serveur.

Une récompense ne doit jamais pouvoir être obtenue plusieurs fois à cause d'un spam de commande, d'une reconnexion ou d'une répétition d'événement.

Chaque récompense importante doit pouvoir être identifiée et auditée.

---

# 13. Factions et réputation

Le framework doit intégrer des factions configurables.

Une faction peut posséder :

- nom ;
- description ;
- rangs ;
- réputation ;
- conditions d'accès ;
- relations avec le joueur ;
- récompenses ;
- restrictions.

La réputation peut être modifiée par :

- quêtes ;
- actions configurées ;
- récompenses ;
- autres systèmes du framework.

La réputation peut ensuite servir de prérequis pour :

- quêtes ;
- rangs ;
- récompenses ;
- accès ;
- métiers ou spécialisations.

---

# 14. Économie

Le framework doit disposer d'un système économique centralisé.

Le système doit gérer :

- monnaie ;
- ajout ;
- retrait ;
- transactions ;
- récompenses ;
- historique.

Les modifications de monnaie doivent passer par un système centralisé.

Le code ne doit pas modifier arbitrairement le solde d'un joueur depuis plusieurs systèmes indépendants.

En V1, l'historique des transactions est l'audit JSONL (`logs/audit.log`) : chaque opération `economy.add` / `subtract` / `transfer` / `grant` / `reward` y est tracée avec joueur, montant et contexte. Un livre de compte dédié n'est pas un livrable V1.

Chaque transaction importante doit pouvoir contenir :

- identifiant ;
- joueur ;
- montant ;
- type ;
- raison ;
- source ;
- timestamp.

---

# 15. Sécurité

La sécurité est une composante fondamentale du framework et doit être développée dès le début.

## Principe fondamental

> **Le client demande. Le serveur décide.**

Le client ne doit jamais être considéré comme une source d'autorité pour :

- race ;
- métier ;
- récompense ;
- monnaie ;
- progression ;
- quête ;
- permissions ;
- objets.

---

# 16. Validation serveur

Toute action sensible doit être validée côté serveur.

Exemple :

```text
Demande client
      ↓
Security Layer
      ↓
Identification du joueur
      ↓
Validation des permissions
      ↓
Validation de l'état actuel
      ↓
Validation des prérequis
      ↓
Exécution
      ↓
Sauvegarde
```

---

# 17. Anti-abus

Le framework doit prévoir :

- cooldowns ;
- rate limiting ;
- protection contre le spam ;
- validation des états ;
- protection contre les récompenses multiples ;
- protection contre les transactions répétées ;
- validation des commandes ;
- validation des paramètres ;
- contrôle des permissions ;
- détection d'anomalies.

Le système doit privilégier la **détection et la journalisation** plutôt qu'un bannissement automatique agressif.

---

# 18. Permissions

Un système de permissions doit être intégré.

Les niveaux envisagés sont :

```text
PLAYER
MODERATOR
GM
ADMIN
OWNER
SYSTEM
```

Les permissions doivent être vérifiées côté serveur.

Exemples :

```text
Voir ses propres données
→ PLAYER

Inspecter certaines données
→ MODERATOR / GM

Créer une quête
→ ADMIN

Modifier la configuration critique
→ ADMIN / OWNER
```

Les permissions exactes doivent rester configurables.

---

# 19. Audit Log

Les actions importantes doivent pouvoir être enregistrées.

Exemples :

```text
[Timestamp]
Player selected Race X

[Timestamp]
Admin created Profession Y

[Timestamp]
Quest Z completed

[Timestamp]
Reward granted

[Timestamp]
Currency transaction

[Timestamp]
Permission denied
```

L'objectif est de pouvoir comprendre précisément ce qui s'est passé lorsqu'un problème survient.

---

# 20. Persistance et données

Les données joueurs doivent être persistantes.

Le système doit prévoir :

- sauvegarde ;
- chargement ;
- gestion des erreurs ;
- backups ;
- versionnement des données ;
- migration lors des changements de structure.

Les données de configuration du serveur doivent être séparées des données persistantes des joueurs.

---

# 21. Configuration

Le contenu du serveur doit être configurable sans modification du C++.

La structure initiale envisagée est :

```text
/config
    settings
    races
    professions
    classes
    factions
    quests
    loadouts
```

Le format exact sera déterminé pendant l'implémentation.

Les configurations doivent pouvoir être validées au chargement afin d'éviter qu'une erreur de configuration fasse fonctionner le système dans un état incohérent.

---

# 22. Interface V1 — sans DevKit

La première version ne doit nécessiter **aucun mod DevKit**.

L'interaction avec les systèmes RP devra utiliser uniquement les possibilités accessibles au plugin serveur et les interfaces/mécanismes natifs réellement disponibles via l'environnement ASA/API utilisé.

Les commandes pourront servir d'interface initiale.

Exemples conceptuels :

```text
/race
/metier
/classe
/quetes
/faction
/reputation
/economy
/framework
/mod
/config
```

Le contenu (races, quêtes, kits, spawn, niveaux staff) doit pouvoir être modifié **en jeu** par le modérateur via `/mod`, sans éditer `config.json` à la main. Les changements sont persistés et appliqués à chaud.

Ces commandes ne constituent pas nécessairement l'API finale.

Elles servent d'interface de développement et de fonctionnement initiale.

---

# 23. Interface future

Le framework doit être conçu pour permettre ultérieurement l'ajout d'une interface visuelle via un mod DevKit.

Le principe sera :

```text
VERSION ACTUELLE

Client
 ↓
Plugin
 ↓
RP Framework


VERSION FUTURE

Client
 ↓
UI / PNJ / Mod DevKit
 ↓
Plugin
 ↓
RP Framework
```

Le mod futur ne devra pas contenir la logique métier critique.

Il servira principalement de couche de présentation et d'interaction.

Le plugin restera l'autorité serveur.

---

# 24. PNJ futurs

Les PNJ ne font **pas partie de la V1**.

Ils sont prévus comme couche future.

Lorsqu'un mod DevKit sera ajouté, un PNJ pourra servir de point d'entrée vers :

- quêtes ;
- métiers ;
- factions ;
- dialogues ;
- autres systèmes RP.

Le cœur du plugin ne doit pas dépendre de l'existence de ces PNJ.

---

# 25. Architecture IA

Une architecture IA est prévue mais **l'IA n'est pas une dépendance de la V1**.

L'IA sera ajoutée comme couche externe au framework.

Architecture prévue :

```text
RPFramework
      │
      └── AI Bridge
              │
              └── AI Service
```

Le plugin ne doit pas intégrer directement un modèle IA lourd.

---

# 26. Principe de sécurité IA

Règle fondamentale :

> **L'IA propose. Le RPFramework décide.**

L'IA pourra ultérieurement servir à :

- dialogues de PNJ ;
- narration ;
- génération assistée de quêtes ;
- événements narratifs ;
- analyse ;
- modération assistée.

Mais elle ne doit pas avoir d'autorité directe sur les données critiques.

---

# 27. AI Tools

L'IA pourra communiquer avec le framework via des fonctions contrôlées.

Exemples :

```text
GetPlayerInfo()
GetAvailableQuests()
GetFactionReputation()
GetProfession()
GetCharacterContext()
```

En V1, `rpframework::api::GetPlayerInfo` est la façade query hors namespace Quest : chat, RCON et un futur AI Bridge lisent le même DTO. L'IA n'exécute pas ces lectures en autonomie tant que le Bridge n'existe pas.

Les actions sensibles devront être protégées et ne devront pas être exécutées simplement parce qu'une IA les demande.

Le système devra prévoir :

- contexte contrôlé ;
- permissions ;
- validation ;
- journalisation ;
- audit des appels IA.

---

# 28. Fournisseur IA

L'architecture ne doit pas dépendre d'un fournisseur particulier.

Le système devra être conçu autour d'une abstraction :

```text
AIProvider
    ├── Provider A
    ├── Provider B
    └── Local Model
```

Le choix du fournisseur sera défini ultérieurement.

---

# 29. Principes d'architecture à respecter

### 1. Server authoritative

Le serveur est l'autorité.

### 2. Configuration-driven

Le contenu vient de la configuration, pas du code.

### 3. Modularité

Les systèmes doivent être séparés et faiblement couplés.

### 4. Extensibilité

Un futur mod DevKit doit pouvoir se connecter au framework sans réécriture du cœur.

### 5. Sécurité by design

La sécurité doit être présente dès la première version.

### 6. Auditabilité

Les actions critiques doivent pouvoir être retracées.

### 7. Persistance

Les données importantes doivent survivre aux redémarrages.

### 8. Indépendance du lore

Le framework ne doit imposer aucun univers.

### 9. Indépendance de l'IA

Le framework doit fonctionner sans IA.

### 10. Pas de logique critique côté client

Le client ne doit jamais être l'autorité sur les données RP.

---

# 30. Ordre de développement validé

Le développement doit commencer par une feuille blanche.

## Phase 0 — Environnement

- Visual Studio 2022 (Build Tools) / Cursor ;
- C++20 ;
- Git ;
- environnement de compilation (MSBuild, vcpkg) ;
- environnement ASA/plugin réellement utilisé (AsaApi).

Objectif :

**obtenir un plugin minimal qui compile et se charge correctement.**

---

## Phase 1 — Core

Créer :

- lifecycle du plugin ;
- configuration ;
- logging ;
- versioning ;
- architecture modulaire.

---

## Phase 2 — Security Core

Avant les systèmes RP :

- identification joueur ;
- permissions ;
- validation ;
- rate limiting ;
- protection contre le spam ;
- audit log ;
- gestion des erreurs.

---

## Phase 3 — Player Data

Créer la persistance :

- identité ;
- profil RP ;
- données de progression ;
- sauvegarde ;
- chargement ;
- backups ;
- migration.

---

## Phase 4 — Character

Ajouter :

- races ;
- métiers ;
- classes ;
- progression ;
- règles de compatibilité.

---

## Phase 5 — Loadouts

Ajouter :

- kits communs ;
- kits race ;
- kits métier ;
- kits classe ;
- combinaison automatique ;
- distribution sécurisée.

---

## Phase 6 — Factions

Ajouter :

- factions ;
- réputation ;
- rangs ;
- conditions ;
- récompenses.

---

## Phase 7 — Economy

Ajouter :

- monnaie ;
- transactions ;
- récompenses ;
- historique ;
- protections anti-abus.

---

## Phase 8 — Quest Engine

Ajouter :

- définition des quêtes ;
- objectifs ;
- conditions ;
- progression ;
- récompenses ;
- chaînes de quêtes ;
- validation serveur.

---

## Phase 9 — Interface V1

Utiliser les mécanismes accessibles sans DevKit pour fournir une interface fonctionnelle aux joueurs et administrateurs.

---

## Phase 10 — AI Bridge

Une fois le framework stable :

- AIManager ;
- AIProvider ;
- AIContext ;
- AI Tools ;
- AI Audit.

L'IA reste optionnelle.

---

## Phase 11 — DevKit futur

Lorsque le DevKit sera disponible :

- UI personnalisée ;
- PNJ ;
- portraits ;
- animations ;
- présentation des quêtes ;
- présentation des races ;
- présentation des métiers.

Le plugin RPFramework reste le backend autoritaire.

---

# 31. Hors périmètre V1

Les éléments suivants ne doivent **pas être développés maintenant** :

- mod DevKit ;
- PNJ personnalisés ;
- UI UMG personnalisée ;
- IA conversationnelle ;
- génération automatique de quêtes par IA ;
- système de Game Master IA.

Ils sont uniquement prévus dans l'architecture future.

> **Révisé par la Partie II (§35).** Le moteur V1 étant livré, le mod DevKit,
> les PNJ et l'UI UMG entrent au périmètre (phases 16 et 20, §50). L'IA
> conversationnelle et le Game Master IA restent hors périmètre.

---

# 32. Objectif final

RPFramework doit devenir un **framework RPG/RP générique pour ARK: Survival Ascended**, permettant à chaque créateur de serveur de construire son propre univers sans modifier le moteur du plugin.

Le créateur doit pouvoir définir :

```text
SON LORE
   ↓
SES RACES
   ↓
SES CLASSES
   ↓
SES MÉTIERS
   ↓
SES FACTIONS
   ↓
SES QUÊTES
   ↓
SES RÉCOMPENSES
   ↓
SES KITS DE DÉPART
   ↓
SON ÉCONOMIE
   ↓
SA PROGRESSION
```

Tout en conservant :

```text
Sécurité
+
Persistance
+
Validation serveur
+
Anti-abus
+
Audit
+
Extensibilité
```

Le framework doit fonctionner **sans DevKit et sans IA**, mais être architecturalement prêt à accueillir ces deux couches ultérieurement.

---

# 33. Règle directrice du projet

> **Construire le moteur une seule fois.**
>
> **Laisser chaque serveur construire son propre monde au-dessus.**

---

# 34. État d'implémentation (8 septembre 2026)

Ce chapitre décrit l'état du code par rapport aux phases du §30. Il ne remplace pas la vision des chapitres 1–33.

| Phase | Contenu | État |
|-------|---------|------|
| 0 | Environnement (VS 2022, AsaApi, vcpkg) | Livré |
| 1 | Core (lifecycle, config, logs, version) | Livré |
| 2 | Security (identité UniqueNetId, permissions, rate-limit, audit JSONL) | Livré |
| 3 | Player Data (fichiers, backups, migration, recovery) | Livré |
| 4 | Character (races, métiers, classes, stats, sélection one-shot) | Livré |
| 5 | Loadouts (compose commun+race+métier+classe, flag idempotent, GiveItem si blueprint) | Livré |
| 6 | Factions (réputation, rangs, join/leave, exclusions) | Livré |
| 7 | Economy (wallets, transfer, grant ; historique = audit) | Livré |
| 8 | Quest Engine (objectifs, récompenses, XP→level, hooks kill/tame/craft/harvest) | Livré |
| 9 | Interface V1 (commandes chat `/race` `/metier` `/quetes` `/faction` `/reputation` `/economy` `/framework`) | Livré |
| 10 | AI Bridge | Hors V1 (§31) — façade `GetPlayerInfo` prête |
| 11 | DevKit (UI UMG, PNJ) | Hors V1 (§31) |

Identité joueur : `GetUniqueNetIdAsString` uniquement. Un échec produit `PlayerId == 0` et le hook ignore l'action. Aucun fallback sur l'adresse du contrôleur.

Kits : `GiveItem` si `blueprint` est renseigné. La config d'exemple livre viande cuite / gourde / torche vanilla.

Quêtes monde : matching souple (`wild_boar` satisfait `boar` ; `*` / `any` ; craft `item` ; collect `harvest`).

Pawn : les modifiers race/métier/classe/rang sont appliqués via `SetMaxStatusValue` (baseline vanilla). `world.spawn_zones.{id}.x/y/z` téléporte à la sélection de race. Le premier joueur devient OWNER si `security.owner_on_first_join` (fichier `owner.json`).

Progression métier : un gain d'XP de quête peut augmenter `PlayerData.level` (`1 + xp / xpPerLevel`) sans jamais le baisser. **Ce couplage est une dette à démêler** (§49 n°2) : « niveau du joueur » et « courbe du métier » sont aujourd'hui le même chiffre.

> La suite du travail est spécifiée en **Partie II** (§35–51) et découpée en
> chantiers exécutables dans `ROADMAP.md`. Les dettes techniques identifiées à
> l'usage du moteur sont listées au §49.

---

**Fin du GDD v0.1**

---
---

# PARTIE II — v0.2 : la couche gameplay RPG

> La Partie I (§1–34) spécifie le **moteur générique**, qui est livré.
> La Partie II spécifie le **jeu construit au-dessus** : un mod RPG
> médiéval-fantastique complet. Elle ne remplace rien : elle ajoute la couche
> gameplay qui manquait, et corrige les points où la Partie I s'est révélée
> inexacte à l'usage.

---

# 35. Changement de cadrage

La Partie I visait un framework technique réutilisable. L'objectif réel est
plus large : **un mod RPG médiéval-fantastique jouable**, dont RPFramework est
le cerveau.

La boucle de jeu cible :

```text
Récolter → fabriquer → progresser → se spécialiser
   → vendre → acheter → accomplir des quêtes
   → débloquer du contenu → développer son personnage
```

Ce qui change concrètement par rapport à la Partie I :

- le DevKit n'est plus « hors périmètre » (§31) mais une **moitié du produit** ;
- le plugin n'est plus une fin, c'est l'**autorité** derrière du contenu ;
- la priorité n'est plus d'ajouter du framework, mais de rendre la première
  boucle réellement jouable dans ASA.

Constat de départ : le moteur existe (persistance, sécurité, quêtes, économie,
personnages). Ce qui manque est le gameplay au-dessus.

---

# 36. Contrainte de plateforme : pas de C++ dans un mod ASA

Décision structurante, à ne plus remettre en question.

Le DevKit ASA **ne compile pas de C++**. Tous les mods ASA sont des plugins
Unreal *content-only* : Blueprints, assets, DataTables. Ce que l'on voit du
C++ dans le DevKit est le code du jeu (`APrimalCharacter`, `UPrimalItem`…),
exposé en lecture pour pouvoir en hériter en Blueprint — les `.cpp` de
ShooterGame ne sont pas livrés.

Deux verrous indépendants ferment la porte :

1. le cook passe par le **cloud cooking CurseForge**, qui produit des assets
   cookés et n'offre aucun canal d'upload de module natif ;
2. le client et le serveur ARK sont des binaires compilés par Wildcard : ils
   chargent des paks de contenu, pas des DLL de mod.

Conséquence : le seul moyen d'exécuter du code natif côté serveur reste une
DLL injectée dans `ArkAscendedServer.exe` par `AsaApiLoader.exe`, c'est-à-dire
AsaApi. Ce n'est pas une limite d'Unreal (les plugins UE5 supportent les
modules C++) mais du DevKit et du canal de distribution.

Corollaires pratiques :

- AsaApi est **côté serveur uniquement**. Les joueurs, consoles incluses, n'ont
  besoin que du mod CurseForge. L'architecture hybride ne ferme la porte à
  personne ; elle impose seulement un serveur Windows auto-hébergé.
- Le coût réel d'AsaApi est la **maintenance aux patchs ARK** (offsets). On le
  contient en gardant la surface ARK minimale : à ce jour, 4 fichiers de glue
  gameplay (`Asa/WorldHooks.cpp`, `Asa/PawnEffects.cpp`, `Asa/Identity.*`,
  `Loadout/AsaDeliver.cpp`), tout le reste étant testable hors serveur via
  `RPFRAMEWORK_TESTS`. **Toute nouvelle dépendance ARK doit passer par `Asa/`.**
- Le DevKit sert aussi de **documentation d'API** : son navigateur de classes
  C++ indique ce qui est appelable, plus lisiblement que les headers AsaApi.

---

# 37. Répartition DevKit / C++ / hybride

```text
                    ARK: SURVIVAL ASCENDED
                             │
                    ┌────────┴────────┐
                  DEVKIT             C++
              CONTENU DU JEU      LOGIQUE RPG
                    │                 │
        ┌───────────┼───────────┐     │
      Items     Structures     UI     │
      Assets    Ateliers      VFX     │
      Recettes  Monde         SFX     │
        └───────────┴───────────┘     │
                    └────────┬────────┘
                             │
                       RPFRAMEWORK
```

**DevKit (contenu)** : items, ressources, armes, armures, outils, ateliers,
structures, nourriture, potions, VFX, SFX, icônes, UI, PNJ, monde, assets.

**C++ (logique)** : PlayerData, progression, métiers, XP, skills, unlocks,
engrams, économie, transactions, effets, quêtes, réputation, factions,
conditions, permissions, administration.

**Hybride** — chaque ligne a une moitié dans chaque camp :

| Système | DevKit | C++ |
|---------|--------|-----|
| Atelier | structure, modèle, UI | conditions d'usage, déblocage |
| Recette | contenu, icône | conditions, XP, produit autorisé |
| Engram | définition de l'item | conditions et unlock |
| Nourriture / potion | item, VFX, SFX | effets, cooldowns, stacking |
| Buff / debuff | buff BP, visuel, durée | décision d'octroi, règles |
| Métier | contenu, présentation | progression |
| Skill | UI de l'arbre | logique, prérequis |
| Quête | présentation | moteur |
| Marchand | PNJ, UI | transaction, prix, stock |
| Économie | — | intégralement |
| UI RPG | widgets | données autoritaires |

Règle d'arbitrage : **le C++ décide ce qui est autorisé, quand, pourquoi et
avec quelles conséquences. Le DevKit montre et exécute.**

---

# 38. Progression métier (schéma PlayerData v4)

État Partie I : un seul couple `level` / `xp` global, alimenté uniquement par
les récompenses de quête de type `"xp"`. Aucune XP de métier n'existe.
`ReportProfessionProgress` ne fait avancer que des objectifs de quête.

Cible : le joueur progresse indépendamment de son niveau ARK, et par métier.

```text
Personnage
├── Niveau général + XP
├── Professions
│   ├── Forgeron Lv. 12
│   ├── Herboriste Lv. 5
│   └── Cuisinier Lv. 8
├── Compétences (Métallurgie, Armurerie, Forge d'armes…)
├── Déblocages (recettes, engrams, ateliers)
├── Réputation
└── Monnaies
```

Structure par métier :

```text
ProfessionProgression
├── profession_id
├── level
├── xp
├── skill_points
├── unlocked_skills[]
└── unlocked_recipes[]
```

Contraintes d'implémentation :

- `kPlayerDataSchemaVersion` passe à **4**, avec une migration v3→v4 qui
  recopie `profession` / `level` / `xp` dans la nouvelle entrée ;
- le champ `profession` (string) est **conservé** : il est lu par
  `Character/Stats`, `Character/Select` et `Asa/PawnEffects` ;
- la courbe réutilise `Profession.maxLevel` et `Profession.xpPerLevel`, déjà
  data-driven ;
- plusieurs métiers peuvent progresser simultanément ; la sélection one-shot
  de la Partie I (§4) désigne le métier *principal*, pas le seul.

---

# 39. Identité des objets : la clé blueprint

Point de conception préalable à tout le reste.

La Partie I identifie les entités par un **slug de nom affiché**
(`GetItemName`, `GetDescriptiveName`). C'est dépendant de la langue du client
et du préfixe de qualité : inutilisable comme clé de recette.

Règle : **la clé canonique d'un item, d'une créature ou d'une structure est son
chemin blueprint normalisé**, au format déjà utilisé dans la config :

```text
/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponSword.PrimalItem_WeaponSword
```

soit sans wrapper `Blueprint'…'`, sans `Default__`, sans suffixe `_C`.

Le slug lisible n'est pas supprimé : il devient un **alias**. Un événement
gameplay transporte donc une liste d'identifiants, `[0]` étant le blueprint
quand il est résolvable. Les quêtes gardent leur matching souple sur les alias
(`wild_boar` satisfait `boar`), les recettes utilisent la clé stricte.

Sans cette séparation, toute config de quête existante casse : le matching
(`Quest/Match.h`) ne découpe que sur `_`, donc un chemin complet ne contient
plus `boar` comme token isolé.

---

# 40. Artisanat et recettes

Boucle cible :

```text
Ressources → Transformation → Atelier → Recette → Objet
   → XP métier → Progression → Déblocage
```

Exemple :

```text
Minerai de fer → Fonderie → Lingot de fer → Forge → Épée
   → XP Forgeron → Forgeron Lv. 4 → nouvelle recette
```

Une recette doit pouvoir définir :

```text
Recipe
├── ID
├── Atelier requis
├── Profession requise
├── Niveau requis
├── Compétence requise
├── Ressources + quantités
├── Temps de fabrication
├── Produit (clé blueprint)
└── XP accordée
```

Exemple concret :

```text
Épée en acier
  Atelier      : Forge
  Profession   : Forgeron Lv. 10
  Compétence   : Métallurgie II
  Ressources   : 8 × Acier, 2 × Bois, 1 × Cuir
  Résultat     : 1 × Épée en acier
  XP           : +35 Forgeron
```

Les recettes sont **data-driven** (`config.crafting.recipes.*`), sur le même
pattern `Definitions` + `Registry` que Character / Faction / Economy / Quest :
validation au chargement, rejet + log d'erreur, absence du Registry si
invalide.

**Comment le C++ arbitre réellement une fabrication.** Il n'existe pas de
concept de « recette » côté ARK que le plugin pourrait valider avant le craft :
quand le hook se déclenche, la demande porte déjà sur un engram que le joueur
possède. Deux leviers, à utiliser dans cet ordre :

1. **verrou en amont (principal)** — ne pas apprendre l'engram tant que les
   conditions ne sont pas remplies. Propre, visible dans l'UI vanilla, et le
   mécanisme existe déjà (`Loadout::TryUnlockEngrams` →
   `ServerUnlockEngram`) ;
2. **refus en aval (filet anti-triche)** — dans le hook, ne pas appeler
   l'original si les conditions échouent ; les ressources ne sont alors pas
   consommées. Mauvaise expérience joueur en verrou principal.

---

# 41. Ateliers spécialisés

L'atelier est du contenu DevKit ; les conditions d'usage et les déblocages sont
gérés par le C++.

```text
Forge              : armes, armures, outils, métallurgie
Fonderie           : minerais, lingots, alliages
Établi de menuisier: meubles, arcs, manches, décoration
Table d'herboriste : plantes, poudres, extraits, potions
Cuisine            : plats, boissons, préparations spéciales
```

Pour le prototype, les ateliers vanilla suffisent : la Refining Forge joue la
fonderie et le Smithy (`PrimalItemStructure_AnvilBench`, déjà en config) joue
la forge. Les ateliers custom viennent au reskin.

---

# 42. Engrams et déblocages

```text
Engram disponible → conditions vérifiées (métier, niveau, skill)
   → déblocage
```

Le joueur ne doit pas obtenir le contenu par ses seuls niveaux ARK.

```text
Épée acier
├── Forgeron Lv. 10
├── Métallurgie II
└── 3 points de compétence
```

État réel : le **mécanisme** existe déjà (`Profession.engrams` +
`TryUnlockEngrams`, câblé à la sélection de métier et à la connexion), ainsi
que `PlayerData.unlocks` (alimenté par les récompenses de quête et les rangs de
faction). Ce qui manque est la **condition** qui décide *quand* débloquer.

---

# 43. Arbre de compétences

```text
                    FORGERON
              ┌────────┴────────┐
        MÉTALLURGIE          ARMURERIE
       ┌──────┴──────┐     ┌────┴─────┐
   Fonte I       Acier I  Armure I  Bouclier I
       │             │
   Fonte II      Acier II
       └──────┬──────┘
        MAÎTRISE DE LA FORGE
```

Les points de compétence viennent de la progression métier. Une compétence peut
débloquer : recettes, ateliers, bonus de fabrication, réduction de coûts,
qualité des objets, spécialités, buffs, possibilités économiques.

Objectif de conception : **deux forgerons de même niveau doivent pouvoir être
différents.**

---

# 44. Système d'effets

Un **moteur unique**, pas un système de buff par source.

```text
Effect
├── ID
├── Type
├── Magnitude
├── Duration
├── Stacking
├── Conditions
└── Source
```

```text
             EFFECT SYSTEM
       ┌───────────┼───────────┐
     Food       Potion      Poison
       └───────────┼───────────┘
          ┌────────┴────────┐
        BUFF              DEBUFF
```

**Contrainte technique décisive : le plugin n'a aucune boucle de tick.** Tout
le code est réactif (hooks + commandes) ; il n'existe ni timer, ni thread, ni
horloge. Les durées et l'expiration ne doivent donc pas être implémentées en
C++ : l'octroi passe par de **vrais buffs ARK** (`PrimalBuff` vanilla ou du
mod), qui expirent seuls.

Répartition retenue :

- **C++** : quel effet est accordé, à qui, sous quelles conditions, avec quels
  cooldowns et quelles règles de stacking ; la persistance de ce qui doit
  survivre à une déconnexion ;
- **ARK / DevKit** : la durée, l'expiration, le visuel, le son.

À noter : l'application de stats actuelle (`Asa/PawnEffects.cpp`) est
**permanente** — elle écrase `SetMaxStatusValue` depuis la baseline vanilla.
Elle ne peut pas servir de base à des effets temporaires.

---

# 45. Nourriture et potions

La nourriture devient une préparation stratégique avant une activité.

```text
Ragoût du chasseur : + régénération, + endurance, + résistance au froid
                     − vitesse de déplacement
Repas du mineur    : + capacité de charge, + résistance, + rendement minier
                     − vitesse
Repas du voyageur  : + endurance, + vitesse, + résistance climatique
```

Potions :

```text
Soin       → heal                 Rapidité   → movement speed
Vigueur    → stamina regen        Résistance → damage resistance
Antidote   → remove poison        Poison     → DoT + faiblesse
```

Le C++ gère le comportement ; le DevKit fournit l'objet, l'icône, le modèle,
le VFX, le son et l'intégration dans le monde.

---

# 46. Économie jouable et marchands

Le wallet existe (§14). Il manque une **raison d'utiliser l'argent**.

```text
Sources  : quêtes, métiers, vente, récompenses, commerce, activités
Dépenses : achat, équipement, ressources, réparation, formation,
           recettes, services, taxes
```

Un marchand doit définir :

```text
Merchant
├── Inventaire
├── Prix d'achat
├── Prix de vente
├── Monnaie acceptée
├── Restrictions (métier, réputation, faction, niveau)
└── Disponibilité
```

La transaction est **entièrement arbitrée par le C++** : vérification du stock,
du solde, des restrictions, débit/crédit atomique via `Economy::Wallet`, audit.

Les boutiques de joueur à joueur viennent **après** le marchand classique.

Pour le prototype, aucun PNJ n'est nécessaire : une commande `/marchand` suffit
à fermer la boucle économique.

---

# 47. Intégration des quêtes

Le moteur de quêtes n'est pas à refaire. Il gère déjà kill, collect, tame,
craft, deliver, explore, interact, profession, reputation, quest.

Le travail restant est de **connecter ces événements au gameplay réel** :

```text
Le joueur fabrique une épée
   → Craft Event → XP métier → Quest Event
   → « Fabriquer 5 épées » → 1/5
```

Limite connue à traiter plus tard : le hook de récolte rapporte une entité
constante (`"harvest"`), donc aucun objectif « récolter 50 minerais de fer »
ne peut fonctionner aujourd'hui. `FAttachedInstancedHarvestingElement`
n'expose pas proprement le type de ressource : cela demande un autre hook.
La boucle d'artisanat n'en a pas besoin (les ressources arrivent dans
l'inventaire, la recette les consomme).

---

# 48. Canal mod ↔ plugin

Les deux moitiés du produit doivent communiquer. Deux sens, deux mécanismes.

**Plugin → mod** : `UVictoryCore::BPLoadClass` sur un chemin
`/Game/Mods/<Mod>/…` donne n'importe quelle classe du mod. Déjà utilisé pour
les engrams (`Loadout/AsaDeliver.cpp`). Sert à donner un item custom,
débloquer un engram custom, appliquer un buff du mod.

**Mod → plugin** : un Blueprint serveur exécute une **commande console** que
le plugin enregistre via `AsaApi::GetCommands()`. Le plugin valide, agit,
répond.

Pourquoi ce choix : ce canal **ne dépend d'aucun offset**, donc il survit aux
patchs ARK, contrairement à un hook. Règle : les hooks restent réservés au
gameplay **vanilla** ; tout ce qui est custom passe par le canal commande.

Côté mod, le véhicule standard en ASA est un **singleton monde** qui applique
un **buff invisible** à chaque joueur au spawn. Ce buff peut écouter des
touches, ajouter des entrées au menu multi-use (« E ») et ouvrir des widgets.
C'est la porte d'entrée de l'UI RPG, de l'interaction marchand et des effets.

---

# 49. Dettes techniques identifiées

À traiter explicitement, elles bloquent ou faussent la suite.

1. **Identité par nom affiché** — `ItemSlug` / `CharacterSlug` dans
   `Asa/WorldHooks.cpp` utilisent le nom localisé. Doit devenir la clé
   blueprint (§39). Bloque les recettes, fausse les quêtes.
2. **Niveau global dérivé de la courbe métier** — dans `Quest/Engine.cpp`, le
   `level` du joueur est calculé par `1 + xp / profession.xpPerLevel`.
   « Niveau du joueur » et « courbe du métier » sont donc le même chiffre. À
   démêler avant d'introduire une vraie progression métier (§38), sous peine
   de deux sources de vérité contradictoires.
3. **Aucune boucle de tick** — voir §44. Conditionne la conception des effets.
4. **Récolte sans granularité** — voir §47.
5. **`Utf8ToFString` dupliqué** — copie locale dans
   `Loadout/AsaDeliver.cpp` ; à mutualiser dans `Asa/` lors de l'ajout du
   module Blueprints.
6. **Application de stats permanente** — voir §44.

---

# 50. Ordre de développement v2

La numérotation continue celle du §30. Principe : **le DevKit n'est pas sur le
chemin critique**. Le prototype entier est réalisable avec des assets vanilla
(minerai de métal, Refining Forge, lingot, Smithy, épée en métal), et la config
utilise déjà ce format de chemins. Le DevKit servira à **reskiner une boucle
déjà prouvée**.

## Phase 12 — Fondations de la boucle (sans DevKit)

- clé blueprint canonique + alias (§39) ;
- `PlayerData` v4 + migration (§38) ;
- registre de recettes data-driven (§40) ;
- registre d'effets data-driven (§44, données seulement).

## Phase 13 — Progression métier

- XP par métier, courbe, montée de niveau, plafond ;
- démêlage de la dette n°2 ;
- points de compétence accumulés.

## Phase 14 — Câblage de l'artisanat

- craft détecté → recette → XP métier → montée de niveau → déblocages →
  engrams.

Critère de réussite : en jeu, forger une épée vanilla fait monter le niveau de
forgeron, et le niveau 2 débloque un nouvel engram. **L'architecture est alors
validée ; le reste n'est plus que du contenu.**

## Phase 15 — Économie jouable

- marchands data-driven, achat / vente, prix, restrictions, stock ;
- commande `/marchand` avant tout PNJ.

## Phase 16 — Canal mod ↔ plugin + première station custom

- commande console côté plugin (§48) ;
- une seule structure custom, comme test d'intégration. C'est le seul vrai
  risque technique DevKit↔C++ du projet.

## Phase 17 — Compétences

- arbre, prérequis, dépense de points, bonus, recettes liées aux skills.

## Phase 18 — Effets appliqués

- octroi de buffs ARK, cooldowns, stacking, conditions, persistance.

## Phase 19 — Cuisine et alchimie

- ingrédients, recettes, plats, potions, effets associés.

## Phase 20 — UI RPG et PNJ

- singleton monde + buff invisible + widgets ;
- marchands PNJ, journal, arbre de compétences visuel.

## Puis seulement

Boutiques de joueur, guildes, contrats, professions avancées, factions et
réputation avancées, races et classes enrichies, titres, événements, contenu
RP.

Ce qu'on ne fait **pas** maintenant : la granularité de récolte (dette n°4),
le renommage de l'arborescence `src/` (elle correspond déjà à la cible), et
tout ce qui touche races / classes / titres / factions avancées, déjà présent
en structure et non bloquant.

---

# 51. Prototype cible (vertical slice)

Ne pas viser le jeu entier. Viser **une tranche verticale complète**, sur
assets vanilla.

```text
Minerai de fer
   ↓ Fonderie
Lingot de fer
   ↓ Forge
Épée de fer  → + XP Forgeron → Forgeron Lv. 2
   ↓
Skill : Métallurgie I
   ↓
Nouvelle recette : Épée renforcée
   ↓ Marchand
Vente → GOLD → Achat d'une nouvelle recette
```

Si cette boucle tourne réellement dans ASA, l'architecture fondamentale du mod
est validée.

---

**Fin du GDD v0.2**