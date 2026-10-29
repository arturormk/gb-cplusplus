#include "gb/container/iso_bmff.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

namespace gb::container {
namespace {

constexpr std::size_t kMaxDepth = 32;
constexpr std::uint64_t kBasicHeaderSize = 8;
constexpr std::uint64_t kExtendedHeaderSize = 16;
constexpr std::uint64_t kUuidExtraSize = 16;
constexpr std::uint32_t kMaxTableEntries = 10'000'000;
constexpr std::uint32_t kMaxOptionalMappedSamples = 1'000'000;

std::uint16_t read_u16(std::ifstream &input, const std::uint64_t offset) {
  std::array<unsigned char, 2> bytes{};
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    throw InspectionError(ErrorKind::corrupt, offset, "truncated 16-bit field");
  }
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8U) | bytes[1]);
}

std::uint32_t read_u32(std::ifstream &input, const std::uint64_t offset) {
  std::array<unsigned char, 4> bytes{};
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    throw InspectionError(ErrorKind::corrupt, offset, "truncated 32-bit field");
  }
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t read_u64(std::ifstream &input, const std::uint64_t offset) {
  const auto high = static_cast<std::uint64_t>(read_u32(input, offset));
  const auto low = static_cast<std::uint64_t>(read_u32(input, offset + 4U));
  return (high << 32U) | low;
}

std::string read_fourcc(std::ifstream &input, const std::uint64_t offset) {
  std::array<char, 4> value{};
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  input.read(value.data(), static_cast<std::streamsize>(value.size()));
  if (!input) {
    throw InspectionError(ErrorKind::corrupt, offset, "truncated FourCC field");
  }
  return {value.data(), value.size()};
}

const Box *find_child(const Box &parent, const std::string &type) {
  for (const auto &child : parent.children) {
    if (child.type == type) {
      return &child;
    }
  }
  return nullptr;
}

const Box *find_descendant(const Box &parent, const std::initializer_list<std::string> path) {
  const Box *current = &parent;
  for (const auto &type : path) {
    current = find_child(*current, type);
    if (current == nullptr) {
      return nullptr;
    }
  }
  return current;
}

void require_range(const Box &box, const std::uint64_t offset, const std::uint64_t size,
                   const std::string &description) {
  const auto end = box.offset + box.size;
  if (offset < box.offset + box.header_size || offset > end || size > end - offset) {
    throw InspectionError(ErrorKind::corrupt, offset, "truncated " + description);
  }
}

std::uint32_t checked_count(std::ifstream &input, const Box &box, const std::uint64_t offset,
                            const std::uint64_t entry_size, const std::string &description) {
  require_range(box, offset, 4U, description + " count");
  const auto count = read_u32(input, offset);
  if (count > kMaxTableEntries) {
    throw InspectionError(ErrorKind::corrupt, offset, description + " exceeds entry limit");
  }
  if (entry_size != 0U) {
    require_range(box, offset + 4U, static_cast<std::uint64_t>(count) * entry_size, description);
  }
  return count;
}

bool is_container_box(const std::string &type) {
  static const std::set<std::string> types{
      "moov", "trak", "mdia", "minf", "dinf", "stbl", "edts", "udta", "mvex",
      "moof", "traf", "mfra", "sinf", "schi", "tref", "meco"};
  return types.contains(type);
}

std::vector<Box> parse_boxes(std::ifstream &input, const std::uint64_t begin,
                             const std::uint64_t end, const std::size_t depth) {
  if (depth > kMaxDepth) {
    throw InspectionError(ErrorKind::corrupt, begin, "box nesting exceeds the limit of 32");
  }

  std::vector<Box> boxes;
  auto offset = begin;
  while (offset < end) {
    const auto remaining = end - offset;
    if (remaining < kBasicHeaderSize) {
      throw InspectionError(ErrorKind::corrupt, offset, "trailing bytes are shorter than a box header");
    }

    const auto size32 = read_u32(input, offset);
    const auto type = read_fourcc(input, offset + 4U);
    std::uint64_t header_size = kBasicHeaderSize;
    std::uint64_t box_size = size32;

    if (size32 == 1U) {
      if (remaining < kExtendedHeaderSize) {
        throw InspectionError(ErrorKind::corrupt, offset, "truncated extended-size box header");
      }
      box_size = read_u64(input, offset + kBasicHeaderSize);
      header_size = kExtendedHeaderSize;
    } else if (size32 == 0U) {
      box_size = remaining;
    }

    if (type == "uuid") {
      if (remaining < header_size + kUuidExtraSize) {
        throw InspectionError(ErrorKind::corrupt, offset, "truncated uuid box header");
      }
      header_size += kUuidExtraSize;
    }
    if (box_size < header_size) {
      throw InspectionError(ErrorKind::corrupt, offset, "box size is smaller than its header");
    }
    if (box_size > remaining) {
      throw InspectionError(ErrorKind::corrupt, offset, "box extends beyond its parent or file");
    }

    Box box{type, offset, box_size, header_size, {}};
    if (is_container_box(type) && box_size > header_size) {
      box.children = parse_boxes(input, offset + header_size, offset + box_size, depth + 1U);
    }
    boxes.push_back(std::move(box));
    offset += box_size;
  }
  return boxes;
}

