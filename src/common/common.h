#pragma once

#include "span.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;
template<typename T>
using sptr = std::shared_ptr<T>;
template<typename T>
using uptr = std::unique_ptr<T>;
using std::move;
using std::optional;
using std::ostream;
using std::size_t;
using std::stringstream;
