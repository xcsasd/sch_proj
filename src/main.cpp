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
#include <cstdlib>

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
        const boost::asio::ip::address& address, const unsigned short port, pqxx::connection& conn)
        : table_flight_(table), ioc_(ioc), address_(address), port_(port), table_routes_(table_routes), db_conn_(conn) { }
    ~Server() = default;

    std::optional<string_request> ReadRequest(boost::asio::ip::tcp::socket& socket, beast::flat_buffer& buffer) {
        beast::error_code ec;
        string_request req;
        http::read(socket, buffer, req, ec);
        if (ec == http::error::end_of_stream) return std::nullopt;
        if (ec) throw std::runtime_error("Ошибка чтения запроса");
        return req;
    }

    string_response MakeStringResponse(http::status status, std::string_view body, unsigned http_version, bool keep_alive, std::string_view content_type = ContentType::TEXT_HTML) {
        string_response response(status, http_version);
        response.set(http::field::content_type, content_type);

        response.set(http::field::access_control_allow_origin, "*");
        response.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        response.set(http::field::access_control_allow_headers, "Content-Type, Authorization");

        response.body() = body;
        response.content_length(body.size());
        response.keep_alive(keep_alive);
        response.prepare_payload();
        return response;
    }

    string_response HandleRequest(string_request&& req) {
        if (req.method() == http::verb::options) {
            return MakeStringResponse(http::status::ok, "f", req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
        }

        if (req.method() == http::verb::post && !req.body().empty()) {
            auto ct = req[http::field::content_type];
            if (ct != "application/json" && ct != "application/json;charset=utf-8") {
                return MakeStringResponse(http::status::unsupported_media_type, "{\"error\":\"Content-Type должен быть application/json\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }
        }

        std::string endpoint = req.target();

        if (endpoint == "/api/count" && req.method() == http::verb::post) {
            RequestData data = ParseJson(req.body());
            std::string key = data.route + "_" + data.date;

            if (table_flight_.find(key) != table_flight_.end() && table_routes_.find(data.route) != table_routes_.end()) {
                FlightForecast flight = table_flight_.at(key);
                Route route = table_routes_.at(data.route);

                if (data.price < route.price_min || data.price > route.price_max) {
                    std::string json_error = "{\"error\":\"Цена выходит за допустимые рамки "
                        + std::to_string(route.price_min) + " - "
                        + std::to_string(route.price_max) + " для этого маршрута.\"}";

                    return MakeStringResponse(http::status::bad_request, json_error, req.version(), req.keep_alive(), ContentType::APP_JSON);
                }

                double demand_5 = flight.base_demand[4];
                double demand = demand_5 - route.price_sensitivity * (data.price - route.base_price);
                double buy = std::min(static_cast<double>(flight.seats), std::max(0.0, demand));

                std::string json_response = "{"
                    "\"seats\": " + std::to_string(flight.seats) + ", "
                    "\"will_buy\": " + std::to_string(static_cast<int>(buy)) +
                "}";

                return MakeStringResponse(http::status::ok, json_response, req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            return MakeStringResponse(http::status::not_found, "{\"error\":\"Рейс или маршрут не найден\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
        }

        if (endpoint == "/api/register" && req.method() == http::verb::post) {
            AuthentificationData auth_data = ParseAuthJson(req.body());

            if (auth_data.username.empty() || auth_data.password.empty()) {
                return MakeStringResponse(http::status::bad_request, "{\"error\":\"Поля не могут быть пустыми\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            RegisterResult res;
            try {
                std::lock_guard<std::mutex> lock(db_mutex_);
                res = RegisterUserInDB(db_conn_, auth_data.username, auth_data.password);
            } catch (const std::exception&) {
                return MakeStringResponse(http::status::internal_server_error, "{\"error\":\"Ошибка сервера\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            if (res == RegisterResult::UserExists) {
                return MakeStringResponse(http::status::conflict, "{\"error\":\"Пользователь с таким логином уже существует\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            if (res == RegisterResult::Error) {
                return MakeStringResponse(http::status::internal_server_error, "{\"error\":\"Ошибка сервера\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            return MakeStringResponse(http::status::ok, "{\"message\":\"Регистрация успешна! :)\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
        }

        if (endpoint == "/api/login" && req.method() == http::verb::post) {
            AuthentificationData auth_data = ParseAuthJson(req.body());

            bool ok = false;
            try {
                std::lock_guard<std::mutex> lock(db_mutex_);

                ok = VerifyUserInDB(db_conn_, auth_data.username, auth_data.password);
            } catch (const std::exception& e) {
                return MakeStringResponse(http::status::internal_server_error, "{\"error\":\"Ошибка сервера\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            if (!ok) {
                return MakeStringResponse(http::status::unauthorized, "{\"error\":\"Неверный логин или пароль попробуйте снова :)\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            std::string token = gen.GenerateToken();
            try {
                std::lock_guard<std::mutex> lock(db_mutex_);

                SaveTokenToDB(db_conn_, token, auth_data.username);
            } catch (const std::exception& e) {
                return MakeStringResponse(http::status::internal_server_error, "{\"error\":\"Ошибка сохранения токена\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            std::string response = "{\"token\":\"" + token + "\"}";
            return MakeStringResponse(http::status::ok, response, req.version(), req.keep_alive(), ContentType::APP_JSON);
        }

        if (endpoint == "/api/tokens" && req.method() == http::verb::get) {
            auto token = GetAuthToken(req);
            if (!token) {
                return MakeStringResponse(http::status::unauthorized, "{\"error\":\"Требуется авторизация\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            std::optional<std::string> username;
            try {
                std::lock_guard<std::mutex> lock(db_mutex_);
                username = GetUsernameByToken(db_conn_, *token);
            } catch (const std::exception&) {
                return MakeStringResponse(http::status::internal_server_error, "{\"error\":\"Ошибка сервера\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            if (!username) {
                return MakeStringResponse(http::status::unauthorized, "{\"error\":\"Недействительный токен\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            std::string body = "{\"username\":\"" + *username + "\"}";
            return MakeStringResponse(http::status::ok, body, req.version(), req.keep_alive(), ContentType::APP_JSON);
        }

        if (endpoint == "/api/logout" && req.method() == http::verb::post) {
            auto token = GetAuthToken(req);
            if (!token) {
                return MakeStringResponse(http::status::unauthorized, "{\"error\":\"Требуется авторизация\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            try {
                std::lock_guard<std::mutex> lock(db_mutex_);
                DeleteTokenFromDB(db_conn_, *token);
            } catch (const std::exception&) {
                return MakeStringResponse(http::status::internal_server_error, "{\"error\":\"Ошибка сервера\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
            }

            return MakeStringResponse(http::status::ok, "{\"message\":\"Выход выполнен\"}", req.version(), req.keep_alive(), ContentType::APP_JSON);
        }

        if (endpoint == "/make_coffee" && req.method() == http::verb::get) {
            return MakeStringResponse(http::status::ok, "I'M TEAPOT!!!! I CAN'T MAKE COFFEE!!!!!!", req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
        }

        if (req.method() == http::verb::get) {
            std::string target = req.target();
            if (target == "/") target = "/login.html";

            if (target.find("..") != std::string::npos) {
                return MakeStringResponse(http::status::bad_request, "bad request", req.version(), req.keep_alive(), ContentType::TEXT_PLAIN);
            }

            if (target == "/login.html" || target == "/index.html" || target == "/style.css" || target == "/script.js" || target == "/auth.js") {
                std::string path = "front_end" + target;

                std::string_view content_type = ContentType::TEXT_HTML;
                if (auto content = ReadFile(path)) {
                    if (target.ends_with(".css")) content_type = ContentType::TEXT_CSS;
                    else if (target.ends_with(".js")) content_type = ContentType::APP_JS;

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

        while (1) {
            boost::asio::ip::tcp::socket socket(ioc_);
            acceptor.accept(socket);

            std::thread t([this](boost::asio::ip::tcp::socket sock) {
                try {
                    beast::flat_buffer buffer;
                    while (auto request = ReadRequest(sock, buffer)) {
                        DumpRequest(*request);
                        string_response response = HandleRequest(*std::move(request));
                        http::write(sock, response);
                        if (response.need_eof()) break;
                    }
                } catch (const std::exception& e) {
                    std::cerr << e.what() << std::endl;
                }
            }, std::move(socket));
            t.detach();
        }
    }

private:
    std::unordered_map<std::string, FlightForecast> table_flight_;
    boost::asio::io_context& ioc_;
    const boost::asio::ip::address address_;
    const unsigned short port_;
    std::unordered_map<std::string, Route> table_routes_;

    pqxx::connection& db_conn_;
    std::mutex db_mutex_;

    TokenGenerator gen;

    std::optional<std::string> GetAuthToken(const string_request& req) {
        auto h = req[http::field::authorization];
        if (h.empty())
            return std::nullopt;

        std::string_view sv(h.data(), h.size());
        constexpr std::string_view prefix = "Bearer "sv;

        if (!sv.starts_with(prefix)) return std::nullopt;
        return std::string(sv.substr(prefix.size()));
    }
};

int main() {
    try {
        const char* conn_str = std::getenv("DB_CONNECTION_STRING");
        std::string full_conn = conn_str ? conn_str : "postgresql://postgres:postgres@localhost:5432/avia_db";
        pqxx::connection conn(full_conn);

        auto flight_forecast_table = LoadForecastFromDB(conn);
        auto routes_table = LoadRoutesFromDB(conn);

        boost::asio::io_context ioc;
        Server server(flight_forecast_table, routes_table, ioc, boost::asio::ip::make_address("0.0.0.0"), 5000, conn);
        server.Run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}