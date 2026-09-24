#include "field/trainer_catalog.h"
#include "field/area.h"
#include "field/map_catalog.h"
namespace studio {
TrainerBattleEntry decode_trainer_battle(unsigned id, View record, View team) {
    require(id > 0 && id <= 65535, "Choose an existing trainer ID");
    require(record.size() == TargetProfile::trainer_record_size, "Unsupported trainer record size");
    TrainerBattleEntry entry;
    entry.id = id;
    entry.category = u16(record, 0);
    entry.mode = record[2];
    require(entry.mode <= 1, "This battle rule needs partner or special setup; choose a single or double trainer battle");
    unsigned count = record[3];
    require(count > 0 && count <= 6, "Trainer needs a team of one to six Pokemon");
    require(team.size() == count * TargetProfile::trainer_pokemon_size,
            "Trainer team size does not match its record");
    for (unsigned i = 0; i < count; ++i) {
        auto at = i * TargetProfile::trainer_pokemon_size;
        TrainerPokemon pokemon{u16(team, at + 16), team[at + 18], team[at + 14]};
        require(pokemon.species > 0 && pokemon.level > 0 && pokemon.level <= 100,
                "Trainer team contains an invalid species or level");
        entry.team.push_back(pokemon);
    }
    return entry;
}
TrainerBattleEntry load_trainer_battle(const std::filesystem::path &source, unsigned id) {
    Archive records(source / TargetProfile::trainer_records_archive);
    Archive teams(source / TargetProfile::trainer_teams_archive);
    require(id < records.size() && id < teams.size(), "Trainer ID is absent from the project");
    return decode_trainer_battle(id, records.raw(id), teams.raw(id));
}
std::vector<TrainerBattleEntry> load_trainer_battles(const std::filesystem::path &source) {
    Archive records(source / TargetProfile::trainer_records_archive);
    Archive teams(source / TargetProfile::trainer_teams_archive);
    std::vector<std::string> names, classes;
    try {
        Archive text(source / TargetProfile::location_text_archive);
        names = decode_location_text(text.decoded(TargetProfile::trainer_names_member));
        classes = decode_location_text(text.decoded(TargetProfile::trainer_classes_member));
    } catch (const std::exception &) {
    }
    std::vector<TrainerBattleEntry> entries;
    for (unsigned id = 1; id < records.size() && id <= 65535; ++id) {
        TrainerBattleEntry entry;
        entry.id = id;
        try {
            require(id < teams.size(), "Trainer team is missing");
            entry = decode_trainer_battle(id, records.raw(id), teams.raw(id));
        } catch (const std::exception &e) {
            entry.error = e.what();
        }
        entry.name = "Trainer " + std::to_string(id);
        if (entry.category < classes.size())
            entry.name += " / " + classes[entry.category];
        if (id < names.size())
            entry.name += " " + names[id];
        entries.push_back(std::move(entry));
    }
    return entries;
}
}
