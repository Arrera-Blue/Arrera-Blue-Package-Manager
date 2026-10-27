# Arrera Blue Package Manager (abpm)

## Arguments et Commandes

### Update

- `update` : Met à jour les dépôts et effectue toutes les mises à jour.
- `check` : Met à jour les dépôts et affiche la liste des applications à mettre à jour, peu importe si elles proviennent de Flathub ou de DNF.

### Install

- `install [PAQUET]` : Lance le programme d'installation en cherchant en priorité dans DNF. S'il ne trouve pas, il cherche dans Flathub. Avant de lancer l'installation, il affiche ce qu'il va installer pour confirmation.
- `install --dnf [PAQUET]` : Lance le programme d'installation en appelant uniquement DNF.
- `install --flatpak [PAQUET]` : Lance le programme d'installation en appelant uniquement Flatpak.
- `install --rpm [FICHIER_RPM]` : Installation d'un paquet RPM local (comme DNF sous Fedora).
- `install --appimage [FICHIER_APPIMAGE]` : Installation et intégration d'une application au format AppImage (avec extraction automatique de l'icône dans `.icons/`).
- `install --tar [FICHIER_ARCHIVE]` : Installation d'une application distribuée sous forme d'archive tar ou zip (`.tar.gz`, `.tar.xz`, `.tar.bz2`, `.tar`, `.zip`). Détecte les binaires, recherche les icônes PNG candidates et propose une sélection interactive si plusieurs icônes sont présentes.
- `install [OPTIONS] --icon [FICHIER_IMAGE] [APPLICATION]` : Permet de spécifier directement une icône lors de l'installation d'une archive ou d'une AppImage sans passer par la détection automatique.

### Set-Icon

- `set-icon [PAQUET] [FICHIER_IMAGE]` : Modifie l'icône d'une application déjà installée (AppImage ou Archive). Copie l'image dans le dossier `.icons/` et met à jour automatiquement le ou les fichiers `.desktop` associés. (Alias : `icon`).

### Remove

- `remove [PAQUET...]` : Désinstalle les applications, peu importe leur provenance (Flatpak, DNF ou autre). Permet de spécifier plusieurs paquets à la suite comme avec DNF.

### Search

- `search [PAQUET]` : Cherche les paquets disponibles, que ce soit sur DNF ou sur Flathub.
- `search --dnf [PAQUET]` : Cherche les paquets disponibles uniquement sur DNF.
- `search --flatpak [PAQUET]` : Cherche les paquets disponibles uniquement sur Flathub.

### List

- `list` : Liste tous les paquets Flatpak et DNF installés.
- `list --dnf` : Liste tous les paquets DNF installés.
- `list --flatpak` : Liste tous les paquets Flatpak installés.
- `list --rpm` : Liste tous les paquets installés via un fichier RPM local.
- `list --appimage` : Liste toutes les applications installées au format AppImage.
- `list --tar` : Liste toutes les applications installées via une archive (tar, zip).

### Options Générales

- `-y`, `--yes` : Répond automatiquement oui à toutes les confirmations d'installation/désinstallation et sélectionne l'icône recommandée pour les archives.
- `-h`, `--help` : Affiche l'aide de l'outil et les commandes disponibles.
- `-v`, `--version` : Affiche la version d'abpm et les technologies sous-jacentes (libdnf5, libflatpak).

## Convention d'Affichage et Messages

Un module centralisé (`Logger` / `Terminal`) en C++ assure un affichage clair, coloré et cohérent dans le terminal :

- `[INFO]` *(en bleu Arrera / cyan)* : Pour les étapes en cours (*ex: « Recherche dans les dépôts DNF... »*).
- `[SUCCÈS]` *(en vert)* : Pour les opérations réussies (*ex: « Paquet vlc installé avec succès ! »*).
- `[ATTENTION]` *(en jaune)* : Pour les avertissements sans blocage (*ex: « Flathub ne répond pas, bascule sur le cache local... »*).
- `[ERREUR]` *(en rouge)* : Pour les erreurs bloquantes avec message explicite (*ex: « Erreur : Le paquet 'xyz' est introuvable ni dans DNF ni sur Flathub »*).

