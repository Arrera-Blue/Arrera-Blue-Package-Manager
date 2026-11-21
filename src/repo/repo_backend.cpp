#include "abpm/repo/repo_backend.hpp"
#include "abpm/core/logger.hpp"
#include "abpm/core/categorizer.hpp"

#include <libdnf5/base/base.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/repo/repo_query.hpp>
#include <libdnf5/repo/repo.hpp>
#include <libdnf5/repo/config_repo.hpp>

#include <flatpak.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <cctype>
#include <fstream>
#include <sys/utsname.h>

namespace abpm::repo {

using core::Logger;
using core::Categorizer;

bool RepoBackend::is_protected(const std::string& id, const std::string& type) {
    // 1. Remotes Flatpak protégés
    if (type == "flatpak" || type.empty()) {
        if (id == "flathub" || id == "fedora") return true;
    }
    // 2. Dépôts RPM protégés (Fedora base & Arrera Blue)
    if (type == "rpm" || type.empty()) {
        if (id == "fedora" || id == "updates" || id == "fedora-updates" || 
            id == "fedora-cisco-openh264") {
            return true;
        }
        if (id == "copr-arrera-blue" || 
            id.find("arrera-software:arrera-blue") != std::string::npos ||
            id.find("arrera-blue") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<RepoItem> RepoBackend::list_repos() {
    std::vector<RepoItem> repos;

    // 1. Dépôts RPM (libdnf5)
    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        auto sack = base.get_repo_sack();
        sack->create_repos_from_system_configuration();

        libdnf5::repo::RepoQuery query(base);
        for (auto repo_weak : query) {
            auto repo = repo_weak.get();
            const auto& conf = repo->get_config();

            RepoItem item;
            item.id = repo->get_id();
            item.name = conf.get_name_option().empty() ? item.id : conf.get_name_option().get_value();
            item.type = "rpm";
            item.enabled = repo->is_enabled();
            item.is_protected = is_protected(item.id, "rpm");
            item.is_testing = is_testing(item.id, item.name);

            std::string url;
            if (!conf.get_metalink_option().empty()) {
                url = conf.get_metalink_option().get_value();
            } else if (!conf.get_mirrorlist_option().empty()) {
                url = conf.get_mirrorlist_option().get_value();
            } else if (!conf.get_baseurl_option().empty() && !conf.get_baseurl_option().get_value().empty()) {
                url = conf.get_baseurl_option().get_value()[0];
            }
            item.url = url;
            repos.push_back(std::move(item));
        }
    } catch (const std::exception& e) {
        Logger::warning(std::string("Erreur lecture dépôts DNF : ") + e.what());
    }

    // 2. Remotes Flatpak (libflatpak)
    g_autoptr(GError) err = nullptr;
    g_autoptr(FlatpakInstallation) inst = flatpak_installation_new_system(nullptr, &err);
    if (inst) {
        g_autoptr(GPtrArray) remotes = flatpak_installation_list_remotes(inst, nullptr, &err);
        if (remotes) {
            for (guint i = 0; i < remotes->len; ++i) {
                auto* remote = FLATPAK_REMOTE(g_ptr_array_index(remotes, i));
                std::string id = flatpak_remote_get_name(remote);
                const char* title = flatpak_remote_get_title(remote);
                std::string name = (title && *title) ? title : id;
                const char* url_c = flatpak_remote_get_url(remote);
                std::string url = url_c ? url_c : "";
                bool enabled = !flatpak_remote_get_disabled(remote);

                RepoItem item;
                item.id = id;
                item.name = name;
                item.type = "flatpak";
                item.enabled = enabled;
                item.is_protected = is_protected(id, "flatpak");
                item.is_testing = is_testing(item.id, item.name);
                item.url = url;
                repos.push_back(std::move(item));
            }
        }
    }

    return repos;
}

std::string RepoBackend::to_json(const std::vector<RepoItem>& repos) {
    std::ostringstream ss;
    ss << "[\n";
    for (size_t i = 0; i < repos.size(); ++i) {
        const auto& r = repos[i];
        ss << "  {\n";
        ss << "    \"id\": \"" << Categorizer::escape_json(r.id) << "\",\n";
        ss << "    \"name\": \"" << Categorizer::escape_json(r.name) << "\",\n";
        ss << "    \"type\": \"" << Categorizer::escape_json(r.type) << "\",\n";
        ss << "    \"enabled\": " << (r.enabled ? "true" : "false") << ",\n";
        ss << "    \"is_protected\": " << (r.is_protected ? "true" : "false") << ",\n";
        ss << "    \"is_testing\": " << (r.is_testing ? "true" : "false") << ",\n";
        ss << "    \"url\": \"" << Categorizer::escape_json(r.url) << "\"\n";
        ss << "  }" << (i + 1 < repos.size() ? "," : "") << "\n";
    }
    ss << "]\n";
    return ss.str();
}

void RepoBackend::print_terminal_repos(const std::vector<RepoItem>& repos) {
    std::cout << "\n--- Dépôts Logiciels Configurés (" << repos.size() << ") ---\n\n";
    for (const auto& r : repos) {
        std::string status = r.enabled ? "\033[1;32m[Activé]\033[0m" : "\033[1;31m[Désactivé]\033[0m";
        std::string prot = r.is_protected ? " \033[1;33m[Protégé]\033[0m" : "";
        std::string test_badge = r.is_testing ? " \033[1;35m[Test/Instable]\033[0m" : "";
        std::string type_badge = (r.type == "rpm") ? "\033[1;36m[RPM]\033[0m" : "\033[1;34m[Flatpak]\033[0m";

        std::cout << " • " << type_badge << " " << "\033[1m" << std::left << std::setw(30) << r.id << "\033[0m "
                  << status << prot << test_badge << "\n";
        std::cout << "   Nom : " << r.name << "\n";
        if (!r.url.empty()) {
            std::cout << "   URL : " << r.url << "\n";
        }
        std::cout << "\n";
    }
}

bool RepoBackend::enable_repo(const std::string& repo_id) {
    if (geteuid() != 0) {
        Logger::warning("L'activation d'un dépôt nécessite les privilèges administrateur (root).");
        Logger::info("Relancez avec sudo ou pkexec : 'sudo abpm repo enable " + repo_id + "'");
        return false;
    }

    auto repos = list_repos();
    auto it = std::find_if(repos.begin(), repos.end(), [&](const RepoItem& r) { return r.id == repo_id; });

    bool is_flatpak = (it != repos.end() && it->type == "flatpak");

    if (is_flatpak) {
        std::string cmd = "flatpak remote-modify --enable \"" + repo_id + "\"";
        int ret = std::system(cmd.c_str());
        if (ret == 0) {
            Logger::success("Dépôt Flatpak '" + repo_id + "' activé avec succès !");
            return true;
        }
    } else {
        std::string cmd = "dnf config-manager enable \"" + repo_id + "\"";
        int ret = std::system(cmd.c_str());
        if (ret == 0) {
            Logger::success("Dépôt RPM '" + repo_id + "' activé avec succès !");
            return true;
        }
    }

    Logger::error("Échec de l'activation du dépôt '" + repo_id + "'.");
    return false;
}

bool RepoBackend::disable_repo(const std::string& repo_id) {
    if (is_protected(repo_id)) {
        Logger::error("Le dépôt '" + repo_id + "' est indispensable au système Arrera Blue et ne peut pas être désactivé.");
        return false;
    }

    if (geteuid() != 0) {
        Logger::warning("La désactivation d'un dépôt nécessite les privilèges administrateur (root).");
        Logger::info("Relancez avec sudo ou pkexec : 'sudo abpm repo disable " + repo_id + "'");
        return false;
    }

    auto repos = list_repos();
    auto it = std::find_if(repos.begin(), repos.end(), [&](const RepoItem& r) { return r.id == repo_id; });

    bool is_flatpak = (it != repos.end() && it->type == "flatpak");

    if (is_flatpak) {
        std::string cmd = "flatpak remote-modify --disable \"" + repo_id + "\"";
        int ret = std::system(cmd.c_str());
        if (ret == 0) {
            Logger::success("Dépôt Flatpak '" + repo_id + "' désactivé avec succès !");
            return true;
        }
    } else {
        std::string cmd = "dnf config-manager disable \"" + repo_id + "\"";
        int ret = std::system(cmd.c_str());
        if (ret == 0) {
            Logger::success("Dépôt RPM '" + repo_id + "' désactivé avec succès !");
            return true;
        }
    }

    Logger::error("Échec de la désactivation du dépôt '" + repo_id + "'.");
    return false;
}

namespace {

// 1. Récupération de la version Fedora de base depuis /etc/os-release
std::string get_fedora_base_version() {
    std::ifstream os_file("/etc/os-release");
    if (os_file.is_open()) {
        std::string line;
        while (std::getline(os_file, line)) {
            if (line.rfind("REDHAT_SUPPORT_PRODUCT_VERSION=", 0) == 0 ||
                line.rfind("REDHAT_BUGZILLA_PRODUCT_VERSION=", 0) == 0) {
                auto eq = line.find('=');
                if (eq != std::string::npos) {
                    std::string ver = line.substr(eq + 1);
                    ver.erase(std::remove(ver.begin(), ver.end(), '\"'), ver.end());
                    ver.erase(std::remove(ver.begin(), ver.end(), '\''), ver.end());
                    ver.erase(std::remove_if(ver.begin(), ver.end(), ::isspace), ver.end());
                    if (!ver.empty()) return ver;
                }
            }
        }
    }
    return "44"; // Socle Arrera Blue OS 2026
}

// 2. Détection de l'architecture machine
std::string get_system_arch() {
    struct utsname buf;
    if (uname(&buf) == 0 && buf.machine[0] != '\0') {
        return std::string(buf.machine);
    }
#if defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__)
    return "arm";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "i386";
#else
    return "x86_64";
#endif
}

// 3. Structure d'information Copr
struct CoprTarget {
    bool is_copr{false};
    std::string owner;
    std::string project;
};

// 4. Analyse de la chaîne fournie en paramètre
CoprTarget parse_copr_identifier(const std::string& input) {
    CoprTarget target;
    std::string s = input;

    // Suppression des espaces de début et fin
    s.erase(0, s.find_first_not_of(" \t\n\r"));
    s.erase(s.find_last_not_of(" \t\n\r") + 1);

    if (s.empty()) return target;

    // Préfixe copr:
    if (s.rfind("copr:", 0) == 0) {
        s = s.substr(5);
    }

    // URL Web copr.fedorainfracloud.org
    const std::string https_prefix = "https://copr.fedorainfracloud.org/coprs/";
    const std::string http_prefix = "http://copr.fedorainfracloud.org/coprs/";
    const std::string domain_prefix = "copr.fedorainfracloud.org/coprs/";

    if (s.rfind(https_prefix, 0) == 0) {
        s = s.substr(https_prefix.size());
    } else if (s.rfind(http_prefix, 0) == 0) {
        s = s.substr(http_prefix.size());
    } else if (s.rfind(domain_prefix, 0) == 0) {
        s = s.substr(domain_prefix.size());
    }

    // Si ça finit par .repo ou commence par / ou ./ ou http, ce n'est pas un identifiant Copr simple
    if (s.find(".repo") != std::string::npos ||
        s.rfind("http://", 0) == 0 ||
        s.rfind("https://", 0) == 0 ||
        s.rfind("/", 0) == 0 ||
        s.rfind("./", 0) == 0) {
        return target;
    }

    // Extraction owner / project
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, '/')) {
        if (!item.empty()) parts.push_back(item);
    }

    if (parts.size() >= 2) {
        target.is_copr = true;
        target.owner = parts[0];
        target.project = parts[1];
    }

    return target;
}

} // namespace

bool RepoBackend::add_repo(const std::string& url_or_file) {
    if (url_or_file.empty()) {
        Logger::error("Paramètre manquant.");
        Logger::info("Utilisation : abpm repo add <cible> (ex: user/project, copr:user/project, .repo ou url)");
        return false;
    }

    if (geteuid() != 0) {
        Logger::warning("L'ajout d'un dépôt nécessite les privilèges administrateur (root).");
        Logger::info("Relancez avec sudo ou pkexec : 'sudo abpm repo add " + url_or_file + "'");
        return false;
    }

    auto copr = parse_copr_identifier(url_or_file);

    // =========================================================================
    // CAS 1 : C'EST UN DÉPÔT COPR
    // =========================================================================
    if (copr.is_copr) {
        std::string copr_full = copr.owner + "/" + copr.project;
        std::string fedora_ver = get_fedora_base_version();
        std::string arch = get_system_arch();
        std::string chroot = "fedora-" + fedora_ver + "-" + arch;

        Logger::info("Détection d'un dépôt Fedora Copr : " + copr_full);
        Logger::info("Ciblage de la version du socle : Fedora " + fedora_ver + " (" + chroot + ")...");

        // 1. Tenter via dnf copr enable avec le chroot Fedora correspondant
        std::string cmd = "dnf -y copr enable \"" + copr_full + "\" \"" + chroot + "\"";
        int ret = std::system(cmd.c_str());
        if (ret == 0) {
            Logger::success("Dépôt Copr '" + copr_full + "' activé avec succès pour Fedora " + fedora_ver + " !");
            return true;
        }

        // 2. Repli : téléchargement direct du fichier .repo depuis Copr
        Logger::info("Tentative de téléchargement direct du fichier .repo...");
        std::string repo_filename = copr.owner + "-" + copr.project + "-fedora-" + fedora_ver + ".repo";
        std::string repo_url = "https://copr.fedorainfracloud.org/coprs/" + copr.owner + "/" + copr.project +
                               "/repo/fedora-" + fedora_ver + "/" + repo_filename;

        std::string fallback_cmd = "dnf config-manager addrepo --from-repofile=\"" + repo_url + "\" --overwrite";
        ret = std::system(fallback_cmd.c_str());
        if (ret == 0) {
            Logger::success("Dépôt Copr '" + copr_full + "' configuré avec succès depuis " + repo_url + " !");
            return true;
        }

        Logger::error("Échec de l'activation du dépôt Copr '" + copr_full + "'.");
        return false;
    }

    // =========================================================================
    // CAS 2 : C'EST UN FICHIER .REPO CLASSIQUE OU UNE URL
    // =========================================================================
    Logger::info("Ajout du dépôt RPM depuis : " + url_or_file + "...");
    std::string cmd = "dnf config-manager addrepo --from-repofile=\"" + url_or_file + "\" --overwrite";
    int ret = std::system(cmd.c_str());
    if (ret == 0) {
        Logger::success("Dépôt RPM ajouté avec succès !");
        return true;
    }

    Logger::error("Échec de l'ajout du dépôt RPM depuis '" + url_or_file + "'.");
    return false;
}

bool RepoBackend::is_testing(const std::string& id, const std::string& name) {
    auto to_lower = [](std::string_view sv) {
        std::string res;
        res.reserve(sv.size());
        for (char c : sv) {
            res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return res;
    };

    std::string lower_id = to_lower(id);
    std::string lower_name = to_lower(name);

    const std::string keywords[] = {"testing", "test", "caution"};
    for (const auto& kw : keywords) {
        if (lower_id.find(kw) != std::string::npos || lower_name.find(kw) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace abpm::repo