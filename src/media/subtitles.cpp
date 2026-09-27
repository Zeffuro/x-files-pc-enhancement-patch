#include "subtitles.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;

namespace media::subtitles {
namespace {

constexpr std::size_t max_manifest = 2 * 1024 * 1024;
constexpr std::size_t max_srt = 1024 * 1024;
constexpr std::size_t max_movies = 10000;
constexpr std::size_t max_cues = 10000;
constexpr std::uint64_t max_pack_bytes = 128 * 1024 * 1024;
constexpr std::string_view signature = "xfiles-subtitles-v1\n";
std::atomic<std::uint64_t> installed_generation{0};

struct Record {
    fs::path movie;
    fs::path subtitle;
    std::string hash;
    std::uint32_t scale;
    std::uint64_t duration;
};

[[noreturn]] void invalid(const char* message) {
    throw std::runtime_error(message);
}

bool linked(const fs::path& path) {
    const auto flags = GetFileAttributesW(path.c_str());
    return flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT);
}

void require_regular(const fs::path& path, std::uint64_t limit) {
    if (linked(path) || !fs::is_regular_file(path) || fs::file_size(path) > limit) {
        invalid("Subtitle pack has a linked, missing or oversized file.");
    }
}

std::string read_file(const fs::path& path, std::uint64_t limit) {
    require_regular(path, limit);
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(fs::file_size(path)), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        invalid("Cannot read subtitle pack file.");
    }
    return bytes;
}

void write_file(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        invalid("Cannot write subtitle pack file.");
    }
}

std::string ascii_path(const fs::path& path) {
    const auto text = path.generic_string();
    if (text.empty() || text.size() > 240 || text.front() == '/' || text.back() == '/' ||
        text.find("//") != std::string::npos || text.find("..") != std::string::npos ||
        text.find(':') != std::string::npos || text.find('\\') != std::string::npos ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) {
            return !(std::isalnum(c) || c == '/' || c == '_' || c == '-' || c == '.');
        })) {
        invalid("Invalid subtitle pack path.");
    }
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('/', start);
        const auto part = text.substr(start, end == std::string::npos ? end : end - start);
        if (part == "." || part.empty() || part.back() == '.') {
            invalid("Invalid subtitle pack path.");
        }
        auto stem = part.substr(0, part.find('.'));
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
             stem.back() >= '1' && stem.back() <= '9')) {
            invalid("Reserved subtitle pack path.");
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return text;
}

fs::path safe_path(std::string_view text) {
    if (text.find('\\') != std::string_view::npos) {
        invalid("Invalid subtitle pack path.");
    }
    return fs::path(ascii_path(fs::path(text)));
}

std::string folded_path(const fs::path& path) {
    auto text = ascii_path(path);
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::uint64_t number(std::string_view text) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        invalid("Invalid subtitle manifest number.");
    }
    return value;
}

std::string digest(const fs::path& path) {
    require_regular(path, 512ull * 1024 * 1024);
    std::ifstream input(path, std::ios::binary);
    BCRYPT_HASH_HANDLE handle = nullptr;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &handle, nullptr, 0, nullptr, 0, 0) < 0) {
        invalid("Cannot initialize movie checksum.");
    }

    struct Guard {
        BCRYPT_HASH_HANDLE handle;

        ~Guard() {
            BCryptDestroyHash(handle);
        }
    } guard{handle};

    std::vector<unsigned char> buffer(65536);
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count && BCryptHashData(handle, buffer.data(), static_cast<ULONG>(count), 0) < 0) {
            invalid("Cannot hash movie.");
        }
    }
    if (!input.eof()) {
        invalid("Cannot read movie for checksum.");
    }
    std::array<unsigned char, 32> hash{};
    if (BCryptFinishHash(handle, hash.data(), hash.size(), 0) < 0) {
        invalid("Cannot finish movie checksum.");
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : hash) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}

