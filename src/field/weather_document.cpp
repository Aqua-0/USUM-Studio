#include "field/weather_document.h"
#include "field/area.h"
#include "formats/compression.h"
#include "core/digest.h"
#include <cmath>
#include <set>
#include <sstream>
namespace studio {
unsigned weather_period(float hour) {
    require(std::isfinite(hour) && hour >= 0 && hour <= 24, "Time must be between 0 and 24 hours");
    if (hour < 6 || hour == 24)
        return 4;
    if (hour < 10)
        return 0;
    if (hour < 17)
        return 1;
    if (hour < 18)
        return 2;
    return 3;
}
const char *weather_period_name(unsigned period) {
    static constexpr const char *names[]{"Morning (06–10)", "Daytime (10–17)", "Evening (17–18)",
                                         "Night (18–24)", "Midnight (00–06)"};
    require(period < std::size(names), "Invalid weather time slot");
    return names[period];
}
WeatherDocument::WeatherDocument(Bytes original)
    : original_(std::move(original)), hash_(sha256(original_)) {
    require(!original_.empty() && original_.size() % 84 == 0, "Unsupported zone weather table");
}
std::size_t WeatherDocument::offset(unsigned zone, unsigned period) const {
    require(zone < original_.size() / 84 && period < 5, "Zone weather entry is out of range");
    return std::size_t(zone) * 84 + 36 + period * 4;
}
WeatherSchedule WeatherDocument::schedule(unsigned zone) const {
    WeatherSchedule result;
    for (unsigned period = 0; period < result.size(); ++period) {
        auto at = offset(zone, period);
        auto it = current_.find({zone, period});
        result[period] = it == current_.end() ? u32(original_, at) : it->second;
    }
    return result;
}
void WeatherDocument::commit(Changes next) {
    if (next == current_)
        return;
    current_ = std::move(next);
    history_.resize(++cursor_);
    history_.push_back(current_);
}
void WeatherDocument::set(unsigned zone, unsigned period, unsigned kind) {
    auto at = offset(zone, period);
    require(kind < TargetProfile::weather_kinds, "Unsupported weather kind");
    auto next = current_;
    if (kind == u32(original_, at))
        next.erase({zone, period});
    else
        next[{zone, period}] = kind;
    commit(std::move(next));
}
void WeatherDocument::reset(unsigned zone) {
    offset(zone, 0);
    auto next = current_;
    std::erase_if(next, [zone](auto &entry) {
        return entry.first.first == zone;
    });
    commit(std::move(next));
}
void WeatherDocument::undo() {
    if (can_undo())
        current_ = history_[--cursor_];
}
void WeatherDocument::redo() {
    if (can_redo())
        current_ = history_[++cursor_];
}
Bytes WeatherDocument::compile() const {
    auto bytes = original_;
    for (auto &[key, value] : current_)
        put32(bytes, offset(key.first, key.second), value);
    return bytes;
}
std::string WeatherDocument::serialize() const {
    std::ostringstream out;
    out << "USUMSTUDIO_WEATHER 1\nsource " << hash_ << '\n';
    for (auto &[key, value] : current_)
        out << "weather " << key.first << ' ' << key.second << ' ' << value << '\n';
    out << "end\n";
    return out.str();
}
void WeatherDocument::restore(const std::string &patch) {
    std::istringstream in(patch);
    std::string word, hash;
    unsigned version;
    require(bool(in >> word >> version) && word == "USUMSTUDIO_WEATHER" && version == 1,
            "Unsupported weather document");
    require(bool(in >> word >> hash) && word == "source" && hash == hash_,
            "Zone weather source changed; reopen the matching project source");
    Changes next;
    std::set<std::pair<unsigned, unsigned>> seen;
    bool ended = false;
    while (in >> word) {
        if (word == "end") {
            ended = true;
            break;
        }
        unsigned zone, period, kind;
        require(word == "weather" && bool(in >> zone >> period >> kind) &&
                    kind < TargetProfile::weather_kinds,
                "Invalid zone weather change");
        auto at = offset(zone, period);
        require(seen.insert({zone, period}).second, "Repeated zone weather entry");
        if (kind != u32(original_, at))
            next[{zone, period}] = kind;
    }
    in >> std::ws;
    require(ended && in.eof(), "Incomplete weather document");
    current_ = saved_ = std::move(next);
    history_ = {current_};
    cursor_ = 0;
}
void WeatherDocument::export_to(const std::filesystem::path &dump,
                                const std::filesystem::path &output) const {
    if (!changed())
        return;
    auto source = dump / TargetProfile::zone_archive, target = output / TargetProfile::zone_archive;
    require(std::filesystem::absolute(source).lexically_normal() !=
                std::filesystem::absolute(target).lexically_normal(),
            "Choose a separate weather export folder");
    Archive archive(source);
    require(sha256(archive.decoded(0)) == hash_,
            "Zone weather source changed; reload before exporting");
    std::filesystem::create_directories(target.parent_path());
    auto bytes = compile();
    auto raw = archive.raw(0);
    if (!raw.empty() && raw.front() == 0x11)
        bytes = compress(bytes);
    require(decompress(bytes) == compile(), "Weather compression readback failed");
    archive.export_to(target, {{0, std::move(bytes)}});
}
}
