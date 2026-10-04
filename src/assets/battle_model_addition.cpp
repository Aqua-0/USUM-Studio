#include "assets/battle_model_addition.h"
#include "core/digest.h"
#include "assets/character_registration.h"
#include "formats/compression.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace studio {
namespace {
const char *category_name(ModelCategory category) {
    require(category == ModelCategory::BattleCharacters || category == ModelCategory::PokeBalls,
            "Choose a battle character or Poke Ball donor");
    return category == ModelCategory::PokeBalls ? "poke-ball" : "battle-character";
}
}
BattleModelAddition plan_battle_model_addition(const Archive &archive, unsigned donor,
                                               ModelCategory category) {
    category_name(category);
    const auto layout = read_character_archive_layout(archive);
    require(donor < layout.models, "The donor is missing from the battle model archive");
    const auto motions = layout.animation_offsets[donor + 1] - layout.animation_offsets[donor];
    require(archive.size() + motions + 1 <= 65535,
            "The battle model archive has no room for this model and its motions");
    require(archive.subfiles(donor) == std::vector<unsigned>{0},
            "Choose a single-package battle model donor");
    auto raw = archive.raw(donor);
    auto package = Container::parse(archive.decoded(donor), "CM");
    require(package.files.size() == 6 && !package.files.front().empty(),
            "Choose a complete battle model package");
    auto pack = ModelPack::parse(package.files.front());
    auto model = std::find_if(pack.resources.begin(), pack.resources.end(), [](const auto &r) {
        return r.category == 0;
    });
    require(model != pack.resources.end() && model_category_matches(category, model->name),
            "The donor does not belong to the selected model category");
    require(!model->name.starts_with("p1_") && !model->name.starts_with("p2_"),
            "Choose a complete trainer model; player outfits use a separate assembly system");
    auto identity = sha256(archive.layout_identity()) + sha256(raw) +
                    sha256(archive.decoded(archive.size() - 2)) +
                    sha256(archive.decoded(archive.size() - 1));
    for (unsigned i = layout.animation_offsets[donor]; i < layout.animation_offsets[donor + 1]; ++i)
        identity += sha256(archive.raw(i));
    return {category, donor, layout.models,
            sha256(Bytes(identity.begin(), identity.end())), model->name};
}
std::string BattleModelAddition::serialize() const {
    std::ostringstream out;
    out << "USUMSTUDIO_BATTLE_MODEL_ADDITION 1\ncategory " << category_name(category)
        << "\ndonor " << donor << "\nmember " << member << "\nsource " << source_identity
        << "\nname " << std::quoted(name) << "\nend\n";
    return out.str();
}
BattleModelAddition BattleModelAddition::parse(const std::string &text) {
    std::istringstream in(text);
    std::string word, category;
    unsigned version;
    BattleModelAddition plan;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_BATTLE_MODEL_ADDITION" && version == 1,
            "Unsupported battle model addition document");
    require(bool(in >> word >> category) && word == "category" &&
                (category == "battle-character" || category == "poke-ball") &&
                bool(in >> word >> plan.donor) && word == "donor" &&
                bool(in >> word >> plan.member) && word == "member" &&
                bool(in >> word >> plan.source_identity) && word == "source" &&
                bool(in >> word >> std::quoted(plan.name)) && word == "name" &&
                bool(in >> word) && word == "end",
            "Invalid battle model addition document");
    plan.category = category == "poke-ball" ? ModelCategory::PokeBalls : ModelCategory::BattleCharacters;
    in >> std::ws;
    require(in.eof(), "Unexpected battle model addition data");
    return plan;
}
void export_battle_model_addition(const Archive &archive, const BattleModelAddition &plan,
                                  const std::filesystem::path &output) {
    require(plan_battle_model_addition(archive, plan.donor, plan.category) == plan,
            "Battle model resources changed. Reload the donor and recreate the addition.");
    std::filesystem::create_directories(output.parent_path());
    const auto layout = read_character_archive_layout(archive);
    std::map<std::pair<std::size_t, unsigned>, Bytes> changes;
    changes[{plan.member, 0}] = archive.raw(plan.donor);
    for (unsigned i = layout.models; i < layout.models + layout.animations; ++i)
        changes[{i + 1, 0}] = archive.raw(i);
    auto next = layout.models + layout.animations + 1;
    for (unsigned i = layout.animation_offsets[plan.donor]; i < layout.animation_offsets[plan.donor + 1]; ++i)
        changes[{next++, 0}] = archive.raw(i);
    Bytes offsets((std::size_t(layout.models) + 2) * 4), footer(8);
    for (unsigned i = 0; i <= layout.models; ++i)
        put32(offsets, i * 4, layout.animation_offsets[i] + 1);
    put32(offsets, (layout.models + 1) * 4, next);
    put32(footer, 0, layout.models + 1);
    put32(footer, 4, next - layout.models - 1);
    auto store = [](View original, const Bytes &value) {
        return !original.empty() && (original[0] == 0x10 || original[0] == 0x11) ? compress(value) : value;
    };
    changes[{next, 0}] = store(archive.raw(archive.size() - 2), offsets);
    changes[{next + 1, 0}] = store(archive.raw(archive.size() - 1), footer);
    archive.export_appended(output, changes);
    Archive check(output);
    const auto verified = read_character_archive_layout(check);
    require(verified.models == layout.models + 1 && check.raw(plan.member) == archive.raw(plan.donor),
            "Battle model addition readback failed");
    for (const auto &[member, data] : changes)
        require(check.raw(member.first, member.second) == data, "Battle model or motion readback differs");
}
}
