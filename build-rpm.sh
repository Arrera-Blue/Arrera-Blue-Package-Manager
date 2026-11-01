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

# Répertoire du projet
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Fonction d'aide
print_help() {
    echo -e "${CYAN}${BOLD}Arrera Blue Package Manager (abpm) - RPM & Copr Builder${RESET}"
    echo ""
    echo -e "${BOLD}Utilisation :${RESET} ./build-rpm.sh [OPTIONS]"
    echo ""
    echo -e "${BOLD}Options :${RESET}"
    echo -e "  ${GREEN}-h, --help${RESET}             Affiche cette aide et quitte"
    echo -e "  ${GREEN}--srpm-only${RESET}            Génère uniquement le paquet source (SRPM, rapide)"
    echo -e "  ${GREEN}--all${RESET}                  Force la compilation locale complète (SRPM + binaire RPM)"
    echo -e "  ${GREEN}--copr <user/projet>${RESET}   Envoie le SRPM sur Fedora Copr et déclenche le build distant"
    echo -e "  ${GREEN}--nowait${RESET}               Ne pas bloquer en attendant la fin de compilation Copr"
    echo ""
    echo -e "${BOLD}Exemples :${RESET}"
    echo -e "  ./build-rpm.sh                                 # Génération locale complète (SRPM + RPM)"
    echo -e "  ./build-rpm.sh --copr arrera-software/arrera-blue  # Envoi direct sur Copr (SRPM uniquement)"
    echo -e "  ./build-rpm.sh --srpm-only                     # Création rapide du SRPM"
    echo ""
}

