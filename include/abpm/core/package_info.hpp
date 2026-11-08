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
    bool is_system = false;
};

struct UpdateInfo {
    std::string name;          // Nom lisible affiché à l'utilisateur (ex: "Firefox", "Noyau Linux")
    std::string id;            // ID unique (ex: "org.mozilla.firefox" ou "kernel-core")
    std::string category;      // "app", "utility", ou "system"
    std::string current_ver;   // Version installée
    std::string new_ver;       // Nouvelle version disponible
    std::string arch;          // Architecture (ex: "x86_64")
    PackageSource source;      // DNF, FLATPAK
    std::string origin;        // Dépôt d'origine (ex: "updates", "flathub")
    std::string package;       // Nom exact pour la mise à jour (paquet RPM ou ref Flatpak)
    std::string icon;          // Nom ou chemin de l'icône (ex: "firefox", "system-component")
    std::string summary;       // Court résumé descriptif
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