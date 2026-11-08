#pragma once

#include <string>
#include <vector>

namespace abpm::cli {

enum class CommandType {
    NONE,
    UPDATE,
    CHECK,
    INSTALL,
    REMOVE,
    SEARCH,
    LIST,
    HELP,
    SET_ICON,
    DISCOVER,
    VERSION,
    CLEAN,
    REPO,    // <-- NOUVEAU : abpm repo (list, enable, disable, add)
    CONFIG   // <-- NOUVEAU : abpm config (get, set)
};

enum class SourceFilter {
    ALL,
    DNF,
    FLATPAK,
    RPM,
    APPIMAGE,
    TAR
};

enum class ScopeFilter {
    AUTO,
    SYSTEM,
    USER
};

struct CliOptions {
    CommandType command = CommandType::NONE;
    SourceFilter filter = SourceFilter::ALL;
    ScopeFilter scope = ScopeFilter::AUTO;
    std::vector<std::string> arguments;
    std::string custom_icon;
    bool assumeyes = false;

    int discover_page = 1;
    int discover_per_page = 6;
    bool json_output = false;
    bool list_apps_only = false;
    std::string clean_target = "all";
    bool progress = false;
};

class CliParser {
public:
    static CliOptions parse(int argc, char* argv[]);
    static void print_help();
    static void print_version();
};

} // namespace abpm::cli