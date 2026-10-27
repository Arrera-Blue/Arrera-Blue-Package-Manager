#include "abpm/archive/archive_backend.hpp"
#include "abpm/core/logger.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <unistd.h>

namespace abpm::archive {

namespace fs = std::filesystem;
using core::Logger;
using core::PackageInfo;
using core::PackageSource;

namespace {

struct ArchiveRecord {
    std::string name;
    std::string install_dir;
    std::string binary_path;
    std::string symlink_path;
    std::string desktop_path;
    std::string version;
    bool is_system{false};
};

struct ArchivePaths {
    fs::path apps_dir;
    fs::path icons_dir;
    fs::path bin_dir;
    fs::path desktop_dir;
    fs::path manifest_dir;
    fs::path manifest_file;
    bool is_system{false};
};

ArchivePaths get_paths(bool force_system = false) {
    ArchivePaths p;
    p.is_system = force_system || (geteuid() == 0);

    if (p.is_system) {
        p.apps_dir = "/opt/applications";
        p.icons_dir = p.apps_dir / ".icons";
        p.bin_dir = "/usr/local/bin";
        p.desktop_dir = "/usr/share/applications";
        p.manifest_dir = "/var/lib/abpm";
        p.manifest_file = p.manifest_dir / "archives.json";
    } else {
        const char* home = std::getenv("HOME");
        std::string home_str = home ? home : "";
        p.apps_dir = home_str + "/Applications";
        p.icons_dir = p.apps_dir / ".icons";
        p.bin_dir = home_str + "/.local/bin";
        p.desktop_dir = home_str + "/.local/share/applications";
        p.manifest_dir = home_str + "/.local/share/abpm";
        p.manifest_file = p.manifest_dir / "archives.json";
    }
    return p;
}

// Nettoie l'extension et extrait nom + version
std::pair<std::string, std::string> parse_archive_filename(const std::string& filename) {
    std::string name = fs::path(filename).filename().string();

    // Retirer les extensions composées
    const std::vector<std::string> exts = {
        ".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst", ".tar",
        ".tgz", ".txz", ".tbz2", ".zip"
    };
    for (const auto& ext : exts) {
        if (name.size() >= ext.size() &&
            name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
            name = name.substr(0, name.size() - ext.size());
            break;
        }
    }

    // Retirer les suffixes d'architectures
    const std::vector<std::string> arch_suffixes = {
        "-linux-x64", "-linux-x86_64", "_linux_x64",
        "-x86_64", "_x86_64", ".x86_64",
        "-amd64", "_amd64", ".amd64",
        "-aarch64", "_aarch64", "-arm64",
        "-i386", "-x86"
    };
    for (const auto& suffix : arch_suffixes) {
        if (name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            name = name.substr(0, name.size() - suffix.size());
            break;
        }
    }

    std::string app_name = name;
    std::string version = "standalone";

    size_t first_digit = name.find_first_of("0123456789");
    if (first_digit != std::string::npos && first_digit > 1) {
        char sep = name[first_digit - 1];
        if (sep == '-' || sep == '_') {
            app_name = name.substr(0, first_digit - 1);
            version = name.substr(first_digit);
        }
    }

    std::transform(app_name.begin(), app_name.end(), app_name.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    return {app_name, version};
}

// Extraction native avec libarchive
bool extract_archive(const std::string& archive_path, const fs::path& dest_dir) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    struct archive* ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    archive_write_disk_set_standard_lookup(ext);

    int r = archive_read_open_filename(a, archive_path.c_str(), 10240);
    if (r != ARCHIVE_OK) {
        Logger::error(std::string("Impossible d'ouvrir l'archive : ") + archive_error_string(a));
        archive_read_free(a);
        archive_write_free(ext);
        return false;
    }

    struct archive_entry* entry;
    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        fs::path entry_name = archive_entry_pathname(entry);
        fs::path full_dest = dest_dir / entry_name;

        archive_entry_set_pathname(entry, full_dest.string().c_str());

        r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK) {
            Logger::warning(std::string("Avertissement extraction : ") + archive_error_string(ext));
        } else {
            const void* buff;
            size_t size;
            la_int64_t offset;
            while ((r = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK) {
                archive_write_data_block(ext, buff, size, offset);
            }
            archive_write_finish_entry(ext);
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return true;
}

// Détection d'un binaire ELF compilé Linux
bool is_elf_binary(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return false;
    char magic[4];
    if (!f.read(magic, 4)) return false;
    return (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F');
}

// Vérifie si un chemin se trouve dans un sous-dossier interne à ignorer
bool is_blacklisted_path(const fs::path& p) {
    std::string s = p.string();
    const std::vector<std::string> blacklisted = {
        "/node_modules/",
        "/resources/",
        "/lib/",
        "/lib64/",
        "/lib32/",
        "/site-packages/",
        "/dist-packages/",
        "/.git/",
        "/obj/",
        "/CMakeFiles/"
    };
    for (const auto& b : blacklisted) {
        if (s.find(b) != std::string::npos) return true;
    }
    if (p.filename().string().starts_with('.')) return true;
    return false;
}

// Vérifie si c'est un helper interne (bac à sable Chrome, crashpad, etc.)
bool is_helper_binary(const std::string& filename) {
    const std::vector<std::string> helpers = {
        "chrome-sandbox",
        "chrome_crashpad_handler",
        "crashpad_handler",
        "elevate",
        "xdg-open",
        "xdg-mime",
        "xdg-settings"
    };
    for (const auto& h : helpers) {
        if (filename == h) return true;
    }
    if (filename.ends_with(".so") || filename.find(".so.") != std::string::npos ||
        filename.ends_with(".a") || filename.ends_with(".js") || filename.ends_with(".json")) {
        return true;
    }
    return false;
}

// Recherche tous les binaires exécutables légitimes dans l'archive
std::vector<fs::path> find_executable_binaries(const fs::path& root_dir, const std::string& app_name) {
    std::vector<fs::path> execs;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(root_dir)) {
            if (!entry.is_regular_file()) continue;
            const auto& path = entry.path();
            if (is_blacklisted_path(path)) continue;

            std::string filename = path.filename().string();
            if (is_helper_binary(filename)) continue;

            auto perms = entry.status().permissions();
            bool has_exec = (perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
            bool is_elf = is_elf_binary(path);
            bool is_script = filename.ends_with(".sh");

            if ((is_elf || is_script) && (has_exec || is_elf)) {
                execs.push_back(path);
            }
        }
    } catch (...) {}

    // Trier pour mettre en avant le binaire portant le nom de l'application
    // et privilégier les binaires les plus proches de la racine
    auto calc_depth = [](const fs::path& p) -> size_t {
        size_t d = 0;
        for (const auto& _ : p) { (void)_; ++d; }
        return d;
    };

    std::sort(execs.begin(), execs.end(), [&](const fs::path& a, const fs::path& b) {
        std::string fa = a.filename().string();
        std::string fb = b.filename().string();
        std::transform(fa.begin(), fa.end(), fa.begin(), [](unsigned char c) { return std::tolower(c); });
        std::transform(fb.begin(), fb.end(), fb.begin(), [](unsigned char c) { return std::tolower(c); });

        bool match_a = (fa == app_name || fa == (app_name + ".sh"));
        bool match_b = (fb == app_name || fb == (app_name + ".sh"));
        if (match_a != match_b) return match_a > match_b;

        return calc_depth(a) < calc_depth(b);
    });

    return execs;
}

// Recherche de fichiers .desktop fournis directement dans l'archive
std::vector<fs::path> find_bundled_desktop_files(const fs::path& root_dir) {
    std::vector<fs::path> desktops;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(root_dir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".desktop") {
                // On accepte les .desktop même dans resources s'ils sont dans un sous-dossier d'assets
                if (entry.path().string().find("/node_modules/") == std::string::npos) {
                    desktops.push_back(entry.path());
                }
            }
        }
    } catch (...) {}
    return desktops;
}

// Recherche et scoring de toutes les icônes candidates dans l'archive
std::vector<fs::path> find_candidate_icons(const fs::path& root_dir, const std::string& app_name) {
    struct ScoredIcon {
        fs::path path;
        size_t score;
        size_t depth;
    };
    std::vector<ScoredIcon> scored;
    std::error_code ec;

    auto calc_depth = [](const fs::path& p) -> size_t {
        size_t d = 0;
        for (const auto& _ : p) { (void)_; ++d; }
        return d;
    };

    // Mots-clés tirés de app_name (ex: "antigravity", "ide")
    std::vector<std::string> app_words;
    {
        std::stringstream ss(app_name);
        std::string w;
        while (ss >> w) {
            if (w.size() >= 3) app_words.push_back(w);
        }
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(root_dir, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) continue;

            const auto& path = entry.path();
            std::string full_path = path.string();

            // Ne jamais fouiller dans node_modules, git, tests, build
            if (full_path.find("/node_modules/") != std::string::npos ||
                full_path.find("/.git/") != std::string::npos ||
                full_path.find("/test/") != std::string::npos ||
                full_path.find("/tests/") != std::string::npos ||
                full_path.find("/CMakeFiles/") != std::string::npos) {
                continue;
            }

            if (!entry.is_regular_file(ec)) continue;

            std::string ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            // Pour les archives tar / zip, on ne prend que les icônes au format PNG (pas de SVG)
            if (ext != ".png") continue;

            std::string fname = path.filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), [](unsigned char c) { return std::tolower(c); });

