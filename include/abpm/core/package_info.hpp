#pragma once

#include <string>

namespace abpm::core {

enum class PackageSource {
    DNF,
    FLATPAK,
    RPM_LOCAL,
    APPIMAGE,
    TAR
};

struct PackageInfo {
    std::string name;          // Nom lisible (ex: "Visual Studio Code" ou "bash")
    std::string id;            // Identifiant (ex: "com.visualstudio.code" ou "bash")
    std::string version;       // Version (ex: "1.139.1")
    std::string arch;          // Architecture (ex: "x86_64")
    PackageSource source;      // DNF, FLATPAK, etc.
    std::string origin;        // "system", "user", etc.
};

struct UpdateInfo {
    std::string name;          // Nom
    std::string id;            // Identifiant
    std::string current_ver;   // Version actuellement installée
    std::string new_ver;       // Version disponible
    std::string arch;          // Architecture
    PackageSource source;      // DNF ou FLATPAK
    std::string origin;        // "flathub", "dnf", etc.
};

} // namespace abpm::core