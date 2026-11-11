#include "rhydb/query_engine/query_execution_error.h"

#include <string>

#include <arrow/status.h>
#include <gtest/gtest.h>

namespace {

using rhydb::query_engine::isQueryExecutionError;
using rhydb::query_engine::markAsQueryExecutionError;
using rhydb::query_engine::QueryExecutionException;

TEST(QueryExecutionError, marksAndRecognizesAStatus) {
   const auto status = markAsQueryExecutionError(arrow::Status::Invalid("bad value"));
   EXPECT_TRUE(isQueryExecutionError(status));
   EXPECT_EQ(status.message(), "bad value");
}

TEST(QueryExecutionError, doesNotRecognizeAnUntaggedStatus) {
   EXPECT_FALSE(isQueryExecutionError(arrow::Status::Invalid("bad value")));
   EXPECT_FALSE(isQueryExecutionError(arrow::Status::ExecutionError("boom")));
}

TEST(QueryExecutionError, doesNotRecognizeAnOkStatus) {
   EXPECT_FALSE(isQueryExecutionError(arrow::Status::OK()));
}

TEST(QueryExecutionError, exceptionCarriesTheBareMessage) {
   const QueryExecutionException exception{"could not convert value"};
   EXPECT_EQ(std::string{exception.what()}, "could not convert value");
}

}  // namespace
