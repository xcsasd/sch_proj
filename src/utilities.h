#pragma once

#include <optional>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>

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
        if(pos == std::string::npos) return "";

        size_t pos_dcomma = json.find(':', pos);
        if(pos_dcomma == std::string::npos) return "";

        size_t start = json.find_first_not_of(" \"", pos_dcomma + 1);
        if(start == std::string::npos) return "";

        size_t end = json.find_first_of(",}", start);
        if(end == std::string::npos) return "";

        std::string val = json.substr(start, end - start);

        if(!val.empty() && val.back() == '"') val.pop_back();
        if(!val.empty() && val.front() == '"') val.erase(0, 1);
        
        return val;
    };

    data.route = get_val("route");
    data.date = get_val("date");
    std::string price_str = get_val("price");

    if(!price_str.empty()) {
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
        if(pos == std::string::npos) return "";

        size_t pos_dcomma = json.find(':', pos);
        if(pos_dcomma == std::string::npos) return "";

        size_t start = json.find_first_not_of(" \"", pos_dcomma + 1);
        if(start == std::string::npos) return "";
        size_t end = json.find_first_of(",}", start);
        if(end == std::string::npos) return "";

        std::string val = json.substr(start, end - start);

        if(!val.empty() && val.back() == '"') val.pop_back();
        if(!val.empty() && val.front() == '"') val.erase(0, 1);
        
        return val;
    };

    data.username = get_val("username");
    data.password = get_val("password");
    return data;
}

//delete
std::unordered_map<std::string, Route> LoadRoutes(const std::string& filename) {
    std::unordered_map<std::string, Route> routes_map;
    std::ifstream file(filename);

    if(!file.is_open()) {
        std::cerr << "не удалось открыть файл: " << filename << std::endl;
        return routes_map;
    }

    std::string line;
    
    if(!std::getline(file, line)) {
        return routes_map;
    }

    while(std::getline(file, line)) {
        if(line.empty()) continue;

        std::stringstream ss(line);
        std::string value;
        Route route;
        
        std::getline(ss, route.route, ',');
        std::getline(ss, route.name, ',');

        std::getline(ss, value, ',');
        route.base_price = std::stoi(value);

        std::getline(ss, value, ',');
        route.cost_per_passenger = std::stoi(value);

        std::getline(ss, value, ',');
        route.price_sensitivity = std::stod(value);

        std::getline(ss, value, ',');
        route.price_min = std::stoi(value);

        std::getline(ss, value, ',');
        route.price_max = std::stoi(value);

        routes_map[route.route] = route;
    }

    file.close();
    return routes_map; 
}

//delete
std::unordered_map<std::string, FlightForecast> LoadForecast(const std::string& filename) {
    std::unordered_map<std::string, FlightForecast> forecast_map;
    std::ifstream file(filename);

    if(!file.is_open()) {
        std::cerr << "не удалось открыть файл: " << filename << std::endl;
        return forecast_map;
    }

    std::string line;

    if(!std::getline(file, line)) {
        return forecast_map;
    }

    while(std::getline(file, line)) {
        if(line.empty()) continue;

        std::stringstream ss(line);
        std::string value;
        FlightForecast flight;

        std::getline(ss, flight.route, ',');
        std::getline(ss, flight.date, ',');

        std::getline(ss, value, ',');
        flight.seats = std::stoi(value);

        for(int i = 0; i < 9; ++i) {
            std::getline(ss, value, ',');
            flight.base_demand[i] = std::stod(value);
            
            std::getline(ss, value, ',');
            flight.price[i] = std::stod(value);

            std::getline(ss, value, ',');
            flight.price_demand[i] = std::stod(value);
        }

        std::string key = flight.route + '_' + flight.date;
        forecast_map[key] = flight;
    }

    file.close();
    return forecast_map;
}

std::unordered_map<std::string, Route> LoadRoutesFromDB(pqxx::connection& conn) {
    std::unordered_map<std::string, Route> routes_map;

    pqxx::work w(conn);
    pqxx::result RES = w.exec("SELECT route, name, base_price, cost_per_passenger, price_sensitivity, price_min, price_max FROM routes;");
    w.commit();
    for(auto row : RES) {
        Route route;
        route.route = row["route"].as<std::string>();
        route.name = row["name"].as<std::string>();

        route.base_price = row["base_price"].as<int>();
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
    
    for(auto row : RES) {
        FlightForecast flight;
        flight.route = row["route"].as<std::string>();
        flight.date = row["date"].as<std::string>();
        flight.seats = row["seats"].as<int>();
        
        for(int i = 0; i < 9; ++i) {
            std::string numb = (i < 9 ? "0" : "") + std::to_string(i+1);
            flight.base_demand[i] = row["base_demand_" + numb].as<double>();
            flight.price[i] = row["price_"+numb].as<double>();
            flight.price_demand[i] = row["price_demand_" + numb].as<double>();
        }

        std::string key = flight.route + '_' + flight.date;
        forecast_map[key] = flight;
    }

    return forecast_map;
}
/*
cd /c/sch_proj
rm -rf build
mkdir build
cd build
cmake ..
*/