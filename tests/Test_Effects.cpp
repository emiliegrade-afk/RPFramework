// ============================================================================
// RPFramework - Tests du registre d'effets (chantier A4)
//
// Couvre : parsing complet du schéma figé, chaque règle de rejet, les trois
// modes de stacking (none / refresh / stack). Data layer uniquement : aucune
// application d'effet, aucun tick, aucune horloge.
// ============================================================================
#include "TestHarness.h"

#include "Core/Config.h"
#include "Effects/Definitions.h"
#include "Effects/Registry.h"

#include <algorithm>
#include <stdexcept>
#include <string>

using namespace rpframework::effects;

namespace
{
    nlohmann::json ValidHunterStew()
    {
        return nlohmann::json::parse(R"({
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
        })");
    }

    nlohmann::json ValidBuffBlueprintOnly()
    {
        return nlohmann::json::parse(R"({
            "name": "Buff ARK",
            "type": "buff",
            "buff_blueprint": "/Game/PrimalEarth/CoreBlueprints/Buffs/PrimalBuff_Boost.PrimalBuff_Boost",
            "duration_sec": 30,
            "stacking": "none",
            "max_stacks": 1,
            "cooldown_sec": 10
        })");
    }

    nlohmann::json ValidDebuffStack()
    {
        return nlohmann::json::parse(R"({
            "name": "Poison",
            "type": "debuff",
            "buff_blueprint": "/Game/PrimalEarth/CoreBlueprints/Buffs/PrimalBuff_Poison.PrimalBuff_Poison",
            "duration_sec": 15,
            "stacking": "stack",
            "max_stacks": 3,
            "cooldown_sec": 0,
            "modifiers": [
                { "target": "health", "op": "multiply", "value": 0.9 }
            ]
        })");
    }

    nlohmann::json BaseValid()
    {
        nlohmann::json j;
        j["name"] = "Valide";
        j["type"] = "buff";
        j["buff_blueprint"] = "";
        j["duration_sec"] = 60;
        j["stacking"] = "refresh";
        j["max_stacks"] = 1;
        j["cooldown_sec"] = 0;
        j["modifiers"] = nlohmann::json::array({
            nlohmann::json{{"target", "stamina"}, {"op", "add"}, {"value", 10.0}}
        });
        j["conditions"] = nlohmann::json{{"min_level", 1}};
        return j;
    }

    void LoadSection(const nlohmann::json& section)
    {
        Registry::ResetForTests();
        Registry::LoadDefinitionsFromSection(&section);
    }

    bool HasId(const std::vector<Effect>& effects, const std::string& id)
    {
        return std::any_of(effects.begin(), effects.end(),
                           [&](const Effect& e) { return e.id == id; });
    }
}

// ---------------------------------------------------------------------------
// Parsing complet du schéma figé
// ---------------------------------------------------------------------------
TEST(Effects_Parse_HunterStewComplet)
{
    nlohmann::json section;
    section["hunter_stew"] = ValidHunterStew();
    LoadSection(section);

    EXPECT(HasEffect("hunter_stew") == true);
    EXPECT(HasEffect("missing") == false);

    const auto e = GetEffect("hunter_stew");
    EXPECT(e.has_value());
    if (e)
    {
        EXPECT(e->id == "hunter_stew");
        EXPECT(e->name == "Ragoût du chasseur");
        EXPECT(e->type == EffectType::Buff);
        EXPECT(e->buffBlueprint.empty());
        EXPECT(e->durationSeconds == 600);
        EXPECT(e->stacking == StackingMode::Refresh);
        EXPECT(e->maxStacks == 1);
        EXPECT(e->cooldownSeconds == 0);
        EXPECT(e->modifiers.size() == 2);
        if (e->modifiers.size() >= 2)
        {
            EXPECT(e->modifiers[0].target == "stamina");
            EXPECT(e->modifiers[0].op == rpframework::character::StatModifier::Op::Add);
            EXPECT(e->modifiers[0].value == 20.0f);
            EXPECT(e->modifiers[1].target == "speed");
            EXPECT(e->modifiers[1].op == rpframework::character::StatModifier::Op::Multiply);
            EXPECT(e->modifiers[1].value == 0.95f);
        }
        EXPECT(e->conditions.minLevel == 1);
    }

    const auto listed = ListEffects();
    EXPECT(listed.size() == 1);
    EXPECT(HasId(listed, "hunter_stew") == true);

    Registry::ResetForTests();
}

