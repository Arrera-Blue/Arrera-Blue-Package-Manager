# Arrera Blue Package Manager (`abpm`) - Documentation

The **Arrera Blue Package Manager (`abpm`)** is a next-generation unified package management CLI written in native C++20, purpose-built for the **Arrera Blue OS** distribution (Fedora Linux base).

It seamlessly consolidates system packages (**DNF/RPM** via `libdnf5`), sandboxed desktop applications (**Flatpak** via `libflatpak`), standalone **AppImages**, and compressed archive applications (**Tarballs and Zip** via `libarchive`).

---

## Table of Contents

1. [Architecture & Design](#architecture--design)
2. [Prerequisites & Build](#prerequisites--build)
3. [Command Reference](#command-reference)
   - [update, check & clean](#1-system-updates)
   - [install](#2-installation)
   - [remove](#3-uninstallation)
   - [search](#4-search)
   - [discover](#5-discover-trending-applications)
   - [list](#6-listing-installed-packages)
   - [set-icon](#7-icon-management)
   - [repo](#8-repository-management)
   - [config](#9-configuration--automatic-updates)
4. [Standalone Application Storage & Integration](#standalone-application-storage--integration)
   - [User Mode vs. System (Root) Mode](#user-mode-vs-system-mode)
   - [AppImage Integration](#appimage-integration)
   - [Archive (Tar / Zip) Integration](#archive-tar--zip-integration)
   - [Icon Resolution & Storage](#icon-resolution--storage)
5. [Logging & Output Conventions](#logging--output-conventions)

---

## Architecture & Design

`abpm` eliminates the fragmentation between system package managers and desktop application distribution models by providing a unified, high-performance interface:

- **Native C++ APIs (Zero Shell Wrapping for core engines):**
  - Uses `libdnf5` directly for fast transactional package queries, local RPM installations, GPG validation, and system updates.
  - Uses `libflatpak` and GLib/GIO natively for managing Flathub applications, runtimes, and local refs.
  - Uses `libarchive` for fast, streaming in-memory extraction of compressed archives (`.tar.gz`, `.tar.xz`, `.tar.bz2`, `.tar.zst`, `.tar`, `.zip`).
  - Uses `libcurl` and `json-glib` for querying remote app catalogs and trending collections.
- **Unified Transaction Model:**
  - `install` and `remove` commands can resolve multiple packages across all package types in a single execution.
  - Multi-package uninstallation intelligently routes each target to its respective backend (Archive, AppImage, Flatpak, or DNF).
- **Desktop & CLI Path Integration:**
  - Automatically creates and manages FreeDesktop-compliant `.desktop` launchers and `$PATH` symlinks.
  - Maintains internal JSON registries (`archives.json` and `appimages.json`) for precise lifecycle management and clean uninstallations.

---

## Prerequisites & Build

### System Dependencies (Fedora / Arrera Blue OS)

```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    pkg-config \
    libdnf5-devel \
    flatpak-devel \
    glib2-devel \
    libarchive-devel \
    libcurl-devel \
    json-glib-devel \
    fuse-libs
```

### Compiling from Source

```bash
# Generate build configuration
cmake -B build -S .

# Compile executable
cmake --build build -j$(nproc)

# The resulting binary is available at:
./build/abpm --version
```

### Packaging RPM & Deploying to Fedora Copr

`abpm` includes a dedicated automation script (`build-rpm.sh`) to generate source archives, SRPMs, and binary RPM packages conforming to the official `abpm.spec` file:

```bash
# Full local build (generates both SRPM and binary RPM)
./build-rpm.sh

# Build SRPM only (ready for cloud submission)
./build-rpm.sh --srpm-only

# Build and immediately trigger build on Fedora Copr
./build-rpm.sh --copr <username>/<project>
```

---

## Command Reference

### 1. System Updates

#### `abpm update`
Synchronizes repositories and upgrades packages. Supports targeted updates, source filters, and real-time GUI progress streaming for **Blue Hub**.
```bash
# Global system update
abpm update
abpm update -y    # Bypass confirmation prompts

# Targeted updates (specific packages or applications)
abpm update --dnf firefox less
abpm update --flatpak org.mozilla.firefox

# Real-time progress output for GUI integrations (Blue Hub)
abpm update -y --progress --dnf firefox
abpm update -y --progress --flatpak org.mozilla.firefox
```

#### `abpm check`
Refreshes repository metadata and displays pending updates. Supports `--json` machine-readable output categorized into `app`, `utility`, and `system` components.
```bash
# Human-readable summary
abpm check

# Machine-readable JSON output for Blue Hub
abpm check --json
abpm check --json --dnf
abpm check --json --flatpak
```

#### `abpm clean [all | expire-cache | packages]`
Manages and cleans local repository caches and downloaded RPM packages.
```bash
# Expire repo cache to force immediate metadata re-synchronization (no sudo needed)
abpm clean expire-cache

# Purge cached RPM packages to free up disk space
sudo abpm clean packages

# Completely purge all DNF5 cache files and metadata
sudo abpm clean all
```

---

### 2. Installation

`abpm install` features automatic source resolution. If no source filter is explicitly specified, it looks up the package in **DNF** repositories first; if not found, it queries **Flathub**.

```bash
# Smart multi-source installation
abpm install vlc

# Force specific distribution source
abpm install --dnf htop
abpm install --flatpak org.mozilla.firefox

# Local RPM installation with DNF5 dependency resolution & progress callbacks
abpm install --rpm package-1.0.rpm

# Standalone AppImage installation
abpm install --appimage arduino-ide.AppImage

# Compressed Archive installation (tar.gz, tar.xz, tar.bz2, zip, etc.)
abpm install --tar antigravity-ide.tar.gz

# Override or specify custom icon during installation
abpm install --tar application.tar.gz --icon /path/to/custom-icon.png
abpm install --tar application.tar.gz --icon internal-icon.png
```

#### Global Confirmation Flag: `-y`, `--yes`
Bypasses all interactive confirmation prompts and automatically selects the recommended default option (e.g. recommended icon in multi-icon archives).
```bash
abpm install -y --tar toolsuite.zip
```

#### Real-time Progress Streaming Flag: `--progress`
Emits machine-readable, unbuffered execution progress lines directly to `stdout` (`PROGRESS:<0-100>:<MESSAGE>` and `PROGRESS:ERROR:<DESCRIPTION>`) without ANSI escape sequences or banners. Designed for graphical frontend integrations such as **Blue Hub**. Automatically enables non-interactive confirmation (`-y`).

```bash
abpm install --progress vlc
abpm install -y --progress --tar application.tar.gz
```

---

### 3. Uninstallation

#### `abpm remove [OPTIONS] <PACKAGE...>`
Uninstalls one or more applications regardless of their source format. `abpm` automatically detects whether the target is an Archive application, AppImage, Flatpak, or DNF package.

Returns exit code `0` on complete success, and non-zero exit code `1` if any target is not found, requires administrative privileges, or encounters a deletion failure.

```bash
# Remove multiple packages across different formats simultaneously
abpm remove arduino-ide antigravity-ide vlc htop

# Remove with real-time progress streaming for Blue Hub
abpm remove --progress antigravity-ide

# Explicitly target system-level packages (/opt, /var/lib/abpm, root required)
sudo abpm remove --system my-system-tool

# Explicitly target user-level packages (~/Applications, ~/.local/share/abpm)
abpm remove --user my-local-tool
```

#### Scope Flags (`--system` and `--user`)
- `--system`: Restricts operations (listing, installation, uninstallation) strictly to system scopes (`/opt/applications/`, `/var/lib/abpm/`, `/usr/share/applications/`). Root privileges (`sudo` / `pkexec`) are required.
- `--user`: Restricts operations strictly to local user scope (`~/Applications/`, `~/.local/share/abpm/`, `~/.local/share/applications/`). No root privileges required.
- Default (`auto`): Checks user scope first; if not found, checks system scope (and prompts for root privileges if targeted).

---

### 4. Search

#### `abpm search [OPTIONS] <QUERY>`
Searches available packages across remote catalogs.
```bash
# Search both DNF and Flathub
abpm search blender

# Filter search to DNF or Flatpak only
abpm search --dnf python3
abpm search --flatpak kdenlive
```

---

### 5. Discover (Trending Applications)

#### `abpm discover [OPTIONS]`
Discovers and lists trending desktop applications from Flathub with automatic, high-performance DNF priority resolution.

This command is designed both for CLI exploration and as the backend engine for **Blue Hub** (the Arrera Blue OS Software Center).

#### Source Resolution Logic (DNF Priority)
For each trending application:
1. `abpm` resolves candidate RPM package names (e.g. `org.videolan.VLC` -> `vlc`, `org.gimp.GIMP` -> `gimp`).
2. It queries available packages in the DNF repository cache via `libdnf5` in a single batch lookup (~150ms).
3. If available in DNF repositories: `"source": "dnf"`, `"package": "<rpm_name>"`.
4. If exclusive to Flathub: `"source": "flatpak"`, `"package": "<flatpak_id>"`.

#### CLI Options
| Option | Type | Default | Description |
|---|---|---|---|
| `--dnf` | Flag | `false` | Discover desktop applications exclusively from DNF via AppStream metadata. |
| `--flatpak` | Flag | `false` | Discover trending applications exclusively from Flathub. |
| `--page <N>` | Integer ($\ge 1$) | `1` | Page number to retrieve. |
| `--per-page <N>` | Integer ($\ge 1$) | `6` | Number of applications per page. |
| `--json` | Flag | `false` | Outputs raw JSON to `stdout` without ANSI colors or banners for Blue Hub integration. |

```bash
# Explore mixed applications (50% DNF AppStream, 50% Flathub)
abpm discover

# Explore DNF applications only
abpm discover --dnf

# Explore Flathub applications only
abpm discover --flatpak

# Pagination: fetch 6 items starting from page 2
abpm discover --page 2 --per-page 6

# Raw JSON mode (consumed directly by Blue Hub)
abpm discover --page 1 --per-page 6 --json
```

#### JSON Output Schema (`--json`)
```json
[
  {
    "id": "org.videolan.VLC",
    "name": "VLC",
    "summary": "Lecteur multimédia libre et polyvalent",
    "icon": "vlc",
    "source": "dnf",
    "package": "vlc"
  },
  {
    "id": "net.armatik.Kitsune",
    "name": "Kitsune",
    "summary": "Regardez des animes depuis AniLiberty",
    "icon": "net.armatik.Kitsune",
    "source": "flatpak",
    "package": "net.armatik.Kitsune"
  }
]
```

---

### 6. Listing Installed Packages

#### `abpm list [OPTIONS]`
Lists installed packages and applications on the system.
```bash
# List all installed DNF, Flatpak, AppImage, and Archive applications
abpm list

# Filter list by origin
abpm list --dnf
abpm list --flatpak
abpm list --rpm
abpm list --appimage
abpm list --tar

# List only true installed desktop applications (ignores low-level system packages)
abpm list --apps

# Raw JSON output for Blue Hub integration
abpm list --json
```

##### List JSON Output Schema (`abpm list --json`)
```json
[
  {
    "id": "antigravity",
    "name": "antigravity",
    "version": "standalone",
    "source": "archive",
    "icon": "/opt/applications/.icons/antigravity.png",
    "package": "antigravity",
    "is_system": true
  },
  {
    "id": "com.visualstudio.code",
    "name": "Visual Studio Code",
    "version": "1.139.1",
    "source": "flatpak",
    "icon": "com.visualstudio.code",
    "package": "com.visualstudio.code",
    "is_system": true
  },
  {
    "id": "org.gnome.Settings",
    "name": "Paramètres",
    "version": "1:50.4-2026.beta.1.fc44",
    "source": "dnf",
    "icon": "org.gnome.Settings",
    "package": "arrera-gnome-control-center",
    "is_system": true
  }
]
```
```

---

### 7. Icon Management

#### `abpm set-icon <APP_NAME> <IMAGE_PATH>` *(Alias: `abpm icon`)*
Customizes the icon of an already installed AppImage or Archive application. Copies the image to the dedicated `.icons/` directory and synchronizes all associated `.desktop` files.
```bash
abpm set-icon arduino-ide /home/user/Pictures/arduino-custom.png
abpm icon antigravity-ide /home/user/Pictures/custom-code.png

# For system-wide installations (installed in /opt/applications):
sudo abpm set-icon arduino-ide /usr/share/pixmaps/new-icon.png
```

---

### 8. Repository Management (`abpm repo`)

Provides unified discovery and control of RPM repositories (via DNF5) and Flatpak remotes (via `libflatpak`), designed for the **Blue Hub** preferences GUI.

#### `abpm repo list [--json]`
Lists all configured repositories. With `--json`, outputs raw, unadorned JSON for UI consumption.
```bash
# Terminal view
abpm repo list

# JSON schema for Blue Hub
abpm repo list --json
```

##### Repo JSON Schema
```json
[
  {
    "id": "fedora",
    "name": "Fedora 44 - x86_64",
    "type": "rpm",
    "enabled": true,
    "is_protected": true,
    "url": "https://mirrors.fedoraproject.org/metalink?repo=fedora-44&arch=x86_64"
  },
  {
    "id": "flathub",
    "name": "Flathub",
    "type": "flatpak",
    "enabled": true,
    "is_protected": true,
    "url": "https://dl.flathub.org/repo/"
  }
]
```

#### `abpm repo enable <REPO_ID>`
Enables a disabled repository. Automatically detects whether the repository is RPM or Flatpak. Requires administrator (`sudo`) privileges.
```bash
sudo abpm repo enable rpmfusion-free
sudo abpm repo enable fedora-testing
```

#### `abpm repo disable <REPO_ID>`
Disables a non-essential repository. Protects core system repositories (`fedora`, `updates`, `copr-arrera-blue`, `flathub`), returning exit code `1` and blocking disablement if attempted. Requires administrator (`sudo`) privileges.
```bash
sudo abpm repo disable rpmfusion-free
```

#### `abpm repo add <URL_OR_FILE>`
Adds a new RPM repository file (.repo) from a local path or remote URL. Requires administrator (`sudo`) privileges.
```bash
sudo abpm repo add https://example.com/repo/custom.repo
```

---

### 9. Configuration & Automatic Updates (`abpm config`)

Manages ABPM system settings stored in `/etc/abpm/abpm.conf` (root) or `~/.config/abpm/abpm.conf` (user).

#### `abpm config get <KEY>`
Reads a configuration value. If `auto-update` is queried and not explicitly defined, it defaults to `true`.
```bash
abpm config get auto-update
```

#### `abpm config set <KEY> <VALUE>`
Sets a configuration key. When setting `auto-update` to `true` or `false`, ABPM automatically enables or disables the systemd timer `abpm-auto-update.timer`.
```bash
# Disable automatic background updates
sudo abpm config set auto-update false

# Re-enable automatic background updates
sudo abpm config set auto-update true
```

---

## Standalone Application Storage & Integration

### User Mode vs. System Mode

`abpm` automatically switches storage directories based on the execution context (user privileges vs. root/`sudo`):

| Resource | User Mode (Normal User) | System Mode (`sudo` / Root) |
|---|---|---|
| **App Binaries / Folders** | `~/Applications/<app>/` | `/opt/applications/<app>/` |
| **Dedicated Icons** | `~/Applications/.icons/` | `/opt/applications/.icons/` |
| **System PATH Symlinks** | `~/.local/bin/` | `/usr/local/bin/` |
| **Desktop Launchers** | `~/.local/share/applications/` | `/usr/share/applications/` |
| **Manifest Database** | `~/.local/share/abpm/` | `/var/lib/abpm/` |

---

### AppImage Integration

When installing an AppImage (`abpm install --appimage <file>`):
1. **FUSE Preflight Check**: Detects if `fuse-libs` is present. If missing, it offers to install it via DNF automatically in system mode, or advises the user in user mode.
2. **File Deployment**: Copies the binary into the application directory and sets executable permissions (`chmod +x`).
3. **PATH Symlink**: Creates a clean symlink in the user's or system `$PATH` directory.
4. **Icon Extraction**: Runs `--appimage-extract` to unpack `.DirIcon` and top-level `.png` assets from the embedded SquashFS image into `~/.Applications/.icons/` (or `/opt/applications/.icons/`).
5. **Desktop Launcher**: Generates a standard FreeDesktop `.desktop` entry.
6. **Manifest Registry**: Saves metadata in `appimages.json` for deterministic tracking.

---

### Archive (Tar / Zip) Integration

When installing an archive (`abpm install --tar <file>`):
1. **Native In-Memory Extraction**: Uses `libarchive` to safely stream files to `~/Applications/<app>/` or `/opt/applications/<app>/`.
2. **ELF Executable Discovery**:
   - Recursively detects valid Linux ELF binaries and `.sh` launcher scripts.
   - Filters out non-executable assets, libraries (`.so`), background helpers (`chrome-sandbox`, `crashpad_handler`), and dependency folders (`node_modules/`, `lib/`, `tests/`).
   - If multiple executables are identified, creates corresponding `.desktop` launchers and symlinks for each.
3. **Smart PNG Icon Detection & Interactive Menu**:
   - Scans the archive for legitimate PNG application icons, calculating relevance scores based on filename similarity, application depth, and well-known conventions (`code.png`, `app.png`, `icon.png`, `logo.png`).
   - Strictly ignores vector SVG assets to ensure optimal rendering across GNOME, KDE, and other desktop docks.
   - If multiple candidate icons are detected and `--yes` was not provided, an interactive selection menu allows the user to choose the desired launcher icon.
4. **Manifest Registry**: Saves tracking records in `archives.json`.

---

### Icon Resolution & Storage

- All standalone application icons are stored centrally in `.icons/` inside the application base directory (`~/Applications/.icons/` or `/opt/applications/.icons/`).
- Filenames are normalized (e.g. spaces replaced by hyphens) to ensure compatibility with all desktop shell parsers.
- Changing icons via `abpm set-icon` immediately updates the central `.icons/` cache and modifies the `Icon=` attribute in all relevant `.desktop` entries.

---

## Logging & Output Conventions

`abpm` uses a standardized, colored C++ logging engine (`abpm::core::Logger`):

- **`[INFO]`** (Arrera Blue / Cyan): Progress messages, extraction notices, repository queries.
- **`[SUCCÈS]`** (Green): Successful operations, completed installations, and registry updates.
- **`[ATTENTION]`** (Yellow): Non-fatal notices (e.g. missing optional FUSE library, missing candidate icon fallback).
- **`[ERREUR]`** (Red): Fatal errors, missing dependencies, or unfound packages.
