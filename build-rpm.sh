#!/usr/bin/env bash
# ==============================================================================
# Arrera Blue Package Manager (abpm)
# Script d'automatisation de génération du RPM et SRPM pour Copr
# ==============================================================================

set -euo pipefail

# Couleurs Arrera
CYAN='\033[0;36m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
RESET='\033[0m'
BOLD='\033[1m'

echo -e "${CYAN}${BOLD}==========================================================${RESET}"
echo -e "${CYAN}${BOLD}  Arrera Blue Package Manager (abpm) - RPM Builder        ${RESET}"
echo -e "${CYAN}${BOLD}==========================================================${RESET}"

# Répertoire du projet
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SPEC_FILE="abpm.spec"
if [[ ! -f "$SPEC_FILE" ]]; then
    echo -e "${RED}[!] Erreur : Fichier $SPEC_FILE introuvable dans $SCRIPT_DIR.${RESET}" >&2
    exit 1
fi

# Extraction dynamique du nom et de la version depuis le fichier .spec
PKG_NAME=$(grep -E '^Name:' "$SPEC_FILE" | awk '{print $2}')
PKG_VERSION=$(grep -E '^Version:' "$SPEC_FILE" | awk '{print $2}')
PKG_RELEASE=$(grep -E '^Release:' "$SPEC_FILE" | awk '{print $2}' | sed 's/%{?dist}//')

echo -e "${YELLOW}[*] Paquet  :${RESET} ${PKG_NAME}"
echo -e "${YELLOW}[*] Version :${RESET} ${PKG_VERSION}"

# Vérifier la présence des outils de build RPM
MISSING_TOOLS=()
for tool in rpmbuild rpmdev-setuptree tar; do
    if ! command -v "$tool" &>/dev/null; then
        MISSING_TOOLS+=("$tool")
    fi
done

if [[ ${#MISSING_TOOLS[@]} -gt 0 ]]; then
    echo -e "${YELLOW}[!] Outils manquants détectés : ${MISSING_TOOLS[*]}${RESET}"
    echo -e "${YELLOW}[*] Tentative d'installation des paquets requis (rpm-build, rpmdevtools, copr-cli)...${RESET}"
    if command -v sudo &>/dev/null; then
        sudo dnf install -y rpm-build rpmdevtools copr-cli
    else
        echo -e "${RED}[!] Veuillez installer manuellement : sudo dnf install -y rpm-build rpmdevtools copr-cli${RESET}" >&2
        exit 1
    fi
fi

# Préparation de l'arborescence rpmbuild
echo -e "${YELLOW}[*] Préparation de l'arborescence ~/rpmbuild...${RESET}"
rpmdev-setuptree || mkdir -p ~/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

RPM_SOURCES="$HOME/rpmbuild/SOURCES"
RPM_SPECS="$HOME/rpmbuild/SPECS"
TARBALL="${PKG_NAME}-${PKG_VERSION}.tar.gz"

# Création de l'archive tarball source
echo -e "${YELLOW}[*] Création de l'archive source ${TARBALL}...${RESET}"
if git rev-parse --is-inside-work-tree &>/dev/null; then
    # Archive propre depuis Git
    git archive --format=tar.gz --prefix="${PKG_NAME}-${PKG_VERSION}/" -o "${RPM_SOURCES}/${TARBALL}" HEAD
else
    # Fallback tar standard
    tar --exclude-vcs --transform "s,^,${PKG_NAME}-${PKG_VERSION}/," -czf "${RPM_SOURCES}/${TARBALL}" .
fi

# Copie du specfile
cp "$SPEC_FILE" "${RPM_SPECS}/"

# Parsing des options de ligne de commande
BUILD_MODE="all"
COPR_PROJECT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --srpm-only)
            BUILD_MODE="srpm"
            shift
            ;;
        --copr)
            if [[ $# -ge 2 ]]; then
                COPR_PROJECT="$2"
                shift 2
            else
                echo -e "${RED}[!] Erreur : --copr nécessite <utilisateur/projet>${RESET}" >&2
                exit 1
            fi
            ;;
        *)
            shift
            ;;
    esac
done

# Exécution du build RPM / SRPM
if [[ "$BUILD_MODE" == "srpm" ]]; then
    echo -e "${YELLOW}[*] Génération du paquet source (SRPM)...${RESET}"
    rpmbuild -bs "${RPM_SPECS}/${SPEC_FILE}"
else
    echo -e "${YELLOW}[*] Compilation complète (SRPM + binaire RPM)...${RESET}"
    rpmbuild -ba "${RPM_SPECS}/${SPEC_FILE}"
fi

# Récupération des chemins des fichiers produits
SRPM_FILE=$(find "$HOME/rpmbuild/SRPMS" -name "${PKG_NAME}-${PKG_VERSION}*.src.rpm" | sort -r | head -n1)

echo ""
echo -e "${GREEN}${BOLD}[✓] Build terminé avec succès !${RESET}"
if [[ -n "$SRPM_FILE" && -f "$SRPM_FILE" ]]; then
    echo -e " • ${CYAN}Paquet source (SRPM) :${RESET} ${SRPM_FILE}"
fi

if [[ "$BUILD_MODE" == "all" ]]; then
    BINARY_RPMS=$(find "$HOME/rpmbuild/RPMS" -name "${PKG_NAME}-${PKG_VERSION}*.rpm" 2>/dev/null || true)
    if [[ -n "$BINARY_RPMS" ]]; then
        echo -e " • ${CYAN}Paquet binaire (RPM)  :${RESET}"
        for rpm in $BINARY_RPMS; do
            echo -e "   -> $rpm"
        done
    fi
fi

# Téléversement optionnel vers Copr si demandé
if [[ -n "$COPR_PROJECT" ]]; then
    echo ""
    echo -e "${YELLOW}[*] Envoi du SRPM vers Fedora Copr (${COPR_PROJECT})...${RESET}"
    if command -v copr-cli &>/dev/null; then
        copr-cli build "$COPR_PROJECT" "$SRPM_FILE"
        echo -e "${GREEN}[✓] Build lancé sur Copr !${RESET}"
    else
        echo -e "${RED}[!] copr-cli n'est pas installé ou non configuré.${RESET}"
    fi
else
    echo ""
    echo -e "${YELLOW}${BOLD}Pour envoyer ce paquet sur Copr :${RESET}"
    echo -e "  1. Via copr-cli : ${CYAN}copr-cli build <utilisateur/projet> ${SRPM_FILE}${RESET}"
    echo -e "  2. Ou importez directement le fichier ${CYAN}${SRPM_FILE}${RESET} sur https://copr.fedorainfracloud.org"
fi
