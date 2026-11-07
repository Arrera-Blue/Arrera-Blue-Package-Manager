#include "abpm/dnf/dnf_backend.hpp"
#include "abpm/core/logger.hpp"
#include "abpm/core/progress_emitter.hpp"
#include "abpm/core/categorizer.hpp"
#include <libdnf5/base/base.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/repo/repo_query.hpp>
#include <libdnf5/repo/repo_callbacks.hpp>
#include <libdnf5/repo/download_callbacks.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/rpm/transaction_callbacks.hpp>
#include <libdnf5/rpm/rpm_signature.hpp>
#include <libdnf5/rpm/nevra.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <libdnf5/repo/repo_cache.hpp>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <memory>
#include <unistd.h>
#include <filesystem>
#include <algorithm>

namespace abpm::dnf {

using core::PackageInfo;
using core::PackageSource;
using core::UpdateInfo;
using core::Logger;
using core::ProgressEmitter;
using core::Categorizer;

namespace {

// Callbacks pour le téléchargement des paquets DNF (mappé de 15% à 50%)
class AbpmDownloadCallbacks : public libdnf5::repo::DownloadCallbacks {
public:
    void* add_new_download(void* user_data, const char* /*description*/, double total_to_download) override {
        m_total_bytes += total_to_download;
        return user_data;
    }

    int progress(void* /*user_cb_data*/, double total_to_download, double downloaded) override {
        if (ProgressEmitter::is_enabled() && total_to_download > 0) {
            double ratio = std::min(1.0, std::max(0.0, downloaded / total_to_download));
            int pct = 15 + static_cast<int>(ratio * 35.0); // 15% -> 50%
            if (pct > m_last_pct) {
                m_last_pct = pct;
                ProgressEmitter::emit(pct, "Téléchargement des paquets (" + std::to_string(pct) + "%)...");
            }
        }
        return 0;
    }

    int end(void* /*user_cb_data*/, TransferStatus /*status*/, const char* /*msg*/) override {
        return 0;
    }

private:
    double m_total_bytes = 0;
    int m_last_pct = 15;
};

// Callbacks pour l'installation RPM (mappé de 50% à 95%)
class AbpmTransactionCallbacks : public libdnf5::rpm::TransactionCallbacks {
public:
    void verify_start(uint64_t /*total*/) override {
        ProgressEmitter::emit(52, "Vérification des signatures et de l'intégrité...");
    }

    void elem_progress(const libdnf5::base::TransactionPackage & item, uint64_t amount, uint64_t total) override {
        if (ProgressEmitter::is_enabled()) {
            uint64_t safe_total = std::max<uint64_t>(1, total);
            int pct = 55 + static_cast<int>((double)amount / safe_total * 40.0); // 55% -> 95%
            std::string name = item.get_package().get_name();
            ProgressEmitter::emit(pct, "Installation de " + name + " (" + std::to_string(amount + 1) + "/" + std::to_string(total) + ")...");
        }
    }

    void install_start(const libdnf5::base::TransactionPackage & item, uint64_t /*total*/) override {
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Installation de " + item.get_package().get_name() + " (" + item.get_package().get_evr() + ")...");
        }
    }

    void uninstall_start(const libdnf5::base::TransactionPackage & item, uint64_t /*total*/) override {
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Suppression de " + item.get_package().get_name() + "...");
        }
    }

    void script_start(const libdnf5::base::TransactionPackage * item, libdnf5::rpm::Nevra nevra, ScriptType type) override {
        std::string name = item ? item->get_package().get_name() : nevra.get_name();
        ProgressEmitter::emit(92, "Configuration de " + name + " (script " + std::string(script_type_to_string(type)) + ")...");
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Exécution du script " + std::string(script_type_to_string(type)) + " de " + name + "...");
        }
    }

    void script_error(const libdnf5::base::TransactionPackage * item, libdnf5::rpm::Nevra nevra, ScriptType type, uint64_t return_code) override {
        std::string name = item ? item->get_package().get_name() : nevra.get_name();
        ProgressEmitter::emit_error("Le script " + std::string(script_type_to_string(type)) + " de " + name + " a échoué (code " + std::to_string(return_code) + ")");
        Logger::error("Le script " + std::string(script_type_to_string(type)) + " de " + name + " a échoué (code retour: " + std::to_string(return_code) + ")");
    }

    void unpack_error(const libdnf5::base::TransactionPackage & item) override {
        ProgressEmitter::emit_error("Échec lors de l'extraction de " + item.get_package().get_name());
        Logger::error("Échec lors de l'extraction de " + item.get_package().get_name());
    }

    void cpio_error(const libdnf5::base::TransactionPackage & item) override {
        ProgressEmitter::emit_error("Erreur CPIO pour " + item.get_package().get_name());
        Logger::error("Erreur CPIO pour " + item.get_package().get_name());
    }

    void after_complete(bool success) override {
        if (success) {
            ProgressEmitter::emit(100, "Installation terminée avec succès");
        } else {
            ProgressEmitter::emit_error("Échec de la transaction RPM");
        }
    }
};

