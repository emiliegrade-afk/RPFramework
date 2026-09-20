// ============================================================================
// RPFramework - Loadout / Distribute
//
// Distribue le kit de départ à un joueur. La distribution est idempotente :
// une fois qu'un joueur a reçu son starter kit, on ne le re-distribue pas.
//
// Le flux est :
//   1. Vérifier que le kit n'a pas déjà été distribué (flag + outbox vide)
//   2. Composer le kit via Composer::ComposeStarterKit
//   3. Persister l'outbox `pendingStarterKit` AVANT tout GiveItem
//   4. Tenter la livraison ASA, confirmer les succès, poser le flag
//      seulement quand l'outbox est vide
//   5. Un échec de save ne livre pas ; un GiveItem raté retente au login
//
// Phase 4b ne fait PAS l'attribution UE/ASA effective : le framework
// retourne la liste des items, et c'est à l'appelant (hook Phase 4a ou
// commande admin Phase 9) de traduire `Item.id` en item ASA réel.
// ============================================================================
#pragma once

#include "Loadout/Item.h"
#include "Security/Types.h"  // PlayerId

#include <vector>

namespace rpframework::loadout
{
    using PlayerId = rpframework::security::PlayerId;

    enum class DistributionStatus
    {
        Delivered,    // kit distribué maintenant
        AlreadyGiven, // kit déjà distribué, rien fait
        NotReady,     // sélections incomplètes (GDD §9) : flag intact
        NoProfile,    // profil joueur indisponible (corrompu, etc.)
    };

    class Distributor
    {
    public:
        // Distribue le kit combiné (commun + race + métier + classe).
        // No-op (NotReady) tant que les sélections requises manquent :
        // on ne pose PAS `starterKitDelivered` dans ce cas, pour que le
        // kit race/métier/classe reste atteignable après Select*.
        // Ne redistribue pas si `starterKitDelivered` est déjà vrai.
        struct Result
        {
            DistributionStatus  status = DistributionStatus::Delivered;
            std::vector<Item>   items;
        };

        static Result GiveStarterKit(PlayerId player);

        // Force la redistribution (admin / debug). Bypass le flag.
        // Audité comme "loadout.starter.forced".
        static Result ForceGiveStarterKit(PlayerId player);
    };
}
