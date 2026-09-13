#pragma once
#include <filesystem>
#include <map>
#include <string>
namespace studio {
class Preferences {
  public:
    explicit Preferences(std::filesystem::path file);
    std::string get(const std::string &key) const;
    void set(const std::string &key, const std::string &value);
    bool save();
    std::string error;

  private:
    std::filesystem::path file_;
    std::map<std::string, std::string> values_;
};
}
