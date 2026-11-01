#include "abpm/dnf/dnf_backend.hpp"
#include "abpm/core/logger.hpp"
#include <libdnf5/base/base.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/repo/repo_query.hpp>
#include <libdnf5/repo/repo_callbacks.hpp>
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

namespace abpm::dnf {

using core::PackageInfo;
using core::PackageSource;
using core::Logger;
using core::UpdateInfo;

namespace {

class AbpmTransactionCallbacks : public libdnf5::rpm::TransactionCallbacks {
public:
    void install_start(const libdnf5::base::TransactionPackage & item, uint64_t /*total*/) override {
        Logger::info("Installation de " + item.get_package().get_name() + " (" + item.get_package().get_evr() + ")...");
    }

    void uninstall_start(const libdnf5::base::TransactionPackage & item, uint64_t /*total*/) override {
        Logger::info("Suppression de " + item.get_package().get_name() + "...");
    }

    void script_start(const libdnf5::base::TransactionPackage * item, libdnf5::rpm::Nevra nevra, ScriptType type) override {
        std::string name = item ? item->get_package().get_name() : nevra.get_name();
        Logger::info("Exécution du script " + std::string(script_type_to_string(type)) + " de " + name + "...");
    }

    void script_error(const libdnf5::base::TransactionPackage * item, libdnf5::rpm::Nevra nevra, ScriptType type, uint64_t return_code) override {
        std::string name = item ? item->get_package().get_name() : nevra.get_name();
        Logger::error("Le script " + std::string(script_type_to_string(type)) + " de " + name + " a échoué (code retour: " + std::to_string(return_code) + ")");
    }

    void unpack_error(const libdnf5::base::TransactionPackage & item) override {
        Logger::error("Échec lors de l'extraction de " + item.get_package().get_name());
    }

    void cpio_error(const libdnf5::base::TransactionPackage & item) override {
        Logger::error("Erreur CPIO pour " + item.get_package().get_name());
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

std::vector<UpdateInfo> DnfBackend::check_updates() {
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
                    info.name = name;
                    info.id = pkg.get_nevra();
                    info.new_ver = pkg.get_evr();
                    info.arch = pkg.get_arch();
                    info.source = PackageSource::DNF;
                    info.origin = "dnf";

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
        if (!logs.empty()) {
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
bool DnfBackend::update() {
    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();

        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();
        setup_repo_callbacks(base);
        repo_sack->load_repos();

        libdnf5::Goal goal(base);
        // Demander la mise à jour de tous les paquets système
        goal.add_rpm_upgrade();

        auto transaction = goal.resolve();

        auto logs = transaction.get_resolve_logs_as_strings();
        if (!logs.empty()) {
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
            if (!logs.empty()) {
                Logger::info("Aucune mise à jour applicable immédiatement (les paquets en conflit sont différés).");
            } else {
                Logger::success("Tous les paquets système DNF sont déjà à jour !");
            }
            return true;
        }

        auto count = transaction.get_transaction_packages_count();
        Logger::info(std::string("Paquets RPM à mettre à jour : ") + std::to_string(count));

        // Vérification des privilèges root pour installer des paquets système
        if (geteuid() != 0) {
            Logger::warning("Pour installer les mises à jour système DNF, exécutez la commande avec sudo : 'sudo abpm update'");
            return false;
        }

        Logger::info("Téléchargement des paquets RPM...");
        transaction.download();

        Logger::info("Application des paquets système...");
        transaction.set_callbacks(std::make_unique<AbpmTransactionCallbacks>());
        auto result = transaction.run();

        if (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            Logger::success("Mise à jour système DNF terminée avec succès !");
            return true;
        } else {
            Logger::error("Échec de l'application de la transaction DNF.");
            log_transaction_errors(transaction, result);
            return false;
        }
    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur mise à jour DNF : ") + e.what());
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
        repo_sack->load_repos();
        // 1. Vérifier si le paquet est déjà installé
        libdnf5::rpm::PackageQuery installed_q(base);
        installed_q.filter_installed();
        installed_q.filter_name(std::string(package_name));
        if (!installed_q.empty()) {
            Logger::info(std::string("Le paquet '") + std::string(package_name) + "' est déjà installé sur le système.");
            return true;
        }
        // 2. Préparer la transaction d'installation
        libdnf5::Goal goal(base);
        goal.add_rpm_install(std::string(package_name));
        auto transaction = goal.resolve();

        // Si la transaction est vide, vérifier si le paquet existe mais que ses dépendances échouent
        if (transaction.empty()) {
            libdnf5::rpm::PackageQuery avail_q(base);
            avail_q.filter_name(std::string(package_name));
            if (!avail_q.empty()) {
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
                return true; // Pris en charge comme erreur DNF (ne pas basculer à tort sur Flathub)
            }
            return false;
        }

        // 3. Afficher ce qui va être installé (le paquet et ses dépendances)
        auto tx_packages = transaction.get_transaction_packages();
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
        // 4. Demander confirmation
        if (!assumeyes) {
            if (!Logger::confirm("Voulez-vous procéder à l'installation ?")) {
                Logger::info("Installation annulée.");
                return true;
            }
        }
        // 5. Vérifier les privilèges root
        if (geteuid() != 0) {
            Logger::warning("L'installation de paquets système DNF nécessite les privilèges administrateur.");
            Logger::info("Relancez avec sudo : 'sudo abpm install " + std::string(package_name) + "'");
            return true;
        }
        Logger::info("Téléchargement des paquets...");
        transaction.download();
        Logger::info("Installation des paquets RPM...");
        transaction.set_callbacks(std::make_unique<AbpmTransactionCallbacks>());
        auto result = transaction.run();
        if (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            Logger::success("Paquet '" + std::string(package_name) + "' installé avec succès via DNF !");
            return true;
        } else {
            Logger::error("Échec de l'installation DNF.");
            log_transaction_errors(transaction, result);
            return true;
        }
    } catch (const std::exception& e) {
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

        auto repo_sack = base.get_repo_sack();
        repo_sack->create_repos_from_system_configuration();
        setup_repo_callbacks(base);
        repo_sack->load_repos();

        libdnf5::Goal goal(base);
        // add_install gère nativement le chemin vers un fichier RPM local
        goal.add_install(canonical_path);

        auto transaction = goal.resolve();
        if (transaction.empty()) {
            for (const auto& log : transaction.get_resolve_logs_as_strings()) {
                Logger::warning(log);
            }
            Logger::error("Impossible de résoudre l'installation de ce fichier RPM.");
            return false;
        }

        // Afficher ce qui va être installé (le paquet local + dépendances)
        auto tx_packages = transaction.get_transaction_packages();
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

        // Confirmation
        if (!assumeyes) {
            if (!Logger::confirm("Voulez-vous installer ce paquet RPM ?")) {
                Logger::info("Installation annulée.");
                return true;
            }
        }

        // Vérifier les droits root
        if (geteuid() != 0) {
            Logger::warning("L'installation d'un fichier RPM nécessite les privilèges administrateur.");
            Logger::info("Relancez avec sudo : 'sudo abpm install --rpm " + file_path + "'");
            return true;
        }

        Logger::info("Téléchargement des dépendances éventuelles...");
        transaction.download();

        Logger::info("Installation du paquet RPM...");
        transaction.set_callbacks(std::make_unique<AbpmTransactionCallbacks>());
        auto result = transaction.run();

        if (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            Logger::success("Fichier RPM installé avec succès !");
            return true;
        } else {
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