#pragma once

#include <sw/redis++/redis++.h>

extern sw::redis::Redis redis_client;  // ��������� ���������� ����������
void initRedis();  // ������� �������������