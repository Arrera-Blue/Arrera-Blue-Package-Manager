#include "abpm/appimage/appimage_backend.hpp"
#include "abpm/dnf/dnf_backend.hpp"
#include "abpm/core/logger.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <unistd.h>

namespace abpm::appimage {

namespace fs = std::filesystem;
using core::Logger;
using core::PackageInfo;
using core::PackageSource;

namespace {

struct AppImageRecord {
    std::string name;
    std::string file_path;
    std::string symlink_path;
    std::string desktop_path;
    std::string version;
    bool is_system{false};
};

// Récupération des chemins selon le contexte (root vs utilisateur)
struct AppImagePaths {
    fs::path apps_dir;
    fs::path icons_dir;
    fs::path bin_dir;
    fs::path desktop_dir;
    fs::path manifest_dir;
    fs::path manifest_file;
    bool is_system{false};
};

AppImagePaths get_paths(bool force_system = false) {
    AppImagePaths p;
    p.is_system = force_system || (geteuid() == 0);

    if (p.is_system) {
        p.apps_dir = "/opt/applications";
        p.icons_dir = p.apps_dir / ".icons";
        p.bin_dir = "/usr/local/bin";
        p.desktop_dir = "/usr/share/applications";
        p.manifest_dir = "/var/lib/abpm";
        p.manifest_file = p.manifest_dir / "appimages.json";
    } else {
        const char* home = std::getenv("HOME");
        std::string home_str = home ? home : "";
        p.apps_dir = home_str + "/Applications";
        p.icons_dir = p.apps_dir / ".icons";
        p.bin_dir = home_str + "/.local/bin";
        p.desktop_dir = home_str + "/.local/share/applications";
        p.manifest_dir = home_str + "/.local/share/abpm";
        p.manifest_file = p.manifest_dir / "appimages.json";
    }
    return p;
}

// Nettoie le nom de l'application à partir du nom du fichier
// Ex: "cursor-0.42.3-x86_64.AppImage" -> name: "cursor", version: "0.42.3"
std::pair<std::string, std::string> parse_appimage_filename(const std::string& filename) {
    std::string stem = fs::path(filename).stem().string();

    // Retirer les suffixes d'architectures courants
    const std::vector<std::string> arch_suffixes = {
        "-x86_64", "_x86_64", ".x86_64",
        "-amd64", "_amd64", ".amd64",
        "-aarch64", "_aarch64",
        "-i386", "-x86"
    };
    for (const auto& suffix : arch_suffixes) {
        if (stem.size() >= suffix.size() &&
            stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
            stem = stem.substr(0, stem.size() - suffix.size());
            break;
        }
    }

    // Séparer nom et version si un tiret ou underscore sépare des chiffres
    std::string name = stem;
    std::string version = "standalone";

    size_t first_digit = stem.find_first_of("0123456789");
    if (first_digit != std::string::npos && first_digit > 1) {
        char sep = stem[first_digit - 1];
        if (sep == '-' || sep == '_') {
            name = stem.substr(0, first_digit - 1);
            version = stem.substr(first_digit);
        }
    }

    // Nom en minuscules
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    return {name, version};
}

// Lecture des enregistrements dans un fichier JSON simple
std::vector<AppImageRecord> load_manifest(const fs::path& manifest_path, bool is_system) {
    std::vector<AppImageRecord> records;
    if (!fs::exists(manifest_path)) return records;

    std::ifstream file(manifest_path);
    if (!file.is_open()) return records;

    std::string line;
    AppImageRecord current;
    bool in_entry = false;

    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            current = AppImageRecord{};
            current.is_system = is_system;
            in_entry = true;
        } else if (line.find('}') != std::string::npos) {
            if (in_entry && !current.name.empty()) {
                records.push_back(current);
            }
            in_entry = false;
        } else if (in_entry) {
            auto extract_val = [&](const std::string& key) -> std::string {
                size_t kpos = line.find("\"" + key + "\"");
                if (kpos == std::string::npos) return "";
                size_t colon = line.find(':', kpos);
                if (colon == std::string::npos) return "";
                size_t first_quote = line.find('"', colon);
                if (first_quote == std::string::npos) return "";
                size_t second_quote = line.find('"', first_quote + 1);
                if (second_quote == std::string::npos) return "";
                return line.substr(first_quote + 1, second_quote - first_quote - 1);
            };

            if (line.find("\"name\"") != std::string::npos) current.name = extract_val("name");
            else if (line.find("\"file_path\"") != std::string::npos) current.file_path = extract_val("file_path");
            else if (line.find("\"symlink_path\"") != std::string::npos) current.symlink_path = extract_val("symlink_path");
            else if (line.find("\"desktop_path\"") != std::string::npos) current.desktop_path = extract_val("desktop_path");
            else if (line.find("\"version\"") != std::string::npos) current.version = extract_val("version");
        }
    }
    return records;
}

