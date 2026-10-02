#include "devtools/database/model.h"
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

devtools::NativeDatabaseObject object(unsigned type, unsigned id, bool state = false) {
    devtools::NativeDatabaseObject value;
    value.class_id = type;
    value.id = id;
    value.state_database = state;
    return value;
}
}

int main() {
    using namespace devtools;
    NativeDatabaseSnapshot before;
    before.available = true;
    before.manager_address = 100;
    before.hdb_address = 104;
    before.state_address = 316;
    before.objects = {object(0x54, 10), object(0x41, 20), object(0x2f, 20), object(0x53, 20, true),
                      object(0x53, 20)};
    before.objects[0].relationships = {
        {L"Action", 0x41, 20}, {L"Name", 0x2f, 20}, {L"Missing", 0x41, 99}, {L"None", 0x41, 0}};
    before.objects[3].variable = DatabaseVariable{1, 1};
    before.objects[4].variable = DatabaseVariable{2, 1};
    require(database_resolve(before, {L"Action", 0x41, 20}) == 1u, "Class-aware ID lookup failed");
    require(!database_resolve(before, {L"Missing", 0x41, 99}), "Uncached reference resolved");
    require(database_links(before, 0).size() == 2, "Forward links missing");
    const auto incoming = database_links(before, 1);
    require(incoming.size() == 1 && incoming[0].target == 0, "Reverse link failed");
    require(database_links(before, 99).empty(), "Invalid source accepted");
    auto after = before;
    after.objects[3].variable->raw_value = 9;
    after.objects[4].address = 1000;
    after.objects[4].refcount = 15;
    auto changes = database_variable_changes(before, after);
    require(changes.size() == 1 && changes[0] == after.objects[3].key(),
            "State variable change not isolated");
    after.objects[4].variable->type_flags = 0x81;
    require(database_variable_changes(before, after).size() == 2, "Type flag change missed");
    after.objects.push_back(object(0x53, 77, true));
    after.objects.back().variable = DatabaseVariable{99, 1};
    require(database_variable_changes(before, after).size() == 2,
            "New cached variable reported as changed");
    after.manager_address = 101;
    require(database_variable_changes(before, after).empty(), "Manager change retained baseline");
    after = before;
    after.available = false;
    require(database_variable_changes(before, after).empty(), "Unavailable snapshot compared");
    after = before;
    after.objects.push_back(object(0x41, 20, true));
    require(!database_resolve(after, {L"Action", 0x41, 20}),
            "Ambiguous database reference resolved");
    require(database_links(after, 0).size() == 1, "Ambiguous target exposed in navigation");
    require(database_find(after, {0x41, 20, false}) == 1u, "Database identity lost");
    before.objects.push_back(before.objects[3]);
    before.objects.back().variable.reset();
    after = before;
    after.objects.pop_back();
    after.objects[3].variable->raw_value = 7;
    require(database_variable_changes(before, after).empty(),
            "Partially readable duplicate baseline compared");
    before.objects.back().variable = before.objects[3].variable;
    after = before;
    after.objects[3].variable->raw_value = 7;
    require(database_variable_changes(before, after).empty(),
            "Duplicate variable identity compared");
}
