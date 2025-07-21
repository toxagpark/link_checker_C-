#pragma once

#include <sw/redis++/redis++.h>

extern sw::redis::Redis redis_client;  // ќбъ€вл€ем глобальную переменную
void initRedis();  // ‘ункци€ инициализации