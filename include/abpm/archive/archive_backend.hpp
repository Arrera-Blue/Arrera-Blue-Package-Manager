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
    static bool install(const std::string& file_path, bool assumeyes);

    // Liste des applications installées depuis une archive
    static std::vector<core::PackageInfo> list_installed();

    // Suppression d'une ou plusieurs applications archives
    static bool remove(const std::vector<std::string>& package_names);

    // Vérifie si une application archive est installée
    static bool is_installed(std::string_view name);
};

} // namespace abpm::archive

#endif // ABPM_ARCHIVE_ARCHIVE_BACKEND_HPP