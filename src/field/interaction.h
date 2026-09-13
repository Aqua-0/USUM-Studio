#pragma once
#include "formats/amx.h"
#include "field/archive_sources.h"
#include <optional>
#include <functional>
namespace studio {
struct InteractionAction {
    unsigned address = 0;
    std::string title, detail;
    bool recognized = false;
};
struct InteractionInspection {
    std::string source, listing, notice;
    unsigned entry = 0, message_member = 0;
    std::vector<InteractionAction> actions;
};
InteractionInspection inspect_interaction(const std::filesystem::path &dump,
                                          const ArchiveSources &archives, unsigned area,
                                          unsigned local_zone, int zone, unsigned script,
                                          unsigned event);
InteractionInspection trace_interaction(
    const AmxProgram &program, unsigned script, unsigned event,
    const std::vector<std::string> &messages,
    const std::function<std::vector<std::string>(bool, unsigned)> &load_messages = {});
}
