#pragma once
#include <string>

struct ClickEvent
{
    long long urlId = 0;
    std::string referrer;
    std::string userAgent;
    std::string ipHash;
};

struct UrlStats
{
    long long totalClicks = 0;
    long long todayClicks = 0;
    long long uniqueVisitors = 0;
    std::string lastClickedAt;
};