# Parsing des options de ligne de commande dès le début
BUILD_MODE="default"
COPR_PROJECT=""
COPR_NOWAIT=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            print_help
            exit 0
            ;;
        --srpm-only)
            BUILD_MODE="srpm"
            shift
            ;;
        --all)
            BUILD_MODE="all"
            shift
            ;;
        --nowait)
            COPR_NOWAIT=true
            shift
            ;;
        --copr)
            if [[ $# -ge 2 && ! "$2" =~ ^-- ]]; then
                COPR_PROJECT="$2"
                shift 2
            else
                echo -e "${RED}[!] Erreur : --copr nécessite <utilisateur/projet> (ex: arrera-software/arrera-blue)${RESET}" >&2
                exit 1
            fi
            ;;
        *)
            echo -e "${RED}[!] Option inconnue : $1${RESET}" >&2
            echo -e "Tapez ${CYAN}./build-rpm.sh --help${RESET} pour voir les options disponibles."
            exit 1
            ;;
    esac
done

echo -e "${CYAN}${BOLD}==========================================================${RESET}"
echo -e "${CYAN}${BOLD}  Arrera Blue Package Manager (abpm) - RPM Builder        ${RESET}"
echo -e "${CYAN}${BOLD}==========================================================${RESET}"

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
echo -e "${YELLOW}[*] Version :${RESET} ${PKG_VERSION}-${PKG_RELEASE}"

# Vérification de cohérence entre abpm.spec et CMakeLists.txt
if [[ -f "CMakeLists.txt" ]]; then
    CMAKE_VERSION=$(grep -A 3 'project(' CMakeLists.txt | grep -E 'VERSION' | awk '{print $2}' || true)
    if [[ -n "$CMAKE_VERSION" && "$CMAKE_VERSION" != "$PKG_VERSION" ]]; then
        echo -e "${RED}[!] Incohérence de version détectée !${RESET}"
        echo -e "    - ${BOLD}abpm.spec${RESET}      : ${PKG_VERSION}"
        echo -e "    - ${BOLD}CMakeLists.txt${RESET} : ${CMAKE_VERSION}"
        echo -e "${RED}[!] Veuillez synchroniser les versions avant de continuer.${RESET}" >&2
        exit 1
    fi
fi

# Vérification des outils requis
MISSING_TOOLS=()
for tool in rpmbuild tar; do
    if ! command -v "$tool" &>/dev/null; then
        MISSING_TOOLS+=("$tool")
    fi
done

if [[ -n "$COPR_PROJECT" ]]; then
    if ! command -v copr-cli &>/dev/null; then
        MISSING_TOOLS+=("copr-cli")
    fi
    if [[ ! -f "$HOME/.config/copr" ]]; then
        echo -e "${RED}[!] Erreur : Configuration copr-cli introuvable ($HOME/.config/copr).${RESET}" >&2
        echo -e "${YELLOW}[i] Récupérez votre token sur https://copr.fedorainfracloud.org/api/ et collez-le dans ~/.config/copr${RESET}" >&2
        exit 1
    fi
fi

if [[ ${#MISSING_TOOLS[@]} -gt 0 ]]; then
    echo -e "${YELLOW}[!] Outils manquants détectés : ${MISSING_TOOLS[*]}${RESET}"
    echo -e "${YELLOW}[*] Installation des paquets requis...${RESET}"
    if command -v sudo &>/dev/null; then
        sudo dnf install -y "${MISSING_TOOLS[@]}"
    else
        echo -e "${RED}[!] Veuillez installer manuellement : sudo dnf install -y ${MISSING_TOOLS[*]}${RESET}" >&2
        exit 1
    fi
fi

# Préparation de l'arborescence rpmbuild
RPMBUILD_DIR="${RPMBUILD_DIR:-$HOME/rpmbuild}"
if ! mkdir -p "$RPMBUILD_DIR/SOURCES" 2>/dev/null; then
    RPMBUILD_DIR="$SCRIPT_DIR/rpmbuild"
fi

mkdir -p "$RPMBUILD_DIR"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
echo -e "${YELLOW}[*] Répertoire rpmbuild :${RESET} ${RPMBUILD_DIR}"

RPM_SOURCES="$RPMBUILD_DIR/SOURCES"
RPM_SPECS="$RPMBUILD_DIR/SPECS"
RPM_SRPMS="$RPMBUILD_DIR/SRPMS"
RPM_RPMS="$RPMBUILD_DIR/RPMS"
TARBALL="${PKG_NAME}-${PKG_VERSION}.tar.gz"

# Création de l'archive tarball source
echo -e "${YELLOW}[*] Création de l'archive source ${TARBALL}...${RESET}"
if git rev-parse --is-inside-work-tree &>/dev/null; then
    # Vérification des fichiers non commités ou non suivis
    DIRTY_FILES=$(git status --porcelain 2>/dev/null || true)
    if [[ -n "$DIRTY_FILES" ]]; then
        echo -e "${YELLOW}[ATTENTION] Modifications locales non commitées détectées dans Git :${RESET}"
        git status -s
        echo -e "${YELLOW}[*] Inclusion dynamique de TOUS vos fichiers locaux actuels dans l'archive tarball...${RESET}"
        echo -e "${CYAN}[i] Pensez à faire un 'git commit' et 'git push' pour synchroniser votre dépôt GitHub.${RESET}"
    fi

    # Packaging propre de l'arborescence actuelle (fichiers suivis + nouveaux fichiers hors .gitignore)
    # Ceci garantit que le tarball contient TOUJOURS le code en cours de développement
    git ls-files -z -c -o --exclude-standard | tar --null --transform "s,^,${PKG_NAME}-${PKG_VERSION}/," -czf "${RPM_SOURCES}/${TARBALL}" -T -
else
    # Fallback tar standard
    tar --exclude-vcs --exclude=build --exclude=out --exclude="*.tar.gz" --exclude="*.rpm" \
        --transform "s,^,${PKG_NAME}-${PKG_VERSION}/," -czf "${RPM_SOURCES}/${TARBALL}" .
fi

# Copie du specfile
cp "$SPEC_FILE" "${RPM_SPECS}/"

# Détermination du mode de compilation
# Si un projet Copr est spécifié et que l'utilisateur n'a pas explicitement demandé --all,
# on compile uniquement le SRPM pour un téléversement instantané vers le cloud Copr.
if [[ -n "$COPR_PROJECT" && "$BUILD_MODE" == "default" ]]; then
    BUILD_MODE="srpm"
elif [[ "$BUILD_MODE" == "default" ]]; then
    BUILD_MODE="all"
fi

# Exécution de rpmbuild
if [[ "$BUILD_MODE" == "srpm" ]]; then
    echo -e "${YELLOW}[*] Génération rapide du paquet source (SRPM)...${RESET}"
    rpmbuild --define "_topdir ${RPMBUILD_DIR}" -bs "${RPM_SPECS}/${SPEC_FILE}"
else
    echo -e "${YELLOW}[*] Compilation locale complète (SRPM + binaire RPM)...${RESET}"
    rpmbuild --define "_topdir ${RPMBUILD_DIR}" -ba "${RPM_SPECS}/${SPEC_FILE}"
fi

# Récupération du fichier SRPM le plus récent généré
SRPM_FILE=$(find "$RPM_SRPMS" -name "${PKG_NAME}-${PKG_VERSION}*.src.rpm" -type f -printf '%T@ %p\n' 2>/dev/null | sort -n | tail -1 | cut -f2- -d" ")

if [[ -z "$SRPM_FILE" || ! -f "$SRPM_FILE" ]]; then
    echo -e "${RED}[!] Erreur : Impossible de localiser le fichier SRPM généré dans $RPM_SRPMS.${RESET}" >&2
    exit 1
fi

echo ""
echo -e "${GREEN}${BOLD}[✓] Paquet(s) généré(s) avec succès !${RESET}"
echo -e " • ${CYAN}Paquet source (SRPM) :${RESET} ${SRPM_FILE}"

if [[ "$BUILD_MODE" == "all" ]]; then
    BINARY_RPMS=$(find "$RPM_RPMS" -name "${PKG_NAME}-${PKG_VERSION}*.rpm" 2>/dev/null || true)
    if [[ -n "$BINARY_RPMS" ]]; then
        echo -e " • ${CYAN}Paquet(s) binaire(s) (RPM) :${RESET}"
        for rpm in $BINARY_RPMS; do
            echo -e "   -> $rpm"
        done
    fi
fi

# Téléversement vers Copr si demandé
if [[ -n "$COPR_PROJECT" ]]; then
    echo ""
    echo -e "${YELLOW}[*] Envoi du SRPM vers Fedora Copr (${COPR_PROJECT})...${RESET}"
    COPR_CMD=("copr-cli" "build")
    if [[ "$COPR_NOWAIT" == true ]]; then
        COPR_CMD+=("--nowait")
    fi
    COPR_CMD+=("$COPR_PROJECT" "$SRPM_FILE")

    "${COPR_CMD[@]}"

    echo ""
    echo -e "${GREEN}${BOLD}[✓] Téléversement réussi et build lancé sur Copr !${RESET}"
    echo -e " • Suivi des builds : ${CYAN}https://copr.fedorainfracloud.org/coprs/${COPR_PROJECT}/builds/${RESET}"
    echo ""
    echo -e "${YELLOW}${BOLD}Pour appliquer la mise à jour sur votre machine une fois le build terminé :${RESET}"
    echo -e "  1. Expirer le cache local : ${CYAN}abpm clean expire-cache${RESET}"
    echo -e "  2. Mettre à jour abpm     : ${CYAN}sudo dnf upgrade abpm${RESET} ou ${CYAN}sudo abpm update${RESET}"
else
    echo ""
    echo -e "${YELLOW}${BOLD}Pour envoyer ce paquet sur Copr :${RESET}"
    echo -e "  1. Via copr-cli : ${CYAN}copr-cli build <utilisateur/projet> ${SRPM_FILE}${RESET}"
    echo -e "  2. Ou importez directement le fichier ${CYAN}${SRPM_FILE}${RESET} sur https://copr.fedorainfracloud.org"
fi
