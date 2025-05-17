#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>

// Helper to trim whitespace from both ends of a string
static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

bool Config::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        values[key] = value;
    }
    // Print all loaded configs
    std::cout << "[CONFIG] Loaded values from '" << filename << "':" << std::endl;
    for (const auto& kv : values) {
        std::cout << "  " << kv.first << " = " << kv.second << std::endl;
    }
    return true;
}

std::string Config::get(const std::string& key, const std::string& def) const {
    auto it = values.find(key);
    return it != values.end() ? it->second : def;
}

int Config::getInt(const std::string& key, int def) const {
    auto it = values.find(key);
    if (it != values.end()) {
        try { return std::stoi(it->second); } catch (...) { return def; }
    }
    return def;
}