FileIdentity identify(std::ifstream &input, const std::vector<Box> &boxes) {
  FileIdentity identity{};
  for (const auto &box : boxes) {
    if (box.type != "ftyp") {
      continue;
    }
    identity.is_iso_bmff = true;
    const auto payload_size = box.size - box.header_size;
    if (payload_size < 8U || ((payload_size - 8U) % 4U) != 0U) {
      throw InspectionError(ErrorKind::corrupt, box.offset, "invalid ftyp payload length");
    }
    const auto payload = box.offset + box.header_size;
    identity.major_brand = read_fourcc(input, payload);
    identity.minor_version = read_u32(input, payload + 4U);
    for (std::uint64_t pos = payload + 8U; pos < box.offset + box.size; pos += 4U) {
      identity.compatible_brands.push_back(read_fourcc(input, pos));
    }
    break;
  }
  return identity;
}

struct SampleTables {
  std::uint32_t sample_size{};
  std::uint32_t sample_count{};
  std::vector<std::uint32_t> sizes;
  std::vector<std::uint64_t> chunks;
  struct ChunkRun {
    std::uint32_t first_chunk{};
    std::uint32_t samples_per_chunk{};
  };
  std::vector<ChunkRun> chunk_runs;
  struct TimeRun {
    std::uint32_t count{};
    std::uint32_t duration{};
  };
  std::vector<TimeRun> time_runs;
  struct CompositionRun {
    std::uint32_t count{};
    std::int64_t offset{};
  };
  std::vector<CompositionRun> composition_runs;
};

void parse_track_header(std::ifstream &input, const Box &box, Track &track) {
  const auto payload = box.offset + box.header_size;
  require_range(box, payload, 4U, "tkhd header");
  input.seekg(static_cast<std::streamoff>(payload), std::ios::beg);
  const auto version = static_cast<unsigned char>(input.get());
  const auto id_offset = payload + (version == 1U ? 20U : 12U);
  require_range(box, id_offset, 4U, "tkhd track id");
  track.id = read_u32(input, id_offset);
  require_range(box, box.offset + box.size - 8U, 8U, "tkhd dimensions");
  track.width = static_cast<std::uint16_t>(read_u32(input, box.offset + box.size - 8U) >> 16U);
  track.height = static_cast<std::uint16_t>(read_u32(input, box.offset + box.size - 4U) >> 16U);
}

void parse_media_header(std::ifstream &input, const Box &box, Track &track) {
  const auto payload = box.offset + box.header_size;
  require_range(box, payload, 4U, "mdhd header");
  input.seekg(static_cast<std::streamoff>(payload), std::ios::beg);
  const auto version = static_cast<unsigned char>(input.get());
  const auto timescale_offset = payload + (version == 1U ? 20U : 12U);
  const auto duration_offset = timescale_offset + 4U;
  require_range(box, duration_offset, version == 1U ? 8U : 4U, "mdhd timing");
  const auto timescale = read_u32(input, timescale_offset);
  if (timescale == 0U) {
    throw InspectionError(ErrorKind::corrupt, timescale_offset, "mdhd timescale is zero");
  }
  track.time_base = Rational{1U, timescale};
  track.duration_ticks = version == 1U ? read_u64(input, duration_offset)
                                       : read_u32(input, duration_offset);
}

void parse_handler(std::ifstream &input, const Box &box, Track &track) {
  const auto payload = box.offset + box.header_size;
  require_range(box, payload, 24U, "hdlr fields");
  track.handler_type = read_fourcc(input, payload + 8U);
  const auto name_offset = payload + 24U;
  const auto name_size = box.offset + box.size - name_offset;
  if (name_size == 0U) {
    return;
  }
  std::vector<char> name(static_cast<std::size_t>(name_size));
  input.seekg(static_cast<std::streamoff>(name_offset), std::ios::beg);
  input.read(name.data(), static_cast<std::streamsize>(name.size()));
  if (!input) {
    throw InspectionError(ErrorKind::corrupt, name_offset, "truncated hdlr name");
  }
  while (!name.empty() && name.back() == '\0') {
    name.pop_back();
  }
  if (!name.empty() && static_cast<unsigned char>(name.front()) == name.size() - 1U) {
    name.erase(name.begin());
  }
  track.handler_name.assign(name.begin(), name.end());
}

