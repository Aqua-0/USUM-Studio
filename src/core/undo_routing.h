#pragma once
#include <map>
#include <set>
#include <string>
namespace studio {
class UndoRouting {
  public:
    void clear() { candidates_.clear(); }
    void offer(const std::string &window, const std::string &history) { candidates_[window].insert(history); }
    void activate(const std::string &window, const std::string &history) { active_[window]=history; }
    std::string target(const std::string &window) const {
        auto found=candidates_.find(window);
        if(found==candidates_.end() || found->second.empty()) return {};
        if(found->second.size()==1) return *found->second.begin();
        auto active=active_.find(window);
        return active!=active_.end() && found->second.contains(active->second) ? active->second : std::string{};
    }
  private:
    std::map<std::string,std::set<std::string>> candidates_;
    std::map<std::string,std::string> active_;
};
}
