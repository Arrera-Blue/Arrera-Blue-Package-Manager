#include "abpm/dnf/dnf_backend.hpp"
#include "abpm/core/logger.hpp"
#include <libdnf5/base/base.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/base/transaction_package.hpp>
#include <iostream>
#include <unistd.h>

namespace abpm::dnf {

using core::PackageInfo;
using core::PackageSource;
using core::Logger;
using core::UpdateInfo;

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
        // Récupérer les paquets qui sont des mises à jour de paquets installés
        libdnf5::rpm::PackageQuery query(base);
        query.filter_upgrades();
        updates.reserve(query.size());
        for (const auto& pkg : query) {
            UpdateInfo info;
            info.name = pkg.get_name();
            info.id = pkg.get_nevra();
            info.new_ver = pkg.get_evr();
            info.arch = pkg.get_arch();
            info.source = PackageSource::DNF;
            info.origin = "dnf";
            // Trouver la version actuellement installée
            libdnf5::rpm::PackageQuery installed_query(base);
            installed_query.filter_installed();
            installed_query.filter_name(pkg.get_name());
            if (installed_query.size() > 0) {
                info.current_ver = (*installed_query.begin()).get_evr();
            } else {
                info.current_ver = "inconnue";
            }
            updates.push_back(std::move(info));
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
        repo_sack->load_repos();

        libdnf5::Goal goal(base);
        // Demander la mise à jour de tous les paquets système
        goal.add_rpm_upgrade();

        auto transaction = goal.resolve();

        if (transaction.empty()) {
            Logger::success("Tous les paquets système DNF sont déjà à jour !");
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
        auto result = transaction.run();

        if (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            Logger::success("Mise à jour système DNF terminée avec succès !");
            return true;
        } else {
            Logger::error("Échec de l'application de la transaction DNF.");
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
        // Si la transaction est vide, le paquet n'a pas été trouvé dans DNF
        if (transaction.empty()) {
            return false;
        }
        // 3. Afficher ce qui va être installé (le paquet et ses dépendances)
        auto tx_packages = transaction.get_transaction_packages();
        std::cout << "\n--- Paquets DNF à installer (" << tx_packages.size() << ") ---\n";
        for (const auto& tp : tx_packages) {
            const auto& pkg = tp.get_package();
            std::cout << " • " << pkg.get_name() << " " << pkg.get_evr() << " (" << pkg.get_arch() << ")\n";
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
        auto result = transaction.run();
        if (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS) {
            Logger::success("Paquet '" + std::string(package_name) + "' installé avec succès via DNF !");
            return true;
        } else {
            Logger::error("Échec de l'installation DNF.");
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
        if (geteuid() != 0) {
            Logger::warning("La suppression de paquets système DNF nécessite les privilèges administrateur.");
            Logger::info("Relancez avec sudo pour finaliser la suppression DNF.");
            return false;
        }
        Logger::info("Désinstallation des paquets RPM en cours...");
        auto result = transaction.run();
        return (result == libdnf5::base::Transaction::TransactionRunResult::SUCCESS);
    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur suppression DNF : ") + e.what());
        return false;
    }
}

} // namespace abpm::dnf