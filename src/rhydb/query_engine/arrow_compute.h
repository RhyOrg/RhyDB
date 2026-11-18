#pragma once

namespace rhydb::query_engine {

/// Registers Arrow's compute functions, which query execution needs, e.g. the sorting of `order`.
/// Without them, such a query returns no rows. Idempotent: the registration runs once.
/// `Database` calls this on construction, so only code that executes query engine components
/// without a `Database` (e.g. unit tests) needs to call it itself.
void initializeArrowCompute();

}  // namespace rhydb::query_engine
