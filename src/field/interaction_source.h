#pragma once
#include "field/archive_sources.h"
#include "formats/archive.h"
namespace studio {
struct SharedScriptRoute {
    unsigned first = 0, last = 0, message_kind = 0, message_member = 0, member = 0;
};
std::vector<SharedScriptRoute> decode_script_routes(View bytes);
Bytes read_shared_script(const Archive &archive, unsigned member);
struct InteractionSource {
    bool shared = false;
    std::filesystem::path archive, messages;
    unsigned member = 0, local_zone = 0, message_member = 0;
    Bytes program;
};
InteractionSource resolve_interaction_source(const std::filesystem::path &dump,
                                             const ArchiveSources &archives, unsigned area,
                                             unsigned local_zone, int zone, unsigned script);
struct InteractionUse {
    unsigned area = 0, local_zone = 0, row = 0, event = 0, script = 0;
    int zone = -1;
    std::string kind;
};
std::vector<InteractionUse> shared_script_uses(const std::filesystem::path &dump,
                                               const ArchiveSources &archives, unsigned member);
}
