#include <windows.h>
#include <cstdint>
#include <stdexcept>

namespace {
BOOL WINAPI checked_read(HANDLE process, LPCVOID source, LPVOID destination, SIZE_T size,
                         SIZE_T* copied);
}

#define ReadProcessMemory checked_read
#include "../tools/native-story/state.cpp"
#undef ReadProcessMemory
#include <iostream>

namespace {
using namespace native_game;

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

std::uintptr_t raw_address = 0, flags_address = 0;
bool short_copy = false;
bool drift = false;
unsigned raw_requests = 0, flags_requests = 0;
std::uintptr_t cursor_header = 0, cursor_node = 0, cursor_first = 0;
unsigned header_reads = 0, node_reads = 0, topology_drift = 0, node_drift = 0;
bool move_iterator = false;

BOOL WINAPI checked_read(HANDLE process, LPCVOID source, LPVOID destination, SIZE_T size,
                         SIZE_T* copied) {
    require(process == GetCurrentProcess(), "Reader changed the selected process handle");
    const auto address = reinterpret_cast<std::uintptr_t>(source);
    if (address == raw_address) {
        require(size == sizeof(std::int32_t), "Variable value read has the wrong width");
        ++raw_requests;
        if (short_copy) {
            *copied = size - 1;
            return TRUE;
        }
    }
    if (address == flags_address) {
        require(size == sizeof(std::uint8_t), "Variable flags read has the wrong width");
        ++flags_requests;
    }
    const auto result = ReadProcessMemory(process, source, destination, size, copied);
    if (result && drift && address == raw_address && raw_requests == 2) {
        ++*static_cast<std::int32_t*>(destination);
    }
    if (result && address == cursor_header) {
        if (size == sizeof(List<void>)) {
            ++header_reads;
            if (move_iterator && header_reads == 1) {
                *reinterpret_cast<std::uintptr_t*>(address + offsetof(List<void>, current)) =
                    cursor_first;
            }
        } else if (size == offsetof(List<void>, current) && topology_drift) {
            auto* words = static_cast<std::uint32_t*>(destination);
            ++words[topology_drift == 1 ? 1 : 2];
        }
    }
    if (result && address == cursor_node && size == sizeof(List<void>::Node)) {
        if (++node_reads == 2 && node_drift) {
            auto& node = *static_cast<List<void>::Node*>(destination);
            if (node_drift == 1) {
                node.next = nullptr;
            } else {
                node.value = nullptr;
            }
        }
    }
    return result;
}

struct ObjectNode {
    DatabaseNode node;
    DatabaseObjectEntry entry;
};

struct TestInput {
    std::uint32_t vtable;
    std::byte prefix[offsetof(MovieInputPrefix, skippable) - sizeof(vtable)];
    unsigned skippable;
};

static_assert(offsetof(TestInput, skippable) == offsetof(MovieInputPrefix, skippable));
static_assert(profile_cd_10012.native_input_list == 0x253638);
static_assert(profile_cd_10019.native_input_list == 0x2554e8);
static_assert(profile_dvd_20000.native_input_list == 0x256278);
static_assert(profile_cd_10020.native_input_list == 0x2564e8);

struct Fixture {
    std::byte* image = static_cast<std::byte*>(
        VirtualAlloc(nullptr, 4 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Profile profile;
    Build build;
    ApplicationQueuesPrefix app{};
    ScriptStatePrefix state{};
    DatabaseManagerPrefix manager{};
    ObjectNode node{};
    Variable variable{};
    PreferencesPrefix preferences{};
    TestInput menu{}, movie{};
    List<void>::Node first{}, second{};
    MainView view{};
    TextPrefix modal_text{};
    InventoryItem modal_item{};
    List<InventoryItem>::Node modal_node{};
    char question[11] = "Try again?";
    char name[5] = "bomb";

    explicit Fixture(const Build& selected = build_cd_10012)
        : profile(*selected.profile), build(selected) {
        build.profile = &profile;
        require(image != nullptr, "Fixture allocation failed");
        *reinterpret_cast<Application**>(image + profile.application) = &app.application;
        *reinterpret_cast<DatabaseManagerPrefix**>(image + profile.database_manager) = &manager;
        app.application.state = &state;
        app.application.preferences = &preferences;
        const auto fake_type = image + 0x200000;
        for (auto group :
             {&state.roots, &state.controls, &state.scene.navigation, &state.scene.hotspots}) {
            group->vtable = fake_type;
        }
        inputs().vtable = image + profile.native_input_list;
        auto& children =
            *reinterpret_cast<List<ChildView>*>(MainViewRef(&view, &profile).children().address());
        children.vtable = view.inventory.items.vtable = fake_type;
        menu.vtable =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image + profile.main_menu));
        movie.vtable =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image + profile.movie));
        manager.hdb.root = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&node));
        node.node.object.vtable = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(image + profile.database_object_node));
        node.node.count = 1;
        node.entry = {2383, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&variable)),
                      0};
        variable.id = node.entry.id;
        variable.vtable = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(fake_type));
        const auto getter =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image + 0x1000));
        std::memcpy(fake_type + persistent_class_getter_offset, &getter, sizeof(getter));
        constexpr std::array<std::uint8_t, 6> code{0xb8, 0x53, 0, 0, 0, 0xc3};
        std::memcpy(image + 0x1000, code.data(), code.size());
        const auto shape_getter =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image + 0x2000));
        std::memcpy(image + profile.world_navigation_shape_resource +
                        persistent_class_getter_offset,
                    &shape_getter, sizeof(shape_getter));
        constexpr std::array<std::uint8_t, 6> shape_code{0xb8, 0x3b, 0, 0, 0, 0xc3};
        std::memcpy(image + 0x2000, shape_code.data(), shape_code.size());
        variable.raw_value = -37;
        variable.type_flags = 1;
        variable.name.data = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(name));
        variable.name.capacity = sizeof(name);
        raw_address = VariableRef(&variable).raw_value().address();
        flags_address = VariableRef(&variable).type_flags().address();
    }

    ~Fixture() {
        VirtualFree(image, 0, MEM_RELEASE);
    }

    std::string snapshot() {
        return native_story::snapshot(GetCurrentProcess(), reinterpret_cast<std::uintptr_t>(image),
                                      build);
    }

    List<void>& inputs() {
        return *reinterpret_cast<List<void>*>(SceneStateRef(&state, &profile).inputs().address());
    }

    void show_modal() {
        app.application.view = &view;
        const auto type = image + profile.text;
        std::memcpy(modal_text.reserved, &type, sizeof(type));
        modal_text.string = question;
        modal_item.icon = reinterpret_cast<InventoryIcon*>(&modal_text);
        modal_item.position = {275, 214};
        modal_node.value = &modal_item;
        const MainViewRef owner(&view, &profile);
        auto& background = *reinterpret_cast<Container*>(owner.modal_background().address());
        auto& foreground =
            *reinterpret_cast<List<InventoryItem>*>(owner.modal_foreground().address());
        background.count = 1;
        foreground.vtable = image + 0x200000;
        foreground.count = 1;
        foreground.first = foreground.last = &modal_node;
    }
};
}

