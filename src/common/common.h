#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstddef>
#include <sstream>
#include <ostream>
#include <utility>
#include <optional>
using std::string;
using std::vector;
using std::unordered_map;
template<typename T>
using sptr = std::shared_ptr<T>;
template<typename T>
using uptr = std::unique_ptr<T>;
using std::size_t;
using std::ostream;
using std::stringstream;
using std::move;
using std::optional;