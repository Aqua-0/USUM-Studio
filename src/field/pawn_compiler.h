#pragma once
#include "field/authored_interaction.h"
namespace studio {
struct PawnCompilerSettings {
    std::filesystem::path executable, include_directory, scratch_directory;
};
struct PawnDiagnostic {
    std::string file, severity, message;
    unsigned line = 0;
    std::optional<unsigned> step;
};
struct PawnCompileResult {
    Bytes program;
    std::string output;
    std::vector<PawnDiagnostic> diagnostics;
    int exit_code = -1;
    bool succeeded() const {
        return exit_code == 0 && !program.empty();
    }
};
PawnCompileResult compile_conversation(const ConversationPawn &source,
                                       const PawnCompilerSettings &settings);
}