// Sauvegarde des enregistrements
void save_manifest(const fs::path& manifest_path, const std::vector<AppImageRecord>& records) {
    fs::create_directories(manifest_path.parent_path());
    std::ofstream file(manifest_path);
    if (!file.is_open()) return;

    file << "[\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        file << "  {\n";
        file << "    \"name\": \"" << r.name << "\",\n";
        file << "    \"file_path\": \"" << r.file_path << "\",\n";
        file << "    \"symlink_path\": \"" << r.symlink_path << "\",\n";
        file << "    \"desktop_path\": \"" << r.desktop_path << "\",\n";
        file << "    \"version\": \"" << r.version << "\"\n";
        file << "  }" << (i + 1 < records.size() ? "," : "") << "\n";
    }
    file << "]\n";
}

// Extrait l'icône embarquée dans l'AppImage
fs::path extract_appimage_icon(const fs::path& appimage_path, const fs::path& icons_dir, const std::string& app_name) {
    fs::create_directories(icons_dir);
    fs::path temp_dir = fs::temp_directory_path() / ("abpm_icon_" + app_name);
    try { fs::remove_all(temp_dir); } catch (...) {}
    fs::create_directories(temp_dir);

    std::string base_cmd = "cd \"" + temp_dir.string() + "\" && \"" + appimage_path.string() + "\"";

    // 1. Extraction ciblée standard Type 2 (.DirIcon, *.png, *.svg)
    std::system((base_cmd + " --appimage-extract \".DirIcon*\" >/dev/null 2>&1").c_str());
    std::system((base_cmd + " --appimage-extract \"*.png\" >/dev/null 2>&1").c_str());
    std::system((base_cmd + " --appimage-extract \"*.svg\" >/dev/null 2>&1").c_str());
    std::system((base_cmd + " --appimage-extract \"usr/share/icons/*\" >/dev/null 2>&1").c_str());

    fs::path sq_root = temp_dir / "squashfs-root";
    if (!fs::exists(sq_root)) {
        // Fallback complet pour les AppImages plus anciennes ne gérant pas les motifs
        std::system((base_cmd + " --appimage-extract >/dev/null 2>&1").c_str());
    }

    fs::path found_icon;
    std::error_code ec;

    if (fs::exists(sq_root, ec)) {
        // A. Priorité 1 : .DirIcon officiel (spécification AppImage)
        fs::path dir_icon = sq_root / ".DirIcon";
        if (fs::exists(dir_icon, ec) && fs::is_regular_file(dir_icon, ec)) {
            found_icon = dir_icon;
        }

        // B. Priorité 2 : Recherche récursive d'un .png ou .svg
        if (found_icon.empty()) {
            for (const auto& entry : fs::recursive_directory_iterator(sq_root, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                std::string ext = entry.path().extension().string();
                if (ext == ".png" || ext == ".svg") {
                    std::string fname = entry.path().filename().string();
                    std::transform(fname.begin(), fname.end(), fname.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (fname.find(app_name) != std::string::npos || fname.find("icon") != std::string::npos) {
                        found_icon = entry.path();
                        break;
                    }
                    if (found_icon.empty()) {
                        found_icon = entry.path();
                    }
                }
            }
        }
    }

    fs::path result_icon;
    if (!found_icon.empty()) {
        std::string ext = found_icon.extension().string();
        if (ext.empty() || ext == ".DirIcon") ext = ".png";

        std::string safe_name = app_name;
        std::replace(safe_name.begin(), safe_name.end(), ' ', '-');
        fs::path dest = icons_dir / (safe_name + ext);

        try {
            fs::copy_file(found_icon, dest, fs::copy_options::overwrite_existing);
            result_icon = dest;
        } catch (...) {}
    }

    // Nettoyage complet et sécurisé du dossier temporaire
    std::string clean_cmd = "rm -rf \"" + temp_dir.string() + "\" >/dev/null 2>&1";
    std::system(clean_cmd.c_str());

    return result_icon;
}

} // anonymous namespace

bool AppImageBackend::install(const std::string& file_path, bool assumeyes, const std::string& custom_icon) {
    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        Logger::error("Fichier AppImage introuvable : '" + file_path + "'");
        return false;
    }

    auto [app_name, version] = parse_appimage_filename(file_path);
    auto paths = get_paths();

    // Vérification intelligente de la présence de FUSE (fuse-libs)
    if (!dnf::DnfBackend::is_installed("fuse-libs")) {
        Logger::warning("La bibliothèque système 'fuse-libs' est requise pour exécuter les AppImages.");
        if (paths.is_system) {
            Logger::info("Installation automatique de 'fuse-libs' via DNF...");
            dnf::DnfBackend::install("fuse-libs", true);
        } else {
            Logger::info("Pour que votre application puisse démarrer, installez-la avec : 'sudo abpm install fuse-libs'");
        }
    }

    std::cout << "\n--- Installation AppImage ---\n";
    std::cout << " • Application : " << app_name << "\n";
    std::cout << " • Version     : " << version << "\n";
    std::cout << " • Destination : " << (paths.is_system ? "/opt/applications/" : "~/Applications/") << "\n";
    std::cout << " • Contexte    : " << (paths.is_system ? "Système (administrateur)" : "Utilisateur local") << "\n\n";

    if (!assumeyes) {
        if (!Logger::confirm("Voulez-vous installer cette application AppImage ?")) {
            Logger::info("Installation annulée.");
            return true;
        }
    }

    try {
        // 1. Créer les répertoires nécessaires
        fs::create_directories(paths.apps_dir);
        fs::create_directories(paths.bin_dir);
        fs::create_directories(paths.desktop_dir);

        // 2. Préparer le fichier destination
        fs::path target_appimage = paths.apps_dir / (app_name + ".AppImage");
        fs::path canonical_src = fs::canonical(file_path);

        Logger::info("Copie de l'AppImage vers " + target_appimage.string() + "...");
        fs::copy_file(canonical_src, target_appimage, fs::copy_options::overwrite_existing);

        // 3. Rendre exécutable (chmod +x)
        fs::permissions(target_appimage,
            fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
            fs::perms::group_read | fs::perms::group_exec |
            fs::perms::others_read | fs::perms::others_exec,
            fs::perm_options::replace);

        // 4. Créer le lien symbolique dans le PATH
        fs::path target_symlink = paths.bin_dir / app_name;
        if (fs::exists(target_symlink) || fs::is_symlink(target_symlink)) {
            fs::remove(target_symlink);
        }
        fs::create_symlink(target_appimage, target_symlink);
        Logger::info("Lien symbolique créé : " + target_symlink.string());

        // 5. Extraction et configuration de l'icône
        fs::create_directories(paths.icons_dir);
        fs::path extracted_icon;
        if (!custom_icon.empty() && fs::exists(custom_icon) && fs::is_regular_file(custom_icon)) {
            std::string ext = fs::path(custom_icon).extension().string();
            std::string safe_name = app_name;
            std::replace(safe_name.begin(), safe_name.end(), ' ', '-');
            fs::path dest = paths.icons_dir / (safe_name + ext);
            try {
                fs::copy_file(custom_icon, dest, fs::copy_options::overwrite_existing);
                extracted_icon = dest;
            } catch (...) {
                extracted_icon = custom_icon;
            }
        } else {
            extracted_icon = extract_appimage_icon(target_appimage, paths.icons_dir, app_name);
        }
        std::string icon_str = !extracted_icon.empty() ? extracted_icon.string() : "application-x-executable";
        if (!extracted_icon.empty()) {
            Logger::info("Icône configurée : " + extracted_icon.string());
        }
        // 6. Créer le fichier .desktop pour le bureau
        fs::path target_desktop = paths.desktop_dir / (app_name + ".desktop");
        {
            std::ofstream df(target_desktop);
            df << "[Desktop Entry]\n";
            df << "Name=" << app_name << "\n";
            df << "Exec=" << target_appimage.string() << "\n";
            df << "Icon=" << icon_str << "\n";
            df << "Type=Application\n";
            df << "Terminal=false\n";
            df << "Categories=Utility;Application;\n";
            df << "Comment=Application AppImage installée via abpm\n";
        }
        Logger::info("Lanceur bureau créé : " + target_desktop.string());

        // 6. Mettre à jour la base de données locale
        auto records = load_manifest(paths.manifest_file, paths.is_system);
        // Supprimer si déjà présent pour mise à jour
        records.erase(std::remove_if(records.begin(), records.end(),
            [&](const AppImageRecord& r) { return r.name == app_name; }), records.end());

        AppImageRecord rec;
        rec.name = app_name;
        rec.file_path = target_appimage.string();
        rec.symlink_path = target_symlink.string();
        rec.desktop_path = target_desktop.string();
        rec.version = version;
        rec.is_system = paths.is_system;
        records.push_back(std::move(rec));

        save_manifest(paths.manifest_file, records);

        Logger::success("Application '" + app_name + "' installée avec succès !");
        return true;

    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur lors de l'installation AppImage : ") + e.what());
        return false;
    }
}

