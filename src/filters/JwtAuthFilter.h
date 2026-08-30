#pragma once
#include <drogon/HttpFilter.h>

// A Drogon filter runs BEFORE the controller. Registering it on a route means
// the controller body simply cannot execute without a valid token -- the check
// cannot be forgotten in one handler.
class JwtAuthFilter : public drogon::HttpFilter<JwtAuthFilter>
{
  public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};
