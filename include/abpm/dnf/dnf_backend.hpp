#pragma once

#include "abpm/core/package_info.hpp"
#include <vector>

namespace abpm::dnf {

class DnfBackend {
public:
    // Récupère la liste de tous les paquets RPM installés sur le système
    static std::vector<core::PackageInfo> list_installed();

    static std::vector<core::PackageInfo> search(std::string_view term);
};

} // namespace abpm::dnf