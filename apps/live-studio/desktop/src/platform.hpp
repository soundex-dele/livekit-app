#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
namespace studio_app::platform {
std::filesystem::path default_settings();
std::string browse(bool save);
nlohmann::json load_account(const std::filesystem::path &);
void save_account(const std::filesystem::path &, const nlohmann::json &);
} // namespace studio_app::platform
