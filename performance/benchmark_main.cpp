// Entry point for the merged performance benchmark binary (rhydb_benchmark).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <exception>

#include <spdlog/spdlog.h>

#include "sequence_generator.h"

#include "rhydb/query_engine/arrow_compute.h"

int main(int argc, char** argv) {
   try {
      changeCwdToTestFolder();
   } catch (const std::exception& exception) {
      SPDLOG_ERROR("{}", exception.what());
      return EXIT_FAILURE;
   }
   rhydb::query_engine::initializeArrowCompute();
   ::testing::InitGoogleMock(&argc, argv);
   return RUN_ALL_TESTS();
}
