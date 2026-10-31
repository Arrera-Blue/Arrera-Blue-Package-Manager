#include "abpm/cli/cli_parser.hpp"
#include "abpm/core/logger.hpp"
#include <iostream>

namespace abpm::cli {

using core::Logger;

void CliParser::print_version() {
    Logger::info("Arrera Blue Package Manager (abpm) version 2026.1.2");
    std::cout << "  Cœur système : libdnf5 (C++)\n"
              << "  Cœur applications : libflatpak (C/GObject)\n";
}

void CliParser::print_help() {
    Logger::banner();
    std::cout << "UTILISATION:\n"
              << "  abpm <COMMANDE> [OPTIONS] [ARGUMENTS...]\n\n"
              << "COMMANDES PRINCIPALES:\n"
              << "  update                  Met a jour les depots et effectue toutes les mises a jour\n"
              << "  check                   Affiche la liste des mises a jour disponibles\n"
              << "  install [OPTIONS] <PKG> Installe un paquet (priorite DNF puis Flathub)\n"
              << "  remove <PKG...>         Desinstalle un ou plusieurs paquets\n"
              << "  search [OPTIONS] <PKG>  Recherche un paquet disponible\n"
              << "  list [OPTIONS]          Liste les paquets installes\n"
              << "  set-icon <PKG> <IMAGE>  Modifie l'icone d'une application installee\n\n"
              << "  discover [OPTIONS]      Decouvrir les applications populaires / tendances\n\n"
              << "OPTIONS DE LISTAGE:\n"
              << "  --apps                  Lister uniquement les vraies applications (lanceurs .desktop)\n"
              << "  --json                  Sortie brute au format JSON pour Blue Hub\n\n"
              << "OPTIONS DE SOURCE / FORMAT:\n"
              << "  --dnf                   Cibler uniquement DNF / RPM\n"
              << "  --flatpak               Cibler uniquement Flatpak / Flathub\n"
              << "  --rpm <FICHIER>         Installer / lister des paquets RPM locaux\n"
              << "  --appimage <FICHIER>    Installer / lister des applications AppImage\n"
              << "  --tar <FICHIER>         Installer / lister des applications en archives (tar, zip)\n"
              << "  --icon <FICHIER>        Definir l'icone a utiliser lors de l'installation\n\n"
              << "OPTIONS GENERALES:\n"
              << "  --page <N>              Numero de la page a charger (defaut: 1)\n"
              << "  --per-page <N>          Nombre d'applications par page (defaut: 6)\n"
              << "  --json                  Sortie brute au format JSON pour Blue Hub\n"
              << "  -y, --yes               Repondre oui a toutes les confirmations\n"
              << "  -h, --help              Affiche cette aide\n"
              << "  -v, --version           Affiche la version de abpm\n";
}

CliOptions CliParser::parse(int argc, char* argv[]) {
    CliOptions options;

    if (argc < 2) {
        options.command = CommandType::HELP;
        return options;
    }

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    std::string_view first_arg = args[0];

    // Options globales directes
    if (first_arg == "-h" || first_arg == "--help") {
        options.command = CommandType::HELP;
        return options;
    }
    if (first_arg == "-v" || first_arg == "--version") {
        options.command = CommandType::VERSION;
        return options;
    }

    // Commandes
    if (first_arg == "update") {
        options.command = CommandType::UPDATE;
    } else if (first_arg == "check") {
        options.command = CommandType::CHECK;
    } else if (first_arg == "install") {
        options.command = CommandType::INSTALL;
    } else if (first_arg == "remove") {
        options.command = CommandType::REMOVE;
    } else if (first_arg == "search") {
        options.command = CommandType::SEARCH;
    } else if (first_arg == "list") {
        options.command = CommandType::LIST;
    } else if (first_arg == "set-icon" || first_arg == "icon") {
        options.command = CommandType::SET_ICON;
    } else if (first_arg == "discover") {
        options.command = CommandType::DISCOVER;
    } else {
        Logger::error(std::string("Commande inconnue : '") + std::string(first_arg) + "'");
        options.command = CommandType::HELP;
        return options;
    }

    // Traitement des drapeaux et arguments suivants
    for (size_t i = 1; i < args.size(); ++i) {
        const auto& arg = args[i];

        if (arg == "--dnf") {
            options.filter = SourceFilter::DNF;
        } else if (arg == "--flatpak") {
            options.filter = SourceFilter::FLATPAK;
        } else if (arg == "--rpm") {
            options.filter = SourceFilter::RPM;
        } else if (arg == "--appimage") {
            options.filter = SourceFilter::APPIMAGE;
        } else if (arg == "--tar") {
            options.filter = SourceFilter::TAR;
        } else if (arg.starts_with("--icon=")) {
            options.custom_icon = arg.substr(7);
        } else if (arg == "--icon") {
            if (i + 1 < args.size()) {
                options.custom_icon = args[++i];
            } else {
                Logger::error("L'option --icon requiert un chemin de fichier.");
            }
        } else if (arg == "--json") {
            options.json_output = true;
        } else if (arg == "--apps") {
            options.list_apps_only = true;
        } else if (arg.starts_with("--page=")) {
            try { options.discover_page = std::max(1, std::stoi(arg.substr(7))); } catch (...) {}
        } else if (arg == "--page") {
            if (i + 1 < args.size()) {
                try { options.discover_page = std::max(1, std::stoi(args[++i])); } catch (...) {}
            }
        } else if (arg.starts_with("--per-page=")) {
            try { options.discover_per_page = std::max(1, std::stoi(arg.substr(11))); } catch (...) {}
        } else if (arg == "--per-page") {
            if (i + 1 < args.size()) {
                try { options.discover_per_page = std::max(1, std::stoi(args[++i])); } catch (...) {}
            }
        } else if (arg == "-y" || arg == "--yes" || arg == "--assumeyes") {
            options.assumeyes = true;
        } else if (arg == "-h" || arg == "--help") {
            options.command = CommandType::HELP;
            return options;
        } else {
            // C'est un argument de paquet ou de fichier
            options.arguments.push_back(arg);
        }
    }

    return options;
}

} // namespace abpm::cli