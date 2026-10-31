#include "gb/media/chapters.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <system_error>

namespace gb::media {
namespace {

std::string upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return text;
}

bool digits(const std::string &text) {
  return std::all_of(text.begin(), text.end(),
                     [](const unsigned char c) { return std::isdigit(c) != 0; });
}

} // namespace

std::optional<ChapterName> parse_chapter_name(const std::string &file_name) {
  // GSnnmmmm.360 / GHnnmmmm.MP4 / GXnnmmmm.MP4: two letters, six digits, a dot and three
  // characters.
  if (file_name.size() != 12U || file_name[8] != '.')
    return std::nullopt;
  const auto prefix = upper(file_name.substr(0, 2));
  const auto extension = upper(file_name.substr(8));
  if (!((prefix == "GS" && extension == ".360") ||
        ((prefix == "GH" || prefix == "GX") && extension == ".MP4"))) {
    return std::nullopt;
  }
  const auto chapter = file_name.substr(2, 2);
  const auto recording = file_name.substr(4, 4);
  if (!digits(chapter) || !digits(recording) || chapter == "00")
    return std::nullopt;
  return ChapterName{file_name.substr(0, 2), std::stoi(chapter), recording, file_name.substr(8)};
}

std::vector<std::filesystem::path> chapter_sequence(const std::filesystem::path &path) {
  const auto self = parse_chapter_name(path.filename().string());
  if (!self.has_value())
    return {path};
  auto directory = path.parent_path();
  if (directory.empty())
    directory = ".";
  std::map<int, std::filesystem::path> chapters;
  std::error_code error;
  for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
       it.increment(error)) {
    std::error_code type_error;
    if (!it->is_regular_file(type_error))
      continue;
    const auto name = parse_chapter_name(it->path().filename().string());
    if (name.has_value() && name->chapter != self->chapter &&
        upper(name->prefix) == upper(self->prefix) && name->recording == self->recording &&
        upper(name->extension) == upper(self->extension)) {
      chapters.emplace(name->chapter, directory / it->path().filename());
    }
  }
  // The opened file stands for its own chapter, as given.
  chapters.insert_or_assign(self->chapter, path);
  std::vector<std::filesystem::path> result;
  result.reserve(chapters.size());
  for (auto &[chapter, file] : chapters)
    result.push_back(std::move(file));
  return result;
}

} // namespace gb::media
