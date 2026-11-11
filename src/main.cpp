#define BOOST_BEAST_USE_STD_STRING_VIEW

#ifdef WIN32
#include <sdkddkver.h>
#endif

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include "tokens.h"
#include "utilities.h"

#include <thread>
#include <algorithm>
#include <mutex>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace beast = boost::beast;
namespace http = boost::beast::http;

using string_request = http::request<http::string_body>;
using string_response = http::response<http::string_body>;

using namespace std::literals;

class Server {
public:
    Server(const std::unordered_map<std::string, FlightForecast>& table, const std::unordered_map<std::string, Route>& table_routes, boost::asio::io_context& ioc,
    const boost::asio::ip::address& address, const unsigned short port) 
        : table_flight_(table), ioc_(ioc), address_(address), port_(port), table_routes_(table_routes) { }

    ~Server() = default;

    std::optional<string_request> ReadRequest(boost::asio::ip::tcp::socket& socket, beast::flat_buffer& buffer) {
        beast::error_code ec; 
        string_request req;
        http::read(socket, buffer, req, ec);
        if(ec == http::error::end_of_stream) return std::nullopt;
        if(ec) throw std::runtime_error("Ошибка чтения запроса");
        return req;
    }

    string_response MakeStringResponse(http::status status, std::string_view body, 
    unsigned http_version, bool keep_alive, std::string_view content_type = ContentType::TEXT_HTML) {
        string_response response(status, http_version);
        response.set(http::field::content_type, content_type);

        //тут поменять когда буду выкалиывать на хостинг cors
        response.set(http::field::access_control_allow_origin, "*"); // на ip сервака-
        response.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        response.set(http::field::access_control_allow_headers, "Content-Type");

        response.body() = body;
        response.content_length(body.size());
        response.keep_alive(keep_alive);
        response.prepare_payload();

        return response;
    }