void parse_sample_description(std::ifstream &input, const Box &box, Track &track) {
  const auto payload = box.offset + box.header_size;
  require_range(box, payload, 8U, "stsd header");
  const auto count = read_u32(input, payload + 4U);
  if (count == 0U) {
    return;
  }
  const auto entry = payload + 8U;
  require_range(box, entry, 8U, "stsd sample entry");
  const auto entry_size = read_u32(input, entry);
  if (entry_size < 8U || entry_size > box.offset + box.size - entry) {
    throw InspectionError(ErrorKind::corrupt, entry, "invalid stsd sample entry size");
  }
  track.codec = read_fourcc(input, entry + 4U);
  if (track.handler_type == "vide" && entry_size >= 36U) {
    track.width = read_u16(input, entry + 32U);
    track.height = read_u16(input, entry + 34U);
  } else if (track.handler_type == "soun" && entry_size >= 36U) {
    track.channel_count = read_u16(input, entry + 24U);
    track.bits_per_sample = read_u16(input, entry + 26U);
    track.sample_rate = read_u32(input, entry + 32U) >> 16U;
  }
}

SampleTables parse_sample_tables(std::ifstream &input, const Box &stbl,
                                 const bool mapping_required) {
  SampleTables tables;
  if (const auto *stsz = find_child(stbl, "stsz")) {
    const auto payload = stsz->offset + stsz->header_size;
    require_range(*stsz, payload, 12U, "stsz header");
    tables.sample_size = read_u32(input, payload + 4U);
    tables.sample_count = read_u32(input, payload + 8U);
    const auto mapping_limit = mapping_required ? kMaxTableEntries : kMaxOptionalMappedSamples;
    if (tables.sample_count > mapping_limit) {
      if (tables.sample_size == 0U) {
        require_range(*stsz, payload + 12U,
                      static_cast<std::uint64_t>(tables.sample_count) * 4U, "stsz entries");
      }
      if (mapping_required) {
        throw InspectionError(ErrorKind::corrupt, payload + 8U, "stsz exceeds entry limit");
      }
      return tables;
    }
    if (tables.sample_size == 0U) {
      require_range(*stsz, payload + 12U, static_cast<std::uint64_t>(tables.sample_count) * 4U,
                    "stsz entries");
      tables.sizes.reserve(tables.sample_count);
      for (std::uint32_t index = 0; index < tables.sample_count; ++index) {
        tables.sizes.push_back(read_u32(input, payload + 12U + static_cast<std::uint64_t>(index) * 4U));
      }
    }
  }
  if (const auto *stco = find_child(stbl, "stco")) {
    const auto payload = stco->offset + stco->header_size;
    const auto count = checked_count(input, *stco, payload + 4U, 4U, "stco");
    tables.chunks.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      tables.chunks.push_back(read_u32(input, payload + 8U + static_cast<std::uint64_t>(index) * 4U));
    }
  } else if (const auto *co64 = find_child(stbl, "co64")) {
    const auto payload = co64->offset + co64->header_size;
    const auto count = checked_count(input, *co64, payload + 4U, 8U, "co64");
    tables.chunks.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      tables.chunks.push_back(read_u64(input, payload + 8U + static_cast<std::uint64_t>(index) * 8U));
    }
  }
  if (const auto *stsc = find_child(stbl, "stsc")) {
    const auto payload = stsc->offset + stsc->header_size;
    const auto count = checked_count(input, *stsc, payload + 4U, 12U, "stsc");
    tables.chunk_runs.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto entry = payload + 8U + static_cast<std::uint64_t>(index) * 12U;
      const auto first_chunk = read_u32(input, entry);
      const auto samples_per_chunk = read_u32(input, entry + 4U);
      if (first_chunk == 0U || samples_per_chunk == 0U ||
          (!tables.chunk_runs.empty() && first_chunk <= tables.chunk_runs.back().first_chunk)) {
        throw InspectionError(ErrorKind::corrupt, entry, "invalid stsc run");
      }
      tables.chunk_runs.push_back({first_chunk, samples_per_chunk});
    }
  }
  if (const auto *stts = find_child(stbl, "stts")) {
    const auto payload = stts->offset + stts->header_size;
    const auto count = checked_count(input, *stts, payload + 4U, 8U, "stts");
    tables.time_runs.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto entry = payload + 8U + static_cast<std::uint64_t>(index) * 8U;
      tables.time_runs.push_back({read_u32(input, entry), read_u32(input, entry + 4U)});
    }
  }
  if (const auto *ctts = find_child(stbl, "ctts")) {
    const auto payload = ctts->offset + ctts->header_size;
    require_range(*ctts, payload, 8U, "ctts header");
    input.seekg(static_cast<std::streamoff>(payload), std::ios::beg);
    const auto version = static_cast<unsigned char>(input.get());
    if (version > 1U) {
      throw InspectionError(ErrorKind::unsupported, payload, "unsupported ctts version");
    }
    const auto count = checked_count(input, *ctts, payload + 4U, 8U, "ctts");
    tables.composition_runs.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto entry = payload + 8U + static_cast<std::uint64_t>(index) * 8U;
      const auto raw_offset = read_u32(input, entry + 4U);
      const auto offset = version == 0U
                              ? static_cast<std::int64_t>(raw_offset)
                              : static_cast<std::int64_t>(std::bit_cast<std::int32_t>(raw_offset));
      tables.composition_runs.push_back({read_u32(input, entry), offset});
    }
  }
  return tables;
}

