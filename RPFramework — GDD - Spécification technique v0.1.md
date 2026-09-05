# RPFramework
## Framework RPG / RP générique pour ARK: Survival Ascended

**Version :** 0.1  
**Statut :** Spécification initiale validée  
**Cible :** ARK: Survival Ascended  
**Technologie principale :** C++ / plugin serveur ASA  
**Environnement de développement :** VS Code  
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
```

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

- VS Code ;
- C++ ;
- Git ;
- environnement de compilation ;
- environnement ASA/plugin réellement utilisé.

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

**Fin du GDD v0.1**