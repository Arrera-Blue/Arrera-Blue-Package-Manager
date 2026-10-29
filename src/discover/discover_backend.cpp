#include "abpm/discover/discover_backend.hpp"
#include "abpm/dnf/dnf_backend.hpp"
#include "abpm/core/logger.hpp"

#include <appstream.h>
#include <curl/curl.h>
#include <json-glib/json-glib.h>

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace abpm::discover {

namespace {

// Callback curl et helper d'en-tête locale
size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

std::string detect_locale_header() {
    const char* lang = std::getenv("LANG");
    if (!lang || std::string_view(lang).empty()) lang = std::getenv("LC_ALL");
    if (!lang || std::string_view(lang).empty()) return "fr-FR,fr;q=0.9,en;q=0.8";

    std::string l(lang);
    size_t dot = l.find('.');
    if (dot != std::string::npos) l = l.substr(0, dot);
    size_t under = l.find('_');
    if (under != std::string::npos) {
        std::string primary = l.substr(0, under);
        l[under] = '-';
        return l + "," + primary + ";q=0.9,en;q=0.8";
    }
    return l + ";q=0.9,en;q=0.8";
}

std::string resolve_rpm_candidate_name(const std::string& app_id) {
    auto pos = app_id.rfind('.');
    std::string candidate = (pos != std::string::npos && pos + 1 < app_id.size()) 
        ? app_id.substr(pos + 1) 
        : app_id;
    std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return candidate;
}

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

// Extrait le nom de paquet RPM candidat depuis un composant AppStream
std::string extract_candidate_rpm_from_as(AsComponent* c) {
    if (const char* pkg = as_component_get_pkgname(c)) {
        return pkg;
    }
    AsLaunchable* l = as_component_get_launchable(c, AS_LAUNCHABLE_KIND_DESKTOP_ID);
    if (l) {
        GPtrArray* entries = as_launchable_get_entries(l);
        if (entries && entries->len > 0) {
            std::string dname = static_cast<const char*>(g_ptr_array_index(entries, 0));
            if (dname.ends_with(".desktop")) dname = dname.substr(0, dname.size() - 8);
            auto pos = dname.rfind('.');
            if (pos != std::string::npos) dname = dname.substr(pos + 1);
            std::transform(dname.begin(), dname.end(), dname.begin(), ::tolower);
            return dname;
        }
    }
    std::string id = as_component_get_id(c) ? as_component_get_id(c) : "";
    return resolve_rpm_candidate_name(id);
}

} // anonymous namespace

// 1. Récupération des applications DNF via AppStream
std::vector<DiscoverApp> DiscoverBackend::get_dnf_apps(int page, int per_page) {
    std::vector<DiscoverApp> all_dnf;

    AsPool* pool = as_pool_new();
    as_pool_load(pool, nullptr, nullptr);
    AsComponentBox* box = as_pool_get_components(pool);
    guint len = as_component_box_len(box);

    std::vector<std::string> candidate_pkgs;
    struct AsEntry {
        std::string id;
        std::string name;
        std::string summary;
        std::string icon;
        std::string pkg;
    };
    std::vector<AsEntry> entries;

    for (guint i = 0; i < len; ++i) {
        AsComponent* c = as_component_box_index(box, i);
        if (as_component_get_kind(c) != AS_COMPONENT_KIND_DESKTOP_APP) continue;

        const char* raw_name = as_component_get_name(c);
        const char* raw_summary = as_component_get_summary(c);
        const char* raw_id = as_component_get_id(c);
        if (!raw_name || !raw_summary || !raw_id) continue;

        std::string pkg = extract_candidate_rpm_from_as(c);
        if (pkg.empty()) continue;

        std::string summary = raw_summary;
        std::replace(summary.begin(), summary.end(), '\n', ' ');
        std::replace(summary.begin(), summary.end(), '\r', ' ');

        std::string icon = pkg;
        GPtrArray* icons = as_component_get_icons(c);
        if (icons && icons->len > 0) {
            AsIcon* ic = AS_ICON(g_ptr_array_index(icons, 0));
            if (as_icon_get_name(ic)) icon = as_icon_get_name(ic);
            else if (as_icon_get_url(ic)) icon = as_icon_get_url(ic);
        }

        entries.push_back({raw_id, raw_name, summary, icon, pkg});
        candidate_pkgs.push_back(pkg);
    }

    g_object_unref(box);
    g_object_unref(pool);

    // Vérifier les paquets réellement disponibles dans DNF
    auto available = dnf::DnfBackend::get_available_packages(candidate_pkgs);

    for (const auto& e : entries) {
        bool in_dnf = std::find(available.begin(), available.end(), e.pkg) != available.end();
        if (in_dnf) {
            all_dnf.push_back({e.id, e.name, e.summary, e.icon, "dnf", e.pkg});
        }
    }

    // Pagination DNF
    std::vector<DiscoverApp> paged;
    int start = (page - 1) * per_page;
    if (start < static_cast<int>(all_dnf.size())) {
        int count = std::min(per_page, static_cast<int>(all_dnf.size()) - start);
        paged.assign(all_dnf.begin() + start, all_dnf.begin() + start + count);
    }
    return paged;
}

// 2. Récupération des tendances Flathub
std::vector<DiscoverApp> DiscoverBackend::get_flatpak_trending(int page, int per_page) {
    std::vector<DiscoverApp> results;

    CURL* curl = curl_easy_init();
    if (!curl) return results;

    std::string url = "https://flathub.org/api/v2/collection/trending?page=" + 
                      std::to_string(page) + "&per_page=" + std::to_string(per_page);
    std::string response_data;

    struct curl_slist* headers = nullptr;
    std::string accept_lang = "Accept-Language: " + detect_locale_header();
    headers = curl_slist_append(headers, accept_lang.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "abpm/2026.1 (Arrera Blue OS)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200 || response_data.empty()) return results;

    JsonParser* parser = json_parser_new();
    GError* error = nullptr;
    if (!json_parser_load_from_data(parser, response_data.data(), response_data.size(), &error)) {
        if (error) g_error_free(error);
        g_object_unref(parser);
        return results;
    }

    JsonNode* root = json_parser_get_root(parser);
    if (!root) {
        g_object_unref(parser);
        return results;
    }

    JsonArray* array = nullptr;
    if (JSON_NODE_HOLDS_ARRAY(root)) {
        array = json_node_get_array(root);
    } else if (JSON_NODE_HOLDS_OBJECT(root)) {
        JsonObject* obj = json_node_get_object(root);
        if (json_object_has_member(obj, "hits")) {
            array = json_object_get_array_member(obj, "hits");
        } else if (json_object_has_member(obj, "apps")) {
            array = json_object_get_array_member(obj, "apps");
        }
    }

    if (array) {
        guint len = json_array_get_length(array);

        auto get_str = [&](JsonObject* item_obj, const char* key) -> std::string {
            if (json_object_has_member(item_obj, key)) {
                JsonNode* val_node = json_object_get_member(item_obj, key);
                if (val_node && !json_node_is_null(val_node) && JSON_NODE_HOLDS_VALUE(val_node)) {
                    if (json_node_get_value_type(val_node) == G_TYPE_STRING) {
                        const char* val = json_node_get_string(val_node);
                        return val ? val : "";
                    }
                }
            }
            return "";
        };

        for (guint i = 0; i < len; ++i) {
            JsonNode* item_node = json_array_get_element(array, i);
            if (!JSON_NODE_HOLDS_OBJECT(item_node)) continue;
            JsonObject* item = json_node_get_object(item_node);

            std::string app_id = get_str(item, "app_id");
            if (app_id.empty()) app_id = get_str(item, "id");
            if (app_id.empty()) continue;

            std::string name = get_str(item, "name");
            if (name.empty()) name = app_id;
            std::string summary = get_str(item, "summary");
            std::replace(summary.begin(), summary.end(), '\n', ' ');
            std::replace(summary.begin(), summary.end(), '\r', ' ');
            std::string icon = get_str(item, "icon");

            results.push_back({app_id, name, summary, icon.empty() ? app_id : icon, "flatpak", app_id});
        }
    }

    g_object_unref(parser);
    return results;
}

// 3. Méthode principale unifiée
std::vector<DiscoverApp> DiscoverBackend::discover(cli::SourceFilter filter, int page, int per_page) {
    if (filter == cli::SourceFilter::DNF) {
        return get_dnf_apps(page, per_page);
    }
    if (filter == cli::SourceFilter::FLATPAK) {
        return get_flatpak_trending(page, per_page);
    }

    // Mode MIXTE par défaut : 50% DNF et 50% Flatpak
    int dnf_quota = (per_page + 1) / 2;
    int flatpak_quota = per_page / 2;

    auto dnf_apps = get_dnf_apps(page, dnf_quota);
    auto flatpak_apps = get_flatpak_trending(page, flatpak_quota);

    std::vector<DiscoverApp> mixed;
    mixed.reserve(dnf_apps.size() + flatpak_apps.size());
    mixed.insert(mixed.end(), dnf_apps.begin(), dnf_apps.end());
    mixed.insert(mixed.end(), flatpak_apps.begin(), flatpak_apps.end());

    return mixed;
}

std::string DiscoverBackend::to_json(const std::vector<DiscoverApp>& apps) {
    std::ostringstream ss;
    ss << "[\n";
    for (size_t i = 0; i < apps.size(); ++i) {
        const auto& app = apps[i];
        ss << "  {\n";
        ss << "    \"id\": \"" << escape_json(app.id) << "\",\n";
        ss << "    \"name\": \"" << escape_json(app.name) << "\",\n";
        ss << "    \"summary\": \"" << escape_json(app.summary) << "\",\n";
        ss << "    \"icon\": \"" << escape_json(app.icon) << "\",\n";
        ss << "    \"source\": \"" << escape_json(app.source) << "\",\n";
        ss << "    \"package\": \"" << escape_json(app.package) << "\"\n";
        ss << "  }" << (i + 1 < apps.size() ? "," : "") << "\n";
    }
    ss << "]\n";
    return ss.str();
}

} // namespace abpm::discover