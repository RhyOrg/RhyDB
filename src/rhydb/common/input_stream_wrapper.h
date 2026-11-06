#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>

#include <boost/iostreams/filtering_stream.hpp>

namespace rhydb {
class InputStreamWrapper {
   std::ifstream file_stream;
   std::unique_ptr<std::istream> input_stream;

   enum class Compression { NONE, ZSTD, XZ };

   struct ResolvedFile {
      std::filesystem::path path;
      Compression compression;
   };

   static ResolvedFile resolveFileWithEndings(const std::filesystem::path& file_path);
   static ResolvedFile resolveExactFile(const std::filesystem::path& file_path);

   explicit InputStreamWrapper(const ResolvedFile& resolved_file);

  public:
   /// Wrap a given istream in this class without applying decompression
   explicit InputStreamWrapper(std::unique_ptr<std::istream> existing_stream)
       : input_stream(std::move(existing_stream)) {}

   /// Opens `file_path`, or `file_path` + ".zst" / ".xz" if such a file exists (in that order of
   /// preference). Decompresses accordingly.
   static InputStreamWrapper openFileWithEndingLookup(const std::filesystem::path& file_path);

   /// Opens exactly `file_path` without looking for files with additional endings. Decompresses
   /// if the file ends with ".zst" or ".xz", otherwise reads it raw. Throws if the file does not
   /// exist.
   static InputStreamWrapper openExactFile(const std::filesystem::path& file_path);

   [[nodiscard]] std::istream& getInputStream() const;

   static InputStreamWrapper openFileOrStdIn(
      const std::optional<std::filesystem::path>& maybe_file_path
   );
};
}  // namespace rhydb
