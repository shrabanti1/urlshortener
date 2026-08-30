#pragma once
#include <drogon/drogon.h>

#include <functional>
#include <string>
#include <vector>

#include "models/ClickEvent.h"

class AnalyticsRepository
{
  public:
    using ErrorCb = std::function<void(const std::string &)>;

    // Fire-and-forget: no success callback, because the redirect has already
    // been sent by the time this runs. Failures are logged only.
    void recordClick(const ClickEvent &event) const;

    // Same insert, but tells the caller when it has committed. Used only by
    // ANALYTICS_MODE=sync, which exists to demonstrate the cost.
    void recordClickAwait(const ClickEvent &event,
                          std::function<void()> onDone) const;

    void statsFor(long long urlId,
                  std::function<void(UrlStats)> onSuccess,
                  ErrorCb onError) const;

    void topReferrers(long long urlId,
                      int limit,
                      std::function<void(std::vector<std::pair<std::string, long long>>)> onSuccess,
                      ErrorCb onError) const;
};
