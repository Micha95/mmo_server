#pragma once
#include <string>
#include <unordered_map>
#include <cstring>

// Simple key-value config loader (no JSON)
class Config {
public:
    bool load(const std::string& filename);
    std::string get(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def = 0) const;
private:
    std::unordered_map<std::string, std::string> values;
};

// Helper to parse --config argument from command line
inline std::string getConfigPathFromArgs(int argc, char** argv, const std::string& defaultPath = "../common/config_example.cfg") {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            return argv[i + 1];
        }
    }
    return defaultPath;
}
