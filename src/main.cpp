#include <iostream>
#include <flatpak.h>
#include <libdnf5/base/base.hpp>



int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "===========================================" << std::endl;
    std::cout << "  Arrera Blue Package Manager (abpm) v0.1  " << std::endl;
    std::cout << "===========================================" << std::endl;
    std::cout << "[INFO] Initialisation du système..." << std::endl;

    // Test de détection libdnf5
    libdnf5::Base dnf_base;
    std::cout << "[SUCCÈS] libdnf5 liée avec succès !" << std::endl;

    // Test de détection libflatpak
    std::cout << "[SUCCÈS] libflatpak liée avec succès (version " 
              << flatpak_get_default_arch() << " détectée) !" << std::endl;

    return 0;
}