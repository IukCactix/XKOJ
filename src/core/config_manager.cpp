#include "core/config_manager.h"
#include <fstream>
#include <iostream>
#include <sstream>

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::load_config(const std::string& config_path) {
    try {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << config_path << std::endl;
            return false;
        }
        
        file >> config_;
        config_file_path_ = config_path;
        
        std::cout << "Configuration loaded from: " << config_path << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse config file: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::save_config(const std::string& config_path) {
    std::string path = config_path.empty() ? config_file_path_ : config_path;
    
    try {
        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }
        
        file << config_.dump(4); // 4 spaces indentation
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to save config: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::has(const std::string& key) const {
    auto keys = parse_key(key);
    const auto* value = get_nested_value(keys);
    return value != nullptr && !value->is_null();
}

std::vector<std::string> ConfigManager::parse_key(const std::string& key) const {
    std::vector<std::string> keys;
    std::stringstream ss(key);
    std::string item;
    
    while (std::getline(ss, item, '.')) {
        if (!item.empty()) {
            keys.push_back(item);
        }
    }
    
    return keys;
}

nlohmann::json* ConfigManager::get_nested_value(const std::vector<std::string>& keys, bool create) {
    nlohmann::json* current = &config_;
    
    for (const auto& key : keys) {
        if (current->is_object() && current->contains(key)) {
            current = &(*current)[key];
        } else if (create) {
            (*current)[key] = nlohmann::json::object();
            current = &(*current)[key];
        } else {
            return nullptr;
        }
    }
    
    return current;
}

const nlohmann::json* ConfigManager::get_nested_value(const std::vector<std::string>& keys) const {
    const nlohmann::json* current = &config_;
    
    for (const auto& key : keys) {
        if (current->is_object() && current->contains(key)) {
            current = &(*current)[key];
        } else {
            return nullptr;
        }
    }
    
    return current;
}