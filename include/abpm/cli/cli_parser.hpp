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
    VERSION
};

enum class SourceFilter {
    ALL,        // Par défaut (DNF + Flatpak)
    DNF,        // --dnf
    FLATPAK,    // --flatpak
    RPM,        // --rpm (fichier local)
    APPIMAGE,   // --appimage
    TAR         // --tar (archive tar.xz)
};

struct CliOptions {
    CommandType command = CommandType::NONE;
    SourceFilter filter = SourceFilter::ALL;
    std::vector<std::string> arguments;
};

class CliParser {
public:
    // Parse argc et argv
    static CliOptions parse(int argc, char* argv[]);

    // Affiche l'aide complète conforme au README
    static void print_help();

    // Affiche la version
    static void print_version();
};

} // namespace abpm::cli