#pragma once

#include <string_view>

namespace abpm::core {

class Logger {
public:
    // Affiche la bannière d'accueil abpm
    static void banner();

    // Méthodes d'affichage conformes au README
    static void info(std::string_view message);
    static void success(std::string_view message);
    static void warning(std::string_view message);
    static void error(std::string_view message);
    static bool confirm(std::string_view question, bool default_val = true);
};

} // namespace abpm::core