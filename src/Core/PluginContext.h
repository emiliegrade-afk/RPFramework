// ============================================================================
// RPFramework - Core / PluginContext
//
// Point d'entrée unique pour le cycle de vie du plugin.
// Centralise l'init et le shutdown afin que Main.cpp reste un fin glue
// et que les modules métier (Character, Quest, Faction, Economy, ...)
// aient un seul point d'ancrage au démarrage / à l'arrêt.
// ============================================================================
#pragma once

namespace rpframework::core
{
    class PluginContext
    {
    public:
        enum class State
        {
            Uninitialized,
            Initializing,
            Ready,
            ShuttingDown,
            Failed,
        };

        PluginContext() = default;
        ~PluginContext() = default;

        PluginContext(const PluginContext&)            = delete;
        PluginContext& operator=(const PluginContext&) = delete;
        PluginContext(PluginContext&&)                 = delete;
        PluginContext& operator=(PluginContext&&)      = delete;

        // Initialise le framework (logger, config, hooks minimaux).
        // - Init = true  : appelé depuis Plugin_Init (AsaApi).
        // - Init = false : appelé depuis Plugin_Unload (cleanup).
        // Renvoie true si l'opération a réussi (best-effort : on log et
        // on continue même en cas d'erreur partielle).
        static bool Initialize();
        static bool ReloadConfig();
        static void Shutdown();

        // True si Initialize() a réussi.
        static bool IsInitialized();
        static State GetState();

    private:
        static bool initialized_;
        static State state_;
    };
}