int main() {
    try {
        for (const auto selected :
             {&build_cd_10012, &build_cd_10019, &build_dvd_20000, &build_cd_10020}) {
            Fixture fixture(*selected);
            const auto state = fixture.snapshot();
            require(state.find("\"status\":\"ok\"") != std::string::npos, state.c_str());
            fixture.show_modal();
            require(fixture.snapshot().find("\"modal\":{\"text\":\"Try again?\"") !=
                        std::string::npos,
                    "Native modal did not honor the selected build's child-list layout");
        }
        Fixture fixture;
        const auto valid = fixture.snapshot();
        require(valid.find("\"status\":\"ok\"") != std::string::npos, valid.c_str());
        require(valid.find("\"raw_value\":-37") != std::string::npos,
                "Snapshot lost the signed native value");
        require(valid.find("\"name\":\"bomb\"") != std::string::npos,
                "Snapshot lost the checked native variable name");
        require(raw_requests >= 2 && flags_requests >= 2,
                "Value and flags were not read at their exact widths and rechecked");
        require(valid.find("\"input_address\":0") != std::string::npos &&
                    valid.find("\"skip_available\":false") != std::string::npos,
                "Empty native input list did not report its verified empty state");
        fixture.first = {&fixture.menu, &fixture.second, nullptr};
        fixture.second = {&fixture.movie, nullptr, &fixture.first};
        fixture.inputs().count = 2;
        fixture.inputs().first = &fixture.first;
        fixture.inputs().last = fixture.inputs().current = &fixture.second;
        const auto menu = fixture.snapshot();
        require(menu.find("\"input_address\":" + std::to_string(reinterpret_cast<std::uintptr_t>(
                                                     &fixture.menu))) != std::string::npos &&
                    fixture.inputs().current == &fixture.second,
                "Probe selected the iterator instead of the first value or wrote the iterator");
        fixture.inputs().current = reinterpret_cast<List<void>::Node*>(1);
        require(fixture.snapshot().find("invalid_list_tail_or_current") != std::string::npos,
                "An invalid initial native iterator bypassed membership validation");
        fixture.inputs().current = &fixture.second;
        cursor_header = SceneStateRef(&fixture.state, &fixture.profile).inputs().address();
        cursor_node = cursor_first = reinterpret_cast<std::uintptr_t>(&fixture.first);
        move_iterator = true;
        require(fixture.snapshot().find("\"status\":\"ok\"") != std::string::npos &&
                    fixture.inputs().current == &fixture.first,
                "An unpublished iterator change invalidated otherwise coherent list membership");
        move_iterator = false;
        for (unsigned kind : {1u, 2u}) {
            topology_drift = kind;
            require(fixture.snapshot().find("state_changed_during_snapshot") != std::string::npos,
                    "Changed list count or first node bypassed coherence");
        }
        topology_drift = 0;
        for (unsigned kind : {1u, 2u}) {
            node_reads = 0;
            node_drift = kind;
            require(fixture.snapshot().find("state_changed_during_snapshot") != std::string::npos,
                    "Changed native node link or value bypassed coherence");
        }
        node_drift = 0;
        cursor_header = cursor_node = 0;
        fixture.first = {&fixture.movie, nullptr, nullptr};
        fixture.inputs().count = 1;
        fixture.inputs().last = fixture.inputs().current = &fixture.first;
        fixture.preferences.skip_movies = fixture.movie.skippable = 1;
        require(fixture.snapshot().find("\"skip_available\":true") != std::string::npos,
                "Skippable movie did not honor both native flags");
        fixture.preferences.skip_movies = 0;
        require(fixture.snapshot().find("\"skip_available\":false") != std::string::npos,
                "Movie skipping bypassed the native preference");
        fixture.preferences.skip_movies = 1;
        fixture.movie.skippable = 0;
        const auto unskippable = fixture.snapshot();
        require(unskippable.find("\"skip_available\":false") != std::string::npos &&
                    unskippable.find("\"movie_active\":true") != std::string::npos,
                "Movie skipping bypassed the current input flag");
        fixture.movie.vtable = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(fixture.image + fixture.profile.action_movie));
        fixture.movie.skippable = 1;
        require(fixture.snapshot().find("\"skip_available\":true") != std::string::npos,
                "Action movie did not honor the same native skip flags");
        fixture.movie.vtable = 0;
        require(fixture.snapshot().find("invalid_native_input_type") != std::string::npos,
                "Nonempty native input with a null vtable was accepted");
        fixture.inputs().count = 0;
        fixture.inputs().first = fixture.inputs().last = fixture.inputs().current = nullptr;
        fixture.inputs().vtable = fixture.image;
        require(fixture.snapshot().find("unexpected_native_input_list_type") != std::string::npos,
                "Unknown input container type bypassed the checked build layout");
        fixture.inputs().vtable = fixture.image + fixture.profile.native_input_list;
        fixture.show_modal();
        const auto modal = fixture.snapshot();
        require(modal.find("\"modal\":{\"text\":\"Try again?\"") != std::string::npos,
                "Native modal text was not read from its checked native text object");
        const MainViewRef owner(&fixture.view, &fixture.profile);
        auto& background = *reinterpret_cast<Container*>(owner.modal_background().address());
        background.count = 0;
        require(fixture.snapshot().find("\"modal\":null") != std::string::npos,
                "An inactive native modal produced a false dialog identity");
        fixture.app.application.view = nullptr;
        short_copy = true;
        const auto partial = fixture.snapshot();
        require(partial.find("\"status\":\"unavailable\"") != std::string::npos &&
                    partial.find("raw_value") == std::string::npos,
                "Short native read produced a fabricated or partial successful snapshot");
        short_copy = false;
        raw_requests = 0;
        drift = true;
        const auto changed = fixture.snapshot();
        require(changed.find("state_changed_during_snapshot") != std::string::npos &&
                    changed.find("raw_value") == std::string::npos,
                "Changing native value bypassed the snapshot coherence check");
        drift = false;
        fixture.state.controls.count = 257;
        require(fixture.snapshot().find("invalid_list_header") != std::string::npos,
                "Malformed native list bypassed the traversal bound");
        std::cout << "Native story snapshot exact-width, coherence and failure checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
