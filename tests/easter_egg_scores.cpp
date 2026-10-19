#include <windows.h>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
unsigned reads = 0;
const void* denied = nullptr;
const void* partial = nullptr;
const void* effect_source = nullptr;
std::function<void()> effect;

BOOL WINAPI score_read(HANDLE process, LPCVOID source, LPVOID destination, SIZE_T size,
                       SIZE_T* copied) {
    ++reads;
    if (source == denied) {
        *copied = 0;
        return FALSE;
    }
    const auto result = ReadProcessMemory(process, source, destination, size, copied);
    if (result && source == partial && *copied) {
        --*copied;
    }
    if (result && source == effect_source && effect) {
        effect();
    }
    return result;
}
}

#define ReadProcessMemory score_read
#include "devtools/easter_egg_scores.cpp"
#undef ReadProcessMemory

namespace {
using namespace native_game;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class T> std::uint32_t pointer(T* value) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
}

template <class T, std::size_t N> struct Node {
    DatabaseNode node{};
    std::array<T, N> entries{};
};

using BranchNode = Node<DatabaseBranchEntry, 2>;
static_assert(offsetof(BranchNode, entries) == sizeof(DatabaseNode));

constexpr std::array ids{1411u, 859u, 860u, 858u, 4683u, 4682u};
constexpr std::array names{" iAstaScore",  " iUberParanoia",     " iUberLoss",
                           " iUberXTrack", " iAstaThresholdLow", " iAstaThresholdHigh"};
constexpr std::array values{107, 150, 50, 100, 50, 150};

struct Fixture {
    std::vector<std::byte> image = std::vector<std::byte>(0x300000);
    DatabaseManagerPrefix manager{};
    Application application{};
    std::byte state{};
    Node<DatabaseBranchEntry, 2> root{}, objects{};
    Node<DatabaseClassEntry, 2> classes{};
    std::array<Node<DatabaseObjectEntry, 255>, 2> leaves{};
    std::array<Variable, 6> variables{};
    std::array<std::array<char, 64>, 6> texts{};
    Profile profile;

    explicit Fixture(const Profile& build) : profile(build) {
        put(profile.database_manager, pointer(&manager));
        put(profile.application, &application);
        put(profile.session_active, 1);
        application.state = &state;
        manager.state.root = pointer(&root.node);
        root.node.object.vtable = objects.node.object.vtable =
            pointer(image.data() + profile.database_branch_node);
        root.node.count = 2;
        root.entries[0].cached_node = pointer(&classes.node);
        classes.node.object.vtable = pointer(image.data() + profile.database_class_node);
        classes.node.count = 2;
        classes.entries[0].class_id = 0x49;
        classes.entries[0].index_count = 1;
        classes.entries[0].indexes[0].cached_root = 1;
        auto& entry = classes.entries[1];
        entry.class_id = 0x53;
        entry.index_count = 2;
        entry.indexes[0] = {0x0b, 0x53, 0, 0x12345678, 1, 0, {}};
        entry.indexes[1] = {7, 0x53, 0, 0x6e756c6c, pointer(&objects.node), 0, {}};
        objects.node.count = 2;
        for (unsigned i = 0; i < leaves.size(); ++i) {
            objects.entries[i].cached_node = pointer(&leaves[i].node);
            leaves[i].node.object.vtable = pointer(image.data() + profile.database_object_node);
            leaves[i].node.count = 255;
            for (unsigned j = 0; j < 255; ++j) {
                leaves[i].entries[j].id = 100000 + i * 255 + j;
                leaves[i].entries[j].cached_object = 1;
            }
        }
        constexpr std::array<std::uint8_t, 6> getter{0xb8, 0x53, 0, 0, 0, 0xc3};
        put(0x2008, pointer(image.data() + 0x1800));
        put(0x1800, getter);
        for (unsigned i = 0; i < variables.size(); ++i) {
            std::memcpy(texts[i].data(), names[i], std::strlen(names[i]) + 1);
            variables[i].vtable = pointer(image.data() + 0x2000);
            variables[i].id = ids[i];
            variables[i].name = {0, pointer(texts[i].data()), 64, 0};
            variables[i].raw_value = values[i];
            variables[i].type_flags = 1;
            object_entry(i) = {ids[i], pointer(&variables[i]), 0};
        }
    }

    template <class T> void put(unsigned offset, const T& value) {
        std::memcpy(image.data() + offset, &value, sizeof(value));
    }

    DatabaseObjectEntry& object_entry(unsigned i) {
        constexpr std::array slots{0u, 100u, 254u};
        return leaves[i / 3].entries[slots[i % 3]];
    }

