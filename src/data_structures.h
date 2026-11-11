#pragma once

#include <string>
#include <string_view>

using namespace std::literals;

struct ContentType {
    ContentType() = delete;
    constexpr static std::string_view TEXT_HTML = "text/html"sv;
    constexpr static std::string_view TEXT_PLAIN = "text/plain"sv;
    constexpr static std::string_view TEXT_CSS = "text/css"sv;
    constexpr static std::string_view APP_JS = "application/javascript"sv;
    constexpr static std::string_view APP_JSON = "application/json"sv;
};

struct Route { 
    std::string route;
    std::string name;
    int base_price;
    int cost_per_passenger;
    double price_sensitivity;
    int price_min;
    int price_max;
};

struct FlightForecast {
    std::string route;
    std::string date;
    int seats;

    double base_demand[9];
    double price[9];
    double price_demand[9];
};

struct RequestData {
    std::string route;
    std::string date;
    double price = 0;
};

struct AuthentificationData {
    std::string username;
    std::string password;
};