            size_t depth = calc_depth(path);
            size_t score = 10; // score de base pour toute image png légitime

            // 1. Nom exact ou partiel de l'application
            if (fname.find(app_name) != std::string::npos) {
                score += 100;
            } else {
                for (const auto& w : app_words) {
                    if (fname.find(w) != std::string::npos) {
                        score += 80;
                    }
                }
            }

            // 2. Icônes applicatives standards
            if (fname == "code.png" || fname == "app.png" || fname == "icon.png" ||
                fname == "logo.png" || fname == "app-icon.png" || fname == "code-icon.png") {
                score += 70;
            } else if (fname.find("icon") != std::string::npos || fname.find("logo") != std::string::npos) {
                score += 40;
            } else if (full_path.find("/icons/") != std::string::npos || full_path.find("/pixmaps/") != std::string::npos) {
                score += 30;
            }

            // Pénalité pour les dossiers d'extensions ou plugins internes
            if (full_path.find("/extensions/") != std::string::npos ||
                full_path.find("/plugins/") != std::string::npos ||
                full_path.find("/addons/") != std::string::npos) {
                if (score > 30) score -= 30;
                else score = 1;
            }

            scored.push_back({path, score, depth});
        }
    } catch (...) {}

    // Trier : meilleur score d'abord, puis plus proche de la racine (profondeur minimale)
    std::sort(scored.begin(), scored.end(), [](const ScoredIcon& a, const ScoredIcon& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.depth < b.depth;
    });

    std::vector<fs::path> candidates;
    for (const auto& si : scored) {
        candidates.push_back(si.path);
    }
    return candidates;
}

