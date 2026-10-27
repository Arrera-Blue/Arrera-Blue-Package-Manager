#!/usr/bin/env bash
# ==============================================================================
# Arrera Blue Package Manager (abpm)
# Script d'installation des dépendances de développement
# ==============================================================================

set -euo pipefail

CYAN='\033[0;36m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
RESET='\033[0m'

echo -e "${CYAN}==========================================================${RESET}"
echo -e "${CYAN}  Arrera Blue Package Manager (abpm) - Dev Setup          ${RESET}"
echo -e "${CYAN}==========================================================${RESET}"

# Vérifier si l'utilisateur a les droits root ou sudo
if [ "$EUID" -ne 0 ]; then
    if ! command -v sudo &>/dev/null; then
        echo -e "${RED}[!] Erreur: Ce script nécessite des droits administrateur (sudo ou root).${RESET}" >&2
        exit 1
    fi
    SUDO="sudo"
else
    SUDO=""
fi

# Vérifier la présence de DNF
if ! command -v dnf &>/dev/null; then
    echo -e "${RED}[!] Erreur: Gestionnaire de paquets 'dnf' introuvable.${RESET}" >&2
    exit 1
fi

# Liste des paquets requis pour le développement C++ natif et le packaging RPM de abpm
DEV_PACKAGES=(
    gcc-c++
    cmake
    make
    ninja-build
    pkgconf-pkg-config
    libdnf5-devel
    flatpak-devel
    glib2-devel
    libarchive-devel
    fuse-libs
    rpm-build
    rpmdevtools
    copr-cli
)

echo -e "${YELLOW}[*] Paquets à installer :${RESET} ${DEV_PACKAGES[*]}"
echo -e "${YELLOW}[*] Lancement de l'installation via dnf...${RESET}"

$SUDO dnf install -y "${DEV_PACKAGES[@]}"

echo ""
echo -e "${GREEN}[✓] Toutes les dépendances de développement ont été installées avec succès !${RESET}"
echo -e "${GREEN}[*] Environnement prêt pour compiler abpm avec libdnf5 et libflatpak.${RESET}"
