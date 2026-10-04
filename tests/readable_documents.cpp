#include "enhancements/script_controls.h"
#include "enhancements/document_text.h"
#include "enhancements/game_resources.h"
#include "enhancements/ui/document_page.h"
#include "platform/game_fonts.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace enhancements;

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

template <class T> void put(std::byte* target, std::size_t offset, const T& value) {
    std::memcpy(target + offset, &value, sizeof(value));
}

void capture(const game::Edition& profile) {
    std::vector<std::byte> image(0x300000), state(0x300), root(0x30), resource(8);
    std::array<std::array<std::byte, 0x1c0>, 2> controls{}, graphics{};
    std::array<std::byte, 0xc0> field{};
    std::array<game::List<std::byte>::Node, 2> nodes{};
    std::string body = "First line\r\n\r\n" + std::string(3000, 'W') + "\nFinal line";
    put(root.data(), 0, image.data() + profile.script_root);
    put(root.data(), 0x18, resource.data());
    put(resource.data(), 4, resource::pda_message);
    game::List<std::byte>::Node root_node{root.data(), nullptr, nullptr};
    game::List<std::byte> roots{nullptr, 1, &root_node, &root_node, nullptr};
    put(state.data(), 0x25c, roots);
    for (unsigned i = 0; i < 2; ++i) {
        auto* control = controls[i].data();
        put(control, 0, image.data() + profile.script_control);
        put(control, 8, 1u);
        put(control, 0x144, script_control::input_first);
        put(control, profile.control_rectangle + 4, RECT{200, 90, 410, 340});
        put(control, 0x140, graphics[i].data());
        nodes[i] = {control, i == 0 ? &nodes[1] : nullptr, i ? &nodes[0] : nullptr};
    }
    game::List<std::byte> inputs{nullptr, 2, nodes.data(), &nodes.back(), nullptr};
    put(state.data(), 0x270, inputs);
    put(graphics[0].data(), 0, image.data() + profile.input_graphic);
    put(graphics[0].data(), 0x154, field.data());
    put(field.data(), 0, image.data() + profile.text);
    put(field.data(), 0x28, body.c_str());
    put(graphics[1].data(), 0, image.data() + profile.text);
    put(graphics[1].data(), 0x28, body.c_str());
    auto result = game::read_script_controls(state.data(), image.data(), profile);
    require(result.document_text.size() == 2 && result.document_text[0].value == body &&
                result.document_text[1].value == body,
            "Native text did not retain the complete body through both known graphic classes");
    require(result.fields[0].value.size() == 128, "Navigation field capture changed its bound");
    put(controls[1].data(), 8, 0u);
    put(controls[0].data(), 12, 1u);
    require(game::read_script_controls(state.data(), image.data(), profile).document_text.empty(),
            "Disabled or hidden controls supplied document text");
    put(controls[0].data(), 12, 0u);
    const auto guard =
        static_cast<char*>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    require(guard != nullptr, "Cannot allocate guarded native string fixture");
    DWORD previous = 0;
    VirtualProtect(guard + 4096, 4096, PAGE_NOACCESS, &previous);
    std::memset(guard, 'Q', 4096);
    put(field.data(), 0x28, guard + 4096 - 128);
    require(game::read_script_controls(state.data(), image.data(), profile).document_text.empty(),
            "An unterminated inaccessible native string was displayed as complete text");
    guard[4095] = '\0';
    result = game::read_script_controls(state.data(), image.data(), profile);
    require(result.document_text.size() == 1 && result.document_text[0].value.size() == 127,
            "A valid native string at a page boundary was rejected");
    VirtualFree(guard, 0, MEM_RELEASE);
    std::string oversized(65536, 'Z');
    put(field.data(), 0x28, oversized.c_str());
    require(game::read_script_controls(state.data(), image.data(), profile).document_text.empty(),
            "Oversized native text was truncated and presented as complete");
}