std::wstring wide(std::string_view text, UINT page, DWORD flags) {
    if (text.empty()) {
        return {};
    }
    const auto length =
        MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) {
        invalid("Invalid subtitle text encoding.");
    }
    std::wstring result(length, L'\0');
    if (!MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), result.data(),
                             length)) {
        invalid("Invalid subtitle text encoding.");
    }
    return result;
}

std::string utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) {
        invalid("Cannot encode subtitle text.");
    }
    std::string result(length, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), result.data(), length, nullptr,
                             nullptr)) {
        invalid("Cannot encode subtitle text.");
    }
    return result;
}

std::wstring native_text(std::span<const std::uint8_t> packet) {
    if (packet.size() < 2) {
        invalid("Truncated native subtitle sample.");
    }
    const std::size_t length = (packet[0] << 8) | packet[1];
    if (length > packet.size() - 2) {
        invalid("Native subtitle exceeds sample.");
    }
    const auto text = wide({reinterpret_cast<const char*>(packet.data() + 2), length}, 10000, 0);
    if (text.find_first_not_of(L" \r\n\t") == std::wstring::npos) {
        return {};
    }
    std::wstring result;
    bool space = false;
    for (const auto c : text) {
        if (c == L' ' || c == L'\t') {
            space = !result.empty();
            continue;
        }
        if (space && c != L')' && c != L']' && c != L'\r' && c != L'\n' && result.back() != L'(' &&
            result.back() != L'[' && result.back() != L'\n' && result.back() != L'\r') {
            result += L' ';
        }
        result += c;
        space = false;
    }
    for (std::size_t at = 0; (at = result.find(L". . .", at)) != std::wstring::npos;) {
        if (at && std::iswalnum(result[at - 1])) {
            ++at;
            continue;
        }
        result.replace(at, 5, L"...");
        at += 3;
    }
    return result;
}

std::wstring rendered(const Movie& movie, std::uint64_t time) {
    struct Layer {
        const Track* track;
        std::wstring text;
    };

    std::vector<Layer> layers;
    bool has_video = false;
    for (const auto& track : movie.tracks) {
        has_video |= (track.flags & 1) && track.handler == "vide";
        if (track.handler != "text") {
            continue;
        }
        const auto sample = track.sample_at(time, movie.timescale);
        if (!sample) {
            continue;
        }
        auto text = native_text(movie.packet(track.samples[*sample]));
        if (text.empty()) {
            continue;
        }
        const auto prior = std::find_if(layers.begin(), layers.end(), [&](const Layer& layer) {
            return layer.track->placement == track.placement;
        });
        if (prior == layers.end()) {
            layers.push_back({&track, std::move(text)});
        } else if (track.layer <= prior->track->layer) {
            *prior = {&track, std::move(text)};
        }
    }
    std::wstring result;
    for (const auto& layer : layers) {
        if (!result.empty()) {
            result += L'\n';
        }
        result += layer.text;
    }
    return has_video ? result : std::wstring{};
}

std::uint64_t ceil_ratio(std::uint64_t distance, std::uint32_t movie_scale,
                         std::uint32_t track_scale, std::uint32_t rate) {
    if (!rate) {
        return 0;
    }
    std::uint64_t factors[] = {distance, movie_scale, 65536};
    std::uint64_t denominator = static_cast<std::uint64_t>(track_scale) * rate;
    for (auto& factor : factors) {
        const auto gcd = std::gcd(factor, denominator);
        factor /= gcd;
        denominator /= gcd;
    }
    std::uint64_t numerator = 1;
    for (const auto factor : factors) {
        if (factor && numerator > UINT64_MAX / factor) {
            invalid("Subtitle timing overflow.");
        }
        numerator *= factor;
    }
    return numerator / denominator + (numerator % denominator != 0);
}