std::vector<PackageInfo> AppImageBackend::list_installed() {
    std::vector<PackageInfo> results;

    // Charger les AppImages utilisateur
    auto user_paths = get_paths(false);
    auto user_records = load_manifest(user_paths.manifest_file, false);
    for (const auto& r : user_records) {
        PackageInfo info;
        info.name = r.name;
        info.id = r.file_path;
        info.version = r.version;
        info.arch = "x86_64";
        info.source = PackageSource::APPIMAGE;
        info.origin = "appimage (user)";
        results.push_back(std::move(info));
    }

    // Charger les AppImages système (/opt/applications)
    auto sys_paths = get_paths(true);
    auto sys_records = load_manifest(sys_paths.manifest_file, true);
    for (const auto& r : sys_records) {
        PackageInfo info;
        info.name = r.name;
        info.id = r.file_path;
        info.version = r.version;
        info.arch = "x86_64";
        info.source = PackageSource::APPIMAGE;
        info.origin = "appimage (système)";
        results.push_back(std::move(info));
    }

    return results;
}

bool AppImageBackend::is_installed(std::string_view name) {
    auto user_paths = get_paths(false);
    auto user_records = load_manifest(user_paths.manifest_file, false);
    for (const auto& r : user_records) {
        if (r.name == name) return true;
    }

    auto sys_paths = get_paths(true);
    auto sys_records = load_manifest(sys_paths.manifest_file, true);
    for (const auto& r : sys_records) {
        if (r.name == name) return true;
    }

    return false;
}

