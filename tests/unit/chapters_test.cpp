#include "gb/media/chapters.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void touch(const std::filesystem::path &path) { std::ofstream(path).put('x'); }

std::vector<std::string> names(const std::vector<std::filesystem::path> &paths) {
  std::vector<std::string> result;
  for (const auto &path : paths)
    result.push_back(path.filename().string());
  return result;
}

} // namespace

int main() {
  try {
    const auto max = gb::media::parse_chapter_name("GS040047.360");
    require(max.has_value() && max->prefix == "GS" && max->chapter == 4 &&
                max->recording == "0047" && max->extension == ".360",
            "MAX chapter name not parsed");
    const auto hero = gb::media::parse_chapter_name("gh050083.mp4");
    require(hero.has_value() && hero->chapter == 5 && hero->recording == "0083" &&
                hero->extension == ".mp4",
            "lower-case HERO chapter name not parsed");
    require(!gb::media::parse_chapter_name("GS040047.MP4"), "GS must pair with .360");
    require(!gb::media::parse_chapter_name("GH050083.360"), "GH must pair with .MP4");
    const auto hevc = gb::media::parse_chapter_name("GX020223.MP4");
    require(hevc.has_value() && hevc->prefix == "GX" && hevc->chapter == 2 &&
                hevc->recording == "0223",
            "HEVC HERO chapter name not parsed");
    require(!gb::media::parse_chapter_name("GX020223.360"), "GX must pair with .MP4");
    require(!gb::media::parse_chapter_name("GL010083.MP4"),
            "only GS, GH and GX are chaptered here");
    require(!gb::media::parse_chapter_name("GS000047.360"), "chapter 00 does not exist");
    require(!gb::media::parse_chapter_name("GS__0033.JPG"), "photos are not chapters");
    require(!gb::media::parse_chapter_name("GS04004.360"), "short names are not chapters");
    require(!gb::media::parse_chapter_name("GS0A0047.360"), "chapter must be digits");

    const auto directory = std::filesystem::temp_directory_path() / "gb_chapters_test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory / "GS050047.360"); // a directory, not a chapter
    for (const auto *name :
         {"GS030047.360", "GS010047.360", "GS020047.360", "GS010048.360", "GH010047.MP4",
          "GX020047.MP4", "GS100047.360", "GS020047.LRV", "notes.txt"}) {
      touch(directory / name);
    }
    const auto sequence = gb::media::chapter_sequence(directory / "GS020047.360");
    require(names(sequence) == std::vector<std::string>{"GS010047.360", "GS020047.360",
                                                        "GS030047.360", "GS100047.360"},
            "MAX sequence wrong or misordered");
    require(sequence[1] == directory / "GS020047.360", "opened path must be kept as given");

    require(names(gb::media::chapter_sequence(directory / "GH010047.MP4")) ==
                std::vector<std::string>{"GH010047.MP4"},
            "AVC recording must not include MAX or HEVC chapters");
    require(names(gb::media::chapter_sequence(directory / "GX020047.MP4")) ==
                std::vector<std::string>{"GX020047.MP4"},
            "HEVC recording must not include AVC chapters");
    require(gb::media::chapter_sequence(directory / "notes.txt") ==
                std::vector<std::filesystem::path>{directory / "notes.txt"},
            "unchaptered file must be alone");
    require(gb::media::chapter_sequence(directory / "missing" / "GS010047.360").size() == 1U,
            "unlistable directory must yield the file alone");
    std::filesystem::remove_all(directory);
  } catch (const std::exception &error) {
    std::cerr << "gb_chapters_tests: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
