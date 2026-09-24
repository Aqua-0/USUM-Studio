#pragma once
#include "field/interaction.h"
#include <map>
namespace studio {
struct InteractionArgument {
    std::string label, source;
    std::optional<unsigned> literal;
    std::int32_t value = 0;
    bool resolved = false;
};
struct InteractionStep {
    unsigned address = 0;
    std::string label;
    std::vector<unsigned> targets;
    std::vector<InteractionArgument> arguments;
};
std::string interaction_native_name(unsigned hash);
std::vector<InteractionStep> interaction_steps(const AmxProgram &program, unsigned entry);
struct InteractionStateAccess {
    unsigned address = 0;
    bool work = false, write = false;
    std::string title, condition;
    std::optional<unsigned> branch, continuation;
    std::vector<InteractionArgument> arguments;
};
std::vector<InteractionStateAccess>
interaction_state_accesses(const AmxProgram &program, const std::vector<InteractionStep> &steps);
class InteractionDocument {
  public:
    explicit InteractionDocument(Bytes source);
    const AmxProgram &program() const {
        return program_;
    }
    void set(unsigned literal, std::int32_t value);
    bool undo();
    bool redo();
    bool dirty() const {
        return edits_ != saved_;
    }
    void mark_saved() {
        saved_ = edits_;
    }
    Bytes compile() const;
    std::string serialize() const;
    void restore(const std::string &patch);
    void export_to(const std::filesystem::path &source, const std::filesystem::path &output,
                   unsigned area, unsigned local_zone) const;

    void export_shared(const std::filesystem::path &source, const std::filesystem::path &output,
                       unsigned member) const;

  private:
    using Edits = std::map<unsigned, std::int32_t>;
    Bytes source_;
    AmxProgram original_, program_;
    Edits edits_, saved_;
    std::vector<Edits> undo_, redo_;
    void refresh();
};
}