bool AppImageBackend::remove(const std::vector<std::string>& package_names) {
    if (package_names.empty()) return true;

    bool all_ok = true;

    for (const auto& pkg : package_names) {
        bool found = false;

        // 1. Chercher dans l'espace utilisateur
        auto user_paths = get_paths(false);
        auto user_records = load_manifest(user_paths.manifest_file, false);
        auto uit = std::find_if(user_records.begin(), user_records.end(),
            [&](const AppImageRecord& r) { return r.name == pkg; });

        if (uit != user_records.end()) {
            found = true;
            try {
                if (fs::exists(uit->file_path)) fs::remove(uit->file_path);
                if (fs::exists(uit->symlink_path) || fs::is_symlink(uit->symlink_path)) fs::remove(uit->symlink_path);
                if (fs::exists(uit->desktop_path)) fs::remove(uit->desktop_path);

                user_records.erase(uit);
                save_manifest(user_paths.manifest_file, user_records);
                Logger::success("AppImage utilisateur '" + pkg + "' désinstallée avec succès.");
            } catch (const std::exception& e) {
                Logger::error("Erreur lors de la désinstallation de '" + pkg + "': " + e.what());
                all_ok = false;
            }
        }

        // 2. Chercher dans l'espace système
        auto sys_paths = get_paths(true);
        auto sys_records = load_manifest(sys_paths.manifest_file, true);
        auto sit = std::find_if(sys_records.begin(), sys_records.end(),
            [&](const AppImageRecord& r) { return r.name == pkg; });

        if (sit != sys_records.end()) {
            found = true;
            if (geteuid() != 0) {
                Logger::warning("L'AppImage '" + pkg + "' est installée au niveau système.");
                Logger::info("Relancez avec sudo pour la supprimer : 'sudo abpm remove " + pkg + "'");
                all_ok = false;
                continue;
            }

            try {
                if (fs::exists(sit->file_path)) fs::remove(sit->file_path);
                if (fs::exists(sit->symlink_path) || fs::is_symlink(sit->symlink_path)) fs::remove(sit->symlink_path);
                if (fs::exists(sit->desktop_path)) fs::remove(sit->desktop_path);

                sys_records.erase(sit);
                save_manifest(sys_paths.manifest_file, sys_records);
                Logger::success("AppImage système '" + pkg + "' désinstallée avec succès.");
            } catch (const std::exception& e) {
                Logger::error("Erreur lors de la désinstallation de '" + pkg + "': " + e.what());
                all_ok = false;
            }
        }

        if (!found) {
            Logger::error("Aucune AppImage nommée '" + pkg + "' n'a été trouvée.");
            all_ok = false;
        }
    }

    return all_ok;
}