// Helper pour supprimer des chemins séparés par des points-virgules
void remove_semicolon_paths(const std::string& paths_str) {
    std::stringstream ss(paths_str);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        try {
            if (fs::exists(item) || fs::is_symlink(item)) {
                fs::remove(item);
            }
        } catch (...) {}
    }
}

// Lecture des enregistrements dans un fichier JSON simple
std::vector<ArchiveRecord> load_manifest(const fs::path& manifest_path, bool is_system) {
    std::vector<ArchiveRecord> records;
    if (!fs::exists(manifest_path)) return records;

    std::ifstream file(manifest_path);
    if (!file.is_open()) return records;

    std::string line;
    ArchiveRecord current;
    bool in_entry = false;

    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            current = ArchiveRecord{};
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
            else if (line.find("\"install_dir\"") != std::string::npos) current.install_dir = extract_val("install_dir");
            else if (line.find("\"binary_path\"") != std::string::npos) current.binary_path = extract_val("binary_path");
            else if (line.find("\"symlink_path\"") != std::string::npos) current.symlink_path = extract_val("symlink_path");
            else if (line.find("\"desktop_path\"") != std::string::npos) current.desktop_path = extract_val("desktop_path");
            else if (line.find("\"version\"") != std::string::npos) current.version = extract_val("version");
        }
    }
    return records;
}

void save_manifest(const fs::path& manifest_path, const std::vector<ArchiveRecord>& records) {
    fs::create_directories(manifest_path.parent_path());
    std::ofstream file(manifest_path);
    if (!file.is_open()) return;

    file << "[\n";
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        file << "  {\n";
        file << "    \"name\": \"" << r.name << "\",\n";
        file << "    \"install_dir\": \"" << r.install_dir << "\",\n";
        file << "    \"binary_path\": \"" << r.binary_path << "\",\n";
        file << "    \"symlink_path\": \"" << r.symlink_path << "\",\n";
        file << "    \"desktop_path\": \"" << r.desktop_path << "\",\n";
        file << "    \"version\": \"" << r.version << "\"\n";
        file << "  }" << (i + 1 < records.size() ? "," : "") << "\n";
    }
    file << "]\n";
}

} // anonymous namespace

