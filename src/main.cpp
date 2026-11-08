#include "abpm/core/logger.hpp"
#include "abpm/core/progress_emitter.hpp"
#include "abpm/core/categorizer.hpp"
#include "abpm/cli/cli_parser.hpp"
#include "abpm/flatpak/flatpak_backend.hpp"
#include "abpm/dnf/dnf_backend.hpp"
#include "abpm/appimage/appimage_backend.hpp"
#include "abpm/archive/archive_backend.hpp"
#include "abpm/discover/discover_backend.hpp"
#include "abpm/list/list_backend.hpp"
#include "abpm/repo/repo_backend.hpp"
#include "abpm/config/config_backend.hpp"
#include <iomanip>
#include <iostream>
#include <sstream> 
#include <unistd.h>

using abpm::core::Logger;
using abpm::core::ProgressEmitter;
using abpm::cli::CliParser;
using abpm::cli::CommandType;
using abpm::cli::SourceFilter;
using abpm::cli::ScopeFilter;
using abpm::flatpak::FlatpakBackend;
using abpm::dnf::DnfBackend;
using abpm::appimage::AppImageBackend;
using abpm::archive::ArchiveBackend;
using abpm::discover::DiscoverBackend;
using abpm::list::ListBackend;
using abpm::repo::RepoBackend;
using abpm::config::ConfigBackend;
using abpm::core::Categorizer;

