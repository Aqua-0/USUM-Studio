#include "native/preferences.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#include <fstream>
#include <iomanip>
#include <sstream>
namespace studio {
Preferences::Preferences(std::filesystem::path file) : file_(std::move(file)) {
    if (file_.empty()) {
        error = "Settings location is unavailable";
        return;
    }
    std::ifstream input(file_);
    if (!input)
        return;
    std::string key, value;
    while (input >> key) {
        if (!(input >> std::quoted(value))) {
            error = "Some saved settings could not be read";
            break;
        }
        values_[key] = value;
    }
}
std::string Preferences::get(const std::string &key) const {
    auto it = values_.find(key);
    return it == values_.end() ? std::string{} : it->second;
}
void Preferences::set(const std::string &key, const std::string &value) {
    values_[key] = value;
}
bool Preferences::save() {
    if (file_.empty()) {
        error = "Settings location is unavailable";
        return false;
    }
    auto temporary = file_;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    for (auto &[key, value] : values_)
        output << key << ' ' << std::quoted(value) << '\n';
    output.close();
    if (!output) {
        error = "Could not write settings";
        return false;
    }
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), file_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Could not replace saved settings";
        return false;
    }
#else
    std::error_code failure;
    std::filesystem::rename(temporary, file_, failure);
    if (failure) {
        error = "Could not save settings: " + failure.message();
        return false;
    }
#endif
    error.clear();
    return true;
}
}