bool ArchiveBackend::install(const std::string& file_path, bool assumeyes, const std::string& custom_icon) {
    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        Logger::error("Fichier d'archive introuvable : '" + file_path + "'");
        return false;
    }

    auto [app_name, version] = parse_archive_filename(file_path);
    auto paths = get_paths();

    std::cout << "\n--- Installation Archive (Tar / Zip) ---\n";
    std::cout << " • Application : " << app_name << "\n";
    std::cout << " • Version     : " << version << "\n";
    std::cout << " • Destination : " << (paths.is_system ? ("/opt/applications/" + app_name + "/") : ("~/Applications/" + app_name + "/")) << "\n";
    std::cout << " • Contexte    : " << (paths.is_system ? "Système (administrateur)" : "Utilisateur local") << "\n\n";

    if (!assumeyes) {
        if (!Logger::confirm("Voulez-vous installer cette application depuis l'archive ?")) {
            Logger::info("Installation annulée.");
            return true;
        }
    }

    try {
        fs::path target_app_dir = paths.apps_dir / app_name;

        // Préparer les répertoires
        if (fs::exists(target_app_dir)) {
            Logger::info("Nettoyage de l'ancienne version dans " + target_app_dir.string() + "...");
            fs::remove_all(target_app_dir);
        }
        fs::create_directories(target_app_dir);
        fs::create_directories(paths.bin_dir);
        fs::create_directories(paths.desktop_dir);

        // Extraction
        Logger::info("Extraction native de l'archive en cours...");
        if (!extract_archive(file_path, target_app_dir)) {
            Logger::error("Échec de l'extraction de l'archive.");
            fs::remove_all(target_app_dir);
            return false;
        }

        // Recherche de fichiers .desktop déjà fournis dans l'archive
        auto bundled_desktops = find_bundled_desktop_files(target_app_dir);
        auto candidate_binaries = find_executable_binaries(target_app_dir, app_name);

        std::vector<std::string> created_symlinks;
        std::vector<std::string> created_desktops;
        std::string primary_binary_str;

        // Recherche et copie de l'icône dans ~/.Applications/.icons/
        fs::create_directories(paths.icons_dir);
        fs::path chosen_icon;
        std::error_code ec;

        // 1. Si une icône a été spécifiée explicitement via --icon
        if (!custom_icon.empty()) {
            fs::path cip(custom_icon);
            if (fs::exists(cip) && fs::is_regular_file(cip)) {
                chosen_icon = cip;
                Logger::info("Utilisation de l'icône spécifiée : " + chosen_icon.string());
            } else {
                // Recherche par nom de fichier à l'intérieur de l'archive extraite
                for (const auto& entry : fs::recursive_directory_iterator(target_app_dir, fs::directory_options::skip_permission_denied, ec)) {
                    if (entry.is_regular_file(ec)) {
                        std::string entry_fname = entry.path().filename().string();
                        if (entry_fname == custom_icon || entry.path().string().ends_with(custom_icon)) {
                            chosen_icon = entry.path();
                            Logger::info("Icône spécifiée trouvée dans l'archive : " + chosen_icon.string());
                            break;
                        }
                    }
                }
                if (chosen_icon.empty()) {
                    Logger::warning("L'icône spécifiée '" + custom_icon + "' est introuvable. Recherche automatique...");
                }
            }
        }

        // 2. Recherche automatique parmi les images trouvées dans l'archive
        if (chosen_icon.empty()) {
            auto candidates = find_candidate_icons(target_app_dir, app_name);

            if (candidates.size() == 1) {
                chosen_icon = candidates[0];
                Logger::info("Icône unique détectée : " + fs::relative(chosen_icon, target_app_dir).string());
            } else if (candidates.size() > 1) {
                // Mode non interactif (-y / --yes)
                if (assumeyes) {
                    chosen_icon = candidates[0];
                    Logger::info("Sélection automatique de l'icône : " + fs::relative(chosen_icon, target_app_dir).string());
                } else {
                    // Sélection interactive par l'utilisateur
                    std::cout << "\nPlusieurs icônes potentielles ont été trouvées dans l'archive :\n";
                    size_t max_show = std::min(candidates.size(), (size_t)15);
                    for (size_t i = 0; i < max_show; ++i) {
                        fs::path rel = fs::relative(candidates[i], target_app_dir);
                        std::cout << "  [" << (i + 1) << "] " << rel.string()
                                  << (i == 0 ? " (Recommandé)" : "") << "\n";
                    }
                    if (candidates.size() > max_show) {
                        std::cout << "  ... et " << (candidates.size() - max_show) << " autre(s) image(s)\n";
                    }

                    std::cout << "Choisissez le numéro de l'icône à utiliser (1-" << max_show << ") [1] : " << std::flush;
                    std::string line;
                    std::getline(std::cin, line);
                    size_t choice = 1;
                    if (!line.empty()) {
                        try {
                            choice = std::stoul(line);
                        } catch (...) {
                            choice = 1;
                        }
                    }
                    if (choice < 1 || choice > max_show) {
                        choice = 1;
                    }
                    chosen_icon = candidates[choice - 1];
                    Logger::info("Icône sélectionnée : " + fs::relative(chosen_icon, target_app_dir).string());
                }
            }
        }

        std::string default_icon_str = "application-x-executable";

        if (!chosen_icon.empty()) {
            std::string ext = chosen_icon.extension().string();
            std::string safe_name = app_name;
            std::replace(safe_name.begin(), safe_name.end(), ' ', '-');
            fs::path dest_icon = paths.icons_dir / (safe_name + ext);
            try {
                fs::copy_file(chosen_icon, dest_icon, fs::copy_options::overwrite_existing);
                default_icon_str = dest_icon.string();
                Logger::info("Icône copiée : " + dest_icon.string());
            } catch (...) {
                default_icon_str = chosen_icon.string();
            }
        }

        if (!bundled_desktops.empty()) {
            Logger::info("Fichier(s) .desktop détecté(s) dans l'archive (" + std::to_string(bundled_desktops.size()) + ")");

            for (const auto& bdf : bundled_desktops) {
                std::ifstream in_df(bdf);
                if (!in_df.is_open()) continue;

                std::string line;
                std::vector<std::string> desktop_lines;
                std::string exec_bin_name;

                while (std::getline(in_df, line)) {
                    if (line.starts_with("Exec=")) {
                        std::string exec_val = line.substr(5);
                        // Extraire le premier mot qui correspond au binaire
                        std::stringstream ss(exec_val);
                        ss >> exec_bin_name;
                        // Nettoyer si chemin
                        exec_bin_name = fs::path(exec_bin_name).filename().string();
                    }
                    desktop_lines.push_back(line);
                }

                // Trouver le binaire correspondant dans le dossier extrait
                fs::path matched_binary;
                for (const auto& cb : candidate_binaries) {
                    if (cb.filename().string() == exec_bin_name) {
                        matched_binary = cb;
                        break;
                    }
                }
                if (matched_binary.empty() && !candidate_binaries.empty()) {
                    matched_binary = candidate_binaries[0];
                }

                if (!matched_binary.empty()) {
                    fs::permissions(matched_binary,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add);

                    if (primary_binary_str.empty()) {
                        primary_binary_str = matched_binary.string();
                    }

                    // Lien symbolique
                    fs::path symlink_dest = paths.bin_dir / matched_binary.filename();
                    if (fs::exists(symlink_dest) || fs::is_symlink(symlink_dest)) {
                        fs::remove(symlink_dest);
                    }
                    fs::create_symlink(matched_binary, symlink_dest);
                    created_symlinks.push_back(symlink_dest.string());
                    Logger::info("Lien symbolique créé : " + symlink_dest.string());
                }

                // Générer le .desktop adapté
                fs::path target_desktop = paths.desktop_dir / bdf.filename();
                std::ofstream out_df(target_desktop);
                for (const auto& dl : desktop_lines) {
                    if (dl.starts_with("Exec=") && !matched_binary.empty()) {
                        out_df << "Exec=\"" << matched_binary.string() << "\" %U\n";
                    } else if (dl.starts_with("Icon=") && !chosen_icon.empty()) {
                        out_df << "Icon=" << default_icon_str << "\n";
                    } else {
                        out_df << dl << "\n";
                    }
                }
                created_desktops.push_back(target_desktop.string());
                Logger::info("Lanceur bureau configuré : " + target_desktop.string());
            }
        } else {
            // Aucun .desktop fourni : on traite les binaires exécutables détectés
            if (candidate_binaries.empty()) {
                Logger::warning("Aucun fichier exécutable évident n'a été détecté dans l'archive.");
                primary_binary_str = target_app_dir.string();
            } else {
                for (size_t i = 0; i < candidate_binaries.size(); ++i) {
                    const auto& binary = candidate_binaries[i];
                    std::string bin_name = binary.filename().string();

                    fs::permissions(binary,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add);

                    if (i == 0) {
                        primary_binary_str = binary.string();
                    }

                    // Lien symbolique dans le PATH
                    fs::path target_symlink = paths.bin_dir / bin_name;
                    if (fs::exists(target_symlink) || fs::is_symlink(target_symlink)) {
                        fs::remove(target_symlink);
                    }
                    fs::create_symlink(binary, target_symlink);
                    created_symlinks.push_back(target_symlink.string());
                    Logger::info("Lien symbolique créé : " + target_symlink.string());

                    // Fichier .desktop
                    std::string desktop_app_name = (i == 0) ? app_name : (app_name + " (" + bin_name + ")");
                    fs::path target_desktop = paths.desktop_dir / (bin_name + ".desktop");

                    {
                        std::ofstream df(target_desktop);
                        df << "[Desktop Entry]\n";
                        df << "Name=" << desktop_app_name << "\n";
                        df << "Exec=\"" << binary.string() << "\"\n";
                        df << "Icon=" << default_icon_str << "\n";
                        df << "Type=Application\n";
                        df << "Terminal=false\n";
                        df << "Categories=Utility;Application;\n";
                        df << "Comment=Application installée via archive avec abpm\n";
                    }
                    created_desktops.push_back(target_desktop.string());
                    Logger::info("Lanceur bureau créé : " + target_desktop.string());
                }
            }
        }

        // Enregistrement dans le registre
        auto records = load_manifest(paths.manifest_file, paths.is_system);
        records.erase(std::remove_if(records.begin(), records.end(),
            [&](const ArchiveRecord& r) { return r.name == app_name; }), records.end());

        // Joindre les chemins avec des points-virgules pour le stockage
        std::string all_symlinks;
        for (size_t i = 0; i < created_symlinks.size(); ++i) {
            all_symlinks += created_symlinks[i] + (i + 1 < created_symlinks.size() ? ";" : "");
        }
        std::string all_desktops;
        for (size_t i = 0; i < created_desktops.size(); ++i) {
            all_desktops += created_desktops[i] + (i + 1 < created_desktops.size() ? ";" : "");
        }

        ArchiveRecord rec;
        rec.name = app_name;
        rec.install_dir = target_app_dir.string();
        rec.binary_path = primary_binary_str;
        rec.symlink_path = all_symlinks;
        rec.desktop_path = all_desktops;
        rec.version = version;
        rec.is_system = paths.is_system;
        records.push_back(std::move(rec));

        save_manifest(paths.manifest_file, records);

        Logger::success("Application '" + app_name + "' installée avec succès depuis l'archive !");
        return true;

    } catch (const std::exception& e) {
        Logger::error(std::string("Erreur lors de l'installation de l'archive : ") + e.what());
        return false;
    }
}