    string_response HandleRequest(string_request&& req) {
        if(req.method() == http::verb::options) {
            return MakeStringResponse(http::status::ok, "f", req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
        }

        if (req.method() == http::verb::post) {
            auto ct = req[http::field::content_type];
            if (ct != "application/json" && ct != "application/json;charset=utf-8") {
                return MakeStringResponse(http::status::unsupported_media_type, "{\"error\":\"Content-Type должен быть application/json\"}", req.version(), req.keep_alive(), ContentType::APP_JS);
            }
        }

        std::string endpoint = req.target();
        
        if(endpoint == "/api/count" && req.method() == http::verb::post) {
            RequestData data = ParseJson(req.body());
            std::string key = data.route + "_" + data.date;

            if(table_flight_.find(key) != table_flight_.end() && table_routes_.find(data.route) != table_routes_.end()) {
                FlightForecast flight = table_flight_.at(key);
                Route route = table_routes_.at(data.route);

                if (data.price < route.price_min || data.price > route.price_max) {
                    std::string json_error = "{\"error\":\"Цена выходит за допустимые рамки "
                        + std::to_string(route.price_min) + " - "
                        + std::to_string(route.price_max) + " для этого маршрута.\"}";
                    
                    return MakeStringResponse(http::status::bad_request, json_error, req.version(), req.keep_alive(), ContentType::APP_JS);
                }

                double demand_5 = flight.base_demand[4];
                double demand = demand_5 - route.price_sensitivity * (data.price - route.base_price);
                double buy = std::min(static_cast<double>(flight.seats), std::max(0.0, demand));

                std::string json_response = "{"
                    "\"seats\": " + std::to_string(flight.seats) + ", "
                    "\"will_buy\": " + std::to_string(static_cast<int>(buy)) +
                "}";
                return MakeStringResponse(http::status::ok, json_response, req.version(), req.keep_alive(), ContentType::APP_JS);
            } else {
                return MakeStringResponse(http::status::not_found, "{\"error\":\"Рейс или маршрут не найден\"}", req.version(), req.keep_alive(), ContentType::APP_JS);
            }
        }

        if(endpoint == "/api/register" && req.method() == http::verb::post) {
            AuthentificationData auth_data = ParseAuthJson(req.body());
            std::string username = auth_data.username;
            std::string password = auth_data.password;

            if (username.empty() || password.empty()) {
                return MakeStringResponse(http::status::bad_request, "{\"error\":\"Поля не могут быть пустыми\"}", req.version(), req.keep_alive(), ContentType::APP_JS);
            }

            {
                std::lock_guard<std::mutex> lock(users_mutex_);
                if (table_users_.find(username) != table_users_.end()) {
                    return MakeStringResponse(http::status::conflict, "{\"error\":\"Пользователь с таким логином уже существует\"}", req.version(), req.keep_alive(), ContentType::APP_JS);
                }

                table_users_[username] = password;
                SaveUser(username, password);
            }

            return MakeStringResponse(http::status::ok, "{\"message\":\"Регистрация успешна! :)\"}", req.version(), req.keep_alive(), ContentType::APP_JS);
        }

        if(endpoint == "/make_coffee" && req.method() == http::verb::get) {
            return MakeStringResponse(http::status::ok, "I'M TEAPOT!!!! I CAN'T MAKE COFFEE!!!!!!", req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
        }

        if (endpoint == "/api/login" && req.method() == http::verb::post) {
            AuthentificationData auth_data = ParseAuthJson(req.body());
            std::string username = auth_data.username;
            std::string password = auth_data.password;

            {
                std::lock_guard<std::mutex> lock(users_mutex_);
                auto it = table_users_.find(username);
                if (it == table_users_.end() || it->second != password) {
                    return MakeStringResponse(http::status::unauthorized, "{\"error\":\"Неверный логин или пароль попробуйте снова :)\"}", req.version(), req.keep_alive(), ContentType::APP_JS);
                }
            }

            std::string token = gen.GenerateToken();
            std::string response = "{\"token\":\"" + token + "\"}";

            return MakeStringResponse(http::status::ok, response, req.version(), req.keep_alive(), ContentType::APP_JS);
        }

        if (req.method() == http::verb::get) {
            std::string target = req.target();
            if(target == "/") {
                target = "/login.html";
            }

            if(target.find("..") != std::string::npos) return MakeStringResponse(http::status::bad_request, "bad request", req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
            
            if(target == "imdex.html") {
                auto authent_header = req[http::field::authorization];
                if(authent_header.empty() || !authent_header.starts_with("Bearer ")) {
                    string_response res(http::status::found, req.version());
                    
                    res.set(http::field::location, "/login.html");
                    res.keep_alive(req.keep_alive());
                    res.prepare_payload();
                    
                    return res;
                }
            }
            
            if (target == "/login.html" || target == "/index.html" || target == "/style.css" || target == "/script.js" || target == "/auth.js") {
                std::string path = "../front_end" + target;

                std::string_view content_type = ContentType::TEXT_HTML;
                if(auto content = ReadFile(path)) {
                    if(target.ends_with(".css")) content_type = ContentType::TEXT_CSS;
                    else if(target.ends_with(".js")) content_type = ContentType::APP_JS;
                    
                    return MakeStringResponse(http::status::ok, *content, req.version(), req.keep_alive(), content_type);
                }
            }
        }

        return MakeStringResponse(http::status::not_found, "404", req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
    }

    void DumpRequest(const string_request& req) {
        std::cout << req.method_string() << ' ' << req.target() << std::endl;
        for (const auto& header : req) {
            std::cout << "  "sv << header.name_string() << ": "sv << header.value() << std::endl;
        }
    }

    void Run() {
        boost::asio::ip::tcp::acceptor acceptor{ioc_, {address_, port_}};

        while(1) {
            boost::asio::ip::tcp::socket socket(ioc_);
            acceptor.accept(socket);

            std::thread t([&](boost::asio::ip::tcp::socket socket){
                try {
                    beast::flat_buffer buffer;

                    while(auto request = ReadRequest(socket, buffer)) {
                        DumpRequest(*request);
                        string_response response = HandleRequest(*std::move(request));
                        http::write(socket, response);
                        if(response.need_eof()) {
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << e.what() << std::endl;
                }
            }, std::move(socket));
            t.detach();
        }
    }

    void LoadUsers() {
        std::lock_guard<std::mutex> lock(users_mutex_);
        std::ifstream file("../data/users.txt");
        
        if (!file.is_open()) {
            std::cout << "Файл users.txt не найден будет создан при первой регистрации" << std::endl;
            return;
        }

        std::string username, password;
        while (file >> username >> password) {
            table_users_[username] = password;
        }

        file.close();
    }

    void SaveUser(const std::string& username, const std::string& password) {
        std::ofstream file("../data/users.txt", std::ios::app);
        if (file.is_open()) {
            file << username << " " << password << "\n";
            file.close();
        }
    }

private:
    std::unordered_map<std::string, FlightForecast> table_flight_;
    
    boost::asio::io_context& ioc_;
    
    const boost::asio::ip::address address_;
    const unsigned short port_;
    
    std::unordered_map<std::string, Route> table_routes_;
    std::unordered_map<std::string, std::string> table_users_;
    
    std::mutex users_mutex_;

    TokenGenerator gen;
};

int main() {
    auto flight_forecast_table = LoadForecast("../data/seasonal_forecast.csv");
    auto routes_table = LoadRoutes("../data/routes.csv");

    boost::asio::io_context ioc;

    Server server(flight_forecast_table, routes_table, ioc, boost::asio::ip::make_address("127.0.0.1"), 5000);
    server.LoadUsers();
    
    server.Run();
}