std::vector<std::uint64_t> boundaries(const Movie& movie) {
    std::vector<std::uint64_t> points{0, movie.duration};
    for (const auto& track : movie.tracks) {
        if (track.handler != "text") {
            continue;
        }
        std::uint64_t position = 0;
        const auto process = [&](std::uint64_t length, std::int64_t origin, std::int32_t rate) {
            const auto stop = std::min(movie.duration, position + std::min(length, movie.duration));
            points.push_back(position);
            points.push_back(stop);
            if (origin >= 0 && rate >= 0) {
                for (const auto& sample : track.samples) {
                    for (const auto media_time : {sample.time, sample.time + sample.duration}) {
                        if (media_time < static_cast<std::uint64_t>(origin)) {
                            continue;
                        }
                        const auto offset =
                            ceil_ratio(media_time - origin, movie.timescale, track.timescale,
                                       static_cast<std::uint32_t>(rate));
                        if (offset <= stop - position) {
                            points.push_back(position + offset);
                        }
                    }
                }
            }
            position = stop;
        };
        if (track.edits.empty()) {
            process(movie.duration, 0, 65536);
        } else {
            for (const auto& edit : track.edits) {
                process(edit.duration, edit.media_time, edit.rate);
                if (position >= movie.duration) {
                    break;
                }
            }
        }
    }
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

std::string timestamp(std::uint64_t ms) {
    if (ms > 359999999) {
        invalid("Subtitle time exceeds SRT limit.");
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%02llu:%02llu:%02llu,%03llu",
                  static_cast<unsigned long long>(ms / 3600000),
                  static_cast<unsigned long long>(ms / 60000 % 60),
                  static_cast<unsigned long long>(ms / 1000 % 60),
                  static_cast<unsigned long long>(ms % 1000));
    return text;
}

std::uint64_t parse_time(std::string_view text) {
    if (text.size() != 12 || text[2] != ':' || text[5] != ':' || text[8] != ',' ||
        std::any_of(text.begin(), text.end(),
                    [](char c) { return c != ':' && c != ',' && (c < '0' || c > '9'); })) {
        invalid("Invalid SRT timestamp.");
    }
    const auto hours = number(text.substr(0, 2)), minutes = number(text.substr(3, 2));
    const auto seconds = number(text.substr(6, 2)), millis = number(text.substr(9, 3));
    if (minutes >= 60 || seconds >= 60) {
        invalid("Invalid SRT timestamp.");
    }
    return ((hours * 60 + minutes) * 60 + seconds) * 1000 + millis;
}

std::vector<Cue> parse_srt(const std::string& bytes, std::uint64_t duration_ms) {
    std::string_view source(bytes);
    if (source.starts_with("\xef\xbb\xbf")) {
        source.remove_prefix(3);
    }
    wide(source, CP_UTF8, MB_ERR_INVALID_CHARS);
    std::vector<Cue> result;
    std::size_t position = 0;
    const auto line = [&]() -> std::string_view {
        const auto end = source.find('\n', position);
        const auto stop = end == std::string_view::npos ? source.size() : end;
        auto value = source.substr(position, stop - position);
        if (value.ends_with('\r')) {
            value.remove_suffix(1);
        }
        if (value.size() > 4096 || value.find('\r') != std::string_view::npos) {
            invalid("Invalid SRT line.");
        }
        position = end == std::string_view::npos ? source.size() : end + 1;
        return value;
    };
    while (position < source.size()) {
        auto sequence = line();
        if (sequence.empty()) {
            continue;
        }
        if (number(sequence) != result.size() + 1 || position >= source.size()) {
            invalid("Invalid SRT cue number.");
        }
        const auto times = line();
        if (times.size() != 29 || times.substr(12, 5) != " --> ") {
            invalid("Invalid SRT cue timing.");
        }
        Cue cue{parse_time(times.substr(0, 12)), parse_time(times.substr(17, 12)), {}};
        if (cue.begin >= cue.end || cue.end > duration_ms ||
            (!result.empty() && cue.begin < result.back().begin)) {
            invalid("SRT cue lies outside its movie or is out of order.");
        }
        std::string content;
        while (position < source.size()) {
            const auto part = line();
            if (part.empty()) {
                break;
            }
            if (!content.empty()) {
                content += '\n';
            }
            content += part;
            if (content.size() > 4096) {
                invalid("SRT cue text is too long.");
            }
        }
        if (content.empty()) {
            invalid("SRT cue has no text.");
        }
        cue.text = wide(content, CP_UTF8, MB_ERR_INVALID_CHARS);
        result.push_back(std::move(cue));
        if (result.size() > max_cues) {
            invalid("Too many SRT cues.");
        }
    }
    return result;
}

std::string make_srt(const std::vector<Cue>& cues, std::uint32_t scale) {
    std::string result;
    std::size_t sequence = 0;
    for (const auto& cue : cues) {
        const auto begin = cue.begin * 1000 / scale;
        const auto end = (cue.end * 1000 + scale - 1) / scale;
        if (begin >= end) {
            continue;
        }
        result += std::to_string(++sequence) + "\n" + timestamp(begin) + " --> " + timestamp(end) +
                  "\n" + utf8(cue.text) + "\n\n";
        if (result.size() > max_srt) {
            invalid("Exported SRT exceeds size limit.");
        }
    }
    return result;
}

std::vector<Record> manifest(const fs::path& folder) {
    auto bytes = read_file(folder / "manifest.tsv", max_manifest);
    std::size_t written = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] != '\r' || i + 1 == bytes.size() || bytes[i + 1] != '\n') {
            bytes[written++] = bytes[i];
        }
    }
    bytes.resize(written);
    if (!bytes.starts_with(signature)) {
        invalid("Unknown subtitle manifest format.");
    }
    std::vector<Record> result;
    std::set<std::string> names, subtitles;
    std::size_t start = signature.size();
    while (start < bytes.size()) {
        const auto end = bytes.find('\n', start);
        if (end == std::string::npos) {
            invalid("Truncated subtitle manifest.");
        }
        const std::string_view row(bytes.data() + start, end - start);
        start = end + 1;
        if (row.empty()) {
            continue;
        }
        std::array<std::string_view, 5> fields{};
        std::size_t at = 0;
        for (auto& field : fields) {
            const auto tab = row.find('\t', at);
            field = row.substr(at, tab == std::string_view::npos ? tab : tab - at);
            at = tab == std::string_view::npos ? row.size() : tab + 1;
        }
        if (at != row.size() || std::count(row.begin(), row.end(), '\t') != 4) {
            invalid("Invalid subtitle manifest row.");
        }
        Record record{safe_path(fields[0]), safe_path(fields[4]), std::string(fields[1]),
                      static_cast<std::uint32_t>(number(fields[2])), number(fields[3])};
        if (number(fields[2]) > UINT32_MAX || !record.scale || !record.duration ||
            record.hash.size() != 64 ||
            !std::all_of(record.hash.begin(), record.hash.end(),
                         [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
            !names.insert(folded_path(record.movie)).second ||
            !subtitles.insert(folded_path(record.subtitle)).second ||
            record.subtitle.extension() != ".srt") {
            invalid("Invalid subtitle manifest entry.");
        }
        result.push_back(std::move(record));
        if (result.size() > max_movies) {
            invalid("Too many subtitle movies.");
        }
    }
    if (result.empty()) {
        invalid("Subtitle manifest is empty.");
    }
    return result;
}

void check_parents(const fs::path& root, const fs::path& relative) {
    if (linked(root) || !fs::is_directory(root)) {
        invalid("Invalid game or pack folder.");
    }
    auto path = root;
    for (auto part = relative.begin(); part != relative.end(); ++part) {
        path /= *part;
        if (std::next(part) != relative.end() && (!fs::is_directory(path) || linked(path))) {
            invalid("Linked or missing movie folder.");
        }
    }
}

bool selected_movie(const fs::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), std::towlower);
    return extension == L".xmv" || extension == L".amv" || extension == L".dmv" ||
           extension == L".nmv";
}

