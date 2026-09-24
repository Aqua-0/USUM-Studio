#include "audio/audio_document.h"
#include "core/digest.h"
#include <algorithm>
namespace studio {
namespace {
void compatible(View original, View replacement) {
    auto a = decode_music(original), b = decode_music(replacement);
    require(
        a.channels == b.channels && a.rate == b.rate && a.frames() == b.frames() &&
            a.looping == b.looping && a.loop_start == b.loop_start,
        "Replacement must retain the source channel count, sample rate, duration and loop points");
}
}
void require_audio_output(const std::filesystem::path &dump, const std::filesystem::path &output) {
    auto source = std::filesystem::weakly_canonical(dump);
    for (auto p = std::filesystem::weakly_canonical(output); !p.empty();) {
        require(p != source, "Choose an output outside the source dump");
        auto parent = p.parent_path();
        if (parent == p)
            break;
        p = parent;
    }
}
AudioDocument::AudioDocument(const std::filesystem::path &source) : dump(source) {
    std::map<std::string, std::vector<unsigned>> files;
    for (auto &[id, track] : read_music_catalog(dump))
        files[track.file].push_back(id);
    for (auto &[file, ids] : files)
        tracks.push_back({file, ids, file, file});
    effects_ = std::make_shared<Bytes>(read_file(dump / "romfs/data/sound/niji_sound.bcsar"));
    effects_hash_ = sha256(*effects_);
    View b = *effects_;
    auto ref = [&](std::size_t p, std::size_t base) {
        auto at = base + std::size_t(u32(b, p + 4));
        slice(b, at, 1);
        return at;
    };
    auto block = [&](unsigned type) {
        for (unsigned i = 0; i < u16(b, 16); ++i)
            if (u16(b, 20 + i * 12) == type)
                return std::size_t(u32(b, 24 + i * 12));
        return std::size_t(-1);
    };
    auto info = block(0x2001), data = block(0x2002);
    if (data == std::size_t(-1))
        return;
    slice(b, data, 8);
    auto base = info + 8, table = ref(base + 48, base), sounds = ref(base, base),
         banks = ref(base + 16, base), waves = ref(base + 24, base);
    std::map<unsigned, std::vector<unsigned>> uses;
    std::map<std::pair<unsigned, unsigned>, std::vector<unsigned>> sample_uses;
    auto embedded_file = [&](unsigned id) -> View {
        require(id < u32(b, table), "Sound bank references a missing file");
        auto f = ref(table + 4 + id * 8, table);
        if (u16(b, f) != 0x220c)
            return {};
        auto location = ref(f, f), offset = std::size_t(u32(b, location + 4));
        if (offset == 0xffffffff)
            return {};
        return slice(b, data + 8 + offset, u32(b, location + 8));
    };
    for (unsigned i = 0; i < u32(b, sounds); ++i) {
        auto sound = ref(sounds + 4 + i * 8, sounds);
        if (u16(b, sound + 12) != 0x2203)
            continue;
        auto detail = ref(sound + 12, sound), bank_table = ref(detail, detail);
        for (unsigned k = 0; k < u32(b, bank_table); ++k) {
            auto bank = u32(b, bank_table + 4 + k * 4) & 0xffffff;
            if (bank >= u32(b, banks))
                continue;
            auto bank_info = ref(banks + 4 + bank * 8, banks),
                 wave_table = ref(bank_info + 4, bank_info);
            auto bank_bytes = embedded_file(u32(b, bank_info));
            if (bank_bytes.size() >= 20 && text(slice(bank_bytes, 0, 4)) == "CBNK") {
                for (unsigned section = 0; section < u16(bank_bytes, 16); ++section) {
                    if (u16(bank_bytes, 20 + section * 12) != 0x5800)
                        continue;
                    auto info_base = std::size_t(u32(bank_bytes, 24 + section * 12)) + 8;
                    auto offset = u32(bank_bytes, info_base + 4);
                    if (offset == 0xffffffff)
                        continue;
                    auto wave_ids = info_base + offset;
                    auto count = u32(bank_bytes, wave_ids);
                    slice(bank_bytes, wave_ids + 4, std::size_t(count) * 8);
                    for (unsigned j = 0; j < count; ++j) {
                        auto wave = u32(bank_bytes, wave_ids + 4 + j * 8) & 0xffffff;
                        auto sample = u32(bank_bytes, wave_ids + 8 + j * 8);
                        require(wave < u32(b, waves), "Bank references a missing wave archive");
                        auto wave_info = ref(waves + 4 + wave * 8, waves);
                        auto &ids = sample_uses[{u32(b, wave_info), sample}];
                        if (ids.empty() || ids.back() != i)
                            ids.push_back(i);
                    }
                }
            }
            for (unsigned j = 0; j < u32(b, wave_table); ++j) {
                auto wave = u32(b, wave_table + 4 + j * 4) & 0xffffff;
                if (wave >= u32(b, waves))
                    continue;
                auto wave_info = ref(waves + 4 + wave * 8, waves);
                auto &ids = uses[u32(b, wave_info)];
                if (ids.empty() || ids.back() != i)
                    ids.push_back(i);
            }
        }
    }
    for (unsigned i = 0; i < u32(b, table); ++i) {
        auto f = ref(table + 4 + i * 8, table);
        if (u16(b, f) != 0x220c)
            continue;
        auto location = ref(f, f);
        auto offset = u32(b, location + 4);
        if (offset == 0xffffffff)
            continue;
        auto start = data + 8 + offset;
        auto archive = slice(b, start, u32(b, location + 8));
        if (text(slice(archive, 0, 4)) != "CWAR")
            continue;
        std::size_t wi = 0, wf = 0;
        for (unsigned k = 0; k < u16(archive, 16); ++k) {
            auto type = u16(archive, 20 + k * 12);
            if (type == 0x6800)
                wi = u32(archive, 24 + k * 12);
            if (type == 0x6801)
                wf = u32(archive, 24 + k * 12);
        }
        require(wi && wf, "Sound sample archive blocks are missing");
        for (unsigned sample = 0; sample < u32(archive, wi + 8); ++sample) {
            auto entry = wi + 12 + sample * 12;
            auto at = start + wf + 8 + u32(archive, entry + 4);
            auto size = u32(archive, entry + 8);
            require(at >= start && at + size <= start + archive.size(),
                    "Sound sample exceeds its wave archive");
            auto bytes = slice(b, at, size);
            require(text(slice(bytes, 0, 4)) == "CWAV", "Sound sample is not a native wave");
            auto ids = uses[i];
            for (auto sound : sample_uses[{i, sample}])
                if (std::find(ids.begin(), ids.end(), sound) == ids.end())
                    ids.push_back(sound);
            auto label = ids.empty() ? "Sound sample file " + std::to_string(i)
                                     : "Sound " + std::to_string(ids.front());
            label += " / sample " + std::to_string(sample);
            auto key = "niji_sound.bcsar#" + std::to_string(at);
            tracks.push_back({"niji_sound.bcsar", std::move(ids), label, key, at, size, true});
        }
    }
}
Bytes AudioDocument::original(std::size_t index) const {
    auto &track = tracks.at(index);
    if (track.effect) {
        auto bytes = slice(*effects_, track.offset, track.size);
        return {bytes.begin(), bytes.end()};
    }
    return read_file(dump / "romfs/data/sound" / track.file);
}
Bytes AudioDocument::current(std::size_t index) const {
    auto it = edits_.find(index);
    return it == edits_.end() ? original(index) : it->second.bytes;
}
void AudioDocument::replace(std::size_t index, Bytes bytes) {
    auto source = original(index);
    if (tracks.at(index).effect)
        validate_sound_wave_replacement(source, bytes);
    else
        compatible(source, bytes);
    if (bytes == current(index))
        return;
    undo_.push_back(edits_);
    redo_.clear();
    if (bytes == source)
        edits_.erase(index);
    else
        edits_[index] = {sha256(source), std::move(bytes)};
}
void AudioDocument::reset(std::size_t index) {
    if (!edits_.contains(index))
        return;
    undo_.push_back(edits_);
    redo_.clear();
    edits_.erase(index);
}
bool AudioDocument::undo() {
    if (undo_.empty())
        return false;
    redo_.push_back(edits_);
    edits_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}
bool AudioDocument::redo() {
    if (redo_.empty())
        return false;
    undo_.push_back(edits_);
    edits_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}
void AudioDocument::discard() {
    edits_ = saved_;
    undo_.clear();
    redo_.clear();
}
void AudioDocument::save(const std::filesystem::path &path) {
    require_audio_output(dump, path);
    Bytes b{'U', 'S', 'U', 'M', 'A', 'U', 'D', 0};
    append32(b, 1);
    append32(b, narrow(edits_.size()));
    for (auto &[index, edit] : edits_) {
        auto &file = tracks.at(index).key;
        append32(b, narrow(file.size()));
        append32(b, narrow(edit.bytes.size()));
        b.insert(b.end(), file.begin(), file.end());
        b.insert(b.end(), edit.hash.begin(), edit.hash.end());
        append(b, edit.bytes);
    }
    write_file_atomic(path, b);
    saved_ = edits_;
}
void AudioDocument::load(const std::filesystem::path &path) {
    auto b = read_file(path);
    require(text(slice(b, 0, 7)) == "USUMAUD" && b[7] == 0 && u32(b, 8) == 1,
            "Unsupported audio project");
    auto count = u32(b, 12);
    require(count <= tracks.size(), "Invalid audio replacement count");
    State next;
    std::size_t p = 16;
    for (unsigned i = 0; i < count; ++i) {
        auto name_size = u32(b, p), size = u32(b, p + 4);
        p += 8;
        auto name = text(slice(b, p, name_size));
        p += name_size;
        auto hash = text(slice(b, p, 64));
        p += 64;
        auto bytes = slice(b, p, size);
        p += size;
        auto found = std::find_if(tracks.begin(), tracks.end(), [&](auto &t) {
            return t.key == name;
        });
        require(found != tracks.end(), "Audio project track is missing from this dump");
        auto index = std::size_t(found - tracks.begin());
        require(!next.contains(index), "Duplicate audio replacement");
        auto source = original(index);
        require(sha256(source) == hash, "Audio project source differs from this dump");
        if (tracks[index].effect)
            validate_sound_wave_replacement(source, bytes);
        else
            compatible(source, bytes);
        next[index] = {hash, Bytes(bytes.begin(), bytes.end())};
    }
    require(p == b.size(), "Audio project has trailing data");
    edits_ = saved_ = std::move(next);
    undo_.clear();
    redo_.clear();
}
void AudioDocument::export_to(const std::filesystem::path &folder) const {
    require(!edits_.empty(), "No audio replacements to export");
    auto root = std::filesystem::weakly_canonical(dump),
         destination = std::filesystem::weakly_canonical(folder);
    for (auto p = destination; !p.empty();) {
        require(p != root, "Choose an export folder outside the source dump");
        auto parent = p.parent_path();
        if (parent == p)
            break;
        p = parent;
    }
    bool effects = false;
    for (auto &[index, edit] : edits_) {
        auto &track = tracks.at(index);
        require(sha256(original(index)) == edit.hash, "Audio source changed since editing");
        require(!std::filesystem::exists(folder / "romfs/data/sound" / track.file),
                "Choose a fresh export folder");
        effects |= track.effect;
    }
    Bytes archive;
    if (effects) {
        archive = read_file(dump / "romfs/data/sound/niji_sound.bcsar");
        require(sha256(archive) == effects_hash_, "Sound archive changed since loading");
    }
    for (auto &[index, edit] : edits_) {
        auto &track = tracks.at(index);
        if (track.effect) {
            require(edit.bytes.size() == track.size, "Sound sample size changed");
            std::copy(edit.bytes.begin(), edit.bytes.end(), archive.begin() + track.offset);
        } else
            write_new_file(folder / "romfs/data/sound" / track.file, edit.bytes);
    }
    if (effects) {
        auto output = folder / "romfs/data/sound/niji_sound.bcsar";
        write_new_file(output, archive);
        require(read_file(output) == archive, "Sound archive export verification failed");
    }
}
Bytes export_audio_wav(const MusicSamples &samples) {
    require(samples.channels >= 1 && samples.channels <= 2 && samples.rate &&
                samples.pcm.size() % samples.channels == 0,
            "Invalid audio samples");
    Bytes b(44);
    auto tag = [&](unsigned p, const char *value) {
        std::copy_n(value, 4, b.begin() + p);
    };
    tag(0, "RIFF");
    put32(b, 4, narrow(36 + samples.pcm.size() * 2));
    tag(8, "WAVE");
    tag(12, "fmt ");
    put32(b, 16, 16);
    put16(b, 20, 1);
    put16(b, 22, std::uint16_t(samples.channels));
    put32(b, 24, samples.rate);
    put32(b, 28, samples.rate * samples.channels * 2);
    put16(b, 32, std::uint16_t(samples.channels * 2));
    put16(b, 34, 16);
    tag(36, "data");
    put32(b, 40, narrow(samples.pcm.size() * 2));
    for (auto value : samples.pcm) {
        b.push_back(std::uint8_t(value));
        b.push_back(std::uint8_t(unsigned(value) >> 8));
    }
    return b;
}
}
