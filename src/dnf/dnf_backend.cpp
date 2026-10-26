#include "abpm/dnf/dnf_backend.hpp"
#include "abpm/core/logger.hpp"
#include <libdnf5/base/base.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/base/transaction.hpp>
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

} // namespace abpm::dnf