#include <iostream>
#include <boost/asio.hpp>
#include "redis_global.h"


void runServer(boost::asio::io_context& io_context, unsigned short port); //из server.cpp

int main() {
    initRedis();

    // Запуск сервера
    try {
        boost::asio::io_context io_context;
        runServer(io_context, 8080);
    }
    catch (const std::exception& e) {
        std::cerr << "Run Server Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}