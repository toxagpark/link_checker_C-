#include "redis_global.h"
#include <iostream>

sw::redis::Redis redis_client("tcp://127.0.0.1:6379");  // ���������� ���������� ����������

void initRedis() {
    try {
        redis_client.ping();  // ��������� �����������
        std::cout << "Connected to Redis successfully" << std::endl;
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Redis connection error: " + std::string(e.what()));
    }
}