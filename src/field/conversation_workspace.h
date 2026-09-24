#pragma once
#include "field/authored_interaction.h"
#include "field/pawn_compiler.h"
#include "field/interaction_source.h"
#include <memory>
namespace studio {
struct ConversationActor {
    unsigned zone = 0, event = 0;
    InteractionTargetKind kind = InteractionTargetKind::Npc;
    auto operator<=>(const ConversationActor &) const = default;
};
enum class ConversationBuildMode { Current, PreviouslyStaged };
struct ConversationBuild {
    std::map<std::size_t, Bytes> field, messages, shared;
    std::map<unsigned, std::map<std::size_t, Bytes>> translations;
};
class ConversationWorkspace {
  public:
    static constexpr unsigned trainer_workspace = 0xffffffffu;
    ConversationWorkspace(std::filesystem::path source, unsigned area);
    AuthoredInteraction preview(ConversationActor actor) const;
    bool language_available(unsigned language) const;
    AuthoredInteraction &open(ConversationActor actor);
    void remove(ConversationActor actor);
    void restore_actor(ConversationActor actor, const ConversationWorkspace &snapshot);
    const std::map<ConversationActor, AuthoredInteraction> &interactions() const {
        return interactions_;
    }
    ConversationPawn generate(ConversationActor actor) const;
    PawnCompileResult compile(ConversationActor actor, const PawnCompilerSettings &settings);
    void compile_all(const PawnCompilerSettings &settings);
    bool compiled() const;
    bool dirty() const;
    void mark_saved();
    std::string serialize() const;
    void restore(const std::string &record);
    ConversationBuild build(ConversationBuildMode mode = ConversationBuildMode::Current) const;
    void export_to(const std::filesystem::path &output,
                   ConversationBuildMode mode = ConversationBuildMode::Current) const;

  private:
    struct Binary {
        std::string stamp;
        Bytes program;
    };
    struct Allocation {
        unsigned script = 0, member = 0;
        std::map<std::string, unsigned> messages;
    };
    std::map<ConversationActor, Allocation> allocate() const;
    std::map<unsigned, std::set<unsigned>> message_languages() const;
    const Bytes &message_source(unsigned member, unsigned language) const;
    void validate_references(const ConversationDraft &draft) const;
    std::string stamp(ConversationActor actor) const;
    std::filesystem::path source_;
    unsigned area_;
    InteractionSource trainer_source_;
    Bytes placements_, scripts_;
    std::map<unsigned, unsigned> message_members_;
    std::map<unsigned, Bytes> message_sources_;
    mutable std::map<std::pair<unsigned, unsigned>, Bytes> translated_sources_;
    std::map<ConversationActor, AuthoredInteraction> interactions_;
    std::map<ConversationActor, Binary> binaries_;
    std::string fingerprint_, saved_;
};
}
