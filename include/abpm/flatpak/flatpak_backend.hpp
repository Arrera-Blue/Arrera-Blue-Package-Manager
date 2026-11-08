#pragma once

#include "abpm/core/package_info.hpp"
#include "abpm/cli/cli_parser.hpp"
#include <string_view>
#include <vector>

namespace abpm::flatpak {

class FlatpakBackend {
public:
    static std::vector<core::PackageInfo> list_installed();
    static std::vector<core::PackageInfo> search(std::string_view term);

    static std::vector<core::UpdateInfo> check_updates();
    static bool update(const std::vector<std::string>& target_apps = {});
    static bool install(std::string_view app_name, bool assumeyes);
    static std::string find_installed_ref(std::string_view name_or_id, cli::ScopeFilter scope = cli::ScopeFilter::AUTO);
    static bool is_system_ref(std::string_view name_or_id);
    static bool remove(const std::vector<std::string>& formatted_refs);
};

} // namespace abpm::flatpak