int main(int argc, char* argv[]) {
    auto options = CliParser::parse(argc, argv);
    if (options.progress) {
        ProgressEmitter::set_enabled(true);
    }

    switch (options.command) {
        case CommandType::HELP:
            CliParser::print_help();
            break;

        case CommandType::VERSION:
            CliParser::print_version();
            break;

        case CommandType::LIST: {
            bool apps_mode = options.list_apps_only || options.json_output;

            // 1. Sortie JSON brute pour Blue Hub (aucune bannière, aucun texte superflu)
            if (options.json_output) {
                auto apps = ListBackend::get_installed_apps(options.filter, options.scope);
                std::cout << ListBackend::to_json(apps);
                break;
            }

            Logger::banner();

            // 2. Mode --apps dans le terminal : affichage épuré des applications uniquement
            if (apps_mode) {
                Logger::info("Récupération des applications installées...");
                auto apps = ListBackend::get_installed_apps(options.filter, options.scope);
                ListBackend::print_terminal_apps(apps);
                break;
            }

            // 3. Mode standard rétrocompatible (listing exhaustif des paquets DNF système)
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
            std::vector<abpm::core::UpdateInfo> dnf_updates;
            std::vector<abpm::core::UpdateInfo> flatpak_updates;

            // En mode terminal classique, on affiche la bannière
            if (!options.json_output) {
                Logger::banner();
                Logger::info("Vérification des mises à jour disponibles...");
            }

            // 1. Vérification DNF
            if (options.filter == SourceFilter::ALL || options.filter == SourceFilter::DNF) {
                if (!options.json_output) {
                    Logger::info("Vérification des paquets DNF (RPM)...");
                }
                dnf_updates = DnfBackend::check_updates(options.json_output);

                if (!options.json_output && !dnf_updates.empty()) {
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
                if (!options.json_output) {
                    Logger::info("Vérification des applications Flatpak...");
                }
                flatpak_updates = FlatpakBackend::check_updates();

                if (!options.json_output && !flatpak_updates.empty()) {
                    std::cout << "\n--- Mises à jour Flatpak (" << flatpak_updates.size() << ") ---\n";
                    for (const auto& up : flatpak_updates) {
                        std::cout << " • " << std::left << std::setw(32) << up.name
                                  << " [" << up.id << "] (" << up.origin << ")\n";
                    }
                }
            }

            // 3. Sortie JSON pour Blue Hub
            if (options.json_output) {
                std::vector<abpm::core::UpdateInfo> all_updates;
                all_updates.insert(all_updates.end(), dnf_updates.begin(), dnf_updates.end());
                all_updates.insert(all_updates.end(), flatpak_updates.begin(), flatpak_updates.end());

                std::stringstream ss;
                ss << "[\n";
                for (size_t i = 0; i < all_updates.size(); ++i) {
                    const auto& up = all_updates[i];
                    std::string src_str = (up.source == abpm::core::PackageSource::DNF) ? "dnf" : "flatpak";

                    ss << "  {\n";
                    ss << "    \"id\": \"" << Categorizer::escape_json(up.id) << "\",\n";
                    ss << "    \"name\": \"" << Categorizer::escape_json(up.name) << "\",\n";
                    ss << "    \"category\": \"" << Categorizer::escape_json(up.category) << "\",\n";
                    ss << "    \"current_version\": \"" << Categorizer::escape_json(up.current_ver) << "\",\n";
                    ss << "    \"new_version\": \"" << Categorizer::escape_json(up.new_ver) << "\",\n";
                    ss << "    \"source\": \"" << src_str << "\",\n";
                    ss << "    \"package\": \"" << Categorizer::escape_json(up.package) << "\",\n";
                    ss << "    \"icon\": \"" << Categorizer::escape_json(up.icon) << "\",\n";
                    ss << "    \"summary\": \"" << Categorizer::escape_json(up.summary) << "\"\n";
                    ss << "  }" << (i + 1 < all_updates.size() ? ",\n" : "\n");
                }
                ss << "]\n";
                std::cout << ss.str() << std::flush;
                break;
            }

            // 4. Bilan en mode terminal classique
            size_t total_updates = dnf_updates.size() + flatpak_updates.size();
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
            if (options.progress) {
                ProgressEmitter::set_enabled(true);
            }

            const auto& targets = options.arguments;

            // 1. Cas ciblé DNF (--dnf)
            if (options.filter == SourceFilter::DNF) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::banner();
                    Logger::info("Mise à jour DNF en cours...");
                }
                bool ok = DnfBackend::update(targets);
                if (!ok) return 1;
                break;
            }

            // 2. Cas ciblé Flatpak (--flatpak)
            if (options.filter == SourceFilter::FLATPAK) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::banner();
                    Logger::info("Mise à jour Flatpak en cours...");
                }
                bool ok = FlatpakBackend::update(targets);
                if (!ok) return 1;
                break;
            }

            // 3. Cas paquets spécifiés sans filtre explicite
            if (!targets.empty()) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::banner();
                    Logger::info("Application des mises à jour spécifiées...");
                }

                std::vector<std::string> dnf_targets;
                std::vector<std::string> flatpak_targets;

                for (const auto& t : targets) {
                    if (t.find('.') != std::string::npos || t.find('/') != std::string::npos) {
                        flatpak_targets.push_back(t);
                    } else {
                        dnf_targets.push_back(t);
                    }
                }

                bool all_ok = true;
                if (!dnf_targets.empty()) {
                    all_ok = DnfBackend::update(dnf_targets) && all_ok;
                }
                if (!flatpak_targets.empty()) {
                    all_ok = FlatpakBackend::update(flatpak_targets) && all_ok;
                }
                if (!all_ok) return 1;
                break;
            }

            // 4. Cas mise à jour globale (aucun paquet ciblé)
            if (!ProgressEmitter::is_enabled()) {
                Logger::banner();
                Logger::info("Vérification des mises à jour à appliquer...");
            }
            std::vector<abpm::core::UpdateInfo> dnf_updates;
            std::vector<abpm::core::UpdateInfo> flatpak_updates;
            dnf_updates = DnfBackend::check_updates(true);
            flatpak_updates = FlatpakBackend::check_updates();

            size_t total = dnf_updates.size() + flatpak_updates.size();
            if (total == 0) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::success("Tous les paquets et applications sont déjà à jour !");
                }
                ProgressEmitter::emit(100, "Tous les paquets et applications sont déjà à jour");
                break;
            }

            if (!ProgressEmitter::is_enabled()) {
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
            }

            bool dnf_ok = true;
            bool flatpak_ok = true;
            if (!dnf_updates.empty()) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::info("\n--- Application des paquets DNF ---");
                }
                dnf_ok = DnfBackend::update();
            }
            if (!flatpak_updates.empty()) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::info("\n--- Application des paquets Flatpak ---");
                }
                flatpak_ok = FlatpakBackend::update();
            }

            if (!ProgressEmitter::is_enabled()) {
                std::cout << "\n";
                if (dnf_ok && flatpak_ok) {
                    Logger::success("Toutes les mises à jour ont été appliquées avec succès !");
                } else {
                    Logger::warning("Certaines opérations de mise à jour n'ont pas pu aboutir.");
                }
            }
            if (!dnf_ok || !flatpak_ok) {
                return 1;
            }
            break;
        }

         case CommandType::INSTALL: {
            if (options.arguments.empty()) {
                if (ProgressEmitter::is_enabled()) {
                    ProgressEmitter::emit_error("Aucun paquet spécifié pour l'installation.");
                } else {
                    Logger::error("Spécifiez un paquet ou une application à installer (ex: abpm install vlc)");
                }
                return 1;
            }
            const auto& pkg_name = options.arguments[0];
            if (!ProgressEmitter::is_enabled()) {
                Logger::banner();
                Logger::info("Préparation de l'installation de '" + pkg_name + "'...");
            } else {
                ProgressEmitter::emit(0, "Démarrage de l'installation de " + pkg_name + "...");
            }
            bool handled = false;
            // Cas 0 : Installation d'un fichier RPM local (--rpm)
            if (options.filter == SourceFilter::RPM) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::info("Installation du fichier RPM local : " + pkg_name);
                }
                if (!DnfBackend::install_local_rpm(pkg_name, options.assumeyes)) {
                    return 1;
                }
                handled = true;
            }
            // Cas 0.5 : Installation d'une AppImage (--appimage)
            else if (options.filter == SourceFilter::APPIMAGE) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::info("Installation du fichier AppImage : " + pkg_name);
                }
                if (!AppImageBackend::install(pkg_name, options.assumeyes, options.custom_icon, options.scope)) {
                    return 1;
                }
                handled = true;
            }
            // Cas 0.8 : Installation d'une archive Tar / Zip (--tar)
            else if (options.filter == SourceFilter::TAR) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::info("Installation du fichier d'archive : " + pkg_name);
                }
                if (!ArchiveBackend::install(pkg_name, options.assumeyes, options.custom_icon, options.scope)) {
                    return 1;
                }
                handled = true;
            }
            // Cas 1 : Forcer DNF (--dnf)
            else if (options.filter == SourceFilter::DNF) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::info("Recherche dans les dépôts DNF...");
                }
                if (DnfBackend::install(pkg_name, options.assumeyes)) {
                    handled = true;
                } else {
                    std::string err_msg = "Le paquet '" + pkg_name + "' est introuvable dans DNF.";
                    ProgressEmitter::emit_error(err_msg);
                    if (!ProgressEmitter::is_enabled()) {
                        Logger::error(err_msg);
                    }
                    return 1;
                }
            }
            // Cas 2 : Forcer Flatpak (--flatpak)
            else if (options.filter == SourceFilter::FLATPAK) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::info("Recherche sur Flathub / Flatpak...");
                }
                if (FlatpakBackend::install(pkg_name, options.assumeyes)) {
                    handled = true;
                } else {
                    std::string err_msg = "L'application '" + pkg_name + "' est introuvable sur Flathub.";
                    ProgressEmitter::emit_error(err_msg);
                    if (!ProgressEmitter::is_enabled()) {
                        Logger::error(err_msg);
                    }
                    return 1;
                }
            }
            // Cas 3 : Cascade intelligente par défaut (DNF en priorité, puis Flathub)
            else {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::info("Recherche en priorité dans les dépôts DNF...");
                }
                if (DnfBackend::install(pkg_name, options.assumeyes)) {
                    handled = true;
                } else {
                    if (!ProgressEmitter::is_enabled()) {
                        Logger::info("Introuvable dans DNF. Recherche sur Flathub...");
                    }
                    if (FlatpakBackend::install(pkg_name, options.assumeyes)) {
                        handled = true;
                    }
                }
            }
            if (!handled) {
                std::string err_msg = "Le paquet ou l'application '" + pkg_name + "' est introuvable ni dans DNF ni sur Flathub.";
                ProgressEmitter::emit_error(err_msg);
                if (!ProgressEmitter::is_enabled()) {
                    Logger::error(err_msg);
                }
                return 1;
            }
            break;
        }

        case CommandType::REMOVE: {
            if (options.progress) {
                ProgressEmitter::set_enabled(true);
            }

            if (options.arguments.empty()) {
                std::string msg = "Spécifiez au moins un paquet à désinstaller (ex: abpm remove vlc gimp)";
                ProgressEmitter::emit_error(msg);
                if (!ProgressEmitter::is_enabled()) {
                    Logger::error(msg);
                }
                return 1;
            }

            if (options.scope == ScopeFilter::SYSTEM && geteuid() != 0) {
                std::string msg = "L'option --system requiert les privilèges administrateur (sudo / pkexec).";
                ProgressEmitter::emit_error(msg);
                if (!ProgressEmitter::is_enabled()) {
                    Logger::warning(msg);
                    Logger::info("Relancez avec sudo : 'sudo abpm remove --system ...'");
                }
                return 1;
            }

            if (!ProgressEmitter::is_enabled()) {
                Logger::banner();
                Logger::info("Analyse des paquets à désinstaller...");
            }

            std::vector<std::string> archive_to_remove;
            std::vector<std::string> appimage_to_remove;
            std::vector<std::string> dnf_to_remove;
            std::vector<std::string> flatpak_to_remove;
            std::vector<std::string> not_found;

            bool has_error = false;

            for (const auto& pkg : options.arguments) {
                // 1. Vérifier si c'est une application archive (tar / zip)
                if (ArchiveBackend::is_installed(pkg, options.scope)) {
                    if (ArchiveBackend::is_system(pkg) && geteuid() != 0) {
                        std::string msg = "L'application '" + pkg + "' est installée au niveau système. Privilèges root requis.";
                        ProgressEmitter::emit_error(msg);
                        if (!ProgressEmitter::is_enabled()) {
                            Logger::warning("L'application '" + pkg + "' est installée au niveau système.");
                            Logger::info("Relancez avec sudo : 'sudo abpm remove " + pkg + "'");
                        }
                        has_error = true;
                        continue;
                    }
                    archive_to_remove.push_back(pkg);
                    continue;
                }

                // 2. Vérifier si c'est une application AppImage
                if (AppImageBackend::is_installed(pkg, options.scope)) {
                    if (AppImageBackend::is_system(pkg) && geteuid() != 0) {
                        std::string msg = "L'application '" + pkg + "' est installée au niveau système. Privilèges root requis.";
                        ProgressEmitter::emit_error(msg);
                        if (!ProgressEmitter::is_enabled()) {
                            Logger::warning("L'application '" + pkg + "' est installée au niveau système.");
                            Logger::info("Relancez avec sudo : 'sudo abpm remove " + pkg + "'");
                        }
                        has_error = true;
                        continue;
                    }
                    appimage_to_remove.push_back(pkg);
                    continue;
                }

                // 3. Vérifier si c'est une application Flatpak
                std::string flatpak_ref = FlatpakBackend::find_installed_ref(pkg, options.scope);
                if (!flatpak_ref.empty()) {
                    flatpak_to_remove.push_back(flatpak_ref);
                    continue;
                }

                // 4. Vérifier si c'est un paquet DNF (exclu si scope == USER)
                if (options.scope != ScopeFilter::USER && DnfBackend::is_installed(pkg)) {
                    if (geteuid() != 0) {
                        std::string msg = "Le paquet '" + pkg + "' est un composant système DNF. Privilèges root requis.";
                        ProgressEmitter::emit_error(msg);
                        if (!ProgressEmitter::is_enabled()) {
                            Logger::warning("Le paquet '" + pkg + "' est installé au niveau système DNF.");
                            Logger::info("Relancez avec sudo : 'sudo abpm remove " + pkg + "'");
                        }
                        has_error = true;
                        continue;
                    }
                    dnf_to_remove.push_back(pkg);
                    continue;
                }

                // 5. Introuvable
                not_found.push_back(pkg);
            }

            for (const auto& missing : not_found) {
                std::string msg = "Le paquet ou l'application '" + missing + "' n'est actuellement pas installé.";
                ProgressEmitter::emit_error(msg);
                if (!ProgressEmitter::is_enabled()) {
                    Logger::warning(msg);
                }
                has_error = true;
            }

            size_t total_found = archive_to_remove.size() + appimage_to_remove.size() + dnf_to_remove.size() + flatpak_to_remove.size();
            if (total_found == 0) {
                if (!ProgressEmitter::is_enabled()) {
                    Logger::error("Aucun des paquets spécifiés n'est actuellement installé ou accessible avec les privilèges actuels.");
                }
                return 1;
            }

            // Récapitulatif
            if (!ProgressEmitter::is_enabled()) {
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
            }

            // Demande de confirmation
            if (!options.assumeyes) {
                if (!Logger::confirm("Voulez-vous procéder à la désinstallation ?")) {
                    Logger::info("Désinstallation annulée.");
                    return 0;
                }
            }

            // Exécution
            bool archive_ok = true;
            bool appimage_ok = true;
            bool dnf_ok = true;
            bool flatpak_ok = true;

            if (!archive_to_remove.empty()) {
                archive_ok = ArchiveBackend::remove(archive_to_remove, options.scope);
            }
            if (!appimage_to_remove.empty()) {
                appimage_ok = AppImageBackend::remove(appimage_to_remove, options.scope);
            }
            if (!dnf_to_remove.empty()) {
                dnf_ok = DnfBackend::remove(dnf_to_remove);
                if (dnf_ok && !ProgressEmitter::is_enabled()) {
                    Logger::success("Paquets DNF désinstallés avec succès !");
                }
            }
            if (!flatpak_to_remove.empty()) {
                flatpak_ok = FlatpakBackend::remove(flatpak_to_remove);
                if (flatpak_ok && !ProgressEmitter::is_enabled()) {
                    Logger::success("Applications Flatpak désinstallées avec succès !");
                }
            }

            if (has_error || !archive_ok || !appimage_ok || !dnf_ok || !flatpak_ok) {
                return 1;
            }

            if (!ProgressEmitter::is_enabled()) {
                Logger::success("Toutes les désinstallations ont été effectuées avec succès !");
            }
            return 0;
        }

        case CommandType::SET_ICON: {
            if (options.arguments.size() < 2) {
                Logger::error("Utilisation : abpm set-icon <nom_application> <chemin_nouvelle_icone>");
                return 1;
            }
            const auto& app_name = options.arguments[0];
            const auto& icon_path = options.arguments[1];

            Logger::banner();
            Logger::info("Mise à jour de l'icône pour '" + app_name + "'...");

            bool updated = false;
            if (AppImageBackend::is_installed(app_name)) {
                updated = AppImageBackend::set_icon(app_name, icon_path);
            } else if (ArchiveBackend::is_installed(app_name)) {
                updated = ArchiveBackend::set_icon(app_name, icon_path);
            } else {
                Logger::error("Application '" + app_name + "' introuvable parmi les AppImages et Archives installées.");
                return 1;
            }

            if (!updated) {
                return 1;
            }
            break;
        }

                case CommandType::DISCOVER: {
            auto apps = DiscoverBackend::discover(options.filter, options.discover_page, options.discover_per_page);

            if (options.json_output) {
                // Sortie JSON brute sur stdout (pour Blue Hub)
                std::cout << DiscoverBackend::to_json(apps);
            } else {
                Logger::banner();
                std::string filter_str = "Toutes sources (DNF + Flatpak)";
                if (options.filter == SourceFilter::DNF) filter_str = "DNF uniquement";
                else if (options.filter == SourceFilter::FLATPAK) filter_str = "Flathub / Flatpak uniquement";

                std::cout << "Applications populaires & tendances (" << filter_str 
                          << " - Page " << options.discover_page << ", " << options.discover_per_page << " par page) :\n\n";

                if (apps.empty()) {
                    Logger::warning("Aucune application trouvée pour ces critères.");
                    break;
                }

                for (const auto& app : apps) {
                    std::cout << " • " << "\033[1m" << app.name << "\033[0m"
                              << " [" << (app.source == "dnf" ? "\033[1;36mdnf" : "\033[1;34mflatpak")
                              << "\033[0m : " << app.package << "]\n";
                    if (!app.summary.empty()) {
                        std::cout << "   " << app.summary << "\n";
                    }
                }

                std::cout << "\nPour installer une application : abpm install <paquet>\n";
                std::string extra_flag = (options.filter == SourceFilter::DNF ? " --dnf" : (options.filter == SourceFilter::FLATPAK ? " --flatpak" : ""));
                std::cout << "Page suivante                  : abpm discover" << extra_flag 
                          << " --page " << (options.discover_page + 1) << " --per-page " << options.discover_per_page << "\n\n";
            }
            break;
        }

        case CommandType::CLEAN: {
            Logger::banner();
            DnfBackend::CleanTarget target = DnfBackend::CleanTarget::ALL;
            if (options.clean_target == "expire-cache" || options.clean_target == "expire") {
                target = DnfBackend::CleanTarget::EXPIRE_CACHE;
            } else if (options.clean_target == "packages" || options.clean_target == "pkgs") {
                target = DnfBackend::CleanTarget::PACKAGES;
            } else if (options.clean_target == "all" || options.clean_target.empty()) {
                target = DnfBackend::CleanTarget::ALL;
            } else {
                Logger::error("Cible de nettoyage inconnue : '" + options.clean_target + "'. Utilisez 'all', 'expire-cache' ou 'packages'.");
                return 1;
            }
            DnfBackend::clean(target);
            break;
        }

        case CommandType::REPO: {
            if (options.arguments.empty()) {
                Logger::error("Sous-commande repo manquante : 'list', 'enable', 'disable', ou 'add'.");
                return 1;
            }

            const auto& subcmd = options.arguments[0];

            if (subcmd == "list") {
                auto repos = RepoBackend::list_repos();
                if (options.json_output) {
                    std::cout << RepoBackend::to_json(repos);
                } else {
                    Logger::banner();
                    RepoBackend::print_terminal_repos(repos);
                }
                break;
            } else if (subcmd == "enable") {
                if (options.arguments.size() < 2) {
                    Logger::error("Utilisation : abpm repo enable <id_depot>");
                    return 1;
                }
                if (!RepoBackend::enable_repo(options.arguments[1])) return 1;
                break;
            } else if (subcmd == "disable") {
                if (options.arguments.size() < 2) {
                    Logger::error("Utilisation : abpm repo disable <id_depot>");
                    return 1;
                }
                if (!RepoBackend::disable_repo(options.arguments[1])) return 1;
                break;
            } else if (subcmd == "add") {
                if (options.arguments.size() < 2) {
                    Logger::error("Utilisation : abpm repo add <url_ou_fichier>");
                    return 1;
                }
                if (!RepoBackend::add_repo(options.arguments[1])) return 1;
                break;
            } else {
                Logger::error("Sous-commande repo inconnue : '" + subcmd + "'. Utilisez list, enable, disable ou add.");
                return 1;
            }
        }

        case CommandType::CONFIG: {
            if (options.arguments.empty()) {
                Logger::error("Sous-commande config manquante : 'get' ou 'set'.");
                return 1;
            }

            const auto& subcmd = options.arguments[0];

            if (subcmd == "get") {
                if (options.arguments.size() < 2) {
                    Logger::error("Utilisation : abpm config get <cle>");
                    return 1;
                }
                std::string val = ConfigBackend::get(options.arguments[1]);
                std::cout << val << "\n";
                break;
            } else if (subcmd == "set") {
                if (options.arguments.size() < 3) {
                    Logger::error("Utilisation : abpm config set <cle> <valeur>");
                    return 1;
                }
                if (!ConfigBackend::set(options.arguments[1], options.arguments[2])) return 1;
                break;
            } else {
                Logger::error("Sous-commande config inconnue : '" + subcmd + "'. Utilisez get ou set.");
                return 1;
            }
        }

        default:
            Logger::info("Commande en cours de développement...");
            break;
    }

    return 0;
}