TEST(Effects_Parse_BuffBlueprintSansModifier)
{
    nlohmann::json section;
    section["ark_buff"] = ValidBuffBlueprintOnly();
    LoadSection(section);

    EXPECT(HasEffect("ark_buff") == true);
    const auto e = GetEffect("ark_buff");
    EXPECT(e.has_value());
    if (e)
    {
        EXPECT(e->modifiers.empty());
        EXPECT(e->buffBlueprint
            == "/Game/PrimalEarth/CoreBlueprints/Buffs/PrimalBuff_Boost.PrimalBuff_Boost");
        EXPECT(e->stacking == StackingMode::None);
        EXPECT(e->cooldownSeconds == 10);
        EXPECT(e->durationSeconds == 30);
    }

    Registry::ResetForTests();
}

TEST(Effects_Parse_ConditionsEtToJsonRoundTrip)
{
    nlohmann::json payload = ValidHunterStew();
    payload["conditions"] = {
        {"min_level", 5},
        {"max_level", 20},
        {"required_professions", {"hunter"}},
        {"excluded_races", {"dwarf"}}
    };

    const auto parsed = Effect::FromJson("hunter_stew", payload);
    EXPECT(parsed.conditions.minLevel == 5);
    EXPECT(parsed.conditions.maxLevel == 20);
    EXPECT(parsed.conditions.requiredProfessions.size() == 1);
    if (!parsed.conditions.requiredProfessions.empty())
        EXPECT(parsed.conditions.requiredProfessions[0] == "hunter");
    EXPECT(parsed.conditions.excludedRaces.size() == 1);
    if (!parsed.conditions.excludedRaces.empty())
        EXPECT(parsed.conditions.excludedRaces[0] == "dwarf");

    const auto round = Effect::FromJson(parsed.id, parsed.ToJson());
    EXPECT(round.name == parsed.name);
    EXPECT(round.type == parsed.type);
    EXPECT(round.durationSeconds == parsed.durationSeconds);
    EXPECT(round.stacking == parsed.stacking);
    EXPECT(round.maxStacks == parsed.maxStacks);
    EXPECT(round.cooldownSeconds == parsed.cooldownSeconds);
    EXPECT(round.modifiers.size() == parsed.modifiers.size());
    EXPECT(round.conditions.minLevel == 5);
}

TEST(Effects_Load_FromConfigSet)
{
    using namespace rpframework::core;

    nlohmann::json section;
    section["hunter_stew"] = ValidHunterStew();
    Config::Get().Set("effects", section);
    Registry::ResetForTests();
    Load();

    EXPECT(HasEffect("hunter_stew") == true);
    const auto e = GetEffect("hunter_stew");
    EXPECT(e.has_value());
    if (e) EXPECT(e->durationSeconds == 600);

    Registry::ResetForTests();
}

TEST(Effects_Load_SectionAbsenteVideLeRegistre)
{
    Registry::ResetForTests();
    nlohmann::json section;
    section["hunter_stew"] = ValidHunterStew();
    Registry::LoadDefinitionsFromSection(&section);
    EXPECT(HasEffect("hunter_stew") == true);

    Registry::LoadDefinitionsFromSection(nullptr);
    EXPECT(HasEffect("hunter_stew") == false);
    EXPECT(ListEffects().empty());

    Registry::ResetForTests();
}

TEST(Effects_Load_RejetteLesInvalidesGardeLesValides)
{
    nlohmann::json section;
    section["hunter_stew"] = ValidHunterStew();
    auto bad = BaseValid();
    bad["type"] = "aura";
    section["broken"] = bad;
    LoadSection(section);

    EXPECT(HasEffect("hunter_stew") == true);
    EXPECT(HasEffect("broken") == false);
    EXPECT(ListEffects().size() == 1);

    Registry::ResetForTests();
}

