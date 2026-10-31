#include "abpm/list/list_backend.hpp"
#include "abpm/core/logger.hpp"

#include <libdnf5/base/base.hpp>
#include <libdnf5/repo/repo_sack.hpp>
#include <libdnf5/rpm/package_query.hpp>

#include <flatpak.h>
#include <glib.h>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <algorithm>
#include <unistd.h>

namespace abpm::list {

namespace fs = std::filesystem;
using core::InstalledApp;
using cli::SourceFilter;

namespace {

// Utilitaires système critiques Arrera Blue / GNOME (ne doivent pas être supprimables dans Blue Hub)
const std::unordered_set<std::string> SYSTEM_PROTECTED_PACKAGES = {
    "arrera-gnome-control-center",
    "gnome-control-center",
    "nautilus",
    "ptyxis",
    "gnome-terminal",
    "gnome-shell",
    "gnome-tour",
    "gnome-extensions-app",
    "gnome-disk-utility",
    "gnome-session",
    "gdm",
    "mutter"
};

std::string escape_json(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b";  break;
            case '\f': o << "\\f";  break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    o << "\\u00" << std::hex << (int)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

// 1. Collecte des applications DNF via les fichiers .desktop de /usr/share/applications
std::vector<InstalledApp> collect_dnf_apps() {
    std::vector<InstalledApp> apps;
    const fs::path app_dir = "/usr/share/applications";
    if (!fs::exists(app_dir)) return apps;

    try {
        libdnf5::Base base;
        base.load_config();
        base.setup();
        base.get_repo_sack()->load_repos(libdnf5::repo::Repo::Type::SYSTEM);

        std::unordered_set<std::string> seen_packages;

        for (const auto& entry : fs::directory_iterator(app_dir)) {
            if (entry.path().extension() != ".desktop") continue;

            GKeyFile* kf = g_key_file_new();
            if (!g_key_file_load_from_file(kf, entry.path().c_str(), G_KEY_FILE_NONE, nullptr)) {
                g_key_file_free(kf);
                continue;
            }

            gchar* type = g_key_file_get_string(kf, "Desktop Entry", "Type", nullptr);
            gboolean nodisplay = g_key_file_get_boolean(kf, "Desktop Entry", "NoDisplay", nullptr);
            if (!type || std::string(type) != "Application" || nodisplay) {
                if (type) g_free(type);
                g_key_file_free(kf);
                continue;
            }
            g_free(type);

            gchar* raw_name = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", nullptr, nullptr);
            gchar* raw_icon = g_key_file_get_string(kf, "Desktop Entry", "Icon", nullptr);

            std::string app_name = raw_name ? raw_name : entry.path().stem().string();
            std::string app_icon = raw_icon ? raw_icon : "";
            if (raw_name) g_free(raw_name);
            if (raw_icon) g_free(raw_icon);
            g_key_file_free(kf);

            // Recherche du paquet RPM propriétaire du fichier .desktop
            libdnf5::rpm::PackageQuery q(base);
            q.filter_file(entry.path().string());
            if (!q.empty()) {
                auto pkg = *q.begin();
                std::string pkg_name = pkg.get_name();

                // Éviter les doublons si un même paquet fournit plusieurs entrées .desktop
                if (!seen_packages.insert(pkg_name).second) {
                    continue;
                }

                InstalledApp item;
                std::string stem = entry.path().stem().string();
                item.id = stem;
                item.name = app_name;
                item.version = pkg.get_evr();
                item.source = "dnf";
                item.icon = app_icon.empty() ? pkg_name : app_icon;
                item.package = pkg_name;

                // Vérification du statut système protégé
                item.is_system = (SYSTEM_PROTECTED_PACKAGES.find(pkg_name) != SYSTEM_PROTECTED_PACKAGES.end()) ||
                                 (stem == "org.gnome.Settings" || stem == "org.gnome.Nautilus" || stem == "org.gnome.Ptyxis");

                apps.push_back(std::move(item));
            }
        }
    } catch (...) {}

    return apps;
}

// 2. Collecte des applications Flatpak
void collect_flatpak_from_installation(FlatpakInstallation* inst, std::vector<InstalledApp>& out_apps) {
    if (!inst) return;

    g_autoptr(GError) err = nullptr;
    g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs_by_kind(
        inst, FLATPAK_REF_KIND_APP, nullptr, &err);
    if (!refs) return;

    for (guint i = 0; i < refs->len; ++i) {
        auto* ref = FLATPAK_INSTALLED_REF(g_ptr_array_index(refs, i));
        const char* raw_id = flatpak_ref_get_name(FLATPAK_REF(ref));
        const char* raw_name = flatpak_installed_ref_get_appdata_name(ref);
        const char* raw_ver = flatpak_installed_ref_get_appdata_version(ref);

        if (!raw_id) continue;

        InstalledApp item;
        item.id = raw_id;
        item.name = raw_name ? raw_name : raw_id;
        item.version = raw_ver ? raw_ver : "inconnue";
        item.source = "flatpak";
        item.icon = raw_id;
        item.package = raw_id;
        item.is_system = false;

        out_apps.push_back(std::move(item));
    }
}

std::vector<InstalledApp> collect_flatpak_apps() {
    std::vector<InstalledApp> apps;

    g_autoptr(GError) err_sys = nullptr;
    g_autoptr(FlatpakInstallation) sys_inst = flatpak_installation_new_system(nullptr, &err_sys);
    collect_flatpak_from_installation(sys_inst, apps);

    g_autoptr(GError) err_user = nullptr;
    g_autoptr(FlatpakInstallation) user_inst = flatpak_installation_new_user(nullptr, &err_user);
    collect_flatpak_from_installation(user_inst, apps);

    return apps;
}

// 3. Helper de lecture des manifestes JSON simples (AppImage & Archives)
struct ManifestAppEntry {
    std::string name;
    std::string desktop_path;
    std::string version;
};

std::vector<ManifestAppEntry> read_manifest(const fs::path& manifest_path) {
    std::vector<ManifestAppEntry> entries;
    if (!fs::exists(manifest_path)) return entries;

    std::ifstream file(manifest_path);
    if (!file.is_open()) return entries;

    std::string line;
    ManifestAppEntry current;
    bool in_entry = false;

    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            current = ManifestAppEntry{};
            in_entry = true;
        } else if (line.find('}') != std::string::npos) {
            if (in_entry && !current.name.empty()) {
                entries.push_back(current);
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
            else if (line.find("\"desktop_path\"") != std::string::npos) current.desktop_path = extract_val("desktop_path");
            else if (line.find("\"version\"") != std::string::npos) current.version = extract_val("version");
        }
    }
    return entries;
}

std::string extract_icon_from_desktop(const std::string& desktop_path, const std::string& fallback) {
    if (desktop_path.empty() || !fs::exists(desktop_path)) return fallback;
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, desktop_path.c_str(), G_KEY_FILE_NONE, nullptr)) {
        gchar* icon = g_key_file_get_string(kf, "Desktop Entry", "Icon", nullptr);
        if (icon) {
            std::string res = icon;
            g_free(icon);
            g_key_file_free(kf);
            return res;
        }
        g_key_file_free(kf);
    }
    return fallback;
}

