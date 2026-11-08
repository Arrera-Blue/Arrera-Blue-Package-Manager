#pragma once

#include "abpm/core/package_info.hpp"
#include "abpm/cli/cli_parser.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace abpm::appimage {

struct AppImageRecord {
    std::string name;
    std::string version{"standalone"};
    std::string file_path;
    std::string symlink_path;
    std::string desktop_path;
    std::string icon_path;
    bool is_system{false};
    std::string scope{"user"};
};

class AppImageBackend {
public:
    // Installation d'un fichier AppImage local
    static bool install(const std::string& file_path, bool assumeyes, const std::string& custom_icon = "", cli::ScopeFilter scope = cli::ScopeFilter::AUTO);

    // Liste des AppImages installées
    static std::vector<core::PackageInfo> list_installed(cli::ScopeFilter scope = cli::ScopeFilter::AUTO);

    // Suppression d'une ou plusieurs AppImages
    static bool remove(const std::vector<std::string>& package_names, cli::ScopeFilter scope = cli::ScopeFilter::AUTO);

    // Vérifie si une AppImage est installée
    static bool is_installed(std::string_view name, cli::ScopeFilter scope = cli::ScopeFilter::AUTO);
    static bool is_system(std::string_view name);
    static bool set_icon(const std::string& name, const std::string& icon_path);
};

} // namespace abpm::appimage