TEST(Effects_GetEffect_Inconnu)
{
    nlohmann::json section;
    section["hunter_stew"] = ValidHunterStew();
    LoadSection(section);
    EXPECT(GetEffect("nope").has_value() == false);
    Registry::ResetForTests();
}

// ---------------------------------------------------------------------------
// Trois modes de stacking
// ---------------------------------------------------------------------------
TEST(Effects_Stacking_TroisModes)
{
    nlohmann::json section;
    auto noneMode = BaseValid();
    noneMode["name"] = "Unique";
    noneMode["stacking"] = "none";
    noneMode["max_stacks"] = 1;

    auto refreshMode = ValidHunterStew();

    auto stackMode = ValidDebuffStack();

    section["unique_none"] = noneMode;
    section["hunter_stew"] = refreshMode;
    section["poison"] = stackMode;
    LoadSection(section);

    const auto none = GetEffect("unique_none");
    EXPECT(none.has_value());
    if (none)
    {
        EXPECT(none->stacking == StackingMode::None);
        EXPECT(none->maxStacks == 1);
        EXPECT(none->type == EffectType::Buff);
    }

    const auto refresh = GetEffect("hunter_stew");
    EXPECT(refresh.has_value());
    if (refresh)
    {
        EXPECT(refresh->stacking == StackingMode::Refresh);
        EXPECT(refresh->maxStacks == 1);
        EXPECT(refresh->type == EffectType::Buff);
    }

    const auto stacked = GetEffect("poison");
    EXPECT(stacked.has_value());
    if (stacked)
    {
        EXPECT(stacked->stacking == StackingMode::Stack);
        EXPECT(stacked->maxStacks == 3);
        EXPECT(stacked->type == EffectType::Debuff);
        EXPECT(stacked->buffBlueprint.empty() == false);
    }

    EXPECT(ListEffects().size() == 3);

    EXPECT(std::string(ToString(StackingMode::None)) == "none");
    EXPECT(std::string(ToString(StackingMode::Refresh)) == "refresh");
    EXPECT(std::string(ToString(StackingMode::Stack)) == "stack");

    Registry::ResetForTests();
}

