#include "abpm/core/logger.hpp"
#include <flatpak.h>
#include <libdnf5/base/base.hpp>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    using abpm::core::Logger;

    Logger::banner();
    Logger::info("Vérification des liaisons natives...");

    // Test libdnf5
    libdnf5::Base dnf_base;
    Logger::success("libdnf5 (C++) initialisée avec succès !");

    // Test libflatpak
    Logger::success(std::string("libflatpak (C) liée avec succès (architecture : ") + 
                   flatpak_get_default_arch() + ") !");

    Logger::warning("Exemple de message d'avertissement.");
    Logger::error("Exemple de message d'erreur.");

    return 0;
}