void tick(const Progress& progress, std::size_t completed, std::size_t total) {
    if (progress && !progress(completed, total)) {
        invalid("Subtitle operation cancelled.");
    }
}

}

std::vector<Cue> native_cues(const Movie& movie) {
    if (!movie.timescale || movie.duration > INT32_MAX) {
        invalid("Unsupported movie subtitle duration.");
    }
    const auto points = boundaries(movie);
    std::vector<Cue> result;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        auto text = rendered(movie, points[i]);
        if (text.empty()) {
            continue;
        }
        if (!result.empty() && result.back().end == points[i] && result.back().text == text) {
            result.back().end = points[i + 1];
        } else {
            result.push_back({points[i], points[i + 1], std::move(text)});
        }
        if (result.size() > max_cues) {
            invalid("Too many native subtitle cues.");
        }
    }
    return result;
}

std::wstring caption_at(const std::vector<Cue>& cues, std::uint64_t movie_time,
                        std::uint32_t movie_scale) {
    if (!movie_scale) {
        return {};
    }
    const auto ms = movie_time * 1000 / movie_scale;
    std::wstring result;
    for (const auto& cue : cues) {
        if (cue.begin > ms) {
            break;
        }
        if (ms < cue.end) {
            if (!result.empty()) {
                result += L'\n';
            }
            result += cue.text;
        }
    }
    return result;
}

