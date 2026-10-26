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

        default:
            Logger::info("Commande en cours de développement...");
            break;
    }

    return 0;
}