class AbpmRepoCallbacks : public libdnf5::repo::RepoCallbacks {
public:
    bool repokey_import(const libdnf5::rpm::KeyInfo & key_info) override {
        Logger::info("Importation de la clé GPG du dépôt (" + key_info.get_short_key_id() + ")...");
        return true;
    }
};

void setup_repo_callbacks(libdnf5::Base& base) {
    libdnf5::repo::RepoQuery q(base);
    for (auto& repo : q) {
        repo->set_callbacks(std::make_unique<AbpmRepoCallbacks>());
    }
}

void log_transaction_errors(libdnf5::base::Transaction& transaction,
                            libdnf5::base::Transaction::TransactionRunResult result) {
    Logger::error("Statut transaction : " + libdnf5::base::Transaction::transaction_result_to_string(result));
    for (const auto& prob : transaction.get_transaction_problems()) {
        Logger::error("  [Problème transaction] " + prob);
    }
    for (const auto& gpg_prob : transaction.get_gpg_signature_problems()) {
        Logger::error("  [Signature GPG] " + gpg_prob);
    }
    for (const auto& msg : transaction.get_rpm_messages()) {
        Logger::warning("  [Message RPM] " + msg);
    }
    std::string script_out = transaction.get_last_script_output();
    if (!script_out.empty()) {
        Logger::warning("  [Sortie script] " + script_out);
    }
}

} // anonymous namespace

std::vector<PackageInfo> DnfBackend::list_installed() {
    std::vector<PackageInfo> results;

    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();

        // Charger uniquement la base RPM système locale (aucun téléchargement réseau)
        base.get_repo_sack()->load_repos(libdnf5::repo::Repo::Type::SYSTEM);

        libdnf5::rpm::PackageQuery query(base);
        query.filter_installed();

        results.reserve(query.size());
        for (const auto& pkg : query) {
            PackageInfo info;
            info.name = pkg.get_name();
            info.id = pkg.get_nevra();
            info.version = pkg.get_evr();
            info.arch = pkg.get_arch();
            info.source = PackageSource::DNF;
            info.origin = "system";

            results.push_back(std::move(info));
        }
    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur DNF : ") + e.what());
    }

    return results;
}

std::vector<PackageInfo> DnfBackend::search(std::string_view term) {
    std::vector<PackageInfo> results;
    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        auto repo_sack = base.get_repo_sack();
        // Charger la configuration des dépôts système (/etc/yum.repos.d/)
        repo_sack->create_repos_from_system_configuration();
        // Charger les dépôts (cache local pour une recherche instantanée)
        repo_sack->load_repos();
        libdnf5::rpm::PackageQuery query(base);
        // Recherche insensible à la casse (ICONTAINS)
        query.filter_name(std::string(term), libdnf5::sack::QueryCmp::ICONTAINS);
        results.reserve(query.size());
        for (const auto& pkg : query) {
            PackageInfo info;
            info.name = pkg.get_name();
            info.id = pkg.get_nevra();
            info.version = pkg.get_evr();
            info.arch = pkg.get_arch();
            info.source = PackageSource::DNF;
            info.origin = "dnf";
            results.push_back(std::move(info));
        }
    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur recherche DNF : ") + e.what());
    }
    return results;
}

