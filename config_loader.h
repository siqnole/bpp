#pragma once
#include "database/core/types.h"
#include <fstream>
#include <iostream>

namespace bronx {

// Simple JSON-like config parser
inline db::DatabaseConfig load_database_config(const std::string& filepath = "data/db_config.json") {
    db::DatabaseConfig config;
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Don't print warning here, let caller handle it
        return config;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Simple key-value parsing
        if (line.find("\"host\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            config.host = line.substr(first_quote + 1, second_quote - first_quote - 1);
        }
        else if (line.find("\"port\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t comma = line.find(",", start);
            std::string port_str = line.substr(start, comma - start);
            config.port = std::stoi(port_str);
        }
        else if (line.find("\"database\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            config.database = line.substr(first_quote + 1, second_quote - first_quote - 1);
        }
        else if (line.find("\"user\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            config.user = line.substr(first_quote + 1, second_quote - first_quote - 1);
        }
        else if (line.find("\"password\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            config.password = line.substr(first_quote + 1, second_quote - first_quote - 1);
        }
        else if (line.find("\"pool_size\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t comma = line.find(",", start);
            std::string pool_str = line.substr(start, comma != std::string::npos ? comma - start : std::string::npos);
            config.pool_size = std::stoi(pool_str);
        }
        else if (line.find("\"timeout_seconds\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t comma = line.find(",", start);
            size_t end = line.find("}", start);
            if (comma == std::string::npos) comma = end;
            std::string timeout_str = line.substr(start, comma - start);
            config.timeout_seconds = std::stoi(timeout_str);
        }
        else if (line.find("\"log_connections\"") != std::string::npos) {
            // simple bool parse, accept true/false
            if (line.find("true") != std::string::npos) {
                config.log_connections = true;
            } else {
                config.log_connections = false;
            }
        }
    }
    
    return config;
}

} // namespace bronx
