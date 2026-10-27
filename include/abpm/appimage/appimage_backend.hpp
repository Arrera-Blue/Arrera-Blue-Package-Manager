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
    static bool install(const std::string& file_path, bool assumeyes, const std::string& custom_icon = "");

    // Liste des AppImages installées
    static std::vector<core::PackageInfo> list_installed();

    // Suppression d'une ou plusieurs AppImages
    static bool remove(const std::vector<std::string>& package_names);

    // Vérifie si une AppImage est installée
    static bool is_installed(std::string_view name);
    static bool set_icon(const std::string& name, const std::string& icon_path);
};

} // namespace abpm::appimage

#endif // ABPM_APPIMAGE_APPIMAGE_BACKEND_HPP