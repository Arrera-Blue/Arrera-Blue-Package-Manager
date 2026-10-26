#include "abpm/core/logger.hpp"
#include "abpm/cli/cli_parser.hpp"

using abpm::core::Logger;
using abpm::cli::CliParser;
using abpm::cli::CommandType;
using abpm::cli::SourceFilter;

int main(int argc, char* argv[]) {
    auto options = CliParser::parse(argc, argv);

    switch (options.command) {
        case CommandType::HELP:
            CliParser::print_help();
            break;

        case CommandType::VERSION:
            CliParser::print_version();
            break;

        case CommandType::UPDATE:
            Logger::info("Commande reçue : 'update' (Mise à jour globale)");
            break;

        case CommandType::CHECK:
            Logger::info("Commande reçue : 'check' (Vérification des mises à jour)");
            break;

        case CommandType::INSTALL:
            if (options.arguments.empty()) {
                Logger::error("Argument manquant : spécifiez un paquet ou un fichier à installer.");
                return 1;
            }
            Logger::info(std::string("Commande reçue : 'install' pour ") + options.arguments[0]);
            break;

        case CommandType::REMOVE:
            if (options.arguments.empty()) {
                Logger::error("Argument manquant : spécifiez au moins un paquet à désinstaller.");
                return 1;
            }
            Logger::info("Commande reçue : 'remove'");
            for (const auto& pkg : options.arguments) {
                Logger::info(std::string(" - À désinstaller : ") + pkg);
            }
            break;

        case CommandType::SEARCH:
            if (options.arguments.empty()) {
                Logger::error("Argument manquant : spécifiez un terme de recherche.");
                return 1;
            }
            Logger::info(std::string("Commande reçue : 'search' pour '") + options.arguments[0] + "'");
            break;

        case CommandType::LIST:
            Logger::info("Commande reçue : 'list'");
            break;

        default:
            break;
    }

    return 0;
}