std::vector<PackageInfo> ArchiveBackend::list_installed() {
    std::vector<PackageInfo> results;

    // Utilisateur
    auto user_paths = get_paths(false);
    auto user_records = load_manifest(user_paths.manifest_file, false);
    for (const auto& r : user_records) {
        PackageInfo info;
        info.name = r.name;
        info.id = r.install_dir;
        info.version = r.version;
        info.arch = "x86_64";
        info.source = PackageSource::TAR;
        info.origin = "archive (user)";
        results.push_back(std::move(info));
    }

    // Système (/opt/applications)
    auto sys_paths = get_paths(true);
    auto sys_records = load_manifest(sys_paths.manifest_file, true);
    for (const auto& r : sys_records) {
        PackageInfo info;
        info.name = r.name;
        info.id = r.install_dir;
        info.version = r.version;
        info.arch = "x86_64";
        info.source = PackageSource::TAR;
        info.origin = "archive (système)";
        results.push_back(std::move(info));
    }

    return results;
}

bool ArchiveBackend::is_installed(std::string_view name) {
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

bool ArchiveBackend::remove(const std::vector<std::string>& package_names) {
    if (package_names.empty()) return true;

    bool all_ok = true;

    for (const auto& pkg : package_names) {
        bool found = false;

        // 1. Espace utilisateur
        auto user_paths = get_paths(false);
        auto user_records = load_manifest(user_paths.manifest_file, false);
        auto uit = std::find_if(user_records.begin(), user_records.end(),
            [&](const ArchiveRecord& r) { return r.name == pkg; });

        if (uit != user_records.end()) {
            found = true;
            try {
                if (fs::exists(uit->install_dir)) fs::remove_all(uit->install_dir);
                remove_semicolon_paths(uit->symlink_path);
                remove_semicolon_paths(uit->desktop_path);

                user_records.erase(uit);
                save_manifest(user_paths.manifest_file, user_records);
                Logger::success("Application archive utilisateur '" + pkg + "' désinstallée avec succès.");
            } catch (const std::exception& e) {
                Logger::error("Erreur lors de la désinstallation de '" + pkg + "': " + e.what());
                all_ok = false;
            }
        }

        // 2. Espace système
        auto sys_paths = get_paths(true);
        auto sys_records = load_manifest(sys_paths.manifest_file, true);
        auto sit = std::find_if(sys_records.begin(), sys_records.end(),
            [&](const ArchiveRecord& r) { return r.name == pkg; });

        if (sit != sys_records.end()) {
            found = true;
            if (geteuid() != 0) {
                Logger::warning("L'application '" + pkg + "' est installée au niveau système.");
                Logger::info("Relancez avec sudo pour la supprimer : 'sudo abpm remove " + pkg + "'");
                all_ok = false;
                continue;
            }

            try {
                if (fs::exists(sit->install_dir)) fs::remove_all(sit->install_dir);
                remove_semicolon_paths(sit->symlink_path);
                remove_semicolon_paths(sit->desktop_path);

                sys_records.erase(sit);
                save_manifest(sys_paths.manifest_file, sys_records);
                Logger::success("Application archive système '" + pkg + "' désinstallée avec succès.");
            } catch (const std::exception& e) {
                Logger::error("Erreur lors de la désinstallation de '" + pkg + "': " + e.what());
                all_ok = false;
            }
        }

        if (!found) {
            Logger::error("Aucune application archive nommée '" + pkg + "' n'a été trouvée.");
            all_ok = false;
        }
    }

    return all_ok;
}

bool ArchiveBackend::set_icon(const std::string& name, const std::string& icon_path) {
    if (!fs::exists(icon_path)) {
        Logger::error("Fichier d'icône introuvable : " + icon_path);
        return false;
    }

    auto user_paths = get_paths(false);
    auto user_records = load_manifest(user_paths.manifest_file, false);
    auto uit = std::find_if(user_records.begin(), user_records.end(),
        [&](const ArchiveRecord& r) { return r.name == name; });

    bool is_system = false;
    std::string desktop_paths_str;
    fs::path icons_dir;

    if (uit != user_records.end()) {
        desktop_paths_str = uit->desktop_path;
        icons_dir = user_paths.icons_dir;
    } else {
        auto sys_paths = get_paths(true);
        auto sys_records = load_manifest(sys_paths.manifest_file, true);
        auto sit = std::find_if(sys_records.begin(), sys_records.end(),
            [&](const ArchiveRecord& r) { return r.name == name; });
        if (sit != sys_records.end()) {
            is_system = true;
            desktop_paths_str = sit->desktop_path;
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

        // Mettre à jour chaque fichier .desktop associé
        std::stringstream ss(desktop_paths_str);
        std::string dt_file;
        while (std::getline(ss, dt_file, ';')) {
            if (dt_file.empty() || !fs::exists(dt_file)) continue;

            std::ifstream df_in(dt_file);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(df_in, line)) {
                if (line.starts_with("Icon=")) {
                    lines.push_back("Icon=" + dest_icon.string());
                } else {
                    lines.push_back(line);
                }
            }
            std::ofstream df_out(dt_file);
            for (const auto& l : lines) df_out << l << "\n";
        }

        Logger::success("Icône de l'application '" + name + "' mise à jour avec succès !");
        Logger::info("Nouvelle icône : " + dest_icon.string());
        return true;
    } catch (const std::exception& e) {
        Logger::error("Erreur lors du changement d'icône : " + std::string(e.what()));
        return false;
    }
}

} // namespace abpm::archive