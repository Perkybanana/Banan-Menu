#include "missions.h"
#include "ue.h"
#include "lang.h"
#include "log.h"

#include <windows.h>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace missions {
namespace {
using namespace ue;
constexpr uint8_t kMain = 0, kDebugMenu = 1;
constexpr uint8_t kDebugAction = 1, kNewGame = 5, kMainMenuAction = 11;
struct Chapter { const char* path; const char* label; };
// Exact cooked campaign paths read from DA_MainMenuEntries_Debug in build 1.1.1.227384.
constexpr Chapter kChapters[] = {
    {"/Game/Maps/EP_0_Prologue/MAP_EP_0_Prologue", "Prologue"},
    {"/Game/Maps/EP_1/MAP_EP_1", "Chapter 1"},
    {"/Game/Maps/EP_2/MAP_EP_2", "Chapter 2"},
    {"/Game/Maps/EP_3/MAP_EP_3", "Chapter 3"},
    {"/Game/Maps/EP_4/MAP_EP_4", "Chapter 4"},
    {"/Game/Maps/EP_5/MAP_EP_5", "Chapter 5"},
    {"/Game/Maps/Epilogue/MAP_Epilogue", "Epilogue"},
};
const char* ChapterLabel(std::string_view path) {
    for (const auto& ch : kChapters) if (ch.path == path) return ch.label;
    return nullptr;
}

// UE 5.6 FText in this build is 16 bytes, confirmed from reflected SetText/Conv_StringToText.
using Text = std::array<uint8_t, 16>;
Weak mainMenu, textLibrary;
uint64_t nextTick = 0;
struct Patched { Weak button; std::string label; Text value; };
std::vector<Patched> patched;

UObject* Ptr(UObject* o, const char* key) {
    auto p = Member<UObject*>(o, key);
    UObject* result = nullptr;
    return p && SafeRead(p, &result, sizeof result) ? result : nullptr;
}

bool AssignText(Text* destination, const std::string& translated) {
    if (!textLibrary.Get()) textLibrary = Weak(FindObject("Default__KismetTextLibrary"));
    auto* lib = textLibrary.Get();
    auto* fn = FindFunction(ClassOf(lib), "Conv_StringToText");
    if (!fn || StructSize(reinterpret_cast<UClass*>(fn)) != 32) return false;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, translated.data(), int(translated.size()), nullptr, 0);
    if (length <= 0) return false;
    std::wstring wide(size_t(length) + 1, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, translated.data(), int(translated.size()), wide.data(), length);
    FString input{wide.data(), length + 1, length + 1};
    Call convert(lib, "Conv_StringToText");
    // Transfer the destination's owned reference into the return slot. The native conversion
    // move-assigns its result there, releasing the old reference through the engine. Move it back;
    // Call owns raw bytes only. This avoids both guessed reference-counter offsets and leaked FText.
    const Text previous = *destination;
    convert.Arg("InString", input).Arg("ReturnValue", previous);
    *destination = {};
    if (!convert.Run()) { *destination = previous; return false; }
    *destination = convert.Out<Text>("ReturnValue");
    return true;
}

bool LabelButton(UObject* button, uint8_t* data, const UClass* structure, const char* label) {
    std::string translated = lang::T(label);
    const auto name = FindProp(structure, "Name");
    if (!name || name.offset != 0) return false;
    auto* value = reinterpret_cast<Text*>(data + name.offset);
    for (const auto& item : patched)
        if (item.button.Get() == button && item.label == translated && item.value == *value) return true;
    auto* text = Ptr(button, "InfernoRichTextWidget_Entry");
    auto* fn = FindFunction(ClassOf(text), "SetText");
    if (!text || !fn || StructSize(reinterpret_cast<UClass*>(fn)) != sizeof(Text)) return false;
    if (!AssignText(value, translated) || !Call(text, "SetText").ArgRaw(0, value->data(), value->size()).Run()) return false;
    for (auto& item : patched) if (item.button.Get() == button) { item.label = translated; item.value = *value; return true; }
    patched.push_back({Weak(button), translated, *value});
    km::Log("Mission menu: labelled %s -> %s", Name(button).c_str(), translated.c_str());
    return true;
}

bool Visibility(UObject* widget, uint8_t desired) {
    Call current(widget, "GetVisibility");
    if (!current.Run()) return false;
    if (current.Out<uint8_t>("ReturnValue") == desired) return true;
    return Call(widget, "SetVisibility").Arg("InVisibility", desired).Run();
}

void Update() {
    auto inViewport = [](UObject* widget) {
        if (!widget) return false;
        Call visible(widget, "IsInViewport");
        return visible.Run() && visible.Out<bool>("ReturnValue");
    };
    // A removed menu can stay alive until GC. Only patch the instance actually in the viewport.
    if (mainMenu.Get() && !inViewport(mainMenu.Get())) mainMenu = {};
    if (!mainMenu.Get()) {
        ForEachInstance(FindClass("MainMenuWidget"), [&](UObject* widget) {
            if (!inViewport(widget)) return true;
            mainMenu = Weak(widget);
            km::Log("Mission menu: attached to %s", Name(widget).c_str());
            return false;
        });
    }
    auto* menu = mainMenu.Get();
    if (!menu) return;
    auto* current = Member<uint8_t>(menu, "CurrentMenuType");
    if (!current || (*current != kMain && *current != kDebugMenu)) return;
    auto* box = Ptr(menu, "VB_Buttons");
    if (!box) return;
    Call count(box, "GetChildrenCount");
    if (!count.Run()) return;
    int n = count.Out<int>("ReturnValue");
    if (n <= 0 || n > 64) return;
    for (int i = 0; i < n; ++i) {
        Call child(box, "GetChildAt");
        if (!child.Arg("Index", i).Run()) continue;
        auto* button = child.Out<UObject*>("ReturnValue");
        auto field = FindProp(ClassOf(button), "ButtonData");
        auto* structure = reinterpret_cast<const UClass*>(field.inner);
        if (!field || !structure || StructSize(structure) != 64 ||
            Name(reinterpret_cast<const UObject*>(structure)) != "MainMenuButtonData") continue;
        auto action = FindProp(structure, "Action"), hidden = FindProp(structure, "bHide"), path = FindProp(structure, "MapPath");
        if (!action || !hidden || !hidden.boolMask || !path) continue;
        auto* data = reinterpret_cast<uint8_t*>(button) + field.offset;
        uint8_t kind = data[action.offset];
        if (*current == kMain) {
            if (kind != kDebugAction) continue;
            // This is an existing, bound UMG button: retain native keyboard/controller focus, click handler,
            // chapter routing, difficulty selection and playthrough-slot confirmation.
            if (!LabelButton(button, data, structure, "Select mission (mod menu)")) continue;
            data[hidden.offset] &= ~hidden.boolMask;
            if (Visibility(button, 0)) Call(button, "SetIsEnabled").Arg("bInIsEnabled", true).Run();
        } else {
            auto* str = reinterpret_cast<FString*>(data + path.offset);
            const char* label = kind == kNewGame ? ChapterLabel(str->ToUtf8()) : nullptr;
            if (label) {
                if (LabelButton(button, data, structure, label)) Visibility(button, 0);
            } else if (kind != kMainMenuAction) {
                // Do not expose the uncooked developer/test maps that share the original Debug menu.
                Visibility(button, 1);
            }
        }
    }
    std::erase_if(patched, [](const Patched& item) { return !item.button.Get(); });
}
}

void Tick(uint64_t now) {
    if (!ue::Ready() || now < nextTick) return;
    nextTick = now + (mainMenu.Get() ? 250 : 1000);
    Update();
}
}
