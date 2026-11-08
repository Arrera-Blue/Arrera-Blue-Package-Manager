# Arrera Blue Package Manager (abpm)

## Arguments et Commandes

### Update & Check

- `update` : Met à jour les dépôts et effectue toutes les mises à jour.
- `update [PAQUET...]` : Met à jour un ou plusieurs paquets ou applications ciblés.
- `update --dnf [PAQUET...]` : Met à jour uniquement les paquets DNF (tous ou paquets ciblés).
- `update --flatpak [APPLICATION...]` : Met à jour uniquement les applications Flatpak (toutes ou ciblées).
- `update [OPTIONS] --progress [PAQUET...]` : Émet le flux de progression en temps réel non tamponné (`PROGRESS:<0-100>:<ETAPE>`) pour Blue Hub.
- `check` : Met à jour les dépôts et affiche la liste des applications à mettre à jour, peu importe si elles proviennent de Flathub ou de DNF.
- `check --json` : Sortie brute en JSON standardisé sur `stdout` pour **Blue Hub** avec classification automatique (`app`, `utility`, `system`).
- `check --json --dnf` / `check --json --flatpak` : Vérifie les mises à jour pour une source spécifique en sortie JSON.

### Install

- `install [PAQUET]` : Lance le programme d'installation en cherchant en priorité dans DNF. S'il ne trouve pas, il cherche dans Flathub. Avant de lancer l'installation, il affiche ce qu'il va installer pour confirmation.
- `install --dnf [PAQUET]` : Lance le programme d'installation en appelant uniquement DNF.
- `install --flatpak [PAQUET]` : Lance le programme d'installation en appelant uniquement Flatpak.
- `install --rpm [FICHIER_RPM]` : Installation d'un paquet RPM local (comme DNF sous Fedora).
- `install --appimage [FICHIER_APPIMAGE]` : Installation et intégration d'une application au format AppImage (avec extraction automatique de l'icône dans `.icons/`).
- `install --tar [FICHIER_ARCHIVE]` : Installation d'une application distribuée sous forme d'archive tar ou zip (`.tar.gz`, `.tar.xz`, `.tar.bz2`, `.tar`, `.zip`). Détecte les binaires, recherche les icônes PNG candidates et propose une sélection interactive si plusieurs icônes sont présentes.
- `install [OPTIONS] --icon [FICHIER_IMAGE] [APPLICATION]` : Permet de spécifier directement une icône lors de l'installation d'une archive ou d'une AppImage sans passer par la détection automatique.
- `install [OPTIONS] --progress [PAQUET]` : Émet un flux textuel non tamponné (`PROGRESS:<PCT>:<MESSAGE>` ou `PROGRESS:ERROR:<MSG>`) sans style ANSI ni bannières, permettant aux interfaces graphiques (**Blue Hub**) d'afficher la progression en temps réel. Active implicitement le mode non interactif (`-y`).

### Set-Icon

- `set-icon [PAQUET] [FICHIER_IMAGE]` : Modifie l'icône d'une application déjà installée (AppImage ou Archive). Copie l'image dans le dossier `.icons/` et met à jour automatiquement le ou les fichiers `.desktop` associés. (Alias : `icon`).

### Remove

- `remove [PAQUET...]` : Désinstalle les applications, peu importe leur provenance (Flatpak, DNF ou autre). Permet de spécifier plusieurs paquets à la suite comme avec DNF. Retourne le code de sortie `1` en cas d'erreur (paquet introuvable, permissions root manquantes pour une application système, échec d'effacement).
- `remove --progress [PAQUET...]` : Émet le flux de progression en temps réel (`PROGRESS:<0-100>:<ETAPE>` et `PROGRESS:ERROR:<MSG>`) pour les interfaces graphiques (Blue Hub).
- `remove --system [PAQUET...]` : Cible explicitement l'environnement système (`/opt/applications`, `/var/lib/abpm`, root requis).
- `remove --user [PAQUET...]` : Cible explicitement l'environnement utilisateur local (`~/Applications`, `~/.local/share/abpm`).

### Search

- `search [PAQUET]` : Cherche les paquets disponibles, que ce soit sur DNF ou sur Flathub.
- `search --dnf [PAQUET]` : Cherche les paquets disponibles uniquement sur DNF.
- `search --flatpak [PAQUET]` : Cherche les paquets disponibles uniquement sur Flathub.

### Discover

