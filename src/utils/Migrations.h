#pragma once
#include <string>

namespace migrations {

// Applies db/schema.sql then db/migrations/*.sql in filename order.
//
// On Docker Compose the postgres image runs these once, from
// /docker-entrypoint-initdb.d. A managed database (Render, Fly, RDS) offers no
// such hook, so the application applies them itself at start-up.
//
// Every statement is written to be idempotent (CREATE ... IF NOT EXISTS, and a
// guarded sequence reset), so re-running on every boot is safe.
// Runs BEFORE drogon::app().run(), so it cannot use the framework's DbClient:
// that one has no event loop yet and using it segfaults. This creates its own
// short-lived client, which spawns its own thread.
//
// Returns false and fills problemOut if anything fails.
bool run(const std::string &directory,
         const std::string &connInfo,
         std::string &problemOut);

}  // namespace migrations
