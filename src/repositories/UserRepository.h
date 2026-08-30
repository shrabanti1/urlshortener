#pragma once
#include <drogon/drogon.h>

#include <functional>
#include <optional>
#include <string>

#include "models/User.h"

class UserRepository
{
  public:
    using ErrorCb = std::function<void(const std::string &)>;

    // onSuccess receives nullopt if the email is already registered.
    void create(const std::string &email,
                const std::string &passwordHash,
                std::function<void(std::optional<User>)> onSuccess,
                ErrorCb onError) const;

    void findByEmail(const std::string &email,
                     std::function<void(std::optional<User>)> onSuccess,
                     ErrorCb onError) const;
};