bool AppImageBackend::set_icon(const std::string& name, const std::string& icon_path) {
    if (!fs::exists(icon_path)) {
        Logger::error("Fichier d'icône introuvable : " + icon_path);
        return false;
    }

    auto user_paths = get_paths(false);
    auto user_records = load_manifest(user_paths.manifest_file, false);
    auto uit = std::find_if(user_records.begin(), user_records.end(),
        [&](const AppImageRecord& r) { return r.name == name; });

    bool is_system = false;
    fs::path desktop_path;
    fs::path icons_dir;

    if (uit != user_records.end()) {
        desktop_path = uit->desktop_path;
        icons_dir = user_paths.icons_dir;
    } else {
        auto sys_paths = get_paths(true);
        auto sys_records = load_manifest(sys_paths.manifest_file, true);
        auto sit = std::find_if(sys_records.begin(), sys_records.end(),
            [&](const AppImageRecord& r) { return r.name == name; });
        if (sit != sys_records.end()) {
            is_system = true;
            desktop_path = sit->desktop_path;
            icons_dir = sys_paths.icons_dir;
        } else {
            return false;
        }
    }

    if (is_system && geteuid() != 0) {
        Logger::warning("L'application '" + name + "' est installée au niveau système.");
        Logger::info("Relancez avec sudo : 'sudo abpm set-icon " + name + " " + icon_path + "'");
        return false;
    }

    try {
        fs::create_directories(icons_dir);
        std::string ext = fs::path(icon_path).extension().string();
        std::string safe_name = name;
        std::replace(safe_name.begin(), safe_name.end(), ' ', '-');
        fs::path dest_icon = icons_dir / (safe_name + ext);
        fs::copy_file(icon_path, dest_icon, fs::copy_options::overwrite_existing);

        // Mettre à jour le fichier .desktop
        if (fs::exists(desktop_path)) {
            std::ifstream df_in(desktop_path);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(df_in, line)) {
                if (line.starts_with("Icon=")) {
                    lines.push_back("Icon=" + dest_icon.string());
                } else {
                    lines.push_back(line);
                }
            }
            std::ofstream df_out(desktop_path);
            for (const auto& l : lines) df_out << l << "\n";
        }

        Logger::success("Icône de l'AppImage '" + name + "' mise à jour avec succès !");
        Logger::info("Nouvelle icône : " + dest_icon.string());
        return true;
    } catch (const std::exception& e) {
        Logger::error("Erreur lors du changement d'icône : " + std::string(e.what()));
        return false;
    }
}

} // namespace abpm::appimage