    devtools::EasterEggScores capture() {
        reads = 0;
        const auto result = devtools::capture_easter_egg_scores(image.data(), profile);
        require(reads <= 100, "Score capture exceeded its bounded read budget");
        require(result.scores.size() == 6, "Score capture changed its six-variable coverage");
        for (unsigned i = 0; i < ids.size(); ++i) {
            require(result.scores[i].id == ids[i], "A score descriptor used the wrong GAM ID");
        }
        return result;
    }

    void complete(unsigned read_budget = 40) {
        const auto result = capture();
        require(result.available, "A complete cached score tree was unavailable");
        require(reads <= read_budget, "Score capture did not batch its object leaves");
        for (unsigned i = 0; i < ids.size(); ++i) {
            require(result.scores[i].raw_value == values[i] &&
                        result.scores[i].name ==
                            std::wstring(names[i], names[i] + std::strlen(names[i])),
                    "A cached score returned the wrong raw value or name");
        }
    }

    void unavailable() {
        const auto result = capture();
        require(!result.available, "A malformed or replaced score tree was accepted");
        for (const auto& score : result.scores) {
            require(!score.raw_value && score.name.empty(),
                    "Rejected capture leaked partial values");
        }
    }

    void missing(unsigned index) {
        const auto result = capture();
        require(result.available && !result.scores[index].raw_value,
                "An absent or invalid score was presented as a cached value");
    }
};

void verify(const Profile& profile) {
    Fixture fixture(profile);
    fixture.complete();
    fixture.put(profile.session_active, 0);
    fixture.complete();
    fixture.put(profile.session_active, 1);
    const auto image_before = fixture.image;
    const auto variables_before = fixture.variables;
    fixture.complete();
    require(image_before == fixture.image &&
                std::memcmp(variables_before.data(), fixture.variables.data(),
                            sizeof(fixture.variables)) == 0,
            "Read-only score capture changed native memory");

    fixture.root.entries[1].cached_node = pointer(&fixture.root.node);
    fixture.unavailable();
    fixture.root.entries[1].cached_node = 0;
    fixture.objects.entries[1].cached_node = pointer(&fixture.objects.node);
    fixture.unavailable();
    fixture.objects.entries[1].cached_node = pointer(&fixture.leaves[1].node);
    fixture.root.node.object.vtable = 1;
    fixture.unavailable();
    fixture.root.node.object.vtable = pointer(fixture.image.data() + profile.database_branch_node);
    fixture.classes.node.count = 17;
    fixture.unavailable();
    fixture.classes.node.count = 2;
    fixture.classes.entries[1].index_count = 7;
    fixture.unavailable();
    fixture.classes.entries[1].index_count = 2;
    auto saved_index = fixture.classes.entries[1].indexes[0];
    fixture.classes.entries[1].indexes[0] = fixture.classes.entries[1].indexes[1];
    fixture.unavailable();
    fixture.classes.entries[1].indexes[0] = saved_index;
    const auto native_index = fixture.classes.entries[1].indexes[1];
    fixture.classes.entries[1].indexes[1].base_class = 0x54;
    fixture.missing(0);
    fixture.classes.entries[1].indexes[1] = native_index;
    fixture.classes.entries[1].indexes[1].index_class = 0x0b;
    fixture.classes.entries[1].indexes[1].tag = 0x49442020;
    fixture.missing(0);
    fixture.classes.entries[1].indexes[1] = native_index;
    auto saved_entry = fixture.leaves[1].entries[1];
    fixture.leaves[1].entries[1] = fixture.object_entry(0);
    fixture.unavailable();
    fixture.leaves[1].entries[1] = saved_entry;
    fixture.object_entry(0).cached_object = 0;
    fixture.missing(0);
    fixture.object_entry(0).cached_object = pointer(&fixture.variables[0]);
    fixture.object_entry(0).id = 90000;
    fixture.missing(0);
    fixture.object_entry(0).id = ids[0];
    fixture.variables[0].id = 90000;
    fixture.missing(0);
    fixture.variables[0].id = ids[0];
    fixture.variables[0].vtable = 1;
    fixture.missing(0);
    fixture.variables[0].vtable = pointer(fixture.image.data() + 0x2000);
    fixture.image[0x1801] = std::byte{0x54};
    fixture.missing(0);
    fixture.image[0x1801] = std::byte{0x53};
    fixture.variables[0].name.capacity = 0;
    fixture.missing(0);
    fixture.variables[0].name.capacity = 64;
    const auto saved_text = fixture.texts[0];
    fixture.texts[0].fill('a');
    fixture.missing(0);
    fixture.texts[0] = saved_text;
    fixture.texts[0][0] = '\x01';
    fixture.missing(0);
    fixture.texts[0] = saved_text;
    fixture.classes.entries[1].indexes[1].cached_root = 0;
    fixture.missing(0);
    fixture.classes.entries[1].indexes[1].cached_root = pointer(&fixture.objects.node);

    const std::array<const void*, 11> structural{fixture.image.data() + profile.database_manager,
                                                 &fixture.manager.state.root,
                                                 fixture.image.data() + profile.session_active,
                                                 fixture.image.data() + profile.application,
                                                 &fixture.application.state,
                                                 &fixture.root.node,
                                                 fixture.classes.entries.data(),
                                                 fixture.root.entries.data(),
                                                 &fixture.objects.node,
                                                 fixture.objects.entries.data(),
                                                 fixture.leaves[0].entries.data()};
    for (const auto source : structural) {
        denied = source;
        fixture.unavailable();
        denied = nullptr;
        partial = source;
        fixture.unavailable();
        partial = nullptr;
    }
    for (const auto source : {static_cast<const void*>(&fixture.variables[0]),
                              static_cast<const void*>(fixture.texts[0].data()),
                              static_cast<const void*>(fixture.image.data() + 0x1800)}) {
        denied = source;
        fixture.missing(0);
        denied = nullptr;
        partial = source;
        fixture.missing(0);
        partial = nullptr;
    }
    effect_source = fixture.texts.back().data();
    effect = [&] { fixture.manager.state.root = pointer(&fixture.classes.node); };
    fixture.unavailable();
    fixture.manager.state.root = pointer(&fixture.root.node);
    effect = [&] { fixture.put(profile.session_active, 0); };
    fixture.unavailable();
    fixture.put(profile.session_active, 1);
    effect = [&] { fixture.put(profile.database_manager, pointer(&fixture.application)); };
    fixture.unavailable();
    fixture.put(profile.database_manager, pointer(&fixture.manager));
    effect = [&] { fixture.application.state = nullptr; };
    fixture.unavailable();
    fixture.application.state = &fixture.state;
    effect = {};
    effect_source = nullptr;
    fixture.complete();
}

