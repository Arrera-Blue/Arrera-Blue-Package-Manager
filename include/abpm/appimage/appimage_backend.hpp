#ifndef ABPM_APPIMAGE_APPIMAGE_BACKEND_HPP
#define ABPM_APPIMAGE_APPIMAGE_BACKEND_HPP

#include "abpm/core/package_info.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace abpm::appimage {

class AppImageBackend {
public:
    // Installation d'un fichier AppImage local
    static bool install(const std::string& file_path, bool assumeyes);

    // Liste des AppImages installées
    static std::vector<core::PackageInfo> list_installed();

    // Suppression d'une ou plusieurs AppImages
    static bool remove(const std::vector<std::string>& package_names);

    // Vérifie si une AppImage est installée
    static bool is_installed(std::string_view name);
};

} // namespace abpm::appimage

#endif // ABPM_APPIMAGE_APPIMAGE_BACKEND_HPP