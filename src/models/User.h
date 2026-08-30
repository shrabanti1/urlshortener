#pragma once
#include <string>

struct User
{
    long long id = 0;
    std::string email;
    std::string passwordHash;
};
