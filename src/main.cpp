#include "abpm/core/logger.hpp"
#include "abpm/cli/cli_parser.hpp"
#include "abpm/flatpak/flatpak_backend.hpp"
#include "abpm/dnf/dnf_backend.hpp"
#include "abpm/appimage/appimage_backend.hpp"
#include "abpm/archive/archive_backend.hpp"
#include <iomanip>
#include <iostream>

using abpm::core::Logger;
using abpm::cli::CliParser;
using abpm::cli::CommandType;
using abpm::cli::SourceFilter;
using abpm::flatpak::FlatpakBackend;
using abpm::dnf::DnfBackend;
using abpm::appimage::AppImageBackend;
using abpm::archive::ArchiveBackend;

int main(int argc, char* argv[]) {
    auto options = CliParser::parse(argc, argv);

    switch (options.command) {
        case CommandType::HELP:
            CliParser::print_help();
            break;

        case CommandType::VERSION:
            CliParser::print_version();
            break;

        case CommandType::LIST: {
            Logger::banner();

            // 1. DNF (si filtre ALL ou DNF)
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::DNF) {
                Logger::info("Récupération des paquets DNF (RPM)...");
                auto rpm_pkgs = DnfBackend::list_installed();

                std::cout << "\n--- Paquets Système DNF (" << rpm_pkgs.size() << ") ---\n";
                for (const auto& pkg : rpm_pkgs) {
                    std::cout << " • " << std::left << std::setw(30) << pkg.name 
                              << " " << std::setw(32) << pkg.version 
                              << "(" << pkg.arch << ")\n";
                }
                std::cout << "\n";
            }

            // 2. Flatpak (si filtre ALL ou FLATPAK)
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::FLATPAK) {
                Logger::info("Récupération des applications Flatpak...");
                auto apps = FlatpakBackend::list_installed();

                std::cout << "\n--- Applications Flatpak (" << apps.size() << ") ---\n";
                for (const auto& app : apps) {
                    std::cout << " • " << std::left << std::setw(30) << app.name 
                              << " [" << std::setw(25) << app.id << "] "
                              << "v" << std::setw(12) << app.version 
                              << "(" << app.origin << ")\n";
                }
                std::cout << "\n";
            }
            if (options.filter == SourceFilter::RPM) {
                Logger::info("Récupération des paquets installés via fichier RPM local...");
                auto local_rpms = DnfBackend::list_installed_local_rpms();
                std::cout << "\n--- Paquets RPM locaux (" << local_rpms.size() << ") ---\n";
                for (const auto& pkg : local_rpms) {
                    std::cout << " • " << std::left << std::setw(30) << pkg.name 
                              << " " << std::setw(32) << pkg.version 
                              << "(" << pkg.arch << ")\n";
                }
                std::cout << "\n";
            }
            // 3. AppImage (si filtre ALL ou APPIMAGE)
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::APPIMAGE) {
                Logger::info("Récupération des applications AppImage...");
                auto appimages = AppImageBackend::list_installed();
                std::cout << "\n--- Applications AppImage (" << appimages.size() << ") ---\n";
                for (const auto& app : appimages) {
                    std::cout << " • " << std::left << std::setw(30) << app.name 
                              << " " << std::setw(20) << ("v" + app.version)
                              << "[" << app.origin << "]\n";
                }
                std::cout << "\n";
            }
            // 4. Archives / Tarball & Zip (si filtre ALL ou TAR)
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::TAR) {
                Logger::info("Récupération des applications issues d'archives (tar / zip)...");
                auto archives = ArchiveBackend::list_installed();
                std::cout << "\n--- Applications Archives / Tarball & Zip (" << archives.size() << ") ---\n";
                for (const auto& app : archives) {
                    std::cout << " • " << std::left << std::setw(30) << app.name 
                              << " " << std::setw(20) << ("v" + app.version)
                              << "[" << app.origin << "]\n";
                }
                std::cout << "\n";
            }
            break;
        }
        
        case CommandType::SEARCH: {
            if (options.arguments.empty()) {
                Logger::error("Spécifiez un terme à rechercher (ex: abpm search vlc)");
                return 1;
            }
            const auto& term = options.arguments[0];
            Logger::banner();
            Logger::info(std::string("Recherche de '") + term + "'...");
            // 1. Recherche DNF
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::DNF) {
                auto rpm_matches = DnfBackend::search(term);
                std::cout << "\n--- Dépôts DNF / RPM (" << rpm_matches.size() << " résultat(s)) ---\n";
                for (const auto& pkg : rpm_matches) {
                    std::cout << " • " << std::left << std::setw(32) << pkg.name 
                              << " " << std::setw(28) << pkg.version 
                              << "(" << pkg.arch << ")\n";
                }
            }
            // 2. Recherche Flatpak
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::FLATPAK) {
                auto flatpak_matches = FlatpakBackend::search(term);
                std::cout << "\n--- Flathub / Flatpak (" << flatpak_matches.size() << " résultat(s)) ---\n";
                for (const auto& app : flatpak_matches) {
                    std::cout << " • " << std::left << std::setw(40) << app.id 
                              << " [" << app.origin << " / " << app.version << "]\n";
                }
            }
            std::cout << "\n";
            break;
        }

        case CommandType::CHECK: {
            Logger::banner();
            Logger::info("Vérification des mises à jour disponibles...");

            size_t total_updates = 0;

            // 1. Vérification DNF
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::DNF) {
                Logger::info("Vérification des paquets DNF (RPM)...");
                auto dnf_updates = DnfBackend::check_updates();
                total_updates += dnf_updates.size();

                if (!dnf_updates.empty()) {
                    std::cout << "\n--- Mises à jour DNF (" << dnf_updates.size() << ") ---\n";
                    for (const auto& up : dnf_updates) {
                        std::cout << " • " << std::left << std::setw(30) << up.name
                                  << " " << up.current_ver << " -> " << up.new_ver
                                  << " (" << up.arch << ")\n";
                    }
                }
            }

            // 2. Vérification Flatpak
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::FLATPAK) {
                Logger::info("Vérification des applications Flatpak...");
                auto flatpak_updates = FlatpakBackend::check_updates();
                total_updates += flatpak_updates.size();

                if (!flatpak_updates.empty()) {
                    std::cout << "\n--- Mises à jour Flatpak (" << flatpak_updates.size() << ") ---\n";
                    for (const auto& up : flatpak_updates) {
                        std::cout << " • " << std::left << std::setw(32) << up.name
                                  << " [" << up.id << "] (" << up.origin << ")\n";
                    }
                }
            }

            std::cout << "\n";
            if (total_updates == 0) {
                Logger::success("Tous les paquets et applications sont à jour !");
            } else {
                Logger::info(std::string("Total : ") + std::to_string(total_updates) + 
                            " mise(s) à jour disponible(s). Tapez 'abpm update' pour les appliquer.");
            }
            break;
        }

        case CommandType::UPDATE: {
            Logger::banner();
            Logger::info("Vérification des mises à jour à appliquer...");
            std::vector<abpm::core::UpdateInfo> dnf_updates;
            std::vector<abpm::core::UpdateInfo> flatpak_updates;
            // 1. Détecter les mises à jour DNF
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::DNF) {
                dnf_updates = DnfBackend::check_updates();
            }
            // 2. Détecter les mises à jour Flatpak
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::FLATPAK) {
                flatpak_updates = FlatpakBackend::check_updates();
            }
            size_t total = dnf_updates.size() + flatpak_updates.size();
            if (total == 0) {
                Logger::success("Tous les paquets et applications sont déjà à jour !");
                break;
            }
            // Afficher le récapitulatif
            if (!dnf_updates.empty()) {
                std::cout << "\n--- Mises à jour DNF (" << dnf_updates.size() << ") ---\n";
                for (const auto& up : dnf_updates) {
                    std::cout << " • " << std::left << std::setw(30) << up.name
                              << " " << up.current_ver << " -> " << up.new_ver << "\n";
                }
            }
            if (!flatpak_updates.empty()) {
                std::cout << "\n--- Mises à jour Flatpak (" << flatpak_updates.size() << ") ---\n";
                for (const auto& up : flatpak_updates) {
                    std::cout << " • " << std::left << std::setw(32) << up.name
                              << " [" << up.id << "]\n";
                }
            }
            std::cout << "\n";
            // Demande de confirmation si l'option -y n'est pas passée
            if (!options.assumeyes) {
                if (!Logger::confirm("Voulez-vous appliquer ces " + std::to_string(total) + " mise(s) à jour ?")) {
                    Logger::info("Opération annulée par l'utilisateur.");
                    break;
                }
            } else {
                Logger::info("Option -y détectée : validation automatique.");
            }
            // Application des mises à jour
            bool dnf_ok = true;
            bool flatpak_ok = true;
            if (!dnf_updates.empty()) {
                Logger::info("\n--- Application des paquets DNF ---");
                dnf_ok = DnfBackend::update();
            }
            if (!flatpak_updates.empty()) {
                Logger::info("\n--- Application des paquets Flatpak ---");
                flatpak_ok = FlatpakBackend::update();
            }
            std::cout << "\n";
            if (dnf_ok && flatpak_ok) {
                Logger::success("Toutes les mises à jour ont été appliquées avec succès !");
            } else {
                Logger::warning("Certaines opérations de mise à jour n'ont pas pu aboutir.");
            }
            break;
        }

         case CommandType::INSTALL: {
            if (options.arguments.empty()) {
                Logger::error("Spécifiez un paquet ou une application à installer (ex: abpm install vlc)");
                return 1;
            }
            const auto& pkg_name = options.arguments[0];
            Logger::banner();
            Logger::info("Préparation de l'installation de '" + pkg_name + "'...");
            bool handled = false;
            // Cas 0 : Installation d'un fichier RPM local (--rpm)
            if (options.filter == SourceFilter::RPM) {
                Logger::info("Installation du fichier RPM local : " + pkg_name);
                DnfBackend::install_local_rpm(pkg_name, options.assumeyes);
                handled = true;
            }
            // Cas 0.5 : Installation d'une AppImage (--appimage)
            else if (options.filter == SourceFilter::APPIMAGE) {
                Logger::info("Installation du fichier AppImage : " + pkg_name);
                AppImageBackend::install(pkg_name, options.assumeyes);
                handled = true;
            }
            // Cas 0.8 : Installation d'une archive Tar / Zip (--tar)
            else if (options.filter == SourceFilter::TAR) {
                Logger::info("Installation du fichier d'archive : " + pkg_name);
                ArchiveBackend::install(pkg_name, options.assumeyes);
                handled = true;
            }
            // Cas 1 : Forcer DNF (--dnf)
            else if (options.filter == SourceFilter::DNF) {
                Logger::info("Recherche dans les dépôts DNF...");
                if (!DnfBackend::install(pkg_name, options.assumeyes)) {
                    Logger::error("Le paquet '" + pkg_name + "' est introuvable dans DNF.");
                }
                handled = true;
            }
            // Cas 2 : Forcer Flatpak (--flatpak)
            else if (options.filter == SourceFilter::FLATPAK) {
                Logger::info("Recherche sur Flathub / Flatpak...");
                if (!FlatpakBackend::install(pkg_name, options.assumeyes)) {
                    Logger::error("L'application '" + pkg_name + "' est introuvable sur Flathub.");
                }
                handled = true;
            }
            // Cas 3 : Cascade intelligente par défaut (DNF en priorité, puis Flathub)
            else {
                Logger::info("Recherche en priorité dans les dépôts DNF...");
                if (DnfBackend::install(pkg_name, options.assumeyes)) {
                    handled = true;
                } else {
                    Logger::info("Introuvable dans DNF. Recherche sur Flathub...");
                    if (FlatpakBackend::install(pkg_name, options.assumeyes)) {
                        handled = true;
                    }
                }
            }
            if (!handled) {
                Logger::error("Le paquet ou l'application '" + pkg_name + "' est introuvable ni dans DNF ni sur Flathub.");
            }
            break;
        }

        case CommandType::REMOVE: {
            if (options.arguments.empty()) {
                Logger::error("Spécifiez au moins un paquet à désinstaller (ex: abpm remove vlc gimp)");
                return 1;
            }
            Logger::banner();
            Logger::info("Analyse des paquets à désinstaller...");
            std::vector<std::string> archive_to_remove;
            std::vector<std::string> appimage_to_remove;
            std::vector<std::string> dnf_to_remove;
            std::vector<std::string> flatpak_to_remove;
            std::vector<std::string> not_found;
            for (const auto& pkg : options.arguments) {
                // 1. Vérifier si c'est une application archive (tar / zip)
                if (ArchiveBackend::is_installed(pkg)) {
                    archive_to_remove.push_back(pkg);
                    continue;
                }
                // 2. Vérifier si c'est une application AppImage
                if (AppImageBackend::is_installed(pkg)) {
                    appimage_to_remove.push_back(pkg);
                    continue;
                }
                // 3. Vérifier si c'est une application Flatpak
                std::string flatpak_ref = FlatpakBackend::find_installed_ref(pkg);
                if (!flatpak_ref.empty()) {
                    flatpak_to_remove.push_back(flatpak_ref);
                    continue;
                }
                // 4. Vérifier si c'est un paquet DNF
                if (DnfBackend::is_installed(pkg)) {
                    dnf_to_remove.push_back(pkg);
                    continue;
                }
                // 5. Introuvable
                not_found.push_back(pkg);
            }
            for (const auto& missing : not_found) {
                Logger::warning("Le paquet ou l'application '" + missing + "' n'est pas installé sur le système.");
            }
            size_t total_found = archive_to_remove.size() + appimage_to_remove.size() + dnf_to_remove.size() + flatpak_to_remove.size();
            if (total_found == 0) {
                Logger::error("Aucun des paquets spécifiés n'est actuellement installé.");
                break;
            }
            // Récapitulatif
            if (!archive_to_remove.empty()) {
                std::cout << "\n--- Applications Archives (Tar / Zip) à désinstaller (" << archive_to_remove.size() << ") ---\n";
                for (const auto& app : archive_to_remove) {
                    std::cout << " • " << app << "\n";
                }
            }
            if (!appimage_to_remove.empty()) {
                std::cout << "\n--- Applications AppImage à désinstaller (" << appimage_to_remove.size() << ") ---\n";
                for (const auto& app : appimage_to_remove) {
                    std::cout << " • " << app << "\n";
                }
            }
            if (!dnf_to_remove.empty()) {
                std::cout << "\n--- Paquets DNF à désinstaller (" << dnf_to_remove.size() << ") ---\n";
                for (const auto& p : dnf_to_remove) {
                    std::cout << " • " << p << "\n";
                }
            }
            if (!flatpak_to_remove.empty()) {
                std::cout << "\n--- Applications Flatpak à désinstaller (" << flatpak_to_remove.size() << ") ---\n";
                for (const auto& ref : flatpak_to_remove) {
                    std::cout << " • " << ref << "\n";
                }
            }
            std::cout << "\n";
            // Demande de confirmation
            if (!options.assumeyes) {
                if (!Logger::confirm("Voulez-vous procéder à la désinstallation ?")) {
                    Logger::info("Désinstallation annulée.");
                    break;
                }
            }
            // Exécution
            bool archive_ok = true;
            bool appimage_ok = true;
            bool dnf_ok = true;
            bool flatpak_ok = true;
            if (!archive_to_remove.empty()) {
                archive_ok = ArchiveBackend::remove(archive_to_remove);
            }
            if (!appimage_to_remove.empty()) {
                appimage_ok = AppImageBackend::remove(appimage_to_remove);
            }
            if (!dnf_to_remove.empty()) {
                dnf_ok = DnfBackend::remove(dnf_to_remove);
                if (dnf_ok) {
                    Logger::success("Paquets DNF désinstallés avec succès !");
                }
            }
            if (!flatpak_to_remove.empty()) {
                flatpak_ok = FlatpakBackend::remove(flatpak_to_remove);
                if (flatpak_ok) {
                    Logger::success("Applications Flatpak désinstallées avec succès !");
                }
            }
            if (dnf_ok && flatpak_ok && appimage_ok && archive_ok) {
                Logger::success("Toutes les désinstallations ont été effectuées avec succès !");
            }
            break;
        }

        default:
            Logger::info("Commande en cours de développement...");
            break;
    }

    return 0;
}