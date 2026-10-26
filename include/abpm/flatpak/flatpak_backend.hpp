#pragma once

#include "abpm/core/package_info.hpp"
#include <string_view>
#include <vector>

namespace abpm::flatpak {

class FlatpakBackend {
public:
    static std::vector<core::PackageInfo> list_installed();
    static std::vector<core::PackageInfo> search(std::string_view term);

    static std::vector<core::UpdateInfo> check_updates();
    static bool update();
};

} // namespace abpm::flatpak