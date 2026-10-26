#include "abpm/flatpak/flatpak_backend.hpp"
#include "abpm/core/logger.hpp"
#include <flatpak.h>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace abpm::flatpak {

using core::PackageInfo;
using core::PackageSource;
using core::Logger;
using core::UpdateInfo;

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

static void collect_updates_from_installation(FlatpakInstallation* inst,
                                              const std::string& default_origin,
                                              std::vector<UpdateInfo>& out_updates) {
    if (!inst) return;
    g_autoptr(GError) err = nullptr;
    g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs_for_update(inst, nullptr, &err);
    if (!refs) return;
    for (guint i = 0; i < refs->len; ++i) {
        auto* ref = FLATPAK_INSTALLED_REF(g_ptr_array_index(refs, i));
        const char* raw_id = flatpak_ref_get_name(FLATPAK_REF(ref));
        const char* raw_name = flatpak_installed_ref_get_appdata_name(ref);
        const char* raw_ver = flatpak_installed_ref_get_appdata_version(ref);
        const char* origin = flatpak_installed_ref_get_origin(ref);
        const char* arch = flatpak_ref_get_arch(FLATPAK_REF(ref));
        UpdateInfo info;
        info.id = raw_id ? raw_id : "";
        info.name = raw_name ? raw_name : info.id;
        info.current_ver = raw_ver ? raw_ver : "actuelle";
        info.new_ver = "mise à jour dispo";
        info.arch = arch ? arch : "";
        info.source = PackageSource::FLATPAK;
        info.origin = origin ? origin : default_origin;
        out_updates.push_back(std::move(info));
    }
}
std::vector<UpdateInfo> FlatpakBackend::check_updates() {
    std::vector<UpdateInfo> updates;
    // 1. Mises à jour système
    g_autoptr(GError) err_sys = nullptr;
    g_autoptr(FlatpakInstallation) sys_inst = flatpak_installation_new_system(nullptr, &err_sys);
    collect_updates_from_installation(sys_inst, "flathub (system)", updates);
    // 2. Mises à jour utilisateur
    g_autoptr(GError) err_user = nullptr;
    g_autoptr(FlatpakInstallation) user_inst = flatpak_installation_new_user(nullptr, &err_user);
    collect_updates_from_installation(user_inst, "flathub (user)", updates);
    return updates;
}

bool FlatpakBackend::update() {
    g_autoptr(GError) err = nullptr;
    g_autoptr(FlatpakInstallation) inst = flatpak_installation_new_system(nullptr, &err);
    if (!inst) {
        Logger::error(std::string("Erreur d'accès à Flatpak : ") + (err ? err->message : "inconnue"));
        return false;
    }

    // Récupérer les paquets à mettre à jour
    g_autoptr(GError) refs_err = nullptr;
    g_autoptr(GPtrArray) refs = flatpak_installation_list_installed_refs_for_update(inst, nullptr, &refs_err);
    if (!refs || refs->len == 0) {
        Logger::success("Toutes les applications Flatpak sont déjà à jour !");
        return true;
    }

    g_autoptr(GError) tx_err = nullptr;
    g_autoptr(FlatpakTransaction) tx = flatpak_transaction_new_for_installation(inst, nullptr, &tx_err);
    if (!tx) {
        Logger::error(std::string("Impossible d'initialiser la transaction Flatpak : ") + (tx_err ? tx_err->message : ""));
        return false;
    }

    // Ajouter chaque mise à jour à la transaction
    for (guint i = 0; i < refs->len; ++i) {
        auto* ref = FLATPAK_INSTALLED_REF(g_ptr_array_index(refs, i));
        const char* formatted_ref = flatpak_ref_format_ref_cached(FLATPAK_REF(ref));
        if (!formatted_ref) continue;

        g_autoptr(GError) add_err = nullptr;
        if (!flatpak_transaction_add_update(tx, formatted_ref, nullptr, nullptr, &add_err)) {
            if (add_err) {
                Logger::warning(std::string("Impossible d'ajouter ") + formatted_ref + " : " + add_err->message);
            }
        }
    }

    if (flatpak_transaction_is_empty(tx)) {
        Logger::success("Aucune mise à jour requise pour Flatpak.");
        return true;
    }

    // Afficher chaque opération en cours
    g_signal_connect(tx, "new-operation", G_CALLBACK(+[](FlatpakTransaction*, FlatpakTransactionOperation* op, FlatpakTransactionProgress*, gpointer) {
        const char* r = flatpak_transaction_operation_get_ref(op);
        Logger::info(std::string("Mise à jour en cours : ") + (r ? r : ""));
    }), nullptr);

    Logger::info("Exécution de la transaction Flatpak...");
    g_autoptr(GError) run_err = nullptr;
    if (!flatpak_transaction_run(tx, nullptr, &run_err)) {
        Logger::error(std::string("Échec de la transaction Flatpak : ") + (run_err ? run_err->message : ""));
        return false;
    }

    Logger::success("Mises à jour Flatpak appliquées avec succès !");
    return true;
}