std::vector<SampleReference> build_sample_references(const SampleTables &tables,
                                                     const std::uint64_t file_size) {
  std::vector<SampleReference> samples;
  if (tables.sample_count == 0U || tables.chunks.empty() || tables.chunk_runs.empty()) {
    return samples;
  }
  if (tables.chunk_runs.front().first_chunk != 1U) {
    throw InspectionError(ErrorKind::corrupt, 0, "stsc first run does not begin at chunk one");
  }
  std::uint64_t timed_samples = 0;
  for (const auto &time : tables.time_runs) {
    timed_samples += time.count;
  }
  if (timed_samples != tables.sample_count) {
    throw InspectionError(ErrorKind::corrupt, 0, "stts does not describe every sample");
  }
  if (!tables.composition_runs.empty()) {
    std::uint64_t composed_samples = 0;
    for (const auto &run : tables.composition_runs) {
      composed_samples += run.count;
    }
    if (composed_samples != tables.sample_count) {
      throw InspectionError(ErrorKind::corrupt, 0, "ctts does not describe every sample");
    }
  }
  samples.reserve(tables.sample_count);
  std::size_t run = 0;
  std::size_t time_run = 0;
  std::uint32_t time_run_used = 0;
  std::size_t composition_run = 0;
  std::uint32_t composition_run_used = 0;
  std::uint64_t decode_time = 0;
  for (std::size_t chunk_index = 0; chunk_index < tables.chunks.size() &&
                                    samples.size() < tables.sample_count; ++chunk_index) {
    const auto chunk_number = static_cast<std::uint32_t>(chunk_index + 1U);
    while (run + 1U < tables.chunk_runs.size() &&
           tables.chunk_runs[run + 1U].first_chunk <= chunk_number) {
      ++run;
    }
    auto offset = tables.chunks[chunk_index];
    for (std::uint32_t in_chunk = 0; in_chunk < tables.chunk_runs[run].samples_per_chunk &&
                                     samples.size() < tables.sample_count; ++in_chunk) {
      const auto index = samples.size();
      const auto size = tables.sample_size == 0U ? tables.sizes.at(index) : tables.sample_size;
      std::uint32_t duration = 0;
      if (time_run < tables.time_runs.size()) {
        duration = tables.time_runs[time_run].duration;
        if (++time_run_used == tables.time_runs[time_run].count) {
          ++time_run;
          time_run_used = 0;
        }
      }
      if (offset > file_size || size > file_size - offset) {
        throw InspectionError(ErrorKind::corrupt, offset, "sample extends beyond the file");
      }
      if (decode_time > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw InspectionError(ErrorKind::corrupt, offset, "sample decode time exceeds signed range");
      }
      auto presentation_time = static_cast<std::int64_t>(decode_time);
      if (composition_run < tables.composition_runs.size()) {
        const auto composition_offset = tables.composition_runs[composition_run].offset;
        if ((composition_offset > 0 &&
             presentation_time > std::numeric_limits<std::int64_t>::max() - composition_offset) ||
            (composition_offset < 0 &&
             presentation_time < std::numeric_limits<std::int64_t>::min() - composition_offset)) {
          throw InspectionError(ErrorKind::corrupt, offset, "sample presentation time overflows");
        }
        presentation_time += composition_offset;
        if (++composition_run_used == tables.composition_runs[composition_run].count) {
          ++composition_run;
          composition_run_used = 0;
        }
      }
      samples.push_back({offset, size, decode_time, presentation_time, duration});
      offset += size;
      decode_time += duration;
    }
  }
  if (samples.size() != tables.sample_count) {
    throw InspectionError(ErrorKind::corrupt, 0, "sample tables do not map every sample");
  }
  return samples;
}

