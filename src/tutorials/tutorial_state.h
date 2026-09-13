#pragma once
#include <set>
#include <string>
#include <vector>
namespace studio {
struct TutorialTopic {
    std::string id, title, body;
};
const std::vector<TutorialTopic> &tutorial_topics();
const TutorialTopic *tutorial_topic_by_id(const std::string &id);
const TutorialTopic *tutorial_topic(const std::string &module, const std::string &label);
const TutorialTopic &tutorial_workspace(int workspace);
std::string tutorial_label(const std::string &label);
class TutorialState {
  public:
    bool enabled = true, welcomed = false;
    std::string current;
    std::set<std::string> completed;
    void finish(const std::string &id);
    void restart();
    std::string serialize() const;
    void restore(const std::string &text);
    bool unseen(const std::string &id) const {
        return !completed.contains(id);
    }
    std::string choose(const std::vector<std::string> &visible) const;
};
}
