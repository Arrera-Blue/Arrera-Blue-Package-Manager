#include "abpm/core/logger.hpp"
#include "abpm/cli/cli_parser.hpp"
#include "abpm/flatpak/flatpak_backend.hpp"
#include "abpm/dnf/dnf_backend.hpp"
#include <iomanip>
#include <iostream>

using abpm::core::Logger;
using abpm::cli::CliParser;
using abpm::cli::CommandType;
using abpm::cli::SourceFilter;
using abpm::flatpak::FlatpakBackend;
using abpm::dnf::DnfBackend;

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

        default:
            Logger::info("Commande en cours de développement...");
            break;
    }

    return 0;
}