std::vector<Track> parse_tracks(std::ifstream &input, const std::vector<Box> &boxes,
                                const std::uint64_t file_size) {
  std::vector<Track> tracks;
  for (const auto &top : boxes) {
    if (top.type != "moov") {
      continue;
    }
    for (const auto &trak : top.children) {
      if (trak.type != "trak") {
        continue;
      }
      Track track;
      if (const auto *tkhd = find_child(trak, "tkhd")) {
        parse_track_header(input, *tkhd, track);
      }
      if (const auto *mdhd = find_descendant(trak, {"mdia", "mdhd"})) {
        parse_media_header(input, *mdhd, track);
      }
      if (const auto *hdlr = find_descendant(trak, {"mdia", "hdlr"})) {
        parse_handler(input, *hdlr, track);
      }
      if (const auto *stsd = find_descendant(trak, {"mdia", "minf", "stbl", "stsd"})) {
        parse_sample_description(input, *stsd, track);
      }
      if (const auto *stbl = find_descendant(trak, {"mdia", "minf", "stbl"})) {
        const auto tables = parse_sample_tables(input, *stbl, track.codec == "gpmd");
        track.sample_count = tables.sample_count;
        track.samples = build_sample_references(tables, file_size);
      }
      tracks.push_back(std::move(track));
    }
  }
  return tracks;
}

} // namespace

InspectionError::InspectionError(const ErrorKind kind, const std::uint64_t offset,
                                 const std::string &message)
    : std::runtime_error(message), kind_(kind), offset_(offset) {}

ErrorKind InspectionError::kind() const noexcept { return kind_; }
std::uint64_t InspectionError::offset() const noexcept { return offset_; }

Inspection inspect_iso_bmff(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error || !std::filesystem::exists(status)) {
    throw InspectionError(ErrorKind::missing, 0, "input file does not exist");
  }
  if (!std::filesystem::is_regular_file(status)) {
    throw InspectionError(ErrorKind::io, 0, "input is not a regular file");
  }
  const auto file_size = std::filesystem::file_size(path, error);
  if (error) {
    throw InspectionError(ErrorKind::io, 0, "cannot determine input file size");
  }
  if (file_size == 0U) {
    throw InspectionError(ErrorKind::unsupported, 0, "empty input is not ISO BMFF");
  }
  if (file_size > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    throw InspectionError(ErrorKind::io, 0, "input is too large for this platform's file API");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw InspectionError(ErrorKind::io, 0, "cannot open input for reading");
  }

  Inspection result{path, file_size, {}, parse_boxes(input, 0, file_size, 0), {}};
  result.identity = identify(input, result.boxes);
  if (!result.identity.is_iso_bmff) {
    throw InspectionError(ErrorKind::unsupported, 0, "no top-level ftyp box found");
  }
  result.tracks = parse_tracks(input, result.boxes, file_size);
  return result;
}

std::vector<std::uint8_t> read_sample(const std::filesystem::path &path,
                                      const SampleReference &sample,
                                      const std::uint32_t maximum_size) {
  if (sample.size > maximum_size) {
    throw InspectionError(ErrorKind::corrupt, sample.offset, "sample exceeds read limit");
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw InspectionError(ErrorKind::io, sample.offset, "cannot open input for reading");
  }
  std::vector<std::uint8_t> bytes(sample.size);
  input.seekg(static_cast<std::streamoff>(sample.offset), std::ios::beg);
  input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    throw InspectionError(ErrorKind::corrupt, sample.offset, "truncated sample payload");
  }
  return bytes;
}

std::string error_kind_name(const ErrorKind kind) {
  switch (kind) {
  case ErrorKind::missing:
    return "missing";
  case ErrorKind::io:
    return "io";
  case ErrorKind::unsupported:
    return "unsupported";
  case ErrorKind::corrupt:
    return "corrupt";
  }
  return "unexpected";
}

} // namespace gb::container
