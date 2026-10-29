# Arrera Blue Package Manager (`abpm`) - Documentation

The **Arrera Blue Package Manager (`abpm`)** is a next-generation unified package management CLI written in native C++20, purpose-built for the **Arrera Blue OS** distribution (Fedora Linux base).

It seamlessly consolidates system packages (**DNF/RPM** via `libdnf5`), sandboxed desktop applications (**Flatpak** via `libflatpak`), standalone **AppImages**, and compressed archive applications (**Tarballs and Zip** via `libarchive`).

---

## Table of Contents

1. [Architecture & Design](#architecture--design)
2. [Prerequisites & Build](#prerequisites--build)
3. [Command Reference](#command-reference)
   - [update & check](#1-system-updates)
   - [install](#2-installation)
   - [remove](#3-uninstallation)
   - [search](#4-search)
   - [discover](#5-discover-trending-applications)
   - [list](#6-listing-installed-packages)
   - [set-icon](#7-icon-management)
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
Synchronizes repositories and upgrades all eligible DNF packages and Flatpak runtimes/applications in a single pass.
```bash
abpm update
abpm update -y    # Bypass confirmation prompts
```

#### `abpm check`
Refreshes repository metadata and displays a combined overview of all pending updates from both DNF and Flatpak without modifying the system.
```bash
abpm check
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

---

### 3. Uninstallation

#### `abpm remove <PACKAGE...>`
Uninstalls one or more applications regardless of their source format. `abpm` automatically detects whether the target is an Archive application, AppImage, Flatpak, or DNF package.
```bash
# Remove multiple packages across different formats simultaneously
abpm remove arduino-ide antigravity-ide vlc htop
```

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