std::vector<UpdateInfo> DnfBackend::check_updates(bool silent) {
    std::vector<UpdateInfo> updates;
    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();
        repo_sack->load_repos();

        libdnf5::Goal goal(base);
        goal.add_rpm_upgrade();
        auto transaction = goal.resolve();

        // Indexation préalable des fichiers .desktop système pour catégorisation et métadonnées
        auto desktop_index = Categorizer::index_desktop_directory("/usr/share/applications");

        // Récupérer les paquets qui sont réellement dans la transaction de mise à jour résolue
        std::unordered_map<std::string, UpdateInfo> unique_updates;

        for (const auto& trans_pkg : transaction.get_transaction_packages()) {
            auto action = trans_pkg.get_action();
            if (action == libdnf5::transaction::TransactionItemAction::UPGRADE ||
                action == libdnf5::transaction::TransactionItemAction::INSTALL) {
                auto pkg = trans_pkg.get_package();
                std::string name = pkg.get_name();
                if (unique_updates.find(name) == unique_updates.end()) {
                    UpdateInfo info;
                    info.package = name;
                    info.new_ver = pkg.get_evr();
                    info.arch = pkg.get_arch();
                    info.source = PackageSource::DNF;
                    info.origin = "dnf";

                    // Détection des métadonnées via .desktop (si présent)
                    auto it = desktop_index.find(name);
                    if (it == desktop_index.end()) {
                        // Recherche si un .desktop se termine par .name (ex: org.gnome.Name)
                        for (const auto& [did, dmeta] : desktop_index) {
                            if (did == name || did.ends_with("." + name) || dmeta.exec == name) {
                                it = desktop_index.find(did);
                                break;
                            }
                        }
                    }

                    if (it != desktop_index.end()) {
                        const auto& d = it->second;
                        info.id = d.id;
                        info.name = d.name;
                        info.category = d.category; // "utility" ou "app"
                        info.icon = d.icon.empty() ? name : d.icon;
                        info.summary = !d.summary.empty() ? d.summary : pkg.get_summary();
                    } else {
                        // Aucun fichier .desktop : vérification utilitaires CLI connus ou composant système
                        const std::unordered_set<std::string> known_utilities = {
                            "less", "nano", "vim", "tmux", "git", "curl", "wget", "tree", "rsync", "zip", "unzip"
                        };

                        info.id = name;
                        if (known_utilities.find(name) != known_utilities.end()) {
                            info.name = name;
                            info.category = "utility";
                            info.icon = "utilities-terminal";
                            info.summary = pkg.get_summary();
                        } else {
                            info.category = "system";
                            info.icon = "system-component";
                            if (name.starts_with("kernel")) {
                                info.name = "Noyau Linux (" + name + ")";
                            } else {
                                info.name = name;
                            }
                            info.summary = pkg.get_summary();
                            if (info.summary.empty()) {
                                info.summary = "Composant central du système d'exploitation";
                            }
                        }
                    }

                    auto replaced = trans_pkg.get_replaces();
                    if (!replaced.empty()) {
                        info.current_ver = replaced[0].get_evr();
                    } else {
                        libdnf5::rpm::PackageQuery installed_query(base);
                        installed_query.filter_installed();
                        installed_query.filter_name(name);
                        if (!installed_query.empty()) {
                            info.current_ver = (*installed_query.begin()).get_evr();
                        } else {
                            info.current_ver = "nouveau";
                        }
                    }
                    unique_updates[name] = std::move(info);
                }
            }
        }

        updates.reserve(unique_updates.size());
        for (auto& [_, info] : unique_updates) {
            updates.push_back(std::move(info));
        }

        // Si le solveur a rencontré des dépendances non résolues (mises à jour différées)
        auto logs = transaction.get_resolve_logs_as_strings();
        if (!logs.empty() && !silent && !ProgressEmitter::is_enabled()) {
            Logger::warning("Mise(s) à jour DNF différée(s) (conflit de dépendances, ex: RPM Fusion) :");
            std::unordered_set<std::string> seen;
            for (const auto& log : logs) {
                std::istringstream stream(log);
                std::string line;
                while (std::getline(stream, line)) {
                    auto start = line.find_first_not_of(" \t-•");
                    if (start == std::string::npos) continue;
                    std::string clean = line.substr(start);
                    if (clean.starts_with("Problem") || clean.find("requires") != std::string::npos) {
                        if (seen.insert(clean).second) {
                            Logger::warning("  • " + clean);
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur vérification DNF : ") + e.what());
    }
    return updates;
}


bool DnfBackend::update(const std::vector<std::string>& target_packages) {
    try {
        ProgressEmitter::emit(0, "Initialisation de la mise à jour DNF...");
        libdnf5::Base base;
        base.load_config();
        base.setup();

        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();
        setup_repo_callbacks(base);
        base.set_download_callbacks(std::make_unique<AbpmDownloadCallbacks>());
        repo_sack->load_repos();

        ProgressEmitter::emit(10, "Vérification des dépendances DNF...");
        libdnf5::Goal goal(base);

        if (target_packages.empty()) {
            // Demander la mise à jour de tous les paquets système
            goal.add_rpm_upgrade();
        } else {
            // Demander la mise à jour des paquets ciblés
            for (const auto& pkg : target_packages) {
                goal.add_rpm_upgrade(pkg);
            }
        }

        auto transaction = goal.resolve();

        auto logs = transaction.get_resolve_logs_as_strings();
        if (!logs.empty() && !ProgressEmitter::is_enabled()) {
            Logger::warning("Conflit(s) de dépendance détecté(s) lors de la résolution :");
            std::unordered_set<std::string> seen;
            for (const auto& log : logs) {
                std::istringstream stream(log);
                std::string line;
                while (std::getline(stream, line)) {
                    auto start = line.find_first_not_of(" \t-•");
                    if (start == std::string::npos) continue;
                    std::string clean = line.substr(start);
                    if (clean.starts_with("Problem") || clean.find("requires") != std::string::npos) {
                        if (seen.insert(clean).second) {
                            Logger::warning("  • " + clean);
                        }
                    }
                }
            }
        }

        if (transaction.empty()) {
            if (!target_packages.empty()) {
                std::string err_msg = "Aucune mise à jour applicable pour les paquets DNF spécifiés.";
                ProgressEmitter::emit_error(err_msg);
                if (!ProgressEmitter::is_enabled()) {
                    Logger::error(err_msg);
                }
                return false;
            }
            if (!logs.empty() && !ProgressEmitter::is_enabled()) {
                Logger::info("Aucune mise à jour applicable immédiatement (les paquets en conflit sont différés).");
            } else if (!ProgressEmitter::is_enabled()) {
                Logger::success("Tous les paquets système DNF sont déjà à jour !");
            }
            ProgressEmitter::emit(100, "Tous les paquets système DNF sont déjà à jour");
            return true;
        }

        auto count = transaction.get_transaction_packages_count();
        if (!ProgressEmitter::is_enabled()) {
            Logger::info(std::string("Paquets RPM à mettre à jour : ") + std::to_string(count));
        }

        // Vérification des privilèges root pour installer des paquets système
        if (geteuid() != 0) {
            std::string err_msg = "La mise à jour système DNF nécessite les privilèges administrateur (ex: pkexec).";
            ProgressEmitter::emit_error(err_msg);
            if (!ProgressEmitter::is_enabled()) {
                Logger::warning("Pour installer les mises à jour système DNF, exécutez la commande avec sudo : 'sudo abpm update'");
            }
            return false;
        }

        ProgressEmitter::emit(15, "Téléchargement des paquets RPM...");
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Téléchargement des paquets RPM...");
        }
        transaction.download();

        ProgressEmitter::emit(50, "Application des paquets système DNF...");
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Application des paquets système...");
        }
        transaction.set_callbacks(std::make_unique<AbpmTransactionCallbacks>());
        auto result = transaction.run();

        if (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            ProgressEmitter::emit(100, "Mise à jour système DNF terminée avec succès");
            if (!ProgressEmitter::is_enabled()) {
                Logger::success("Mise à jour système DNF terminée avec succès !");
            }
            return true;
        } else {
            ProgressEmitter::emit_error("Échec lors de l'application de la transaction DNF.");
            if (!ProgressEmitter::is_enabled()) {
                Logger::error("Échec de l'application de la transaction DNF.");
                log_transaction_errors(transaction, result);
            }
            return false;
        }
    } catch (const std::exception& e) {
        std::string err_msg = std::string("Erreur mise à jour DNF : ") + e.what();
        ProgressEmitter::emit_error(err_msg);
        if (!ProgressEmitter::is_enabled()) {
            Logger::error(err_msg);
        }
        return false;
    }
}

bool DnfBackend::install(std::string_view package_name, bool assumeyes) {
    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();
        setup_repo_callbacks(base);
        base.set_download_callbacks(std::make_unique<AbpmDownloadCallbacks>());
        repo_sack->load_repos();

        // 1. Vérifier si le paquet est déjà installé
        libdnf5::rpm::PackageQuery installed_q(base);
        installed_q.filter_installed();
        installed_q.filter_name(std::string(package_name));
        if (!installed_q.empty()) {
            if (ProgressEmitter::is_enabled()) {
                ProgressEmitter::emit(100, "L'application est déjà installée sur le système");
            } else {
                Logger::info(std::string("Le paquet '") + std::string(package_name) + "' est déjà installé sur le système.");
            }
            return true;
        }

        // 2. Préparer la transaction d'installation
        ProgressEmitter::emit(5, "Résolution des dépendances...");
        libdnf5::Goal goal(base);
        goal.add_rpm_install(std::string(package_name));
        auto transaction = goal.resolve();

        if (transaction.empty()) {
            libdnf5::rpm::PackageQuery avail_q(base);
            avail_q.filter_name(std::string(package_name));
            if (!avail_q.empty()) {
                ProgressEmitter::emit_error("Conflit de dépendances lors de l'installation de " + std::string(package_name));
                Logger::error("Le paquet '" + std::string(package_name) + "' existe dans les dépôts DNF mais ne peut pas être installé (conflit de dépendances) :");
                for (const auto& log : transaction.get_resolve_logs_as_strings()) {
                    std::istringstream stream(log);
                    std::string line;
                    while (std::getline(stream, line)) {
                        auto start = line.find_first_not_of(" \t-•");
                        if (start != std::string::npos) {
                            Logger::error("  • " + line.substr(start));
                        }
                    }
                }
                return true;
            }
            return false;
        }

        // 3. Affichage si mode interactif classique
        auto tx_packages = transaction.get_transaction_packages();
        if (!ProgressEmitter::is_enabled()) {
            std::cout << "\n--- Paquets DNF à installer (" << tx_packages.size() << ") ---\n";
            for (const auto& tp : tx_packages) {
                const auto& pkg = tp.get_package();
                auto action = tp.get_action();
                std::string action_str = "";
                if (action == libdnf5::transaction::TransactionItemAction::UPGRADE) {
                    action_str = " [mise à jour]";
                } else if (action == libdnf5::transaction::TransactionItemAction::DOWNGRADE) {
                    action_str = " [rétrogradation]";
                } else if (action == libdnf5::transaction::TransactionItemAction::REPLACED ||
                           action == libdnf5::transaction::TransactionItemAction::REMOVE) {
                    action_str = " [suppression]";
                }
                std::cout << " • " << pkg.get_name() << " " << pkg.get_evr() << " (" << pkg.get_arch() << ")" << action_str << "\n";
            }
            std::cout << "\n";
        }

        // 4. Confirmation (ignorée si assumeyes ou si progress est actif)
        if (!assumeyes) {
            if (!Logger::confirm("Voulez-vous procéder à l'installation ?")) {
                Logger::info("Installation annulée.");
                return true;
            }
        }

        // 5. Privilèges root
        if (geteuid() != 0) {
            ProgressEmitter::emit_error("L'installation nécessite les privilèges administrateur (ex: pkexec)");
            Logger::warning("L'installation de paquets système DNF nécessite les privilèges administrateur.");
            Logger::info("Relancez avec sudo : 'sudo abpm install " + std::string(package_name) + "'");
            return true;
        }

        ProgressEmitter::emit(15, "Téléchargement des paquets...");
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Téléchargement des paquets...");
        }
        transaction.download();

        ProgressEmitter::emit(50, "Préparation de l'installation des paquets...");
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Installation des paquets RPM...");
        }
        transaction.set_callbacks(std::make_unique<AbpmTransactionCallbacks>());
        auto result = transaction.run();

        if (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            if (!ProgressEmitter::is_enabled()) {
                Logger::success("Paquet '" + std::string(package_name) + "' installé avec succès via DNF !");
            }
            return true;
        } else {
            ProgressEmitter::emit_error("Échec de la transaction DNF");
            Logger::error("Échec de l'installation DNF.");
            log_transaction_errors(transaction, result);
            return true;
        }
    } catch (const std::exception& e) {
        ProgressEmitter::emit_error(std::string("Erreur DNF : ") + e.what());
        Logger::error(std::string("Erreur DNF : ") + e.what());
        return false;
    }
}

