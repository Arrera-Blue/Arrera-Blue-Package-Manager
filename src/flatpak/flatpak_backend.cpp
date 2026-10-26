#include "abpm/flatpak/flatpak_backend.hpp"
#include "abpm/core/logger.hpp"
#include <flatpak.h>
#include <algorithm>
#include <cstring>

namespace abpm::flatpak {

using core::PackageInfo;
using core::PackageSource;
using core::Logger;

static void collect_from_installation(FlatpakInstallation* installation, 
                                      const std::string& origin_label,
                                      std::vector<PackageInfo>& out_packages) {
    if (!installation) return;

    g_autoptr(GError) error = nullptr;
    g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs_by_kind(
        installation, FLATPAK_REF_KIND_APP, nullptr, &error);

    if (!refs) {
        if (error) {
            Logger::warning(std::string("Flatpak (") + origin_label + ") : " + error->message);
        }
        return;
    }

    for (guint i = 0; i < refs->len; ++i) {
        auto* ref = FLATPAK_INSTALLED_REF(g_ptr_array_index(refs, i));
        
        const char* raw_id = flatpak_ref_get_name(FLATPAK_REF(ref));
        const char* raw_name = flatpak_installed_ref_get_appdata_name(ref);
        const char* raw_version = flatpak_installed_ref_get_appdata_version(ref);
        const char* raw_arch = flatpak_ref_get_arch(FLATPAK_REF(ref));

        PackageInfo pkg;
        pkg.id = raw_id ? raw_id : "";
        pkg.name = raw_name ? raw_name : pkg.id;
        pkg.version = raw_version ? raw_version : "inconnue";
        pkg.arch = raw_arch ? raw_arch : "";
        pkg.source = PackageSource::FLATPAK;
        pkg.origin = origin_label;

        out_packages.push_back(std::move(pkg));
    }
}

std::vector<PackageInfo> FlatpakBackend::list_installed() {
    std::vector<PackageInfo> results;

    // 1. Applications système
    g_autoptr(GError) err_sys = nullptr;
    g_autoptr(FlatpakInstallation) sys_inst = flatpak_installation_new_system(nullptr, &err_sys);
    collect_from_installation(sys_inst, "system", results);

    // 2. Applications utilisateur (~/.local/share/flatpak)
    g_autoptr(GError) err_user = nullptr;
    g_autoptr(FlatpakInstallation) user_inst = flatpak_installation_new_user(nullptr, &err_user);
    collect_from_installation(user_inst, "user", results);

    return results;
}

static std::string to_lower(std::string_view str) {
    std::string lower(str);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower;
}
std::vector<PackageInfo> FlatpakBackend::search(std::string_view term) {
    std::vector<PackageInfo> results;
    std::string needle = to_lower(term);
    g_autoptr(GError) err = nullptr;
    g_autoptr(FlatpakInstallation) inst = flatpak_installation_new_system(nullptr, &err);
    if (!inst) return results;
    // Lister les dépôts distants configurés (ex: flathub)
    g_autoptr(GPtrArray) remotes = flatpak_installation_list_remotes(inst, nullptr, &err);
    if (!remotes) return results;
    const char* default_arch = flatpak_get_default_arch();
    for (guint r = 0; r < remotes->len; ++r) {
        auto* remote = FLATPAK_REMOTE(g_ptr_array_index(remotes, r));
        const char* remote_name = flatpak_remote_get_name(remote);
        if (flatpak_remote_get_disabled(remote)) continue;
        // Récupérer les paquets distants (avec fallback sur le cache si hors-ligne)
        g_autoptr(GError) r_err = nullptr;
        g_autoptr(GPtrArray) refs = flatpak_installation_list_remote_refs_sync_full(
            inst, remote_name, FLATPAK_QUERY_FLAGS_ONLY_CACHED, nullptr, &r_err);
        if (!refs) continue;
        for (guint i = 0; i < refs->len; ++i) {
            auto* ref = FLATPAK_REF(g_ptr_array_index(refs, i));
            
            // On ne cherche que les vraies applications (pas les runtimes/sources)
            if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_APP) continue;
            const char* arch = flatpak_ref_get_arch(ref);
            if (arch && default_arch && std::strcmp(arch, default_arch) != 0) continue;
            const char* raw_id = flatpak_ref_get_name(ref);
            if (!raw_id) continue;
            std::string id_str = raw_id;
            if (to_lower(id_str).find(needle) != std::string::npos) {
                PackageInfo info;
                info.id = id_str;
                info.name = id_str;
                info.version = flatpak_ref_get_branch(ref) ? flatpak_ref_get_branch(ref) : "";
                info.arch = arch ? arch : "";
                info.source = PackageSource::FLATPAK;
                info.origin = remote_name ? remote_name : "flathub";
                results.push_back(std::move(info));
            }
        }
    }
    return results;
}

} // namespace abpm::flatpak