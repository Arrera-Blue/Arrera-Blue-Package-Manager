#include "abpm/core/categorizer.hpp"
#include <glib.h>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace abpm::core {

std::string Categorizer::categorize_xdg(const std::string& categories) {
    // Si les catégories comportent Utility, System, Settings, TerminalEmulator -> "utility"
    const std::vector<std::string> utility_keywords = {
        "Utility", "System", "Settings", "TerminalEmulator", "FileManager", "Monitor"
    };
    for (const auto& kw : utility_keywords) {
        if (categories.find(kw) != std::string::npos) {
            return "utility";
        }
    }
    return "app";
}

bool Categorizer::parse_desktop_file(const fs::path& desktop_file, DesktopMeta& out_meta) {
    if (!fs::exists(desktop_file) || desktop_file.extension() != ".desktop") {
        return false;
    }

    GKeyFile* kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, desktop_file.c_str(), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_free(kf);
        return false;
    }

    gchar* type = g_key_file_get_string(kf, "Desktop Entry", "Type", nullptr);
    gboolean nodisplay = g_key_file_get_boolean(kf, "Desktop Entry", "NoDisplay", nullptr);
    if (!type || std::string(type) != "Application" || nodisplay) {
        if (type) g_free(type);
        g_key_file_free(kf);
        return false;
    }
    g_free(type);

    gchar* raw_name = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", nullptr, nullptr);
    gchar* raw_icon = g_key_file_get_string(kf, "Desktop Entry", "Icon", nullptr);
    gchar* raw_comment = g_key_file_get_locale_string(kf, "Desktop Entry", "Comment", nullptr, nullptr);
    if (!raw_comment) {
        raw_comment = g_key_file_get_locale_string(kf, "Desktop Entry", "GenericName", nullptr, nullptr);
    }
    gchar* raw_cats = g_key_file_get_string(kf, "Desktop Entry", "Categories", nullptr);
    gchar* raw_exec = g_key_file_get_string(kf, "Desktop Entry", "Exec", nullptr);

    out_meta.id = desktop_file.stem().string();
    out_meta.name = raw_name ? raw_name : out_meta.id;
    out_meta.icon = raw_icon ? raw_icon : out_meta.id;
    out_meta.summary = raw_comment ? raw_comment : "";
    out_meta.exec = raw_exec ? raw_exec : "";
    out_meta.category = raw_cats ? categorize_xdg(raw_cats) : "app";

    if (raw_name) g_free(raw_name);
    if (raw_icon) g_free(raw_icon);
    if (raw_comment) g_free(raw_comment);
    if (raw_cats) g_free(raw_cats);
    if (raw_exec) g_free(raw_exec);
    g_key_file_free(kf);

    return true;
}

std::unordered_map<std::string, DesktopMeta> Categorizer::index_desktop_directory(const fs::path& dir) {
    std::unordered_map<std::string, DesktopMeta> index;
    if (!fs::exists(dir)) return index;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".desktop") {
            DesktopMeta meta;
            if (parse_desktop_file(entry.path(), meta)) {
                // Indexer par nom de fichier/stem (ex: "firefox", "org.gnome.Nautilus")
                index[meta.id] = meta;
                // Indexer aussi par binaire si spécifié dans Exec
                if (!meta.exec.empty()) {
                    std::stringstream ss(meta.exec);
                    std::string binary;
                    ss >> binary;
                    binary = fs::path(binary).filename().string();
                    if (!binary.empty() && index.find(binary) == index.end()) {
                        index[binary] = meta;
                    }
                }
            }
        }
    }
    return index;
}

std::string Categorizer::escape_json(const std::string& s) {
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

} // namespace abpm::core