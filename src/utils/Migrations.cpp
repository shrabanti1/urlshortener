#include "Migrations.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path &p)
{
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Splits a script into individual statements.
//
// Drogon sends SQL over the extended query protocol, which accepts exactly one
// statement per call ("cannot insert multiple commands into a prepared
// statement"). Splitting on ';' naively is wrong, because a semicolon can sit
// inside a string literal, a comment, or a dollar-quoted block -- and our
// schema uses DO $$ ... $$ precisely to guard the sequence reset.
std::vector<std::string> splitStatements(const std::string &sql)
{
    std::vector<std::string> out;
    std::string current;

    enum class State { Normal, LineComment, BlockComment, SingleQuote, DollarQuote };
    State state = State::Normal;
    std::string dollarTag;  // e.g. "$$" or "$body$"

    for (size_t i = 0; i < sql.size(); ++i)
    {
        const char c = sql[i];
        const char next = (i + 1 < sql.size()) ? sql[i + 1] : '\0';

        switch (state)
        {
            case State::Normal:
                if (c == '-' && next == '-') { state = State::LineComment; current += c; break; }
                if (c == '/' && next == '*') { state = State::BlockComment; current += c; break; }
                if (c == '\'')             { state = State::SingleQuote; current += c; break; }
                if (c == '$')
                {
                    // A dollar quote opens with $tag$ where tag is empty or an
                    // identifier. Anything else is just a parameter marker.
                    const auto close = sql.find('$', i + 1);
                    if (close != std::string::npos)
                    {
                        const std::string tag = sql.substr(i, close - i + 1);
                        bool looksLikeTag = true;
                        for (size_t k = 1; k + 1 < tag.size(); ++k)
                            if (!std::isalnum(static_cast<unsigned char>(tag[k])) && tag[k] != '_')
                                looksLikeTag = false;
                        if (looksLikeTag)
                        {
                            dollarTag = tag;
                            state = State::DollarQuote;
                            current += tag;
                            i = close;
                            break;
                        }
                    }
                    current += c;
                    break;
                }
                if (c == ';')
                {
                    out.push_back(current);
                    current.clear();
                    break;
                }
                current += c;
                break;

            case State::LineComment:
                current += c;
                if (c == '\n') state = State::Normal;
                break;

            case State::BlockComment:
                current += c;
                if (c == '*' && next == '/') { current += next; ++i; state = State::Normal; }
                break;

            case State::SingleQuote:
                current += c;
                // '' is an escaped quote, not the end of the literal.
                if (c == '\'')
                {
                    if (next == '\'') { current += next; ++i; }
                    else state = State::Normal;
                }
                break;

            case State::DollarQuote:
                if (c == '$' && sql.compare(i, dollarTag.size(), dollarTag) == 0)
                {
                    current += dollarTag;
                    i += dollarTag.size() - 1;
                    state = State::Normal;
                    break;
                }
                current += c;
                break;
        }
    }
    out.push_back(current);

    // Drop anything that is only whitespace or comments.
    std::vector<std::string> statements;
    for (auto &stmt : out)
    {
        bool hasCode = false;
        for (size_t i = 0; i < stmt.size(); ++i)
        {
            if (std::isspace(static_cast<unsigned char>(stmt[i]))) continue;
            if (stmt[i] == '-' && i + 1 < stmt.size() && stmt[i + 1] == '-')
            {
                const auto nl = stmt.find('\n', i);
                if (nl == std::string::npos) break;
                i = nl;
                continue;
            }
            hasCode = true;
            break;
        }
        if (hasCode) statements.push_back(stmt);
    }
    return statements;
}

}  // namespace

namespace migrations {

bool run(const std::string &directory,
         const std::string &connInfo,
         std::string &problemOut)
{
    const fs::path root(directory);
    if (!fs::exists(root))
    {
        problemOut = "migrations directory not found: " + root.string();
        return false;
    }

    std::vector<fs::path> files;
    const fs::path schema = root / "schema.sql";
    if (fs::exists(schema)) files.push_back(schema);

    const fs::path migrationsDir = root / "migrations";
    if (fs::exists(migrationsDir))
    {
        std::vector<fs::path> numbered;
        for (const auto &entry : fs::directory_iterator(migrationsDir))
            if (entry.path().extension() == ".sql") numbered.push_back(entry.path());
        // Filenames are numbered (002_, 003_, ...) so lexicographic order is
        // the intended order.
        std::sort(numbered.begin(), numbered.end());
        files.insert(files.end(), numbered.begin(), numbered.end());
    }

    if (files.empty())
    {
        problemOut = "no .sql files found under " + root.string();
        return false;
    }

    // Held in a static so it outlives this call. Destroying a DbClient while
    // its connection thread is still winding down throws bad_weak_ptr.
    static drogon::orm::DbClientPtr db;
    try
    {
        db = drogon::orm::DbClient::newPgClient(connInfo, 1);
    }
    catch (const std::exception &e)
    {
        problemOut = std::string("could not connect for migrations: ") + e.what();
        return false;
    }
    if (!db)
    {
        problemOut = "could not create a database client for migrations";
        return false;
    }

    for (const auto &file : files)
    {
        const std::string sql = readFile(file);
        if (sql.empty()) continue;

        const auto statements = splitStatements(sql);
        for (size_t n = 0; n < statements.size(); ++n)
        {
            try
            {
                // Synchronous on purpose: this runs before the listener starts,
                // so no request can arrive against a half-built schema.
                db->execSqlSync(statements[n]);
            }
            catch (const std::exception &e)
            {
                problemOut = "migration " + file.filename().string() + " statement " +
                             std::to_string(n + 1) + " failed: " + e.what();
                return false;
            }
        }
        LOG_INFO << "migration applied: " << file.filename().string() << " ("
                 << statements.size() << " statements)";
    }
    return true;
}

}  // namespace migrations
