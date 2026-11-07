#pragma once

#include "abpm/core/package_info.hpp"
#include <vector>

namespace abpm::dnf {

class DnfBackend {
public:
    // Récupère la liste de tous les paquets RPM installés sur le système
    static std::vector<core::PackageInfo> list_installed();

    static std::vector<core::PackageInfo> search(std::string_view term);

    static std::vector<core::UpdateInfo> check_updates(bool silent = false);
    static bool update(const std::vector<std::string>& target_packages = {});
    static bool install(std::string_view package_name, bool assumeyes);
    static bool is_installed(std::string_view name);
    static bool is_available(std::string_view package_name);
    static std::vector<std::string> get_available_packages(const std::vector<std::string>& names);
    static bool remove(const std::vector<std::string>& package_names);
    static bool install_local_rpm(const std::string& file_path, bool assumeyes);
    enum class CleanTarget {
        ALL,
        EXPIRE_CACHE,
        PACKAGES
    };

    static bool clean(CleanTarget target);
    static std::vector<core::PackageInfo> list_installed_local_rpms();
};

} // namespace abpm::dnf