void native_body(const game::Edition& profile) {
    std::vector<std::byte> image(0x300000), state(0x300), view(0x900), root(0x30), resource(8);
    std::array<std::byte, 0xc0> text{}, second_text{};
    std::array<std::byte, 20> record{}, second_record{};
    std::array<std::byte, 0x1c0> control{}, graphic{};
    auto* group = image.data() + profile.main_text;
    std::array<std::byte*, 2> entries{group, nullptr};
    const std::string body =
        "To: Willmore\nFrom: Jim\n\n" + std::string(3000, 'R') + "\nFinal signature";
    put(root.data(), 0, image.data() + profile.script_root);
    put(root.data(), 0x18, resource.data());
    put(resource.data(), 4, resource::pda_message);
    game::List<std::byte>::Node root_node{root.data(), nullptr, nullptr};
    put(state.data(), 0x25c, game::List<std::byte>{nullptr, 1, &root_node, &root_node, nullptr});
    put(control.data(), 0, image.data() + profile.script_control);
    put(control.data(), 8, 1u);
    put(control.data(), 0x144, script_control::text_cursor);
    put(control.data(), profile.control_rectangle + 4, RECT{203, 60, 418, 353});
    game::List<std::byte>::Node control_node{control.data(), nullptr, nullptr};
    put(state.data(), 0x270,
        game::List<std::byte>{nullptr, 1, &control_node, &control_node, nullptr});
    game::ChildView child{view.data() + 0x4c8};
    game::List<game::ChildView>::Node child_node{&child, nullptr, nullptr};
    put(view.data(), profile.children,
        game::List<game::ChildView>{nullptr, 1, &child_node, &child_node, nullptr});
    put(view.data(), 0x4d0, 1u);
    put(view.data(), 0x4d4, 0u);
    put(view.data(), 0x4d8, entries.data());
    put(group, profile.main_text_rectangle + 4, RECT{203, 60, 418, 353});
    put(text.data(), 0, image.data() + profile.text);
    put(text.data(), 0x28, body.c_str());
    put(record.data(), 0, text.data());
    game::List<std::byte>::Node text_node{record.data(), nullptr, nullptr};
    game::List<std::byte> texts{nullptr, 1, &text_node, &text_node, nullptr};
    put(group, 4, texts);
    const auto capture = [&] {
        return game::read_script_controls(state.data(), image.data(), profile, view.data());
    };
    auto result = capture();
    require(result.document_text.size() == 1 && result.document_text[0].value == body &&
                !result.text_input &&
                documents::current_document(result)->text.ends_with(L"Final signature"),
            "Owned native main text lost its body, edition geometry or read-only eligibility");
    child.object = nullptr;
    require(capture().document_text.empty() && capture().text_input,
            "An owner removed from native child views supplied retained text");
    child.object = view.data() + 0x4c8;
    entries[0] = view.data();
    result = capture();
    require(result.document_text.empty() && result.text_input,
            "A hidden retained singleton supplied document text");
    put(graphic.data(), 0, image.data() + profile.text);
    put(graphic.data(), 0x28, body.c_str());
    put(control.data(), 0x140, graphic.data());
    require(capture().text_input, "Unowned graphic fallback bypassed the native input blocker");
    put(control.data(), 0x140, static_cast<std::byte*>(nullptr));
    entries[0] = group;
    put(view.data(), 0x4d4, 1u);
    require(capture().document_text.empty(), "Invalid owner array cursor supplied document text");
    put(view.data(), 0x4d4, 0u);
    put(view.data(), 0x4d0, 257u);
    require(capture().document_text.empty(), "Oversized owner array supplied document text");
    put(view.data(), 0x4d0, 1u);
    put(control.data(), 0x144, script_control::dialog_text);
    require(capture().text_input, "A native dialog field lost its input blocker");
    put(control.data(), 0x144, script_control::text_cursor);
    put(resource.data(), 4, resource::pda_notes);
    require(capture().document_text.empty() && capture().text_input,
            "Unproven editable notes offered Read");
    put(resource.data(), 4, resource::pda_message);
    texts.count = 2;
    put(group, 4, texts);
    require(capture().document_text.empty(), "A short native text list exposed a partial body");
    const std::string tail = "Second native text segment";
    put(second_text.data(), 0, image.data() + profile.text);
    put(second_text.data(), 0x28, tail.c_str());
    put(second_record.data(), 0, second_text.data());
    game::List<std::byte>::Node second_node{second_record.data(), nullptr, &text_node};
    text_node.next = &second_node;
    texts.last = &second_node;
    put(group, 4, texts);
    result = capture();
    require(result.document_text.size() == 2 && result.document_text[0].value == body &&
                result.document_text[1].value == tail,
            "Native text segments lost their list order or complete content");
    second_node.next = &text_node;
    require(capture().document_text.empty(), "A cyclic native list exposed a partial body");
    second_node.next = nullptr;
    put(second_text.data(), 0, image.data() + profile.picture);
    require(capture().document_text.empty(), "An unknown native graphic exposed a partial body");
    put(second_text.data(), 0, image.data() + profile.text);
    put(second_text.data(), 0x28, reinterpret_cast<const char*>(1));
    require(capture().document_text.empty(),
            "An inaccessible native body exposed earlier text segments");
}