bool DnfBackend::is_installed(std::string_view name) {
    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        base.get_repo_sack()->load_repos(libdnf5::repo::Repo::Type::SYSTEM);
        libdnf5::rpm::PackageQuery q(base);
        q.filter_installed();
        q.filter_name(std::string(name));
        return !q.empty();
    } catch (...) {
        return false;
    }
}

bool DnfBackend::is_available(std::string_view package_name) {
    if (package_name.empty()) return false;
    auto available = get_available_packages({std::string(package_name)});
    return !available.empty();
}

std::vector<std::string> DnfBackend::get_available_packages(const std::vector<std::string>& names) {
    std::vector<std::string> found;
    if (names.empty()) return found;

    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();
        repo_sack->load_repos();

        for (const auto& name : names) {
            libdnf5::rpm::PackageQuery q(base);
            q.filter_name(name);
            if (!q.empty()) {
                found.push_back(name);
            }
        }
    } catch (...) {}

    return found;
}

bool DnfBackend::remove(const std::vector<std::string>& package_names) {
    if (package_names.empty()) return true;
    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();
        repo_sack->load_repos();
        libdnf5::Goal goal(base);
        for (const auto& pkg : package_names) {
            goal.add_rpm_remove(pkg);
        }
        auto transaction = goal.resolve();
        if (transaction.empty()) {
            for (const auto& log : transaction.get_resolve_logs_as_strings()) {
                Logger::warning(log);
            }
            return false;
        }

        // Vérifier si d'autres paquets vont être supprimés en cascade
        auto tx_packages = transaction.get_transaction_packages();
        if (tx_packages.size() > package_names.size()) {
            Logger::warning("Attention : Des paquets dépendants vont également être supprimés en cascade :");
            for (const auto& tp : tx_packages) {
                const auto& pkg = tp.get_package();
                if (std::find(package_names.begin(), package_names.end(), pkg.get_name()) == package_names.end()) {
                    Logger::warning("  • " + pkg.get_name() + " (" + pkg.get_evr() + ")");
                }
            }
        }
        if (geteuid() != 0) {
            Logger::warning("La suppression de paquets système DNF nécessite les privilèges administrateur.");
            Logger::info("Relancez avec sudo pour finaliser la suppression DNF.");
            return false;
        }
        Logger::info("Désinstallation des paquets RPM en cours...");
        transaction.set_callbacks(std::make_unique<AbpmTransactionCallbacks>());
        auto result = transaction.run();
        if (result != libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            log_transaction_errors(transaction, result);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur suppression DNF : ") + e.what());
        return false;
    }
}