// 4. Collecte des AppImages
std::vector<InstalledApp> collect_appimage_apps() {
    std::vector<InstalledApp> apps;
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "";

    std::vector<fs::path> paths = {
        fs::path(home_str) / ".local/share/abpm/appimages.json",
        "/var/lib/abpm/appimages.json"
    };

    for (const auto& p : paths) {
        auto entries = read_manifest(p);
        for (const auto& e : entries) {
            InstalledApp item;
            item.id = e.name;
            item.name = e.name;
            item.version = e.version.empty() ? "standalone" : e.version;
            item.source = "appimage";
            item.icon = extract_icon_from_desktop(e.desktop_path, e.name);
            item.package = e.name;
            item.is_system = false;
            apps.push_back(std::move(item));
        }
    }
    return apps;
}

// 5. Collecte des applications Archives (tar/zip)
std::vector<InstalledApp> collect_archive_apps() {
    std::vector<InstalledApp> apps;
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "";

    std::vector<fs::path> paths = {
        fs::path(home_str) / ".local/share/abpm/archives.json",
        "/var/lib/abpm/archives.json"
    };

    for (const auto& p : paths) {
        auto entries = read_manifest(p);
        for (const auto& e : entries) {
            InstalledApp item;
            item.id = e.name;
            item.name = e.name;
            item.version = e.version.empty() ? "standalone" : e.version;
            item.source = "archive";
            item.icon = extract_icon_from_desktop(e.desktop_path, e.name);
            item.package = e.name;
            item.is_system = false;
            apps.push_back(std::move(item));
        }
    }
    return apps;
}

} // anonymous namespace

