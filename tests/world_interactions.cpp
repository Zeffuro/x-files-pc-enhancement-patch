#include "enhancements/game_targets.cpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "game/layouts/database/cache.h"

namespace {
using namespace enhancements::game;
std::vector<std::byte> image(3 * 1024 * 1024);
Edition profile{};

template <class T> void put(void* object, unsigned offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::byte* __stdcall resource(void* object) {
    std::byte* result = nullptr;
    std::memcpy(&result, static_cast<std::byte*>(object) + 0x18, sizeof(result));
    return result;
}

void* expected_wrapper = nullptr;
std::byte* resolved_shape = nullptr;
unsigned lookups = 0, releases = 0, references = 0;

std::byte* __stdcall navigation_lookup(void* wrapper, void* context) {
    require(wrapper == expected_wrapper && !context,
            "Navigation geometry lookup changed its native wrapper or context");
    ++lookups;
    references += resolved_shape != nullptr;
    return resolved_shape;
}

void __fastcall navigation_release(void* shape, void*) {
    require(shape == resolved_shape && references == 1,
            "Navigation cursor did not retain exactly one owned shape during its reads");
    --references;
    ++releases;
}

struct ExecutableStub {
    std::byte* source;
    std::array<std::byte, 6> previous{};
    DWORD protection = 0;

    template <class Function>
    ExecutableStub(unsigned offset, Function function) : source(image.data() + offset) {
        std::memcpy(previous.data(), source, previous.size());
        require(VirtualProtect(source, previous.size(), PAGE_EXECUTE_READWRITE, &protection) != 0,
                "Could not allocate native navigation fixture stub");
        source[0] = std::byte{0x68};
        put(source, 1, function);
        source[5] = std::byte{0xc3};
        FlushInstructionCache(GetCurrentProcess(), source, previous.size());
    }

    ~ExecutableStub() {
        std::memcpy(source, previous.data(), previous.size());
        FlushInstructionCache(GetCurrentProcess(), source, previous.size());
        DWORD ignored = 0;
        VirtualProtect(source, previous.size(), protection, &ignored);
    }
};

void check_navigation() {
    alignas(void*) std::array<std::byte, 0x40> object{};
    alignas(void*) std::array<std::byte, 0x80> wrapper{}, shape{};
    alignas(void*) std::array<std::byte, 0x300> state{};
    Application app{};
    app.state = state.data();
    put(image.data(), profile.application, &app);
    put(image.data(), profile.viewport, native_game::Rectangle{nullptr, {20, 90, 620, 330}});
    put(object.data(), 0, image.data() + profile.world_navigation);
    put(object.data(), 8, 1u);
    put(object.data(), 0x18, wrapper.data());
    put(wrapper.data(), 0, image.data() + profile.world_navigation_resource);
    put(wrapper.data(), 4, 72u);
    put(wrapper.data(), 0x2c, 73u);
    put(shape.data(), 0, image.data() + profile.world_navigation_shape_resource);
    put(shape.data(), 4, 73u);
    List<std::byte>::Node node{object.data(), nullptr, nullptr};
    put(state.data(), 0x68, List<std::byte>{image.data(), 1, &node, &node, nullptr});
    expected_wrapper = wrapper.data();
    resolved_shape = shape.data();
    lookups = releases = references = 0;
    ExecutableStub lookup(profile.lookup, &navigation_lookup);
    ExecutableStub release(profile.release, &navigation_release);
    for (const auto bounds : {RECT{20, 90, 80, 330}, RECT{230, 170, 260, 195}}) {
        put(shape.data(), 0x2c, native_game::Rectangle{nullptr, bounds});
        for (const auto [id, category] :
             {std::pair{0x4454u, Interaction::use}, std::pair{0x1bfcu, Interaction::view},
              std::pair{0x122u, Interaction::talk}, std::pair{0x11cu, Interaction::move_left},
              std::pair{0x11du, Interaction::move_right},
              std::pair{0x11eu, Interaction::move_forward},
              std::pair{0x11fu, Interaction::move_back}, std::pair{0u, Interaction::click}}) {
            put(shape.data(), 0x40, id);
            const auto before = lookups;
            const auto targets = world_targets();
            require(targets.size() == 1 && targets[0].interaction == category &&
                        targets[0].navigation &&
                        targets[0].identity == reinterpret_cast<std::uintptr_t>(object.data()) &&
                        EqualRect(&targets[0].bounds, &bounds) &&
                        EqualRect(&targets[0].exposed, &bounds),
                    "Linked navigation cursor changed its geometry, identity or native navigation "
                    "flag");
            require(
                lookups == before + 1 && releases == lookups && !references,
                "Cursor classification added a lookup or changed the owned-shape release contract");
        }
    }
    put(shape.data(), 0x40, 0x4454u);
    put(wrapper.data(), 0x2c, 74u);
    const auto generic = world_targets();
    require(generic.size() == 1 && generic[0].interaction == Interaction::click &&
                generic[0].navigation && releases == lookups && !references,
            "Unproved navigation link lost geometry or leaked its owned shape");
    resolved_shape = nullptr;
    const auto before = lookups;
    require(world_targets().empty() && lookups == before + 1 && releases + 1 == lookups &&
                !references,
            "Failed native geometry lookup invented a target or released a missing shape");
    expected_wrapper = nullptr;
}

void check(const Edition& value) {
    profile = value;
    alignas(void*) std::array<std::byte, 0x180> picture{}, body{}, registry{};
    alignas(void*) std::array<std::byte, 0x80> picture_resource{}, body_resource{};
    alignas(void*) std::array<std::byte, 0x300> state{};
    alignas(void*) std::array<std::byte, 0x200> cache_manager{}, cache_class{}, cache_objects{};
    alignas(void*) std::array<std::byte, 0x100> association{}, association_leaf{},
        association_branch{};
    std::vector<std::array<unsigned, 4>> keys(332);
    for (unsigned i = 0; i < keys.size(); ++i) {
        keys[i] = {1u, 1000u + i, i + 1u, 1u};
    }
    keys[330][2] = 0x1461f;
    keys[330][3] = 0;
    keys[331][2] = 0x1462c;
    keys[331][3] = 0;
    put(image.data(), profile.database_manager, cache_manager.data());
    put(cache_manager.data(), 0x1c, cache_class.data());
    put(cache_class.data(), 0, image.data() + profile.database_class_node);
    put(cache_class.data(), 0x2d, std::uint8_t{1});
    native_game::DatabaseClassEntry cached_class{};
    cached_class.class_id = 0x32;
    cached_class.index_count = 1;
    cached_class.indexes[0].cached_root =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cache_objects.data()));
    put(cache_class.data(), 0x30, cached_class);
    put(cache_objects.data(), 0, image.data() + profile.database_object_node);
    put(cache_objects.data(), 0x2d, std::uint8_t{1});
    native_game::DatabaseObjectEntry cached_object{
        0x1461e, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(association.data())),
        0};
    put(cache_objects.data(), 0x30, cached_object);
    put(association.data(), 0, image.data() + profile.conversation_association);
    put(association.data(), 4, 0x1461eu);
    put(association.data(), 0x28, image.data() + profile.conversation_association_tree);
    put(association.data(), 0x3c, 1u);
    put(association.data(), 0x44, 0xbu);
    put(association.data(), 0x48, 0x49442020u);
    put(association.data(), 0x58, association_leaf.data());
    put(association_leaf.data(), 0, image.data() + profile.association_id_node);
    put(association_leaf.data(), 0x2d, std::uint8_t{1});
    put(association_leaf.data(), 0x30, 0x1461fu);
    put(registry.data(), 0x98, static_cast<unsigned>(keys.size()));
    put(registry.data(), 0xa0, keys.data());
    put(registry.data(), 0xa4, 365u);
    Application app{};
    app.state = state.data();
    app.unknown = registry.data();
    put(image.data(), profile.application, &app);
    put(image.data(), profile.viewport, native_game::Rectangle{nullptr, {20, 90, 620, 330}});
    put(image.data(), profile.world_picture + 4, &resource);
    put(picture.data(), 0, image.data() + profile.world_picture);
    put(body.data(), 0, image.data() + profile.world_hotspot);
    put(picture.data(), 8, 1u);
    put(body.data(), 8, 1u);
    put(picture.data(), 0x18, picture_resource.data());
    put(body.data(), 0x18, body_resource.data());
    put(picture_resource.data(), 0, image.data() + profile.world_picture_resource);
    put(picture_resource.data(), 4, 0x123u);
    put(picture_resource.data(), 0x44, 0x122u);
    put(picture_resource.data(), 0x30, native_game::Rectangle{nullptr, {416, 120, 504, 273}});
    put(body_resource.data(), 0x2c, native_game::Rectangle{nullptr, {352, 267, 405, 314}});
    void* trigger_type = image.data();
    List<std::byte>::Node trigger{reinterpret_cast<std::byte*>(&trigger_type), nullptr, nullptr};
    List<std::byte> empty{image.data(), 0, nullptr, nullptr, nullptr};
    List<std::byte> click{image.data(), 1, &trigger, &trigger, &trigger};
    put(picture.data(), 0x1c, empty);
    put(body.data(), 0x1c, click);
    put(picture.data(), 0x158, &trigger_type);
    put(picture.data(), 0x15c, &trigger_type);
    put(picture.data(), 0x160, 1u);
    put(picture_resource.data(), 0x48, 0x1461eu);
    put(registry.data(), 0x94, image.data() + profile.registry_container);
    put(registry.data(), 0xdc, image.data() + profile.registry_container);
    List<std::byte>::Node picture_node{picture.data(), nullptr, nullptr};
    List<std::byte>::Node body_node{body.data(), nullptr, nullptr};
    put(state.data(), 0x7c,
        List<std::byte>{image.data(), 1, &picture_node, &picture_node, nullptr});
    put(state.data(), 0x194, List<std::byte>{image.data(), 1, &body_node, &body_node, nullptr});
    const auto targets = world_targets();
    require(targets.size() == 2 && targets[0].interaction == Interaction::item &&
                targets[1].interaction == Interaction::click,
            "7BASE person and body native action types were confused");
    const auto geometry = world_hotspots(false);
    const auto classify = [&] {
        return world_interaction(picture.data(), picture_resource.data(), &app, image.data(),
                                 profile);
    };
    alignas(void*) std::array<std::byte, 0x200> conversation_objects{};
    alignas(void*) std::array<std::byte, 0x100> conversation_resource{};
    native_game::DatabaseClassEntry child_class{};
    child_class.class_id = 0x31;
    child_class.index_count = 1;
    child_class.indexes[0].cached_root =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(conversation_objects.data()));
    put(cache_class.data(), 0x2d, std::uint8_t{2});
    put(cache_class.data(), 0x30 + sizeof(cached_class), child_class);
    put(conversation_objects.data(), 0, image.data() + profile.database_object_node);
    put(conversation_objects.data(), 0x2d, std::uint8_t{1});
    native_game::DatabaseObjectEntry child_object{
        0x1461f,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(conversation_resource.data())),
        0};
    put(conversation_objects.data(), 0x30, child_object);
    put(conversation_resource.data(), 0, image.data() + profile.conversation_resource);
    put(conversation_resource.data(), 4, 0x1461fu);
    keys[330][3] = 1;
    require(classify() == Interaction::talk,
            "Unhovered enabled cached conversation did not register configured Talk");
    put(picture.data(), 0x160, 0u);
    put(picture_resource.data(), 0x4c, 123u);
    require(classify() == Interaction::talk,
            "Positive conversation incorrectly required item actions or evidence absence");
    put(picture_resource.data(), 0x44, 0x4454u);
    require(classify() == Interaction::use,
            "Positive conversation registration did not retain its configured action asset");
    put(picture_resource.data(), 0x44, 0xdeadbeefu);
    require(classify() == Interaction::click,
            "Positive ordinary registration was lost with an unknown base cursor");
    put(picture_resource.data(), 0x44, 0x122u);
    put(picture_resource.data(), 0x4c, 0u);
    put(picture.data(), 0x160, 1u);
    put(registry.data(), 0x98, 0u);
    require(classify() == Interaction::talk,
            "Completely validated empty registry did not default to enabled");
    put(registry.data(), 0x98, static_cast<unsigned>(keys.size()));
    keys[330][2] = 0x1461e;
    require(classify() == Interaction::talk,
            "Missing registry key did not default a cached conversation to enabled");
    keys[330][2] = 0x1461f;
    put(conversation_resource.data(), 4, 0x1461eu);
    require(classify() == Interaction::unknown,
            "Wrong cached conversation identity invented ordinary registration");
    put(conversation_resource.data(), 4, 0x1461fu);
    put(conversation_resource.data(), 0, image.data());
    require(classify() == Interaction::unknown,
            "Unsupported cached conversation type invented ordinary registration");
    put(conversation_resource.data(), 0, image.data() + profile.conversation_resource);
    child_object.cached_object = 0;
    put(conversation_objects.data(), 0x30, child_object);
    require(classify() == Interaction::unknown,
            "Uncached enabled conversation invented ordinary registration");
    put(cache_class.data(), 0x2d, std::uint8_t{1});
    keys[330][3] = 0;
    require(classify() == Interaction::item,
            "Disabled mouth picture lost its item-only classification");
    put(picture_resource.data(), 0, image.data());
    require(classify() == Interaction::unknown,
            "Unsupported own picture resource promised item-only");
    put(picture_resource.data(), 0, image.data() + profile.world_picture_resource);
    put(picture_resource.data(), 4, 0u);
    require(classify() == Interaction::unknown, "Zero own resource identity promised item-only");
    put(picture_resource.data(), 4, 0x123u);
    put(picture.data(), 0x18, body_resource.data());
    require(classify() == Interaction::unknown, "Stale own resource pointer promised item-only");
    put(picture.data(), 0x18, picture_resource.data());
    keys[330][3] = 2;
    require(classify() == Interaction::unknown, "Nonzero flag byte was treated as disabled");
    keys[330][3] = 0x100;
    require(classify() == Interaction::item, "Upper flag bytes changed the native byte predicate");
    keys[330][3] = 0;
    put(association_leaf.data(), 0x30, 0x1461du);
    require(classify() == Interaction::unknown, "Missing registry keys must default to enabled");
    put(association_leaf.data(), 0x30, 0x1461fu);
    put(registry.data(), 0x98, 0u);
    require(classify() == Interaction::unknown,
            "Empty registry must default associations to enabled");
    put(registry.data(), 0x98, static_cast<unsigned>(keys.size()));
    put(registry.data(), 0x94, image.data());
    require(classify() == Interaction::unknown, "Unsupported registry type promised item-only");
    put(registry.data(), 0x94, image.data() + profile.registry_container);
    put(registry.data(), 0xa0, reinterpret_cast<void*>(1));
    require(classify() == Interaction::unknown, "Unreadable registry array was accepted");
    put(registry.data(), 0xa0, keys.data());
    std::swap(keys[0], keys[1]);
    require(classify() == Interaction::unknown, "Unsorted native registry was accepted");
    std::swap(keys[0], keys[1]);
    keys[0][2] = 0;
    require(classify() == Interaction::unknown, "Zero native registry key was accepted");
    keys[0][2] = 1;
    keys[1][2] = 1;
    require(classify() == Interaction::unknown, "Duplicate native registry keys were accepted");
    keys[1][2] = 2;
    put(association_leaf.data(), 0x2d, std::uint8_t{2});
    put(association_leaf.data(), 0x34, 0x1462cu);
    require(classify() == Interaction::unknown, "Partial association count promised item-only");
    put(association.data(), 0x3c, 2u);
    require(classify() == Interaction::item,
            "Contiguous ID-only entries were read as cached objects");
    keys[331][3] = 1;
    require(classify() == Interaction::unknown, "A later enabled association was ignored");
    keys[331][3] = 0;
    put(association_branch.data(), 0, image.data() + profile.database_branch_node);
    put(association_branch.data(), 0x2d, std::uint8_t{1});
    native_game::DatabaseBranchEntry branch_child{
        1, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(association_leaf.data()))};
    put(association_branch.data(), 0x30, branch_child);
    put(association.data(), 0x58, association_branch.data());
    require(classify() == Interaction::item, "Complete cached association branch was rejected");
    branch_child.cached_node = 0;
    put(association_branch.data(), 0x30, branch_child);
    require(classify() == Interaction::unknown, "Uncached association child promised item-only");
    branch_child.cached_node =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(association_branch.data()));
    put(association_branch.data(), 0x30, branch_child);
    require(classify() == Interaction::unknown, "Association cycle was not rejected");
    put(association.data(), 0x58, reinterpret_cast<void*>(1));
    require(classify() == Interaction::unknown, "Unreadable association node promised item-only");
    put(association.data(), 0x58, association_leaf.data());
    put(association_leaf.data(), 0, image.data() + profile.database_object_node);
    require(classify() == Interaction::unknown,
            "Resource cache leaf was treated as an ID-only leaf");
    put(association_leaf.data(), 0, image.data() + profile.association_id_node);
    put(association.data(), 0x58, static_cast<void*>(nullptr));
    require(classify() == Interaction::unknown, "Missing association cache promised item-only");
    put(association.data(), 0x58, association_leaf.data());
    put(association.data(), 0x28, image.data());
    require(classify() == Interaction::unknown, "Unverified association tree promised item-only");
    put(association.data(), 0x28, image.data() + profile.conversation_association_tree);
    cached_object.cached_object = 0;
    put(cache_objects.data(), 0x30, cached_object);
    require(classify() == Interaction::unknown, "Uncached association resource promised item-only");
    cached_object.cached_object =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(association.data()));
    put(cache_objects.data(), 0x30, cached_object);
    cached_class.class_id = 0x33;
    put(cache_class.data(), 0x30, cached_class);
    require(classify() == Interaction::unknown, "Missing cached class promised item-only");
    cached_class.class_id = 0x32;
    put(cache_class.data(), 0x30, cached_class);
    put(association.data(), 0x3c, 0u);
    put(association.data(), 0x58, static_cast<void*>(nullptr));
    put(registry.data(), 0x98, 0u);
    require(classify() == Interaction::item, "Verified empty association should have no fallback");
    put(association.data(), 0x58, reinterpret_cast<void*>(1));
    require(classify() == Interaction::unknown, "Empty association with stale root was accepted");
    put(association.data(), 0x58, association_leaf.data());
    require(classify() == Interaction::unknown, "Empty descriptor with nonempty node was accepted");
    put(association.data(), 0x58, static_cast<void*>(nullptr));
    put(association.data(), 4, 0x1461du);
    require(classify() == Interaction::unknown, "Wrong empty association identity was accepted");
    put(association.data(), 4, 0x1461eu);
    put(association.data(), 0x3c, 2u);
    put(association.data(), 0x58, association_leaf.data());
    put(registry.data(), 0x98, static_cast<unsigned>(keys.size()));
    put(picture_resource.data(), 0x4c, 123u);
    require(classify() == Interaction::unknown, "Unproved evidence association promised item-only");
    put(picture_resource.data(), 0x4c, 0u);
    put(picture_resource.data(), 0x48, 0u);
    require(classify() == Interaction::item, "Absent resource association was not recognized");
    put(picture.data(), 0x1c, click);
    require(classify() == Interaction::talk,
            "Click plus item actions should retain its configured ordinary category");
    auto malformed = click;
    malformed.first = reinterpret_cast<List<std::byte>::Node*>(1);
    put(picture.data(), 0x1c, malformed);
    require(classify() == Interaction::unknown, "Unreadable trigger node was not rejected");
    malformed = click;
    malformed.last = nullptr;
    put(picture.data(), 0x1c, malformed);
    require(classify() == Interaction::unknown, "Inconsistent trigger list promised a click");
    malformed = empty;
    malformed.current = &trigger;
    put(picture.data(), 0x1c, malformed);
    require(classify() == Interaction::unknown, "Stale trigger iterator promised item-only");
    put(picture.data(), 0x1c, empty);
    put(picture.data(), 0x160, 0u);
    require(classify() == Interaction::unknown, "Ineligible own-resource array promised item use");
    put(picture.data(), 0x160, 1u);
    put(picture.data(), 0x158, static_cast<void*>(nullptr));
    put(picture.data(), 0x15c, static_cast<void*>(nullptr));
    put(picture.data(), 0x13c, &trigger_type);
    require(classify() == Interaction::unknown, "Non-item slot was treated as a default action");
    put(picture.data(), 0x158, &trigger_type);
    app.unknown = reinterpret_cast<void*>(1);
    put(picture_resource.data(), 0x48, 0x1461eu);
    require(classify() == Interaction::unknown, "Unreadable manager was not rejected");
    app.unknown = registry.data();
    put(picture_resource.data(), 0x48, 0u);
    put(picture.data(), 0, image.data() + profile.picture);
    require(classify() == Interaction::unknown, "Unverified picture subclass was classified");
    put(picture.data(), 0, image.data() + profile.world_picture);
    const auto after = world_hotspots(false);
    require(after.size() == geometry.size() &&
                std::equal(after.begin(), after.end(), geometry.begin(),
                           [](const RECT& a, const RECT& b) { return EqualRect(&a, &b); }),
            "Interaction metadata changed controller hotspot geometry");
}
}

namespace enhancements {
bool exposed_target(const RECT& bounds, std::span<const RECT>, RECT& exposed) {
    exposed = bounds;
    return true;
}

namespace game {
std::byte* executable_image() {
    return image.data();
}

const Edition& edition() {
    return profile;
}

void* current_input() {
    return nullptr;
}

std::uintptr_t input_vtable() {
    return 0;
}
}
}

int main() {
    try {
        for (const auto& edition :
             {native_game::profile_cd_10012, native_game::profile_cd_10019,
              native_game::profile_dvd_20000, native_game::profile_cd_10020}) {
            check(edition);
            check_navigation();
        }
        std::cout << "Four-profile passive world interaction classification passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