std::vector<PackageInfo> DnfBackend::list_installed_local_rpms() {
    std::vector<PackageInfo> results;

    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        base.get_repo_sack()->load_repos(libdnf5::repo::Repo::Type::SYSTEM);

        libdnf5::rpm::PackageQuery q(base);
        q.filter_installed();

        for (const auto& pkg : q) {
            std::string repo_id = pkg.get_from_repo_id();
            // Les paquets installés via un fichier RPM local sont marqués @commandline
            if (repo_id == "@commandline" || repo_id.find("commandline") != std::string::npos) {
                PackageInfo info;
                info.name = pkg.get_name();
                info.id = pkg.get_nevra();
                info.version = pkg.get_evr();
                info.arch = pkg.get_arch();
                info.source = PackageSource::RPM_LOCAL;
                info.origin = "local_rpm";
                results.push_back(std::move(info));
            }
        }
    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur DNF : ") + e.what());
    }

    return results;
}

bool DnfBackend::install_local_rpm(const std::string& file_path, bool assumeyes) {
    namespace fs = std::filesystem;

    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        Logger::error("Fichier RPM introuvable : '" + file_path + "'");
        return false;
    }

    std::string canonical_path = fs::canonical(file_path).string();

    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();

        ProgressEmitter::emit(5, "Analyse et résolution des dépendances du RPM local...");
        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();
        setup_repo_callbacks(base);
        base.set_download_callbacks(std::make_unique<AbpmDownloadCallbacks>());
        repo_sack->load_repos();

        libdnf5::Goal goal(base);
        // add_install gère nativement le chemin vers un fichier RPM local
        goal.add_install(canonical_path);

        auto transaction = goal.resolve();
        if (transaction.empty()) {
            ProgressEmitter::emit_error("Impossible de résoudre l'installation de ce fichier RPM.");
            for (const auto& log : transaction.get_resolve_logs_as_strings()) {
                Logger::warning(log);
            }
            Logger::error("Impossible de résoudre l'installation de ce fichier RPM.");
            return false;
        }

        // Afficher ce qui va être installé (le paquet local + dépendances)
        auto tx_packages = transaction.get_transaction_packages();
        if (!ProgressEmitter::is_enabled()) {
            std::cout << "\n--- Paquets RPM à installer (" << tx_packages.size() << ") ---\n";
            for (const auto& tp : tx_packages) {
                const auto& pkg = tp.get_package();
                auto action = tp.get_action();
                std::string action_str = "";
                if (action == libdnf5::transaction::TransactionItemAction::UPGRADE) {
                    action_str = " [mise à jour]";
                } else if (action == libdnf5::transaction::TransactionItemAction::DOWNGRADE) {
                    action_str = " [rétrogradation]";
                } else if (action == libdnf5::transaction::TransactionItemAction::REPLACED ||
                           action == libdnf5::transaction::TransactionItemAction::REMOVE) {
                    action_str = " [suppression]";
                }
                std::cout << " • " << pkg.get_name() << " " << pkg.get_evr() << " (" << pkg.get_arch() << ")" << action_str << "\n";
            }
            std::cout << "\n";
        }

        // Confirmation
        if (!assumeyes) {
            if (!Logger::confirm("Voulez-vous installer ce paquet RPM ?")) {
                Logger::info("Installation annulée.");
                return true;
            }
        }

        // Vérifier les droits root
        if (geteuid() != 0) {
            ProgressEmitter::emit_error("L'installation d'un fichier RPM nécessite les privilèges administrateur (ex: pkexec)");
            Logger::warning("L'installation d'un fichier RPM nécessite les privilèges administrateur.");
            Logger::info("Relancez avec sudo : 'sudo abpm install --rpm " + file_path + "'");
            return true;
        }

        ProgressEmitter::emit(15, "Téléchargement des dépendances éventuelles...");
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Téléchargement des dépendances éventuelles...");
        }
        transaction.download();

        ProgressEmitter::emit(50, "Installation du paquet RPM...");
        if (!ProgressEmitter::is_enabled()) {
            Logger::info("Installation du paquet RPM...");
        }
        transaction.set_callbacks(std::make_unique<AbpmTransactionCallbacks>());
        auto result = transaction.run();

        if (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            if (!ProgressEmitter::is_enabled()) {
                Logger::success("Fichier RPM installé avec succès !");
            }
            return true;
        } else {
            ProgressEmitter::emit_error("Échec lors de l'installation du fichier RPM.");
            Logger::error("Échec lors de l'installation du fichier RPM.");
            log_transaction_errors(transaction, result);
            return false;
        }

    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur DNF : ") + e.what());
        return false;
    }
}

