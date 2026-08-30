#include "ClickBatcher.h"

#include "utils/Config.h"

namespace {

size_t batchSize() { return static_cast<size_t>(config::getInt("ANALYTICS_BATCH_SIZE", 100)); }
double flushSeconds() { return config::getInt("ANALYTICS_FLUSH_MS", 1000) / 1000.0; }

}  // namespace

ClickBatcher &ClickBatcher::instance()
{
    static ClickBatcher batcher;
    return batcher;
}

void ClickBatcher::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    running_ = true;
    scheduleTimer();
    LOG_INFO << "click batching enabled (size=" << batchSize()
             << ", interval=" << flushSeconds() << "s)";
}

void ClickBatcher::scheduleTimer()
{
    // A repeating timer bounds latency: a quiet URL still gets its clicks
    // written within one interval instead of waiting for the buffer to fill.
    timerId_ = drogon::app().getLoop()->runEvery(flushSeconds(),
                                                 [this] { flushNow(); });
}

void ClickBatcher::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
        if (timerId_ != 0)
        {
            drogon::app().getLoop()->invalidateTimer(timerId_);
            timerId_ = 0;
        }
    }
    flushNow();
}

void ClickBatcher::add(const ClickEvent &event)
{
    std::vector<ClickEvent> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(event);
        if (buffer_.size() < batchSize()) return;
        // Swap the full buffer out under the lock, then write outside it so a
        // slow database never blocks incoming redirects.
        ready.swap(buffer_);
    }
    flushLocked(std::move(ready));
}

void ClickBatcher::flushNow()
{
    std::vector<ClickEvent> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) return;
        ready.swap(buffer_);
    }
    flushLocked(std::move(ready));
}

size_t ClickBatcher::bufferedCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

void ClickBatcher::flushLocked(std::vector<ClickEvent> &&batch)
{
    if (batch.empty()) return;

    // UNNEST turns four arrays into rows, so the whole batch is one statement
    // with four parameters -- no matter how many events. Building
    // "VALUES ($1,$2,...),($5,...)" instead would need a new prepared
    // statement for every distinct batch size.
    std::vector<long long> urlIds;
    std::vector<std::string> referrers, agents, ipHashes;
    urlIds.reserve(batch.size());
    referrers.reserve(batch.size());
    agents.reserve(batch.size());
    ipHashes.reserve(batch.size());

    auto pgArray = [](const std::vector<std::string> &values)
    {
        std::string out = "{";
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (i) out += ',';
            out += '"';
            for (char c : values[i])
            {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            out += '"';
        }
        out += '}';
        return out;
    };

    std::string idArray = "{";
    for (size_t i = 0; i < batch.size(); ++i)
    {
        if (i) idArray += ',';
        idArray += std::to_string(batch[i].urlId);
        referrers.push_back(batch[i].referrer);
        agents.push_back(batch[i].userAgent);
        ipHashes.push_back(batch[i].ipHash);
    }
    idArray += '}';

    const size_t n = batch.size();

    drogon::app().getDbClient()->execSqlAsync(
        "INSERT INTO click_events (url_id, referrer, user_agent, ip_hash) "
        "SELECT * FROM UNNEST($1::bigint[], $2::text[], $3::text[], $4::text[])",
        [n](const drogon::orm::Result &)
        { LOG_DEBUG << "flushed " << n << " click events"; },
        [n](const drogon::orm::DrogonDbException &e)
        {
            // Losing analytics must never affect serving.
            LOG_WARN << "click batch of " << n << " lost: " << e.base().what();
        },
        idArray, pgArray(referrers), pgArray(agents), pgArray(ipHashes));
}