// ---------------------------------------------------------------------------
// Chaque règle de rejet
// ---------------------------------------------------------------------------
TEST(Effects_Reject_IdVide)
{
    nlohmann::json section;
    section[""] = BaseValid();
    section["hunter_stew"] = ValidHunterStew();
    LoadSection(section);

    EXPECT(HasEffect("") == false);
    EXPECT(HasEffect("hunter_stew") == true);

    bool threw = false;
    try
    {
        (void)Effect::FromJson("", BaseValid());
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    EXPECT(threw == true);

    Registry::ResetForTests();
}

TEST(Effects_Reject_TypeInvalide)
{
    nlohmann::json section;
    auto missing = BaseValid();
    missing.erase("type");
    section["no_type"] = missing;

    auto bad = BaseValid();
    bad["type"] = "aura";
    section["bad_type"] = bad;

    auto notString = BaseValid();
    notString["type"] = 1;
    section["type_int"] = notString;

    section["ok"] = ValidHunterStew();
    LoadSection(section);

    EXPECT(HasEffect("no_type") == false);
    EXPECT(HasEffect("bad_type") == false);
    EXPECT(HasEffect("type_int") == false);
    EXPECT(HasEffect("ok") == true);

    Registry::ResetForTests();
}

TEST(Effects_Reject_StackingInvalide)
{
    nlohmann::json section;
    auto bad = BaseValid();
    bad["stacking"] = "add";
    section["bad_stack"] = bad;

    auto notString = BaseValid();
    notString["stacking"] = 2;
    section["stack_int"] = notString;

    section["ok"] = ValidHunterStew();
    LoadSection(section);

    EXPECT(HasEffect("bad_stack") == false);
    EXPECT(HasEffect("stack_int") == false);
    EXPECT(HasEffect("ok") == true);

    Registry::ResetForTests();
}

TEST(Effects_Reject_MaxStacksInferieurAUn)
{
    nlohmann::json section;
    auto zero = BaseValid();
    zero["max_stacks"] = 0;
    section["zero"] = zero;

    auto neg = BaseValid();
    neg["max_stacks"] = -1;
    section["neg"] = neg;

    section["ok"] = ValidHunterStew();
    LoadSection(section);

    EXPECT(HasEffect("zero") == false);
    EXPECT(HasEffect("neg") == false);
    EXPECT(HasEffect("ok") == true);

    Registry::ResetForTests();
}

TEST(Effects_Reject_DurationNegative)
{
    nlohmann::json section;
    auto bad = BaseValid();
    bad["duration_sec"] = -1;
    section["neg_duration"] = bad;
    section["ok"] = ValidHunterStew();
    LoadSection(section);

    EXPECT(HasEffect("neg_duration") == false);
    EXPECT(HasEffect("ok") == true);

    Registry::ResetForTests();
}

TEST(Effects_Reject_CooldownNegatif)
{
    nlohmann::json section;
    auto bad = BaseValid();
    bad["cooldown_sec"] = -5;
    section["neg_cd"] = bad;
    section["ok"] = ValidHunterStew();
    LoadSection(section);

    EXPECT(HasEffect("neg_cd") == false);
    EXPECT(HasEffect("ok") == true);

    Registry::ResetForTests();
}

TEST(Effects_Reject_NiModifierNiBuffBlueprint)
{
    nlohmann::json section;
    auto empty = BaseValid();
    empty["modifiers"] = nlohmann::json::array();
    empty["buff_blueprint"] = "";
    section["empty"] = empty;

    auto blankTargets = BaseValid();
    blankTargets["buff_blueprint"] = "";
    blankTargets["modifiers"] = nlohmann::json::array({
        nlohmann::json{{"target", ""}, {"op", "add"}, {"value", 5.0}}
    });
    section["blank_target"] = blankTargets;

    auto payloadNotObject = nlohmann::json("pas un objet");
    section["not_object"] = payloadNotObject;

    section["ok_mods"] = ValidHunterStew();
    section["ok_buff"] = ValidBuffBlueprintOnly();
    LoadSection(section);

    EXPECT(HasEffect("empty") == false);
    EXPECT(HasEffect("blank_target") == false);
    EXPECT(HasEffect("not_object") == false);
    EXPECT(HasEffect("ok_mods") == true);
    EXPECT(HasEffect("ok_buff") == true);

    Registry::ResetForTests();
}

TEST(Effects_Accept_DurationZeroPermanent)
{
    nlohmann::json section;
    auto permanent = BaseValid();
    permanent["duration_sec"] = 0;
    section["permanent"] = permanent;
    LoadSection(section);

    EXPECT(HasEffect("permanent") == true);
    const auto e = GetEffect("permanent");
    EXPECT(e.has_value());
    if (e) EXPECT(e->durationSeconds == 0);

    Registry::ResetForTests();
}

TEST(Effects_Conversion_TypeEtStacking)
{
    EffectType type = EffectType::Debuff;
    EXPECT(EffectTypeFromString("buff", type) == true);
    EXPECT(type == EffectType::Buff);
    EXPECT(EffectTypeFromString("debuff", type) == true);
    EXPECT(type == EffectType::Debuff);
    EXPECT(EffectTypeFromString("aura", type) == false);
    EXPECT(std::string(ToString(EffectType::Buff)) == "buff");
    EXPECT(std::string(ToString(EffectType::Debuff)) == "debuff");

    StackingMode mode = StackingMode::None;
    EXPECT(StackingModeFromString("none", mode) == true);
    EXPECT(mode == StackingMode::None);
    EXPECT(StackingModeFromString("refresh", mode) == true);
    EXPECT(mode == StackingMode::Refresh);
    EXPECT(StackingModeFromString("stack", mode) == true);
    EXPECT(mode == StackingMode::Stack);
    EXPECT(StackingModeFromString("add", mode) == false);
}