bool DnfBackend::clean(CleanTarget target) {
    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();

        libdnf5::repo::RepoQuery q(base);

        if (target == CleanTarget::EXPIRE_CACHE) {
            Logger::info("Expiration du cache des dépôts...");
            for (auto& repo : q) {
                repo->expire();
            }
            Logger::success("Cache des dépôts expiré avec succès (les métadonnées seront rechargées au prochain appel).");
            return true;
        }

        if (geteuid() != 0) {
            Logger::warning("Le nettoyage des fichiers de cache système nécessite les privilèges administrateur.");
            Logger::info("Relancez avec sudo : 'sudo abpm clean " + 
                (target == CleanTarget::PACKAGES ? std::string("packages") : std::string("all")) + "'");
            return false;
        }

        size_t total_files = 0;
        for (auto& repo : q) {
            std::string cachedir = repo->get_cachedir();
            if (cachedir.empty()) continue;
            try {
                libdnf5::repo::RepoCache rc(base, cachedir);
                if (target == CleanTarget::PACKAGES) {
                    auto stats = rc.remove_packages();
                    total_files += stats.get_files_removed();
                } else if (target == CleanTarget::ALL) {
                    repo->expire();
                    auto stats = rc.remove_all();
                    total_files += stats.get_files_removed();
                }
            } catch (...) {}
        }

        if (target == CleanTarget::PACKAGES) {
            Logger::success("Paquets RPM mis en cache supprimés (" + std::to_string(total_files) + " fichier(s) effacé(s)).");
        } else {
            Logger::success("Cache système DNF complètement nettoyé (" + std::to_string(total_files) + " fichier(s) supprimé(s)).");
        }
        return true;
    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur nettoyage DNF : ") + e.what());
        return false;
    }
}

} // namespace abpm::dnf