Result export_pack(const fs::path& game_root, const fs::path& output_folder,
                   const Progress& progress) {
    if (linked(game_root) || !fs::is_directory(game_root) ||
        (fs::exists(output_folder) && (linked(output_folder) || !fs::is_directory(output_folder) ||
                                       !fs::is_empty(output_folder)))) {
        invalid("Choose an existing game folder and an empty export folder.");
    }
    std::vector<fs::path> movies;
    for (auto cursor = fs::recursive_directory_iterator(game_root);
         cursor != fs::recursive_directory_iterator(); ++cursor) {
        const auto& entry = *cursor;
        const auto relative = entry.path().lexically_relative(game_root);
        if (entry.is_directory()) {
            if (relative == fs::path("subtitles")) {
                cursor.disable_recursion_pending();
                continue;
            }
            if (std::distance(relative.begin(), relative.end()) > 2) {
                cursor.disable_recursion_pending();
                continue;
            }
            if (linked(entry.path())) {
                invalid("Linked game folder.");
            }
            continue;
        }
        if (entry.is_regular_file() && selected_movie(entry.path())) {
            ascii_path(relative);
            require_regular(entry.path(), 512ull * 1024 * 1024);
            movies.push_back(relative);
            if (movies.size() > max_movies) {
                invalid("Too many movie files.");
            }
        }
    }
    if (movies.empty()) {
        invalid("No installed movies found.");
    }
    std::sort(movies.begin(), movies.end());
    const auto parent = output_folder.parent_path();
    if (parent.empty() || linked(parent) || !fs::is_directory(parent)) {
        invalid("Invalid export folder parent.");
    }
    const auto suffix =
        std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    const auto stage = parent / (".subtitles-export-stage-" + suffix);
    const auto backup = parent / (".subtitles-export-backup-" + suffix);
    if (fs::exists(stage) || fs::exists(backup)) {
        invalid("Cannot stage subtitle export.");
    }
    fs::create_directory(stage);
    Result result;
    std::string listing(signature);
    std::string omissions;
    std::set<std::string> subtitle_names;
    bool backed_up = false;
    try {
        std::size_t processed = 0;
        for (const auto& relative : movies) {
            tick(progress, processed++, movies.size());
            const auto path = game_root / relative;
            std::optional<Movie> movie;
            std::vector<Cue> cues;
            try {
                movie = Movie::open(path);
                cues = native_cues(*movie);
            } catch (const std::exception& error) {
                omissions += ascii_path(relative) + "\t" + error.what() + "\n";
                ++result.skipped;
                continue;
            }
            auto subtitle = relative;
            subtitle.replace_extension(".srt");
            if (!subtitle_names.insert(folded_path(subtitle)).second) {
                invalid("Movies have conflicting subtitle filenames.");
            }
            const auto replacement = load_override(game_root, relative, path);
            if (replacement) {
                cues = *replacement;
            }
            const auto content = make_srt(cues, replacement ? 1000 : movie->timescale);
            write_file(stage / subtitle, content);
            listing += ascii_path(relative) + '\t' + digest(path) + '\t' +
                       std::to_string(movie->timescale) + '\t' + std::to_string(movie->duration) +
                       '\t' + ascii_path(subtitle) + '\n';
            if (listing.size() > max_manifest) {
                invalid("Subtitle manifest is too large.");
            }
            ++result.movies;
            result.cues += cues.size();
        }
        if (result.movies == 0) {
            invalid("No readable movie subtitles to export.");
        }
        write_file(stage / "manifest.tsv", listing);
        if (!omissions.empty()) {
            write_file(stage / "skipped.txt", omissions);
        }
        tick(progress, movies.size(), movies.size());
        if (fs::exists(output_folder)) {
            if (linked(output_folder) || !fs::is_directory(output_folder) ||
                !fs::is_empty(output_folder)) {
                invalid("Export folder changed during export.");
            }
            fs::rename(output_folder, backup);
            backed_up = true;
        }
        fs::rename(stage, output_folder);
    } catch (...) {
        if (fs::exists(stage)) {
            fs::remove_all(stage);
        }
        if (backed_up) {
            fs::rename(backup, output_folder);
        }
        throw;
    }
    if (backed_up) {
        std::error_code ignored;
        fs::remove(backup, ignored);
    }
    return result;
}

