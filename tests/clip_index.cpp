#include "game/assets/clip_index.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Fixture {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (L"xfiles-clip-index-" +
         std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".hdb");

    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    void write(const std::string& bytes) const {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        require(bool(output), "Could not create clip index fixture");
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        require(bool(output), "Could not write clip index fixture");
    }
};

std::string labeled(const std::string& label, const std::string& directory, const std::string& id) {
    return label + ".mov" +
           std::string("\x01"
                       "1"
                       "\x01"
                       "1"
                       "\x02"
                       "2",
                       6) +
           directory + "\177" + id + ".xmv";
}

void verify() {
    Fixture fixture;
    require(!game_assets::ClipIndex::load(fixture.path), "Missing HDB should fail to load");

    const std::string story = labeled("Video\177Node 1\177Field Office\177SC03A", "XV", "019808");
    const std::string story_variant =
        labeled("Video\177Node 1\177Field Office\177sc03a", "XV", "19808");
    const std::string nav = labeled("Navs\177LoopsR\177Field Office\177"
                                    "028 - FONAV1E",
                                    "XN", "38804");
    const std::string game = labeled("Video\177Game\177Teaser", "XV", "19668");
    const std::string same_id_other_dir =
        labeled("Navs\177LoopsR\177Rail Yard\177Alternate", "XN", "19808");
    std::string raw = "binary-prefix\0";
    raw.append(1, '\0');
    const auto story_offset = raw.size();
    raw += story + '\0';
    const auto variant_offset = raw.size();
    raw += story_variant + '\0';
    const auto repeat_offset = raw.size();
    raw += story + '\0';
    const auto nav_offset = raw.size();
    raw += nav + '\0' + game + '\0' + same_id_other_dir + '\0';

    // The asset path must belong to the nearby label and use a supported directory.
    raw += labeled("Video\177Node 2\177Bogus\177Skip", "XG", "20001") + '\0';
    raw += "Video\177Node 2\177Bogus\177Truncated.mov\x01";
    raw += labeled("Video\177Node 999999999999\177Bogus\177Overflow", "XV", "20002") + '\0';
    raw += labeled("Navs\177LoopsR\177Bad\x01Place\177Wrong", "XN", "20003") + '\0';
    raw += labeled("Video\177Node 3\177Bogus\177Bad ID", "XV", "4294967296") + '\0';
    raw += "Video\177Node 1\177Bogus\177Far.mov" + std::string(13, '\0') +
           "XV\177"
           "20004.xmv";
    fixture.write(raw);

    const auto index = game_assets::ClipIndex::load(fixture.path);
    require(bool(index), "Valid HDB fixture should load");
    require(index->entries().size() == 4, "Expected four distinct directory and numeric IDs");
    require(index->entries()[0].movie == L"XV/019808.xmv", "Observed leading zeroes were lost");

    const auto story_labels = index->labels(L"c:\\game\\xv\\19808.XMV");
    require(story_labels.size() == 3, "Distinct labels and source occurrences should remain");
    require(story_labels[0].kind == L"scene" && story_labels[0].location == L"Field Office" &&
                story_labels[0].scene == L"SC03A" && story_labels[0].node == 1u &&
                story_labels[0].offset == story_offset,
            "Story label fields or provenance were lost");
    require(story_labels[1].scene == L"sc03a" && story_labels[1].offset == variant_offset,
            "Second story label was lost");
    require(story_labels[2].scene == L"SC03A" && story_labels[2].offset == repeat_offset,
            "Repeated label lost its separate source offset");

    const auto nav_labels = index->labels(L"XN/038804.xmv");
    require(nav_labels.size() == 1 && nav_labels[0].kind == L"nav" &&
                nav_labels[0].category == L"LoopsR" && nav_labels[0].location == L"Field Office" &&
                nav_labels[0].scene == L"028 - FONAV1E" && !nav_labels[0].node &&
                nav_labels[0].offset == nav_offset,
            "Navigation label fields were incorrect");
    const auto game_labels = index->labels(L"xv/19668.xmv");
    require(game_labels.size() == 1 && game_labels[0].kind == L"game" &&
                game_labels[0].location == L"Game" && game_labels[0].scene == L"Teaser" &&
                !game_labels[0].node,
            "Game label fields were incorrect");
    require(index->labels(L"XN/19808.xmv").size() == 1,
            "Same numeric ID in another directory was conflated");
    require(index->labels(L"XV/20001.xmv").empty() && index->labels(L"XV/20004.xmv").empty() &&
                index->labels(L"XG/20001.xmv").empty() && index->labels(L"XV/19808.mov").empty(),
            "Malformed, distant or unsupported paths were indexed");

    fixture.write("Video\177Node 1\177Incomplete");
    const auto truncated = game_assets::ClipIndex::load(fixture.path);
    require(bool(truncated) && truncated->entries().empty(),
            "Truncated HDB should yield no labels");
}

}

int main() {
    try {
        verify();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
