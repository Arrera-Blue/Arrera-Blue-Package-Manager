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
    std::string name;
    std::string id;
    std::string current_ver;
    std::string new_ver;
    std::string arch;
    PackageSource source;
    std::string origin;
};


struct InstalledApp {
    std::string id;           
    std::string name;          
    std::string version;       
    std::string source;       
    std::string icon;          
    std::string package;       
    bool is_system = false;    
};

} // namespace abpm::core