- `discover` : Découvre et affiche les applications populaires et tendances du moment en mode mixte (50% paquets DNF via AppStream et 50% tendances Flathub).
- `discover --dnf` : Découvre uniquement les applications graphiques disponibles sur DNF via le catalogue AppStream.
- `discover --flatpak` : Découvre uniquement les applications populaires et tendances de Flathub.
- `discover --page [N]` : Spécifie le numéro de la page à afficher (par défaut : `1`).
- `discover --per-page [N]` : Spécifie le nombre d'applications retournées par page (par défaut : `6`).
- `discover --json` : Sortie brute en JSON standardisé sur `stdout` sans styles ANSI pour intégration directe dans **Blue Hub** (Software Center d'Arrera Blue OS).

### List

- `list` : Liste tous les paquets Flatpak et DNF installés (mode complet).
- `list --apps` : Liste uniquement les véritables applications installées (avec lanceurs `.desktop`), ignorant les paquets système bas niveau.
- `list --json` : Sortie brute en JSON standardisé de toutes les applications installées (pour **Blue Hub**).
- `list --dnf` : Liste tous les paquets DNF installés (ou les applications DNF si combiné avec `--apps` ou `--json`).
- `list --flatpak` : Liste les applications Flatpak installées.
- `list --rpm` : Liste tous les paquets installés via un fichier RPM local.
- `list --appimage` : Liste toutes les applications installées au format AppImage.
- `list --tar` : Liste toutes les applications installées via une archive (tar, zip).

### Clean & Cache

- `clean` ou `clean all` : Nettoie l'intégralité du cache système DNF (`/var/cache/libdnf5`). *(Nécessite sudo)*
- `clean expire-cache` : Force l'expiration immédiate du cache local pour actualiser instantanément les nouveaux paquets des dépôts (ex: Copr).
- `clean packages` : Supprime les paquets `.rpm` mis en cache pour libérer de l'espace disque. *(Nécessite sudo)*

### Dépôts Logiciels (Repo)

- `repo list` : Liste tous les dépôts logiciels configurés (DNF/RPM et remotes Flatpak) avec leur état (activé/désactivé) et statut de protection.
- `repo list --json` : Sortie brute en JSON standardisé pour les Préférences de **Blue Hub** (champs `id`, `name`, `type`, `enabled`, `is_protected`, `url`).
- `repo enable [ID]` : Active un dépôt logiciel RPM ou remote Flatpak désactivé. *(Nécessite sudo)*
- `repo disable [ID]` : Désactive un dépôt logiciel. Refuse strictement l'opération si le dépôt est indispensable au système Arrera Blue (`is_protected: true`). *(Nécessite sudo)*
- `repo add [URL_OU_FICHIER]` : Ajoute un nouveau dépôt RPM au système depuis un fichier `.repo` ou une URL. *(Nécessite sudo)*

### Configuration & Mises à Jour Automatiques (Config)

- `config get [CLE]` : Affiche la valeur courante d'une clé de configuration (ex: `config get auto-update`).
- `config set [CLE] [VALEUR]` : Modifie une option de configuration (ex: `config set auto-update false`). Pour `auto-update`, synchronise automatiquement le timer systemd `abpm-auto-update.timer` (`enable --now` ou `disable --now`).

### Options de Portée (Scope)

- `--system` : Cible explicitement l'environnement système (`/opt/applications`, `/var/lib/abpm`, dépôts système). Requiert les privilèges administrateur (root / sudo).
- `--user` : Cible explicitement l'environnement utilisateur local (`~/Applications`, `~/.local/share/abpm`, Flatpak utilisateur).

### Options Générales

- `-y`, `--yes` : Répond automatiquement oui à toutes les confirmations d'installation/désinstallation et sélectionne l'icône recommandée pour les archives.
- `--progress` : Active le mode de progression temps réel sur `stdout` (`PROGRESS:<0-100>:<MESSAGE>`) pour intégration avec Blue Hub.
- `-h`, `--help` : Affiche l'aide de l'outil et les commandes disponibles.
- `-v`, `--version` : Affiche la version d'abpm et les technologies sous-jacentes (libdnf5, libflatpak).

## Convention d'Affichage et Messages

Un module centralisé (`Logger` / `Terminal`) en C++ assure un affichage clair, coloré et cohérent dans le terminal :

- `[INFO]` *(en bleu Arrera / cyan)* : Pour les étapes en cours (*ex: « Recherche dans les dépôts DNF... »*).
- `[SUCCÈS]` *(en vert)* : Pour les opérations réussies (*ex: « Paquet vlc installé avec succès ! »*).
- `[ATTENTION]` *(en jaune)* : Pour les avertissements sans blocage (*ex: « Flathub ne répond pas, bascule sur le cache local... »*).
- `[ERREUR]` *(en rouge)* : Pour les erreurs bloquantes avec message explicite (*ex: « Erreur : Le paquet 'xyz' est introuvable ni dans DNF ni sur Flathub »*).


## Compilation et Packaging RPM (Fedora Copr)

### 1. Installation des dépendances

Le script d'installation configure automatiquement les bibliothèques C++ et les outils de packaging RPM :
```bash
./install-dev-deps.sh
```

### 2. Compilation locale avec CMake

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
./build/abpm --version
```

### 3. Génération automatique du RPM et publication sur Copr

Le script `build-rpm.sh` automatise la création de l'arborescence `~/rpmbuild`, l'archive source tarball et les fichiers `.rpm` et `.src.rpm` :

```bash
# Compilation complète locale (génère le SRPM et le binaire RPM)
./build-rpm.sh

# Générer uniquement le paquet source (.src.rpm) pour Copr
./build-rpm.sh --srpm-only

# Générer le SRPM et l'envoyer directement sur Fedora Copr
./build-rpm.sh --copr <utilisateur>/<projet>
```

Une fois publié sur Copr, l'installation sur Arrera Blue OS / Fedora se fait simplement :
```bash
sudo dnf copr enable <utilisateur>/<projet>
sudo dnf install abpm
```

