#include "rhydb/common/input_stream_wrapper.h"

#include <fstream>
#include <iostream>
#include <string>

#include <spdlog/spdlog.h>
#include <boost/iostreams/detail/error.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <boost/iostreams/filtering_stream.hpp>

#include "rhydb/preprocessing/preprocessing_exception.h"

#if RHYDB_WITH_LZMA
#include <boost/iostreams/filter/lzma.hpp>
#endif

namespace {

std::filesystem::path withXZending(const std::filesystem::path& file_path) {
   return {file_path.extension() == ".xz" ? file_path.string() : file_path.string() + ".xz"};
}

std::filesystem::path withZSTending(const std::filesystem::path& file_path) {
   return {file_path.extension() == ".zst" ? file_path.string() : file_path.string() + ".zst"};
}

}  // namespace

namespace rhydb {
InputStreamWrapper::InputStreamWrapper(const ResolvedFile& resolved_file) {
   const auto& resolved_file_path = resolved_file.path;
   auto boost_input_stream = std::make_unique<boost::iostreams::filtering_istream>();
   switch (resolved_file.compression) {
      case Compression::ZSTD:
         boost_input_stream->push(boost::iostreams::zstd_decompressor());
         break;
      case Compression::XZ:
#if RHYDB_WITH_LZMA
         boost_input_stream->push(boost::iostreams::lzma_decompressor());
         break;
#else
         // Built without lzma (e.g. the WASM build). Fail fast instead of letting the
         // .xz file fall through and be read as if it were uncompressed.
         throw preprocessing::PreprocessingException(
            "This build does not support .xz (lzma) inputs; found: " + resolved_file_path.string()
         );
#endif
      case Compression::NONE:
         break;
   }
   file_stream = std::ifstream(resolved_file_path, std::ios::binary);
   if (!file_stream) {
      throw preprocessing::PreprocessingException(
         "Could not open input file: " + resolved_file_path.string()
      );
   }
   boost_input_stream->push(file_stream);
   // Without this, std::getline swallows decompression errors (e.g. corrupt or truncated input)
   // by only setting badbit, which readers cannot tell apart from the end of the input.
   boost_input_stream->exceptions(std::ios::badbit);
   input_stream = std::move(boost_input_stream);
}

InputStreamWrapper::ResolvedFile InputStreamWrapper::resolveFileWithEndings(
   const std::filesystem::path& file_path
) {
   if (std::filesystem::is_regular_file(withZSTending(file_path))) {
      SPDLOG_INFO("Detected file-ending .zst for input file " + file_path.string());
      return {.path = withZSTending(file_path), .compression = Compression::ZSTD};
   }
   if (std::filesystem::is_regular_file(withXZending(file_path))) {
      SPDLOG_INFO("Detected file-ending .xz for input file " + file_path.string());
      return {.path = withXZending(file_path), .compression = Compression::XZ};
   }
   if (std::filesystem::is_regular_file(file_path)) {
      SPDLOG_INFO(
         "Detected file without specialized ending, processing raw: " + file_path.string()
      );
      return {.path = file_path, .compression = Compression::NONE};
   }
   throw preprocessing::PreprocessingException(
      "Cannot find file with name or associated endings (.xz, .zst): " + file_path.string()
   );
}

InputStreamWrapper::ResolvedFile InputStreamWrapper::resolveExactFile(
   const std::filesystem::path& file_path
) {
   if (!std::filesystem::is_regular_file(file_path)) {
      throw preprocessing::PreprocessingException("Cannot find input file: " + file_path.string());
   }
   if (file_path.extension() == ".zst") {
      return {.path = file_path, .compression = Compression::ZSTD};
   }
   if (file_path.extension() == ".xz") {
      return {.path = file_path, .compression = Compression::XZ};
   }
   return {.path = file_path, .compression = Compression::NONE};
}

InputStreamWrapper InputStreamWrapper::openFileWithEndingLookup(
   const std::filesystem::path& file_path
) {
   return InputStreamWrapper{resolveFileWithEndings(file_path)};
}

InputStreamWrapper InputStreamWrapper::openExactFile(const std::filesystem::path& file_path) {
   return InputStreamWrapper{resolveExactFile(file_path)};
}

std::istream& InputStreamWrapper::getInputStream() const {
   return *input_stream;
}

InputStreamWrapper InputStreamWrapper::openFileOrStdIn(
   const std::optional<std::filesystem::path>& maybe_file_path
) {
   if (maybe_file_path.has_value()) {
      SPDLOG_DEBUG("Given input file: {}", maybe_file_path.value().string());
      return openFileWithEndingLookup(maybe_file_path.value());
   }
   SPDLOG_DEBUG("No input file given, instead opening stdin");
   return InputStreamWrapper{std::make_unique<std::istream>(std::cin.rdbuf())};
}

}  // namespace rhydb
