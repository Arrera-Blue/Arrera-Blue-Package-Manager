# TODO — `Arrera-Blue-Package-Manager` (`abpm`) : Support du remplacement du noyau Fedora par CachyOS & Désinstallation hors-ligne

Ce document détaille les modifications à apporter dans `abpm` pour permettre à l'assistant de premier démarrage **`arrera-blue-lynx`** de désinstaller proprement le noyau Fedora (`kernel`, `kernel-core`, `kernel-modules`, etc.) après l'installation de **`kernel-cachyos`**, avec suivi de progression temps réel (`--progress`).

---

## 1. Analyse du blocage actuel au redémarrage dans `DnfBackend::remove` (`src/dnf/dnf_backend.cpp`)

La désinstallation du noyau Fedora s'exécute **au redémarrage suivant** via `arrera-cleanup-fedora-kernel.service` (une fois que le système a effectivement démarré sur le noyau CachyOS : `uname -r == *cachyos*`).

Or, au redémarrage, lorsque le service appelle `abpm remove --dnf -y kernel kernel-core kernel-modules ...` :
1. **Chargement réseau bloquant au démarrage (`repo_sack->load_repos()`)** :
   - Dans `DnfBackend::remove` ([src/dnf/dnf_backend.cpp](file:///Users/baptistep/Documents/arrera-blue/Arrera-Blue-Package-Manager/src/dnf/dnf_backend.cpp#L668-L670)), `abpm` appelle :
     ```cpp
     repo_sack->create_repos_from_system_configuration();
     repo_sack->load_repos();
     ```
   - Cela force `libdnf5` à charger les dépôts distants sur Internet pour une simple désinstallation locale. Comme `arrera-cleanup-fedora-kernel.service` s'exécute tôt au boot (`After=local-fs.target`, avant la connexion Wi-Fi/DHCP), la commande échoue faute de réseau.
   - Une désinstallation RPM ne nécessite que la base RPM système locale : `repo_sack->load_repos(libdnf5::repo::Repo::Type::SYSTEM)`.
2. **Protection `protected_packages` dans `libdnf5` sur le nom `kernel-core`** :
   - Même une fois démarré sur `kernel-cachyos` (le noyau Fedora n'est donc plus le noyau actif), `libdnf5` protège le nom de paquet `kernel-core` dans `protected_packages` (`/etc/dnf/protected.d/`). Comme le noyau CachyOS s'appelle `kernel-cachyos-core` (et non `kernel-core`), `libdnf5` refuse de supprimer le dernier paquet nommé `kernel-core`.
3. **Émission du flux `--progress` dans `DnfBackend::remove`** :
   - En mode `--progress`, `AbpmTransactionCallbacks::elem_progress` affiche `"Installation de ..."` au lieu de `"Suppression de ..."` lors d'une transaction de désinstallation.

---

## 2. Modifications à effectuer dans `Arrera-Blue-Package-Manager`

### [ ] A. Permettre à `abpm remove --dnf` de désinstaller `kernel-core` au redémarrage sur CachyOS (hors-ligne) (`src/dnf/dnf_backend.cpp`)
- **Fichier** : [src/dnf/dnf_backend.cpp](file:///Users/baptistep/Documents/arrera-blue/Arrera-Blue-Package-Manager/src/dnf/dnf_backend.cpp) (fonction `DnfBackend::remove`)
- **Actions** :
  1. **Chargement 100 % local sans réseau** :
     Remplacer `repo_sack->create_repos_from_system_configuration(); repo_sack->load_repos();` par :
     ```cpp
     base.get_repo_sack()->load_repos(libdnf5::repo::Repo::Type::SYSTEM);
     ```
     (Rend `abpm remove` instantané et fonctionnel dès `local-fs.target` au démarrage sans connexion Internet).
  2. **Déverrouillage de `protected_packages` pour `kernel*` si `kernel-cachyos` est installé et actif** :
     - Si la liste `package_names` cible le noyau Fedora (`kernel`, `kernel-core`, `kernel-modules`, `kernel-modules-core`, `kernel-modules-extra`, `kernel-uki-virt`, etc.) :
     - Vérifier via `libdnf5::rpm::PackageQuery` qu'un autre noyau (ex. `kernel-cachyos` / `kernel-cachyos-core`) est installé sur le système.
     - Retirer alors `kernel`, `kernel-core`, `kernel-modules`, `kernel-modules-core` de `base.get_config().get_protected_packages_option()` (avec `libdnf5::Option::Priority::RUNTIME`) tout en conservant `protect_running_kernel = true` (ainsi, le noyau en cours d'exécution ne peut jamais être supprimé par erreur, et le noyau Fedora n'est supprimé qu'après avoir redémarré sur CachyOS).

### [ ] B. Améliorer le suivi `--progress` pour `abpm remove` (`src/dnf/dnf_backend.cpp`)
- **Fichier** : [src/dnf/dnf_backend.cpp](file:///Users/baptistep/Documents/arrera-blue/Arrera-Blue-Package-Manager/src/dnf/dnf_backend.cpp)
- **Actions** :
  1. Dans `DnfBackend::remove`, émettre `ProgressEmitter::emit(10, "Préparation de la désinstallation DNF...")` avant la résolution et `ProgressEmitter::emit(50, "Suppression des paquets RPM...")` avant `transaction.run()`.
  2. Dans `AbpmTransactionCallbacks::elem_progress`, adapter le libellé selon `item.get_action()` (`"Suppression de "` si l'action est `REMOVE` / `ERASE`, `"Installation de "` sinon).

### [ ] C. Incrémenter la version du paquet RPM (`CMakeLists.txt`, `src/cli/cli_parser.cpp`, `abpm.spec`)
- **Actions** :
  1. Passer la version de `2026.1.10` à **`2026.1.11`**.
  2. Mettre à jour le `%changelog` dans `abpm.spec` et recompiler le paquet sur le Copr `arrera-software/arrera-blue`.