Result install_pack(const fs::path& game_root, const fs::path& input_folder,
                    const Progress& progress) {
    if (linked(game_root) || linked(input_folder) || !fs::is_directory(game_root) ||
        !fs::is_directory(input_folder)) {
        invalid("Invalid game or subtitle pack folder.");
    }
    const auto records = manifest(input_folder);
    std::uint64_t bytes = 0;
    Result result;
    const auto target = game_root / "subtitles";
    const auto stage = game_root / (".subtitles-stage-" + std::to_string(GetCurrentProcessId()));
    const auto backup = game_root / (".subtitles-backup-" + std::to_string(GetCurrentProcessId()));
    if (fs::exists(stage) || fs::exists(backup) ||
        (fs::exists(target) && (linked(target) || !fs::is_directory(target)))) {
        invalid("Cannot safely replace installed subtitles.");
    }
    fs::create_directory(stage);
    bool backed_up = false;
    try {
        std::string listing(signature);
        for (const auto& record : records) {
            tick(progress, result.movies, records.size());
            check_parents(game_root, record.movie);
            check_parents(input_folder, record.subtitle);
            const auto movie_path = game_root / record.movie;
            require_regular(movie_path, 512ull * 1024 * 1024);
            if (digest(movie_path) != record.hash) {
                invalid("Subtitle movie hash does not match installed media.");
            }
            const auto movie = Movie::open(movie_path);
            if (movie.timescale != record.scale || movie.duration != record.duration) {
                invalid("Subtitle movie timing does not match installed media.");
            }
            const auto content = read_file(input_folder / record.subtitle, max_srt);
            if (bytes > max_pack_bytes - content.size()) {
                invalid("Subtitle pack is too large.");
            }
            bytes += content.size();
            result.cues +=
                parse_srt(content, (movie.duration * 1000 + movie.timescale - 1) / movie.timescale)
                    .size();
            write_file(stage / record.subtitle, content);
            listing += ascii_path(record.movie) + '\t' + record.hash + '\t' +
                       std::to_string(record.scale) + '\t' + std::to_string(record.duration) +
                       '\t' + ascii_path(record.subtitle) + '\n';
            ++result.movies;
        }
        write_file(stage / "manifest.tsv", listing);
        tick(progress, records.size(), records.size());
        if (fs::exists(target)) {
            fs::rename(target, backup);
            backed_up = true;
        }
        fs::rename(stage, target);
    } catch (...) {
        if (fs::exists(stage)) {
            fs::remove_all(stage);
        }
        if (backed_up) {
            fs::rename(backup, target);
        }
        throw;
    }
    if (backed_up) {
        std::error_code ignored;
        fs::remove_all(backup, ignored);
    }
    installed_generation.fetch_add(1, std::memory_order_release);
    return result;
}

