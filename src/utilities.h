#pragma once

#include <optional>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <random>
#include <cstdint>
#include <iomanip>

#include <pqxx/pqxx>

#include "data_structures.h"

using namespace std::literals;
using pqxx::operator"" _zv;

std::optional<std::string> ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

RequestData ParseJson(const std::string& json) {
    RequestData data;

    auto get_val = [&](const std::string& key) -> std::string {
        size_t pos = json.find('"' + key + '"');
        if (pos == std::string::npos) return "";

        size_t pos_dcomma = json.find(':', pos);
        if (pos_dcomma == std::string::npos) return "";

        size_t start = json.find_first_not_of(" \"", pos_dcomma + 1);
        if (start == std::string::npos) return "";

        size_t end = json.find_first_of(",}", start);
        if (end == std::string::npos) return "";

        std::string val = json.substr(start, end - start);

        if (!val.empty() && val.back() == '"') val.pop_back();
        if (!val.empty() && val.front() == '"') val.erase(0, 1);

        return val;
    };

    data.route = get_val("route");
    data.date = get_val("date");
    std::string price_str = get_val("price");

    if (!price_str.empty()) {
        try {
            data.price = std::stod(price_str);
        } catch (...) {
            data.price = 0.0;
        }
    }

    return data;
}

AuthentificationData ParseAuthJson(const std::string& json) {
    AuthentificationData data;

    auto get_val = [&](const std::string& key) -> std::string {
        size_t pos = json.find('"' + key + '"');
        if (pos == std::string::npos) return "";

        size_t pos_dcomma = json.find(':', pos);
        if (pos_dcomma == std::string::npos) return "";

        size_t start = json.find_first_not_of(" \"", pos_dcomma + 1);
        if (start == std::string::npos) return "";
        size_t end = json.find_first_of(",}", start);
        if (end == std::string::npos) return "";

        std::string val = json.substr(start, end - start);

        if (!val.empty() && val.back() == '"') val.pop_back();
        if (!val.empty() && val.front() == '"') val.erase(0, 1);

        return val;
    };

    data.username = get_val("username");
    data.password = get_val("password");
    return data;
}

std::unordered_map<std::string, Route> LoadRoutesFromDB(pqxx::connection& conn) {
    std::unordered_map<std::string, Route> routes_map;

    pqxx::work w(conn);
    pqxx::result RES = w.exec(
        "SELECT route, name, base_price, cost_per_passenger, "
        "price_sensivity AS price_sensitivity, "
        "price_min, price_max FROM routes;");
    w.commit();

    for (auto row : RES) {
        Route route;
        route.route = row["route"].as<std::string>();
        route.name = row["name"].as<std::string>();
        route.base_price = row["base_price"].as<int>();
        route.cost_per_passenger = row["cost_per_passenger"].as<int>();
        route.price_sensitivity = row["price_sensitivity"].as<double>();
        route.price_min = row["price_min"].as<int>();
        route.price_max = row["price_max"].as<int>();
        routes_map[route.route] = route;
    }

    return routes_map;
}

std::unordered_map<std::string, FlightForecast> LoadForecastFromDB(pqxx::connection& conn) {
    std::unordered_map<std::string, FlightForecast> forecast_map;

    pqxx::work w(conn);
    pqxx::result RES = w.exec("SELECT * FROM forecasts;");
    
    w.commit();

    for (auto row : RES) {
        FlightForecast flight;
        flight.route = row["route"].as<std::string>();
        flight.date  = row["date"].as<std::string>();
        flight.seats = row["seats"].as<int>();

        for (int i = 0; i < 9; ++i) {
            std::string numb = (i < 9 ? "0" : "") + std::to_string(i + 1);
            flight.base_demand[i] = row["base_demand_" + numb].as<double>();
            flight.price[i] = row["price_" + numb].as<double>();
            flight.price_demand[i] = row["price_demand_" + numb].as<double>();
        }

        std::string key = flight.route + '_' + flight.date;
        forecast_map[key] = flight;
    }

    return forecast_map;
}

enum class RegisterResult { Success, UserExists, Error };

inline std::string SimpleHash(const std::string& input) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : input) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();
}

inline std::string GenerateSalt() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << dist(rng);
    return ss.str();
}

inline RegisterResult RegisterUserInDB(pqxx::connection& conn, const std::string& username, const std::string& password) {
    std::string salt = GenerateSalt();
    std::string hash = SimpleHash(salt + password);
    try {
        pqxx::work w(conn);
        
        w.exec_params("INSERT INTO users (username, password_hash, salt) VALUES ($1, $2, $3);", username, hash, salt);
        
        w.commit();
        
        return RegisterResult::Success;
    } catch (const pqxx::unique_violation&) {
        return RegisterResult::UserExists;
    } catch (const std::exception& e) {
        std::cerr << "RegisterUserInDB error: " << e.what() << std::endl;
        
        return RegisterResult::Error;
    }
}

inline bool VerifyUserInDB(pqxx::connection& conn, const std::string& username, const std::string& password) {
    try {
        pqxx::work w(conn);
        auto r = w.exec_params("SELECT password_hash, salt FROM users WHERE username = $1 LIMIT 1;", username);
        w.commit();

        if (r.empty()) return false;
        
        std::string stored_hash = r[0]["password_hash"].as<std::string>();
        std::string salt = r[0]["salt"].as<std::string>();
        
        return SimpleHash(salt + password) == stored_hash;
    } catch (const std::exception& e) {
        std::cerr << "VerifyUserInDB error: " << e.what() << std::endl;
        return false;
    }
}

inline void SaveTokenToDB(pqxx::connection& conn, const std::string& token, const std::string& username) {
    pqxx::work w(conn);
    w.exec_params("DELETE FROM tokens WHERE username = $1;", username);
    w.exec_params("INSERT INTO tokens (token, username, created_at) VALUES ($1, $2, CURRENT_TIMESTAMP);", token, username);
    w.commit();
}

inline bool ValidateTokenInDB(pqxx::connection& conn, const std::string& token) {
    pqxx::work w(conn);
    auto r = w.exec_params("SELECT 1 FROM tokens WHERE token = $1 LIMIT 1;", token);
    w.commit();
    return !r.empty();
}

inline void DeleteTokenFromDB(pqxx::connection& conn, const std::string& token) {
    pqxx::work w(conn);
    w.exec_params("DELETE FROM tokens WHERE token = $1;", token);
    w.commit();
}

inline std::optional<std::string> GetUsernameByToken(pqxx::connection& conn, const std::string& token) { 
    pqxx::work w(conn);
    auto r = w.exec_params("SELECT username FROM tokens WHERE token = $1 LIMIT 1;", token);
    w.commit();
    if (r.empty()) return std::nullopt;
    return r[0]["username"].as<std::string>();
}