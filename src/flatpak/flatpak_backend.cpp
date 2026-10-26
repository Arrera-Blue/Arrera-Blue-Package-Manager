#include "abpm/flatpak/flatpak_backend.hpp"
#include "abpm/core/logger.hpp"
#include <flatpak.h>

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

} // namespace abpm::flatpak