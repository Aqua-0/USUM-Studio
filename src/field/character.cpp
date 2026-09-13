#include "field/character.h"
#include "formats/compression.h"
namespace studio {
CharacterAsset CharacterAsset::read(const Archive &archive, std::size_t index,
                                    const std::string &asset) {
    require(index < archive.size() / TargetProfile::area_stride, "Area index is out of range");
    CharacterAsset result;
    result.member = index * TargetProfile::area_stride + TargetProfile::character_resource_slot;
    result.area = Container::parse(archive.decoded(result.member), "AC");
    bool found = false;
    for (std::size_t i = 0; i < result.area.files.size(); ++i) {
        auto character = Container::parse(result.area.files[i], "CP");
        require(character.files.size() == 2, "Unsupported character pack shape");
        auto models = Container::parse(character.files[1], "CM");
        require(models.files.size() == 6, "Unsupported character model pack shape");
        auto pack = ModelPack::parse(models.files[0]);
        for (std::size_t j = 0; j < pack.resources.size(); ++j)
            if (pack.resources[j].category == 0 && pack.resources[j].name == asset) {
                require(!found, "Ambiguous character asset name");
                found = true;
                result.actor = i;
                result.model_resource = j;
                result.character = character;
                result.models = models;
                result.pack = pack;
                result.skin = SkinnedModel::parse(pack.resources[j].bytes);
            }
    }
    require(found, "Character asset is not present in this area");
    return result;
}
Bytes CharacterAsset::rebuild(const std::string &joint, std::array<float, 3> offset,
                              bool descendants) const {
    auto model = skin.rebuild(joint, offset, descendants);
    return replace_pack(pack.replace(model_resource, model, 128));
}
Bytes CharacterAsset::replace_pack(View replacement) const {
    auto rebuilt_models = models;
    rebuilt_models.files[0] = Bytes(replacement.begin(), replacement.end());
    auto rebuilt_character = character;
    rebuilt_character.files[1] = rebuilt_models.write(128);
    auto rebuilt_area = area;
    rebuilt_area.files[actor] = rebuilt_character.write(128);
    auto bytes = rebuilt_area.write(128);
    auto checked = Container::parse(bytes, "AC");
    for (std::size_t i = 0; i < area.files.size(); ++i)
        require(i == actor || checked.files[i] == area.files[i],
                "Unrelated character resource changed");
    auto checked_character = Container::parse(checked.files[actor], "CP");
    require(checked_character.files[0] == character.files[0], "Character metadata changed");
    auto checked_models = Container::parse(checked_character.files[1], "CM");
    for (std::size_t i = 1; i < models.files.size(); ++i)
        require(checked_models.files[i] == models.files[i],
                "Character animations or sibling resources changed");
    auto result = compress(bytes);
    require(decompress(result) == bytes, "Character compression readback mismatch");
    return result;
}
}
