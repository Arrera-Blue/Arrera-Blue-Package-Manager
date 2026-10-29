# TODO & Feuille de Route (`abpm`)

Ce fichier recense les améliorations, fonctionnalités planifiées et correctifs à intégrer dans les prochaines versions d'**Arrera Blue Package Manager (`abpm`)**.

---

## 1. DNF5 & Gestion du Cache

- [ ] **Commandes de nettoyage du cache (`abpm clean`) :**
  - [ ] `abpm clean all` : Nettoyage complet de tous les caches de dépôts et métadonnées DNF5 (`/var/cache/libdnf5`).
  - [ ] `abpm clean expire-cache` : Forcer l'invalidation et l'expiration immédiate du cache local pour actualiser instantanément les nouveaux paquets des dépôts (ex: Copr, Fedora Updates).
  - [ ] `abpm clean packages` : Supprimer les fichiers `.rpm` mis en cache après téléchargement pour libérer de l'espace disque.

- [ ] **Dédoublonnage des paquets dans `check_updates()` :**
  - [ ] Filtrer les paquets retournés par `query.filter_upgrades()` afin qu'un paquet présent dans plusieurs fichiers de dépôts (miroirs ou dépôts en doublon dans `/etc/yum.repos.d/`) n'apparaisse qu'une seule fois dans la liste des mises à jour disponibles.
  - [ ] Sélectionner le dépôt prioritaire ou le plus récent en cas de duplication.

---

## 2. Découverte d'Applications (`discover` & Blue Hub)

- [ ] **Filtres par Catégories :**
  - [ ] Ajouter une option `--category <nom>` (ex: `audio`, `video`, `development`, `office`, `games`, `utility`).
  - [ ] Filtrer les métadonnées AppStream et Flathub selon les catégories standard XDG/FreeDesktop.
- [ ] **Tri et Recherche dans Discover :**
  - [ ] Permettre de trier par popularité, date d'ajout ou nom.
  - [ ] Recherche intégrée dans le catalogue Discover avec retour JSON.

---

## 3. Système & Polkit

- [ ] **Gestion fine de Polkit :**
  - [ ] Permettre l'élévation de privilèges transparente via `pkexec` ou action PolicyKit personnalisée (`org.arrerablue.abpm.install`) pour éviter de devoir lancer tout le CLI en `sudo`.