std::vector<InstalledApp> ListBackend::get_installed_apps(SourceFilter filter) {
    std::vector<InstalledApp> all_apps;

    if (filter == SourceFilter::ALL || filter == SourceFilter::FLATPAK) {
        auto flatpaks = collect_flatpak_apps();
        all_apps.insert(all_apps.end(), flatpaks.begin(), flatpaks.end());
    }

    if (filter == SourceFilter::ALL || filter == SourceFilter::APPIMAGE) {
        auto appimages = collect_appimage_apps();
        all_apps.insert(all_apps.end(), appimages.begin(), appimages.end());
    }

    if (filter == SourceFilter::ALL || filter == SourceFilter::TAR) {
        auto archives = collect_archive_apps();
        all_apps.insert(all_apps.end(), archives.begin(), archives.end());
    }

    if (filter == SourceFilter::ALL || filter == SourceFilter::DNF) {
        auto dnfs = collect_dnf_apps();
        all_apps.insert(all_apps.end(), dnfs.begin(), dnfs.end());
    }

    return all_apps;
}

std::string ListBackend::to_json(const std::vector<InstalledApp>& apps) {
    std::ostringstream ss;
    ss << "[\n";
    for (size_t i = 0; i < apps.size(); ++i) {
        const auto& app = apps[i];
        ss << "  {\n";
        ss << "    \"id\": \"" << escape_json(app.id) << "\",\n";
        ss << "    \"name\": \"" << escape_json(app.name) << "\",\n";
        ss << "    \"version\": \"" << escape_json(app.version) << "\",\n";
        ss << "    \"source\": \"" << escape_json(app.source) << "\",\n";
        ss << "    \"icon\": \"" << escape_json(app.icon) << "\",\n";
        ss << "    \"package\": \"" << escape_json(app.package) << "\",\n";
        ss << "    \"is_system\": " << (app.is_system ? "true" : "false") << "\n";
        ss << "  }" << (i + 1 < apps.size() ? "," : "") << "\n";
    }
    ss << "]\n";
    return ss.str();
}

void ListBackend::print_terminal_apps(const std::vector<InstalledApp>& apps) {
    std::cout << "\n--- Applications Installées (" << apps.size() << ") ---\n\n";
    for (const auto& app : apps) {
        std::string source_badge = "[" + app.source + "]";
        std::cout << " • " << "\033[1m" << std::left << std::setw(32) << app.name << "\033[0m "
                  << std::setw(12) << source_badge << " "
                  << "v" << std::setw(16) << app.version << " "
                  << "(paquet: " << app.package << ")"
                  << (app.is_system ? " \033[1;33m[Système protégé]\033[0m" : "")
                  << "\n";
    }
    std::cout << "\n";
}

} // namespace abpm::list