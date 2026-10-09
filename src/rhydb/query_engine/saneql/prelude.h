#pragma once

#include "rhydb/query_engine/saneql/function_definitions.h"

namespace rhydb::query_engine::saneql {

/// The builtin definitions available to every query (see function_definitions.h): the native
/// definitions, and the prelude of `let` definitions, which are written in SaneQL in terms of the
/// functions of the query engine and see the native definitions.
[[nodiscard]] const FunctionDefinitions& builtinDefinitions();

}  // namespace rhydb::query_engine::saneql
