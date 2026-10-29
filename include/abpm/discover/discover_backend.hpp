#pragma once

#include "abpm/cli/cli_parser.hpp"
#include <string>
#include <vector>

namespace abpm::discover {

struct DiscoverApp {
    std::string id;      // Identifiant AppStream (ex: "org.mozilla.firefox")
    std::string name;    // Nom convivial (ex: "Firefox")
    std::string summary; // Description courte 1 ligne
    std::string icon;    // Nom d'icône système ou URL Flathub
    std::string source;  // "dnf" ou "flatpak"
    std::string package; // Paquet à installer (ex: "firefox" ou "org.mozilla.firefox")
};

class DiscoverBackend {
public:
    // Méthode principale unifiée avec support du filtre (--dnf, --flatpak, ALL)
    static std::vector<DiscoverApp> discover(cli::SourceFilter filter = cli::SourceFilter::ALL,
                                             int page = 1, 
                                             int per_page = 6);

    // Extraction des applications DNF via le catalogue AppStream
    static std::vector<DiscoverApp> get_dnf_apps(int page = 1, int per_page = 6);

    // Extraction des tendances Flathub
    static std::vector<DiscoverApp> get_flatpak_trending(int page = 1, int per_page = 6);

    // Sérialisation JSON pour Blue Hub
    static std::string to_json(const std::vector<DiscoverApp>& apps);
};

} // namespace abpm::discover