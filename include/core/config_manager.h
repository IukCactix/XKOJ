#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <unordered_map>
#include <memory>
#include <nlohmann/json.hpp>

class ConfigManager {
public:
    static ConfigManager& instance();
    
    bool load_config(const std::string& config_path);
    
    // 获取配置值
    template<typename T>
    T get(const std::string& key, const T& default_value = T{}) const;
    
    // 设置配置值
    template<typename T>
    void set(const std::string& key, const T& value);
    
    // 保存配置
    bool save_config(const std::string& config_path = "");
    
    // 检查配置是否存在
    bool has(const std::string& key) const;

private:
    ConfigManager() = default;
    nlohmann::json config_;
    std::string config_file_path_;
    
    // 解析嵌套键 "server.port" -> ["server", "port"]
    std::vector<std::string> parse_key(const std::string& key) const;
    nlohmann::json* get_nested_value(const std::vector<std::string>& keys, bool create = false);
    const nlohmann::json* get_nested_value(const std::vector<std::string>& keys) const;
};

// 模板实现
template<typename T>
T ConfigManager::get(const std::string& key, const T& default_value) const {
    auto keys = parse_key(key);
    const auto* value = get_nested_value(keys);
    
    if (value && !value->is_null()) {
        try {
            return value->get<T>();
        } catch (...) {
            return default_value;
        }
    }
    
    return default_value;
}

template<typename T>
void ConfigManager::set(const std::string& key, const T& value) {
    auto keys = parse_key(key);
    auto* target = get_nested_value(keys, true);
    if (target) {
        *target = value;
    }
}

#endif // CONFIG_MANAGER_H