#include "field/dialogue_text.h"
#include <charconv>
namespace studio {
DialogueText encode_dialogue_text(const std::string &text, bool formatted) {
    require(text.size() <= 2048, "Enter dialogue up to 2048 UTF-8 bytes");
    DialogueText result;
    auto &words = result.words;
    for (std::size_t i = 0; i < text.size();) {
        if (formatted && (text[i] == '{' || text[i] == '}')) {
            char brace = text[i];
            if (i + 1 < text.size() && text[i + 1] == brace) {
                words.push_back(unsigned(brace));
                result.preview += brace;
                i += 2;
                continue;
            }
            require(brace == '{', "Use }} for a literal closing brace");
            auto end = text.find('}', i + 1);
            require(end != std::string::npos, "Close the dialogue token with }");
            auto token = text.substr(i + 1, end - i - 1);
            i = end + 1;
            if (token == "page" || token == "scroll") {
                words.insert(words.end(), {16, 1, token == "page" ? 0xbe01u : 0xbe00u});
                result.preview += token == "page" ? "\n[Next page]\n" : "\n[Scroll]\n";
                continue;
            }
            auto colon = token.find(':');
            auto kind = token.substr(0, colon);
            int value = 0;
            if (kind != "player") {
                require(colon != std::string::npos, "Use a numeric value after the token's colon");
                auto number = token.substr(colon + 1);
                auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
                require(parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size(),
                        "Dialogue token needs a whole number");
            } else
                require(colon == std::string::npos, "The player token takes no value");
            require(result.setup.size() < 8, "A message supports eight name/number substitutions");
            auto slot = unsigned(result.setup.size());
            unsigned control = 0;
            std::string call, display;
            if (kind == "player") {
                control = 0x100;
                call = "SetMessagePlayer(" + std::to_string(slot) + ");";
                display = "[Player name]";
            } else if (kind == "pokemon" || kind == "item") {
                require(value > 0 && value <= 65535,
                        "Use a positive species or item ID up to 65535");
                (kind == "pokemon" ? result.species : result.items).push_back(unsigned(value));
                control = kind == "pokemon" ? 0x101 : 0x108;
                call = std::string(kind == "pokemon" ? "SetMessagePokemon(" : "SetMessageItem(") +
                       std::to_string(slot) + ", " + std::to_string(value) + ");";
                display = "[" + kind + " " + std::to_string(value) + "]";
            } else {
                require(kind == "number", "Unknown dialogue token: " + token);
                require(value >= 0, "Use a nonnegative dialogue number");
                control = 0x200;
                call = "SetMessageNumber(" + std::to_string(slot) + ", " + std::to_string(value) +
                       ");";
                display = std::to_string(value);
            }
            words.insert(words.end(), {16, 2, control, slot});
            result.setup.push_back(call);
            result.preview += display;
            continue;
        }
        auto start = i;
        unsigned c = static_cast<unsigned char>(text[i++]), n = 0, minimum = 0;
        if (c >= 0xf0 && c <= 0xf4) {
            c &= 7;
            n = 3;
            minimum = 0x10000;
        } else if (c >= 0xe0 && c <= 0xef) {
            c &= 15;
            n = 2;
            minimum = 0x800;
        } else if (c >= 0xc2 && c <= 0xdf) {
            c &= 31;
            n = 1;
            minimum = 0x80;
        } else
            require(c < 128, "Invalid dialogue UTF-8");
        for (unsigned j = 0; j < n; ++j) {
            require(i < text.size(), "Truncated dialogue UTF-8");
            auto next = static_cast<unsigned char>(text[i++]);
            require((next & 192) == 128, "Invalid dialogue UTF-8 continuation");
            c = (c << 6) | (next & 63);
        }
        require(c >= minimum && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff) &&
                    (c >= 32 || c == 10),
                "Unsupported dialogue character");
        if (c == 0x2642) c = 0xe08e;
        else if (c == 0x2640) c = 0xe08f;
        if (c > 65535) {
            c -= 0x10000;
            words.push_back(0xd800 + (c >> 10));
            c = 0xdc00 + (c & 1023);
        }
        words.push_back(c);
        result.preview += text.substr(start, i - start);
    }
    words.push_back(0);
    return result;
}
DialogueText encode_dialogue_translation(const std::string &fallback, const std::string &translation,
                                         bool formatted) {
    auto base = encode_dialogue_text(fallback, formatted);
    auto localized = encode_dialogue_text(translation, formatted);
    auto identity = [](std::string call) {
        auto start = call.find('(') + 1;
        call.replace(start, call.find_first_of(",)", start) - start, "0");
        return call;
    };
    std::vector<unsigned> slots;
    for (const auto &call : localized.setup) {
        unsigned slot = 0;
        while (slot < base.setup.size() && identity(base.setup[slot]) != identity(call))
            ++slot;
        require(slot < base.setup.size(),
                "Translated name/number tokens must also appear in the English fallback");
        slots.push_back(slot);
    }
    for (std::size_t i = 0; i < localized.words.size(); ++i) {
        if (localized.words[i] != 16)
            continue;
        auto length = localized.words.at(i + 1);
        if (length == 2)
            localized.words.at(i + 3) = slots.at(localized.words.at(i + 3));
        i += 1 + length;
    }
    localized.setup = std::move(base.setup);
    return localized;
}

}