void selection() {
    game::ScriptControls controls;
    controls.resources = {resource::pda_message};
    const std::string text = "First paragraph\r\n\r\n" + std::string(2000, 'X') + "\nFinal line";
    controls.document_text = {{{200, 90, 410, 340}, text}, {{210, 370, 415, 390}, "Back"}};
    const auto document = documents::current_document(controls);
    require(document && document->text.size() == text.size() &&
                document->text.ends_with(L"Final line"),
            "Document selection truncated the body or included navigation labels");
    controls.resources = {resource::pda_notes};
    require(!documents::current_document(controls), "Unproven notes offered a reader");
    controls.resources = {resource::pda_message};
    controls.script_dialog = true;
    require(!documents::current_document(controls), "A popup supplied stale underlying notes");
    controls.script_dialog = false;
    controls.resources = {resource::workstation_root};
    require(!documents::current_document(controls), "A stale workstation root offered Read");
    controls.resources = {resource::workstation_message};
    controls.document_text[0].bounds = {183, 100, 598, 440};
    require(!documents::current_document(controls), "A closed workstation message offered Read");
    controls.buttons = {{80, 330, 110, 360}};
    require(documents::current_document(controls).has_value(), "Workstation message was rejected");
    controls.resources = {resource::pda_inbox};
    require(!documents::current_document(controls), "Inbox selection was mistaken for a document");
    controls.resources = {resource::pda_message};
    controls.document_text.clear();
    require(!documents::current_document(controls), "An image-only message offered empty text");
    controls.document_text = {{{200, 90, 410, 340}, "\x82\xa0"}};
    require(documents::current_document(controls, 932)->text == L"\x3042",
            "Japanese text did not use the game's native code page");
    controls.document_text[0].value = "\xe9";
    require(documents::current_document(controls, 1252)->text == L"\xe9",
            "Western text depended on the host system code page");
}

void screenshot(HDC dc, const std::filesystem::path& path) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 640;
    info.bmiHeader.biHeight = -480;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<std::uint8_t> pixels(640 * 480 * 4);
    require(GetDIBits(dc, static_cast<HBITMAP>(GetCurrentObject(dc, OBJ_BITMAP)), 0, 480,
                      pixels.data(), &info, DIB_RGB_COLORS) != 0,
            "Cannot capture document panel");
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        for (const auto* profile : {&game::dvd, &game::cd, &native_game::profile_cd_10019,
                                    &native_game::profile_cd_10020}) {
            capture(*profile);
            native_body(*profile);
        }
        selection();
        if (argc > 1) {
            platform::register_private_font(std::filesystem::path(argv[1]) / L"HCD.TTR");
        }
        documents::Page page;
        documents::Document document{
            resource::pda_message, L"PDA message",
            L"October 3\r\n\r\nAgent Willmore\r\n\r\n"
            L"Mulder and Scully have been missing for several days. Their last known location "
            L"was Everett, Washington.\r\n\r\nReview the case notes and speak to the local "
            L"agents."};
        page.draw(nullptr, &document, 20, 0);
        require(page.maximum() == 0, "A short document needs unnecessary scrolling");
        if (argc > 2) {
            screenshot(page.dc(), argv[2]);
        }
        document.text = std::wstring(20000, L'W') + L"\nFinal line";
        page.draw(nullptr, &document, 20, 0);
        const auto normal = page.maximum();
        require(normal > 326, "A long body was truncated instead of scrollable");
        page.draw(nullptr, &document, 28, 1000000);
        require(page.maximum() > normal && page.scroll() == page.maximum(),
                "Larger type or the final scroll position lost the document tail");
        page.draw(nullptr, &document, 16, -1000);
        require(page.scroll() == 0 && page.maximum() < normal,
                "Smaller type did not recompute scroll bounds");
        page.draw(nullptr, nullptr, 20, 0);
        std::cout << "Readable document extraction and layout passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