void budget() {
    Fixture fixture(profile_cd_10012);
    Node<DatabaseBranchEntry, 37> root;
    std::array<Node<DatabaseObjectEntry, 1>, 37> leaves;
    root.node.object.vtable = pointer(fixture.image.data() + fixture.profile.database_branch_node);
    root.node.count = 37;
    for (unsigned i = 0; i < leaves.size(); ++i) {
        root.entries[i].cached_node = pointer(&leaves[i].node);
        leaves[i].node.object.vtable =
            pointer(fixture.image.data() + fixture.profile.database_object_node);
        leaves[i].node.count = 1;
        leaves[i].entries[0] = i < 6
                                   ? DatabaseObjectEntry{ids[i], pointer(&fixture.variables[i]), 0}
                                   : DatabaseObjectEntry{100000 + i, 1, 0};
    }
    fixture.classes.entries[1].indexes[1].cached_root = pointer(&root.node);
    fixture.unavailable();
    require(reads == 100, "The pathological cache did not exercise the hard read limit");
}

void original_shape(const Profile& profile, unsigned final_count) {
    Fixture fixture(profile);
    Node<DatabaseBranchEntry, 31> root;
    std::array<Node<DatabaseObjectEntry, 32>, 31> leaves;
    constexpr std::array counts{28u, 30u, 29u, 30u, 27u, 25u, 21u, 30u, 30u, 30u, 31u,
                                22u, 30u, 32u, 21u, 32u, 32u, 32u, 32u, 32u, 32u, 27u,
                                32u, 32u, 32u, 31u, 32u, 32u, 32u, 32u, 12u};
    root.node.object.vtable = pointer(fixture.image.data() + profile.database_branch_node);
    root.node.count = 31;
    for (unsigned i = 0; i < leaves.size(); ++i) {
        root.entries[i].cached_node = pointer(&leaves[i].node);
        leaves[i].node.object.vtable = pointer(fixture.image.data() + profile.database_object_node);
        leaves[i].node.count = static_cast<std::uint8_t>(i == 30 ? final_count : counts[i]);
        for (unsigned j = 0; j < leaves[i].node.count; ++j) {
            leaves[i].entries[j] = {100000 + i * 32 + j, 1, 0};
        }
        if (i < 6) {
            leaves[i].entries[0] = {ids[i], pointer(&fixture.variables[i]), 0};
        }
    }
    fixture.classes.entries[1].indexes[1].cached_root = pointer(&root.node);
    fixture.complete(95);
    require(reads == 92, "The original 31-leaf GAM shape changed its bounded copy count");
}
}

int main() {
    try {
        for (const auto* profile :
             {&profile_cd_10012, &profile_cd_10019, &profile_cd_10020, &profile_dvd_20000}) {
            verify(*profile);
            original_shape(*profile, profile == &profile_dvd_20000 ? 29 : 12);
        }
        budget();
        std::cout
            << "All four bounded cached-score profiles and failure/read-budget checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
