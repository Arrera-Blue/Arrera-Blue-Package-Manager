#ifndef ABPM_ARCHIVE_ARCHIVE_BACKEND_HPP
#define ABPM_ARCHIVE_ARCHIVE_BACKEND_HPP

#include "abpm/core/package_info.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace abpm::archive {

class ArchiveBackend {
public:
    // Installation d'une archive (.tar.gz, .tar.xz, .tar.bz2, .tar, .zip)
    static bool install(const std::string& file_path, bool assumeyes, const std::string& custom_icon = "");

    // Liste des applications installées depuis une archive
    static std::vector<core::PackageInfo> list_installed();

    // Suppression d'une ou plusieurs applications archives
    static bool remove(const std::vector<std::string>& package_names);

    // Vérifie si une application archive est installée
    static bool is_installed(std::string_view name);
    static bool set_icon(const std::string& name, const std::string& icon_path);
};

} // namespace abpm::archive

#endif // ABPM_ARCHIVE_ARCHIVE_BACKEND_HPP