#include "assets/character_registration.h"
#include "core/digest.h"
#include "formats/container.h"
#include "formats/compression.h"
#include <sstream>
namespace studio {
CharacterArchiveLayout read_character_archive_layout(const Archive &archive) {
    require(archive.size() >= 4, "Character archive is missing its registration tables");
    for (std::size_t i = 0; i < archive.size(); ++i)
        require(archive.subfiles(i) == std::vector<unsigned>{0},
                "Character registration requires single-subfile resources");
    auto footer = archive.decoded(archive.size() - 1);
    require(footer.size() == 8, "Unsupported character archive footer");
    CharacterArchiveLayout layout;
    layout.models = u32(footer, 0);
    layout.animations = u32(footer, 4);
    require(layout.models >= 2 &&
                std::uint64_t(layout.models) + layout.animations + 2 == archive.size(),
            "Character archive counts do not match its resources");
    auto offsets = archive.decoded(archive.size() - 2);
    require(offsets.size() == (std::size_t(layout.models) + 1) * 4,
            "Character animation offsets do not match the model count");
    for (unsigned i = 0; i <= layout.models; ++i) {
        auto offset = u32(offsets, i * 4);
        require(offset >= layout.models && offset <= layout.models + layout.animations &&
                    (i == 0 || offset >= layout.animation_offsets.back()),
                "Invalid character animation offset");
        layout.animation_offsets.push_back(offset);
    }
    require(layout.animation_offsets.front() == layout.models &&
                layout.animation_offsets.back() == layout.models + layout.animations,
            "Character animation offsets leave unassigned resources");
    return layout;
}
CharacterRegistration plan_character_registration(const Archive &archive, unsigned donor) {
    auto layout = read_character_archive_layout(archive);
    require(donor > 1 && donor < layout.models, "Choose a non-player character donor");
    auto model = archive.decoded(donor);
    auto package = Container::parse(model, "CM");
    require(package.files.size() == 6 && !package.files[0].empty() && !package.files[3].empty(),
            "Donor must contain a complete character model and behavior metadata");
    auto count = layout.animation_offsets[donor + 1] - layout.animation_offsets[donor];
    require(archive.size() + count + 1 <= 65535,
            "Character registration exceeds the archive capacity");
    auto identity = sha256(archive.layout_identity()) + sha256(model) +
                    sha256(archive.decoded(archive.size() - 2)) +
                    sha256(archive.decoded(archive.size() - 1));
    for (unsigned i = layout.animation_offsets[donor]; i < layout.animation_offsets[donor + 1]; ++i)
        identity += sha256(archive.raw(i));
    return {donor, layout.models, sha256(Bytes(identity.begin(), identity.end()))};
}
std::string CharacterRegistration::serialize() const {
    std::ostringstream out;
    out << "USUMSTUDIO_CHARACTER_REGISTRATION " << (replacement_model.empty() ? 1 : 2) << "\ndonor "
        << donor << "\ncharacter " << character << "\nsource " << source_identity << "\nend\n";
    if (!replacement_model.empty()) {
        out << replacement_model.size() << '\n';
        out.write(reinterpret_cast<const char *>(replacement_model.data()),
                  replacement_model.size());
    }
    return out.str();
}
CharacterRegistration CharacterRegistration::parse(const std::string &text) {
    std::istringstream in(text);
    std::string word;
    unsigned version;
    CharacterRegistration plan;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_CHARACTER_REGISTRATION" &&
                (version == 1 || version == 2),
            "Unsupported character registration document");
    require(bool(in >> word >> plan.donor) && word == "donor" &&
                bool(in >> word >> plan.character) && word == "character" &&
                bool(in >> word >> plan.source_identity) && word == "source" && bool(in >> word) &&
                word == "end",
            "Invalid character registration document");
    if (version == 2) {
        std::size_t size = 0;
        require(bool(in >> size) && size > 0 && size <= text.size(),
                "Invalid character model size");
        require(in.get() == '\n', "Invalid character model boundary");
        plan.replacement_model.resize(size);
        require(bool(in.read(reinterpret_cast<char *>(plan.replacement_model.data()), size)),
                "Incomplete character model");
    }
    in >> std::ws;
    require(in.eof(), "Unexpected character registration data");
    return plan;
}
void export_character_registration(const Archive &archive, const CharacterRegistration &plan,
                                   const std::filesystem::path &output) {
    auto baseline = plan;
    baseline.replacement_model.clear();
    require(plan_character_registration(archive, plan.donor) == baseline,
            "Character resources changed. Recreate the registration against the current project "
            "source.");
    auto layout = read_character_archive_layout(archive);
    std::map<std::pair<std::size_t, unsigned>, Bytes> changes;
    if (!plan.replacement_model.empty()) {
        auto model = Container::parse(plan.replacement_model, "CM");
        require(model.files.size() == 6 && !model.files[0].empty() && !model.files[1].empty() &&
                    model.files[3].size() >= 128 &&
                    (u32(model.files[3], 0) == 1 ||
                     (model.files[3].size() >= 132 && u32(model.files[3], 0) == 2)),
                "Converted character must contain a complete NPC or Pokemon overworld package");
    }
    changes[{layout.models, 0}] =
        plan.replacement_model.empty() ? archive.raw(plan.donor) : compress(plan.replacement_model);
    for (unsigned i = layout.models; i < layout.models + layout.animations; ++i)
        changes[{i + 1, 0}] = archive.raw(i);
    auto next = layout.models + layout.animations + 1;
    for (unsigned i = layout.animation_offsets[plan.donor];
         plan.replacement_model.empty() && i < layout.animation_offsets[plan.donor + 1]; ++i)
        changes[{next++, 0}] = archive.raw(i);
    Bytes offsets((std::size_t(layout.models) + 2) * 4);
    for (unsigned i = 0; i <= layout.models; ++i)
        put32(offsets, i * 4, layout.animation_offsets[i] + 1);
    put32(offsets, (layout.models + 1) * 4, next);
    Bytes footer(8);
    put32(footer, 0, layout.models + 1);
    put32(footer, 4, next - layout.models - 1);
    auto store = [](View original, const Bytes &value) {
        return !original.empty() && (original[0] == 0x10 || original[0] == 0x11) ? compress(value)
                                                                                 : value;
    };
    changes[{next, 0}] = store(archive.raw(archive.size() - 2), offsets);
    changes[{next + 1, 0}] = store(archive.raw(archive.size() - 1), footer);
    std::filesystem::create_directories(output.parent_path());
    archive.export_appended(output, changes);
    Archive check(output);
    auto verified = read_character_archive_layout(check);
    require(verified.models == layout.models + 1 &&
                check.raw(plan.character) == changes.at({layout.models, 0}),
            "Character registration readback failed");
    for (const auto &[member, data] : changes)
        require(check.raw(member.first, member.second) == data,
                "Character resource readback differs");
}
}
