#include "abpm/config/config_backend.hpp"
#include "abpm/core/logger.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <unistd.h>

namespace abpm::config {

namespace fs = std::filesystem;
using core::Logger;

static fs::path get_config_path(bool for_writing = false) {
    if (geteuid() == 0 || (for_writing && geteuid() == 0)) {
        return "/etc/abpm/abpm.conf";
    }

    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "";
    if (for_writing) {
        return fs::path(home_str) / ".config/abpm/abpm.conf";
    }

    // En lecture : vérifier d'abord la config système, puis utilisateur
    std::error_code ec;
    if (fs::exists("/etc/abpm/abpm.conf", ec)) {
        return "/etc/abpm/abpm.conf";
    }
    return fs::path(home_str) / ".config/abpm/abpm.conf";
}

std::string ConfigBackend::get(const std::string& key) {
    fs::path conf_path = get_config_path(false);
    std::error_code ec;

    if (fs::exists(conf_path, ec)) {
        std::ifstream file(conf_path);
        std::string line;
        while (std::getline(file, line)) {
            auto pos = line.find('=');
            if (pos != std::string::npos) {
                std::string k = line.substr(0, pos);
                std::string v = line.substr(pos + 1);
                // Trim
                k.erase(k.find_last_not_of(" \t") + 1);
                k.erase(0, k.find_first_not_of(" \t"));
                v.erase(v.find_last_not_of(" \t") + 1);
                v.erase(0, v.find_first_not_of(" \t"));

                if (k == key) {
                    return v;
                }
            }
        }
    }

    // Valeurs par défaut
    if (key == "auto-update") {
        return "true";
    }

    return "";
}

bool ConfigBackend::set(const std::string& key, const std::string& value) {
    if (key == "auto-update") {
        if (value != "true" && value != "false") {
            Logger::error("Valeur invalide pour 'auto-update' : utilisez 'true' ou 'false'.");
            return false;
        }
    }

    fs::path conf_path = get_config_path(true);
    std::error_code ec;
    fs::create_directories(conf_path.parent_path(), ec);
    if (ec) {
        Logger::error("Impossible de créer le dossier de configuration : " + ec.message());
        return false;
    }

    std::vector<std::pair<std::string, std::string>> entries;
    bool found = false;

    if (fs::exists(conf_path, ec)) {
        std::ifstream file(conf_path);
        std::string line;
        while (std::getline(file, line)) {
            auto pos = line.find('=');
            if (pos != std::string::npos) {
                std::string k = line.substr(0, pos);
                std::string v = line.substr(pos + 1);
                k.erase(k.find_last_not_of(" \t") + 1);
                k.erase(0, k.find_first_not_of(" \t"));
                v.erase(v.find_last_not_of(" \t") + 1);
                v.erase(0, v.find_first_not_of(" \t"));

                if (k == key) {
                    entries.emplace_back(k, value);
                    found = true;
                } else {
                    entries.emplace_back(k, v);
                }
            }
        }
    }

    if (!found) {
        entries.emplace_back(key, value);
    }

    std::ofstream out(conf_path);
    if (!out.is_open()) {
        Logger::error("Impossible d'écrire dans le fichier de configuration : " + conf_path.string());
        return false;
    }

    for (const auto& [k, v] : entries) {
        out << k << "=" << v << "\n";
    }

    // Synchronisation du timer systemd si la clé est 'auto-update'
    if (key == "auto-update") {
        std::string systemd_action = (value == "true") ? "enable --now" : "disable --now";
        std::string cmd = "systemctl " + systemd_action + " abpm-auto-update.timer 2>/dev/null";
        std::system(cmd.c_str());
    }

    Logger::success("Configuration '" + key + "' définie à '" + value + "'.");
    return true;
}

} // namespace abpm::config