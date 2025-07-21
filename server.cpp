#include <iostream>
#include <string>
#include <sstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <pqxx/pqxx>
#include <sw/redis++/redis++.h>
#include "generate_short_urls.h"
#include "redis_global.h"

namespace http = boost::beast::http;
namespace pt = boost::property_tree;
using tcp = boost::asio::ip::tcp;

std::string GetPostgresLog(const std::string& file_path) {
    std::fstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("File with Postgres log is not open: " + file_path);
    }
    std::stringstream buf;
    std::string line;

    while (std::getline(file, line)) {
        buf << line << "\n";
    }
    return buf.str();

}

std::string postgres_log_ = GetPostgresLog("PostgresLog.txt");


std::pair<std::string, int> tryFindInRedis(const std::string& short_code) {
    try {
        std::string redis_key = "url:" + short_code;
        std::cout << "Looking in Redis for key: " << redis_key << std::endl;

        auto cached = redis_client.get(redis_key);

        if (cached) {
            std::cout << "Redis raw data: " << *cached << std::endl;

            // Парсим JSON
            pt::ptree tree;
            std::istringstream iss(*cached);
            pt::read_json(iss, tree);

            std::string url = tree.get<std::string>("url", "");
            int id = tree.get<int>("id", -1);

            std::cout << "Parsed from Redis - url: " << url
                << ", id: " << id << std::endl;

            return { url, id };
        }
        else {
            std::cout << "Key not found in Redis" << std::endl;
            return { "", -1 };
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Redis ERROR: " << e.what() << std::endl;
        return { "", -1 };
    }
}

// 2. Функция поиска в PostgreSQL - возвращает URL и id
std::pair<std::string, int> tryFindInPostgres(const std::string& short_code) {
    try {
        pqxx::connection conn(postgres_log_);
        pqxx::work txn(conn);

        auto result = txn.exec(
            "SELECT original_url, id FROM short_urls WHERE short_code = $1 LIMIT 1",
            pqxx::params(short_code)
        );

        if (!result.empty()) {
            return {
                result[0][0].as<std::string>(), // URL
                result[0][1].as<int>()          // ID
            };
        }
    }
    catch (const std::exception& e) {
        std::cerr << "PostgreSQL error: " << e.what() << std::endl;
        return { "", -1 }; // Не нашли
    }
}

// Создание короткой ссылки и сохранение ее в БД
void handleShorten(http::request<http::string_body>& req, http::response<http::string_body>& res) {
        // Парсинг JSON запроса
        pt::ptree request_json;

        std::istringstream iss(req.body()); // стримим запрос

        pt::read_json(iss, request_json);

        // Извлечение данных
        std::string url = request_json.get<std::string>("url");
        int user_id = request_json.get<int>("user_id", 0);

        // Генерация короткого кода
        std::string short_code = generateShortCode();

        // Сохранение в PostgreSQL

        pqxx::connection conn(postgres_log_);

        // Проверяем, что соединение активно
        if (!conn.is_open()) {
            throw std::runtime_error("Cannot open database connection");
        }
        pqxx::work txn(conn);

        txn.exec(
            "INSERT INTO short_urls (original_url, short_code) VALUES ($1, $2)",
            pqxx::params{ url, short_code }
        );

        txn.commit();


        // Создание ответа
        pt::ptree response_json;
        response_json.put("short_url", short_code);
        response_json.put("original_url", url);
        response_json.put("user_id", user_id);

        std::ostringstream oss;
        pt::write_json(oss, response_json);

        // Установка ответа
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = oss.str();
        res.prepare_payload(); //подготовка перед отправкой
}

// Обработка редиректа
void handle_redirect(
    const std::string& short_code,
    http::response<http::string_body>& res,
    std::string client_ip,
    const std::string& user_agent)
{
    try {
        auto [original_url, url_id] = tryFindInRedis(short_code);

        if (original_url.empty()) {
            auto [url, id] = tryFindInPostgres(short_code);
            if (url.empty()) {
                res.result(http::status::not_found);
                res.body() = "404: URL not found";
                return;
            }

            original_url = url;
            url_id = id;

            // Кэшируем на будущее
            pt::ptree cache_json;
            cache_json.put("url", original_url);
            cache_json.put("id", url_id);

            std::ostringstream oss;
            pt::write_json(oss, cache_json);

            redis_client.setex("url:" + short_code, 3600, oss.str());
        }

        // Сохраняем аналитику
            try {
                pqxx::connection conn(postgres_log_);
                pqxx::work txn(conn);

                txn.exec(
                    "INSERT INTO visits (short_url_id, ip_address, user_agent) "
                    "VALUES ($1, $2, $3)",
                    pqxx::params(
                        url_id,
                        client_ip,
                        user_agent
                    )
                );
                txn.commit();
            }
            catch (const std::exception& e) {
                throw std::runtime_error("PostgreSQL analytics error: " + std::string(e.what()));
            }
        

        //Перенаправление
        res.set(http::field::location, original_url);
        res.result(http::status::moved_permanently);
    }
    catch (const std::exception& e) {
        std::cerr << "Redirect error: " << e.what() << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Server error";
    }
}

void handleRequest(const std::string& client_ip, http::request<http::string_body>& req, http::response<http::string_body>& res) {
    try {
        // Обработка /shorten
        if (req.target() == "/shorten") {
            if (req.method() == http::verb::post) {
                if (req[http::field::content_type] == "application/json") {
                    handleShorten(req, res);
                }
                else {
                    res.result(http::status::bad_request);
                    res.body() = "Invalid Content-Type. Expected application/json";
                    res.prepare_payload();
                }
            }
            else {
                res.result(http::status::method_not_allowed);
                res.set(http::field::allow, "POST");
                res.body() = "Method Not Allowed";
                res.prepare_payload();
            }
        }
        // Обработка коротких URL
        else if (req.method() == http::verb::get) {
            try {
                // Безопасное получение target
                std::string_view target_view = req.target();
                std::string target(target_view.begin(), target_view.end());

                // Проверка формата URL
                if (target.size() <= 1 || target[0] != '/') {
                    res.result(http::status::bad_request);
                    res.body() = "Invalid URL format. Expected '/short_code'";
                    res.prepare_payload();
                    return;
                }

                std::string short_code;
                short_code = target.substr(1);

                // Безопасное получение User-Agent
                std::string user_agent;
                if (req.count(http::field::user_agent)) {
                    std::string_view ua_view = req[http::field::user_agent];
                    user_agent.assign(ua_view.begin(), ua_view.end());
                }
                else {
                    user_agent = "unknown";
                }

                // Логирование перед вызовом
                std::cout << "Redirect attempt:"
                    << "\n  Short code: " << short_code
                    << "\n  Client IP: " << client_ip
                    << "\n  User-Agent: " << user_agent << std::endl;

                // Вызов с обработкой исключений
                try {
                    handle_redirect(short_code, res, client_ip, user_agent);
                }
                catch (const std::exception& e) {
                    std::cerr << "Redirect failed: " << e.what() << std::endl;
                    throw; // Перебрасываем в основной catch
                }
            }
            catch (const std::exception& e) {
                res.result(http::status::internal_server_error);
                res.body() = "Internal error: " + std::string(e.what());
                res.prepare_payload();
            }
        }
        else {
            res.result(http::status::not_found);
            res.body() = "404: Not Found";
            res.prepare_payload();
        }
    }
    catch (const std::exception& e) {
        res.result(http::status::internal_server_error);
        res.body() = "Internal Server Error: " + std::string(e.what());
        res.prepare_payload();
    }
}

void runServer(boost::asio::io_context& io, unsigned short port) {
	tcp::acceptor acceptor(io, { tcp::v4(), port });
	std::cout << "Server started on port " << port << std::endl;

	while (true) {
		tcp::socket socket(io);
		acceptor.accept(socket);
        std::string client_ip = socket.remote_endpoint().address().to_string();

		try {
			boost::beast::flat_buffer buf;
			http::request<http::string_body> req;
			http::read(socket, buf, req);

			http::response<http::string_body> res;
			handleRequest(client_ip, req, res);

			http::write(socket, res);
		}
		catch(const std::exception& e){
			std::cerr << "Error: " << e.what() << std::endl;
		}
	}
}