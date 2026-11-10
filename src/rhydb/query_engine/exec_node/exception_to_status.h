#pragma once

#include <exception>
#include <utility>

#include <arrow/status.h>

namespace rhydb::query_engine::exec_node {

namespace detail {

template <typename Function, typename CallOperator>
struct CatchExceptionsAsStatus;

// The wrapper has the exact signature of the wrapped call operator, because arrow inspects the
// signature of continuations, so a generic lambda does not work.
template <typename Function, typename Return, typename Class, typename... Args>
struct CatchExceptionsAsStatus<Function, Return (Class::*)(Args...) const> {
   static auto wrap(Function function) {
      return [function = std::move(function)](Args... args) -> Return {
         try {
            return function(std::forward<Args>(args)...);
         } catch (const std::exception& exception) {
            return arrow::Status::ExecutionError(exception.what());
         }
      };
   }
};

template <typename Function, typename Return, typename Class, typename... Args>
struct CatchExceptionsAsStatus<Function, Return (Class::*)(Args...)> {
   static auto wrap(Function function) {
      return [function = std::move(function)](Args... args) mutable -> Return {
         try {
            return function(std::forward<Args>(args)...);
         } catch (const std::exception& exception) {
            return arrow::Status::ExecutionError(exception.what());
         }
      };
   }
};

}  // namespace detail

/// Wraps the lambda `function` so that a `std::exception` thrown by it is returned as an
/// `ExecutionError` instead. `function` must return something that can be constructed from an
/// `arrow::Status`, such as `arrow::Status`, `arrow::Result` or `arrow::Future`.
///
/// Use this for code that acero runs on executor threads: source node generators and the
/// continuations passed to `arrow::Future::Then`. Nothing catches exceptions there, so one that
/// escapes terminates the whole process instead of failing the query.
template <typename Function>
auto catchExceptionsAsStatus(Function function) {
   return detail::CatchExceptionsAsStatus<Function, decltype(&Function::operator())>::wrap(
      std::move(function)
   );
}

}  // namespace rhydb::query_engine::exec_node
