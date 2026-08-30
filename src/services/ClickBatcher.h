#pragma once
#include <drogon/drogon.h>

#include <mutex>
#include <vector>

#include "models/ClickEvent.h"

// Buffers click events and writes them in one multi-row INSERT.
//
// One INSERT per click is the first thing to break under load: at 5,000
// redirects/second that is 5,000 statements/second, each with its own round
// trip and transaction. Batching amortises both.
//
// The cost is durability: a crash loses whatever is still buffered. That is an
// acceptable trade for analytics, which are statistical rather than
// transactional -- but it is why the buffer is small and flushed on a timer as
// well as by size.
class ClickBatcher
{
  public:
    static ClickBatcher &instance();

    // Starts the periodic flush timer. Must be called after the event loop is
    // running (drogon::app().getLoop() is not usable before then).
    void start();

    // Flushes and stops. Called on shutdown so the buffer is not lost.
    void stop();

    void add(const ClickEvent &event);

    // Forces a write of whatever is buffered. Used by tests and by stop().
    void flushNow();

    size_t bufferedCount() const;

  private:
    ClickBatcher() = default;

    void scheduleTimer();
    void flushLocked(std::vector<ClickEvent> &&batch);

    mutable std::mutex mutex_;
    std::vector<ClickEvent> buffer_;
    bool running_ = false;
    trantor::TimerId timerId_ = 0;
};
