// ============================================================================
// RPFramework - Security / Types
//
// Types partagés par tous les modules Security (évite le couplage croisé
// Permissions <-> RateLimiter <-> AuditLog).
// ============================================================================
#pragma once

#include <cstdint>
#include <string_view>

namespace rpframework::security
{
    // Identifiant joueur opaque. On ne s'engage pas sur la forme : c'est
    // juste un entier 64-bit non-signé que les modules Security reçoivent
    // et passent. La traduction "SteamID" / "EOS ID" / "ARK ID" est faite
    // par la couche au-dessus (Main.cpp / hooks AsaApi).
    using PlayerId = std::uint64_t;

    // Hash FNV-1a 64-bit appliqué sur une représentation textuelle stable.
    // On l'utilise pour convertir un identifiant serveur (Steam/EOS/UID) en
    // un PlayerId opaque sans dépendre d'un pointeur mémoire ou d'un format
    // de chaîne non normalisé.
    inline PlayerId MakePlayerId(std::string_view text)
    {
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime  = 1099511628211ull;

        std::uint64_t hash = kFnvOffset;
        for (unsigned char c : text)
        {
            hash ^= static_cast<std::uint64_t>(c);
            hash *= kFnvPrime;
        }
        return static_cast<PlayerId>(hash);
    }

    // Fallback défensif pour un pointeur brut. L'idéation du framework est de
    // toujours passer par une chaîne stable (Steam/EOS) si possible, mais ce
    // helper évite la défaillance si une source d'ID n'est pas disponible.
    inline PlayerId MakePlayerIdFromPointer(const void* ptr)
    {
        if (ptr == nullptr)
        {
            return 0;
        }

        const auto raw = reinterpret_cast<std::uintptr_t>(ptr);
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime  = 1099511628211ull;

        std::uint64_t hash = kFnvOffset;
        for (std::size_t i = 0; i < sizeof(raw); ++i)
        {
            const auto byte = static_cast<unsigned char>((raw >> (i * 8)) & 0xFFu);
            hash ^= static_cast<std::uint64_t>(byte);
            hash *= kFnvPrime;
        }
        return static_cast<PlayerId>(hash == 0 ? 1u : hash);
    }
}
