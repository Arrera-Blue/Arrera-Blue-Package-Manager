#pragma once

#include "abpm/core/package_info.hpp"
#include "abpm/cli/cli_parser.hpp"
#include <vector>
#include <string>

namespace abpm::list {

class ListBackend {
public:
    static std::vector<core::InstalledApp> get_installed_apps(cli::SourceFilter filter = cli::SourceFilter::ALL);

    static std::string to_json(const std::vector<core::InstalledApp>& apps);

    static void print_terminal_apps(const std::vector<core::InstalledApp>& apps);
};

} // namespace abpm::list