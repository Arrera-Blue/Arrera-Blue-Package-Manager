#pragma once

#include "abpm/core/package_info.hpp"
#include "abpm/cli/cli_parser.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace abpm::archive {

struct ArchiveRecord {
    std::string name;
    std::string version{"standalone"};
    std::string install_dir;
    std::string binary_path;
    std::string symlink_path;
    std::string desktop_path;
    std::string icon_path;
    bool is_system{false};
    std::string scope{"user"};
};

class ArchiveBackend {
public:
    // Installation d'une archive (.tar.gz, .tar.xz, .tar.bz2, .tar, .zip)
    static bool install(const std::string& file_path, bool assumeyes, const std::string& custom_icon = "", cli::ScopeFilter scope = cli::ScopeFilter::AUTO);

    // Liste des applications installées depuis une archive
    static std::vector<core::PackageInfo> list_installed(cli::ScopeFilter scope = cli::ScopeFilter::AUTO);

    // Suppression d'une ou plusieurs applications archives
    static bool remove(const std::vector<std::string>& package_names, cli::ScopeFilter scope = cli::ScopeFilter::AUTO);

    // Vérifie si une application archive est installée
    static bool is_installed(std::string_view name, cli::ScopeFilter scope = cli::ScopeFilter::AUTO);
    static bool is_system(std::string_view name);
    static bool set_icon(const std::string& name, const std::string& icon_path);
};

} // namespace abpm::archive