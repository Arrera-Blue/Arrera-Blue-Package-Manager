cmake_minimum_required(VERSION 3.20)

project(abpm
    VERSION 0.1.0
    DESCRIPTION "Arrera Blue Package Manager"
    LANGUAGES CXX
)

# Standard C++20 requis
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Avertissements stricts du compilateur pour un code propre
add_compile_options(-Wall -Wextra -pedantic)

# Recherche des dépendances via pkg-config
find_package(PkgConfig REQUIRED)

# Détection de libdnf5, libflatpak et libarchive
pkg_check_modules(LIBDNF5 REQUIRED IMPORTED_TARGET libdnf5)
pkg_check_modules(FLATPAK REQUIRED IMPORTED_TARGET flatpak)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)

# Définition de l'exécutable abpm
add_executable(abpm
    src/main.cpp
    src/core/logger.cpp
    src/cli/cli_parser.cpp
    src/flatpak/flatpak_backend.cpp
    src/dnf/dnf_backend.cpp
    src/appimage/appimage_backend.cpp
    src/archive/archive_backend.cpp
)

# Dossier des en-têtes (#include <abpm/...>)
target_include_directories(abpm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Liaison des bibliothèques C++ et C
target_link_libraries(abpm PRIVATE
    PkgConfig::LIBDNF5
    PkgConfig::FLATPAK
    PkgConfig::GLIB2
    PkgConfig::LIBARCHIVE
)