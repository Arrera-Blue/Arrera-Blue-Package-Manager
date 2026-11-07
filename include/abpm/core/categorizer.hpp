#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace abpm::core {

struct DesktopMeta {
    std::string id;
    std::string name;
    std::string icon;
    std::string summary;
    std::string category; // "app" ou "utility"
    std::string exec;
};

class Categorizer {
public:
    // Détermine la catégorie ("utility" ou "app") à partir des catégories XDG
    static std::string categorize_xdg(const std::string& categories);

    // Parse un fichier .desktop et extrait ses métadonnées
    static bool parse_desktop_file(const std::filesystem::path& desktop_file, DesktopMeta& out_meta);

    // Scanne et indexe un répertoire de fichiers .desktop (ex: /usr/share/applications/)
    // Indexe par ID (stem du .desktop) et par binaire Exec
    static std::unordered_map<std::string, DesktopMeta> index_desktop_directory(const std::filesystem::path& dir);

    // Échappement des chaînes pour sortie JSON valide
    static std::string escape_json(const std::string& s);
};

} // namespace abpm::core