std::string srt_text(const std::vector<Cue>& cues, std::uint32_t scale) {
    if (!scale) {
        invalid("Invalid subtitle timescale.");
    }
    return make_srt(cues, scale);
}

std::vector<Cue> srt_cues(const std::string& text, std::uint64_t duration_ms) {
    if (text.size() > max_srt) {
        invalid("Subtitle text is too large.");
    }
    return parse_srt(text, duration_ms);
}

void install_text(const fs::path& root, const fs::path& relative, const std::string& text) {
    const auto key = folded_path(relative);
    check_parents(root, relative);
    require_regular(root / relative, 512ull * 1024 * 1024);
    const auto movie = Movie::open(root / relative);
    srt_cues(text, (movie.duration * 1000 + movie.timescale - 1) / movie.timescale);
    const auto folder = root / "subtitles";
    const auto stage = root / (".subtitle-edit-" + std::to_string(GetCurrentProcessId()) + "-" +
                               std::to_string(GetTickCount64()));
    if (!fs::create_directory(stage)) {
        invalid("Cannot stage subtitle edit.");
    }
    try {
        std::vector<Record> records;
        if (fs::exists(folder)) {
            if (linked(folder)) {
                invalid("Linked subtitle folder.");
            }
            records = manifest(folder);
        }
        auto subtitle = relative;
        subtitle.replace_extension(".srt");
        std::string listing(signature);
        std::uint64_t bytes = text.size();
        for (const auto& record : records) {
            if (folded_path(record.movie) == key) {
                subtitle = record.subtitle;
                continue;
            }
            check_parents(folder, record.subtitle);
            const auto content = read_file(folder / record.subtitle, max_srt);
            bytes += content.size();
            if (bytes > max_pack_bytes) {
                invalid("Subtitle pack is too large.");
            }
            write_file(stage / record.subtitle, content);
            listing += ascii_path(record.movie) + '\t' + record.hash + '\t' +
                       std::to_string(record.scale) + '\t' + std::to_string(record.duration) +
                       '\t' + ascii_path(record.subtitle) + '\n';
        }
        for (const auto& record : records) {
            if (folded_path(record.movie) != key &&
                folded_path(record.subtitle) == folded_path(subtitle)) {
                invalid("Subtitle filename conflicts with another movie.");
            }
        }
        write_file(stage / subtitle, text);
        listing += ascii_path(relative) + '\t' + digest(root / relative) + '\t' +
                   std::to_string(movie.timescale) + '\t' + std::to_string(movie.duration) + '\t' +
                   ascii_path(subtitle) + '\n';
        write_file(stage / "manifest.tsv", listing);
        install_pack(root, stage);
    } catch (...) {
        fs::remove_all(stage);
        throw;
    }
    fs::remove_all(stage);
}

std::uint64_t install_generation() noexcept {
    return installed_generation.load(std::memory_order_acquire);
}

std::optional<std::vector<Cue>> load_override(const fs::path& game_root,
                                              const fs::path& relative_movie,
                                              const fs::path& actual_movie) noexcept {
    try {
        const auto folder = game_root / "subtitles";
        if (!fs::is_directory(folder) || linked(folder)) {
            return std::nullopt;
        }
        const auto relative = folded_path(relative_movie);
        const auto records = manifest(folder);
        const auto found = std::find_if(records.begin(), records.end(), [&](const Record& record) {
            return folded_path(record.movie) == relative;
        });
        if (found == records.end()) {
            return std::nullopt;
        }
        check_parents(folder, found->subtitle);
        if (digest(actual_movie) != found->hash) {
            return std::nullopt;
        }
        auto cues = parse_srt(read_file(folder / found->subtitle, max_srt),
                              (found->duration * 1000 + found->scale - 1) / found->scale);
        if (cues.empty()) {
            return std::nullopt;
        }
        return cues;
    } catch (...) {
        return std::nullopt;
    }
}

}
