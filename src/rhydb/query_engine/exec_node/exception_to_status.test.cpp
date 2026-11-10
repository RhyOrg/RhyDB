#include "rhydb/query_engine/exec_node/exception_to_status.h"

#include <stdexcept>

#include <arrow/result.h>
#include <arrow/status.h>
#include <arrow/util/future.h>
#include <gtest/gtest.h>

using rhydb::query_engine::exec_node::catchExceptionsAsStatus;

TEST(CatchExceptionsAsStatus, passesThroughArgumentsAndResult) {
   auto add = catchExceptionsAsStatus([](int left, int right) -> arrow::Result<int> {
      return left + right;
   });
   const arrow::Result<int> result = add(2, 3);
   ASSERT_TRUE(result.ok());
   EXPECT_EQ(result.ValueOrDie(), 5);
}

TEST(CatchExceptionsAsStatus, passesThroughErrorStatus) {
   auto fail = catchExceptionsAsStatus([] { return arrow::Status::Invalid("invalid"); });
   const arrow::Status status = fail();
   EXPECT_TRUE(status.IsInvalid());
   EXPECT_EQ(status.message(), "invalid");
}

TEST(CatchExceptionsAsStatus, returnsExceptionAsExecutionError) {
   auto throwing =
      catchExceptionsAsStatus([] -> arrow::Result<int> { throw std::runtime_error("some error"); });
   const arrow::Result<int> result = throwing();
   EXPECT_TRUE(result.status().IsExecutionError());
   EXPECT_EQ(result.status().message(), "some error");
}

TEST(CatchExceptionsAsStatus, returnsExceptionAsFailedFuture) {
   auto throwing =
      catchExceptionsAsStatus([] -> arrow::Future<int> { throw std::runtime_error("some error"); });
   const arrow::Future<int> future = throwing();
   ASSERT_TRUE(future.is_finished());
   EXPECT_TRUE(future.status().IsExecutionError());
   EXPECT_EQ(future.status().message(), "some error");
}

// Without the wrapper, the exception would escape `MarkFinished`, which on an executor thread
// terminates the process
TEST(CatchExceptionsAsStatus, failsTheFutureOfAThrowingContinuation) {
   auto input = arrow::Future<int>::Make();
   auto output = input.Then(catchExceptionsAsStatus([](const int& /*value*/) -> arrow::Result<int> {
      throw std::runtime_error("some error");
   }));
   input.MarkFinished(1);
   ASSERT_TRUE(output.is_finished());
   EXPECT_TRUE(output.status().IsExecutionError());
   EXPECT_EQ(output.status().message(), "some error");
}