bool FlatpakBackend::install(std::string_view app_name, bool assumeyes) {
    // 1. Rechercher les correspondances sur Flathub
    auto candidates = search(app_name);
    if (candidates.empty()) {
        return false;
    }
    // Choisir la meilleure correspondance (correspondance exacte d'ID en priorité)
    PackageInfo chosen = candidates[0];
    std::string needle = to_lower(app_name);
    for (const auto& c : candidates) {
        std::string lower_id = to_lower(c.id);
        if (lower_id == needle || lower_id.ends_with("." + needle)) {
            chosen = c;
            break;
        }
    }
    // 2. Afficher ce qui va être installé
    std::cout << "\n--- Application Flatpak trouvée sur " << chosen.origin << " ---\n"
              << " • " << chosen.id << " [" << chosen.origin << " / " << chosen.version << "]\n\n";
    // 3. Demander confirmation
    if (!assumeyes) {
        if (!Logger::confirm("Voulez-vous installer cette application Flatpak ?")) {
            Logger::info("Installation annulée.");
            return true;
        }
    }
    // 4. Lancer la transaction d'installation
    g_autoptr(GError) err = nullptr;
    g_autoptr(FlatpakInstallation) inst = flatpak_installation_new_system(nullptr, &err);
    if (!inst) {
        Logger::error("Impossible d'accéder au système Flatpak.");
        return true;
    }
    g_autoptr(GError) tx_err = nullptr;
    g_autoptr(FlatpakTransaction) tx = flatpak_transaction_new_for_installation(inst, nullptr, &tx_err);
    if (!tx) {
        Logger::error("Impossible d'initialiser la transaction Flatpak.");
        return true;
    }
    // Formater le ref (ex: app/org.videolan.VLC/x86_64/stable)
    const char* default_arch = flatpak_get_default_arch();
    std::string full_ref = "app/" + chosen.id + "/" + (default_arch ? default_arch : "x86_64") + "/stable";
    g_autoptr(GError) add_err = nullptr;
    if (!flatpak_transaction_add_install(tx, chosen.origin.c_str(), full_ref.c_str(), nullptr, &add_err)) {
        Logger::error(std::string("Erreur préparation Flatpak : ") + (add_err ? add_err->message : "inconnue"));
        return true;
    }
    g_signal_connect(tx, "new-operation", G_CALLBACK(+[](FlatpakTransaction*, FlatpakTransactionOperation* op, FlatpakTransactionProgress*, gpointer) {
        const char* r = flatpak_transaction_operation_get_ref(op);
        Logger::info(std::string("Installation en cours : ") + (r ? r : ""));
    }), nullptr);
    Logger::info("Téléchargement et installation de l'application...");
    g_autoptr(GError) run_err = nullptr;
    if (!flatpak_transaction_run(tx, nullptr, &run_err)) {
        Logger::error(std::string("Échec de l'installation Flatpak : ") + (run_err ? run_err->message : "inconnue"));
        return true;
    }
    Logger::success("Application Flatpak '" + chosen.id + "' installée avec succès !");
    return true;
}

} // namespace abpm::flatpak