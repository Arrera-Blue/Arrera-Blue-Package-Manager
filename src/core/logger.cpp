#include "abpm/core/logger.hpp"
#include <iostream>

namespace abpm::core {

namespace Color {
    constexpr auto RESET   = "\033[0m";
    constexpr auto BOLD    = "\033[1m";
    // Bleu officiel Arrera (RGB: 60, 110, 180)
    constexpr auto BLUE    = "\033[38;2;60;110;180m";
    constexpr auto GREEN   = "\033[1;32m";
    constexpr auto YELLOW  = "\033[1;33m";
    constexpr auto RED     = "\033[1;31m";
}

void Logger::banner() {
    std::cout << Color::BLUE << Color::BOLD
              << "=================================================\n"
              << "   Arrera Blue Package Manager (abpm) v2026.1.1  \n"
              << "================================================="
              << Color::RESET << "\n\n";
}

void Logger::info(std::string_view message) {
    std::cout << Color::BLUE << Color::BOLD << "[INFO] " 
              << Color::RESET << message << "\n";
}

void Logger::success(std::string_view message) {
    std::cout << Color::GREEN << Color::BOLD << "[SUCCÈS] " 
              << Color::RESET << message << "\n";
}

void Logger::warning(std::string_view message) {
    std::cout << Color::YELLOW << Color::BOLD << "[ATTENTION] " 
              << Color::RESET << message << "\n";
}

void Logger::error(std::string_view message) {
    std::cerr << Color::RED << Color::BOLD << "[ERREUR] " 
              << Color::RESET << message << "\n";
}

bool Logger::confirm(std::string_view question, bool default_val) {
    std::cout << Color::YELLOW << Color::BOLD << "[?] " 
              << Color::RESET << question 
              << (default_val ? " [O/n] : " : " [o/N] : ");
    std::string response;
    if (!std::getline(std::cin, response)) {
        return false;
    }
    if (response.empty()) {
        return default_val;
    }
    char first = std::tolower(response[0]);
    return (first == 'o' || first == 'y');
}

} // namespace abpm::core