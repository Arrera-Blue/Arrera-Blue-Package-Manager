#pragma once

#include <string>
#include <vector>

namespace abpm::repo {

struct RepoItem {
    std::string id;
    std::string name;
    std::string type;         // "rpm" ou "flatpak"
    bool enabled{true};
    bool is_protected{false};
    std::string url;
};

class RepoBackend {
public:
    // Liste l'ensemble des dépôts DNF et remotes Flatpak
    static std::vector<RepoItem> list_repos();

    // Export en JSON standardisé pour Blue Hub
    static std::string to_json(const std::vector<RepoItem>& repos);

    // Affichage formaté pour terminal classique
    static void print_terminal_repos(const std::vector<RepoItem>& repos);

    // Vérifie si un dépôt est protégé contre la désactivation
    static bool is_protected(const std::string& repo_id, const std::string& type = "");

    // Active un dépôt
    static bool enable_repo(const std::string& repo_id);

    // Désactive un dépôt (refuse si protégé)
    static bool disable_repo(const std::string& repo_id);

    // Ajoute un nouveau dépôt RPM
    static bool add_repo(const std::string& url_or_file);
};

} // namespace abpm::repo