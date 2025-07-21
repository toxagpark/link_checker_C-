#pragma once

#include <random>
#include <string>

std::string generateShortCode() {
    int length = 6;
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, chars.size() - 1);

    std::string code;
    for (int i = 0; i < length; ++i) {
        code += chars[dis(gen)];
    }
    return code;
}