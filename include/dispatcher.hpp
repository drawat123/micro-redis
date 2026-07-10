#pragma once

#include "store.hpp"
#include <string>
#include <string_view>
#include <vector>

std::string dispatch(const std::vector<std::string_view> &args, Store &store,
                     TimePoint now);
