#include "tutorials/tutorial_state.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
namespace studio {
void TutorialState::finish(const std::string &id) {
    if (!id.empty())
        completed.insert(id);
    if (current == id)
        current.clear();
}
void TutorialState::restart() {
    completed.clear();
    current.clear();
    welcomed = false;
    enabled = true;
}
std::string TutorialState::choose(const std::vector<std::string> &visible) const {
    if (!enabled || !welcomed)
        return {};
    if (!current.empty() && std::find(visible.begin(), visible.end(), current) != visible.end())
        return current;
    for (const auto &id : visible)
        if (unseen(id))
            return id;
    return {};
}
std::string TutorialState::serialize() const {
    std::ostringstream out;
    out << "TUTORIALS 1 " << enabled << ' ' << welcomed << ' ' << std::quoted(current) << '\n';
    for (auto &id : completed)
        out << std::quoted(id) << '\n';
    return out.str();
}
void TutorialState::restore(const std::string &text) {
    if (text.empty())
        return;
    std::istringstream in(text);
    std::string magic;
    int version, on, welcome;
    TutorialState next;
    if (!(in >> magic >> version >> on >> welcome >> std::quoted(next.current)) ||
        magic != "TUTORIALS" || version != 1 || (on != 0 && on != 1) ||
        (welcome != 0 && welcome != 1))
        throw std::runtime_error("Saved tutorial progress could not be read");
    next.enabled = on != 0;
    next.welcomed = welcome != 0;
    for (;;) {
        in >> std::ws;
        if (in.eof())
            break;
        std::string id;
        if (!(in >> std::quoted(id)) || id.empty() || id.size() > 512 ||
            next.completed.size() > 10000)
            throw std::runtime_error("Saved tutorial progress is invalid");
        next.completed.insert(id);
    }
    *this = std::move(next);
}
std::string tutorial_label(const std::string &label) {
    auto value = label.substr(0, label.find("##"));
    return value.empty() ? label : value;
}
const TutorialTopic *tutorial_topic_by_id(const std::string &id) {
    static const auto index = [] {
        std::unordered_map<std::string, const TutorialTopic *> result;
        for (const auto &topic : tutorial_topics())
            result.emplace(topic.id, &topic);
        return result;
    }();
    auto found = index.find(id);
    return found == index.end() ? nullptr : found->second;
}
const TutorialTopic *tutorial_topic(const std::string &module, const std::string &label) {
    auto name = tutorial_label(label);
    if (module == "project_import")
        name = "Archive";
    if (auto *topic = tutorial_topic_by_id(module + "/" + name))
        return topic;
    return tutorial_topic_by_id("control/" + name);
}
const TutorialTopic &tutorial_workspace(int workspace) {
    if (auto *topic = tutorial_topic_by_id("workspace/" + std::to_string(workspace)))
        return *topic;
    throw std::runtime_error("Unknown tutorial workspace");
}
}
