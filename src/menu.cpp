#include "menu.h"
#include "achievements.h"
#include "config.h"
#include "esp.h"
#include "features.h"
#include "lang.h"
#include "log.h"
#include "overlay.h"
#include "text.h"
#include "theme.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace menu {
namespace {

using feat::Toggle;
using feat::Value;
using namespace theme;
using lang::T;

constexpr int kNotBinding = -1, kBindMenu = -2, kBindMenu2 = -3;
constexpr double kToastLife = 2.2;
constexpr float kBaseFont = 16.f;
const char* const kTabs[] = {"Player", "Combat", "Movement", "ESP", "Inventory", "World", "Progress", "Settings"};
constexpr int kTabCount = IM_ARRAYSIZE(kTabs);

HMODULE g_module;
std::atomic<bool> g_open;
std::atomic<unsigned> g_menuKey{VK_INSERT};
std::atomic<unsigned> g_menuKey2{VK_F8}; // second menu key: not every keyboard has Insert (0 = none)
std::atomic<int> g_hotkey[feat::ToggleCount];
std::atomic<int> g_binding{kNotBinding};
float g_scale = 0; // 0 = automatic, from the screen height
float g_appliedScale = -1;
int g_tab = 0;
std::string g_game, g_cpu, g_ram; // footer; the GPU comes from the overlay

struct Toast {
    std::string text;
    int state; // -1 plain message, 0 off, 1 on
    double start;
};
std::mutex g_toastMutex;
std::deque<Toast> g_toasts;

double Now() { return GetTickCount64() / 1000.0; }
float S() { return g_appliedScale; }

void PushToast(std::string text, int state)
{
    std::lock_guard lock(g_toastMutex);
    g_toasts.push_back({std::move(text), state, Now()});
    if (g_toasts.size() > 4) g_toasts.pop_front();
}

std::string KeyName(unsigned vk)
{
    if (!vk) return T("None");
    LONG scan = LONG(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16;
    switch (vk) { // navigation keys share scan codes with the numpad: mark them extended
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_RCONTROL: case VK_RMENU:
        scan |= 1 << 24;
    }
    wchar_t buf[64];
    int n = GetKeyNameTextW(scan, buf, 64);
    if (n <= 0) return "Key " + std::to_string(vk);
    return km::Utf8(std::wstring(buf, size_t(n)));
}

// Drops vendor boilerplate: "AMD Ryzen 7 9800X3D 8-Core Processor" -> "Ryzen 7 9800X3D".
std::string ShortHardware(std::string s)
{
    for (const char* noise : {"(R)", "(TM)", "(tm)", "AMD ", "Intel ", "NVIDIA ", " Processor", " CPU", " with Radeon Graphics"})
        for (size_t p; (p = s.find(noise)) != std::string::npos;) s.erase(p, strlen(noise));
    if (auto core = s.find("-Core"); core != std::string::npos) { // " 8-Core"
        size_t start = s.rfind(' ', core);
        s.erase(start == std::string::npos ? 0 : start, core + 5 - (start == std::string::npos ? 0 : start));
    }
    if (auto at = s.find(" @"); at != std::string::npos) s.resize(at);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s;
}

void ReadHardware()
{
    wchar_t cpu[128] = {};
    DWORD size = sizeof(cpu);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                 RRF_RT_REG_SZ, nullptr, cpu, &size);
    g_cpu = ShortHardware(km::Utf8(cpu));
    MEMORYSTATUSEX mem{sizeof(mem)};
    GlobalMemoryStatusEx(&mem);
    char ram[32];
    snprintf(ram, sizeof(ram), "%.0f GB", double(mem.ullTotalPhys) / (1ull << 30));
    g_ram = ram;
}

ImFont* LoadFont(int id)
{
    HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    HGLOBAL mem = res ? LoadResource(g_module, res) : nullptr;
    void* data = mem ? LockResource(mem) : nullptr;
    if (!data) return nullptr;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; // the data lives in the DLL image
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, int(SizeofResource(g_module, res)), kBaseFont, &cfg);
}

// Read-only view of a Windows font: only the pages ImGui actually reads get loaded.
void* MapFont(const wchar_t* file, int& size)
{
    wchar_t dir[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    std::wstring path = std::wstring(dir) + L"\\Fonts\\" + file;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER bytes{};
    GetFileSizeEx(f, &bytes);
    HANDLE map = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(f);
    void* view = map ? MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (map) CloseHandle(map);
    size = int(bytes.QuadPart);
    return view;
}

// Inter has no Chinese, Japanese or Korean glyphs: merge the Windows fonts for them into ours, so every language name in
// Settings and the current translation render. The current language goes first: it wins for the shared ideographs.
void MergeScriptFonts()
{
    int order[] = {lang::Get(), lang::Zh, lang::Ja, lang::Ko};
    std::vector<const wchar_t*> done;
    for (int l : order) {
        const wchar_t* const* files = lang::kLangs[l].fonts;
        if (!files[0] || std::find(done.begin(), done.end(), files[0]) != done.end()) continue;
        done.push_back(files[0]);
        for (int b = 0; b < 2; b++) {
            ImFont* dst = b ? bold : regular;
            int size = 0;
            void* data = MapFont(files[b] ? files[b] : files[0], size);
            if (!data && b) data = MapFont(files[0], size);
            if (!data || !dst) continue;
            ImFontConfig cfg;
            cfg.MergeMode = true;
            cfg.DstFont = dst;
            cfg.FontDataOwnedByAtlas = false; // the mapped view stays for the process lifetime
            ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, size, kBaseFont, &cfg);
        }
        km::Log("Fonts: merged %ls", files[0]);
    }
}

void ApplyTheme(float scale)
{
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.FontSizeBase = kBaseFont;
    st.FontScaleMain = scale;
    st.WindowPadding = {20, 18};
    st.FramePadding = {10, 6};
    st.ItemSpacing = {10, 8};
    st.ItemInnerSpacing = {8, 6};
    st.CellPadding = {6, 4};
    st.WindowRounding = 14;
    st.ChildRounding = 10;
    st.FrameRounding = 7;
    st.PopupRounding = 10;
    st.GrabRounding = 6;
    st.ScrollbarRounding = 8;
    st.WindowBorderSize = 1;
    st.ChildBorderSize = 0;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 0;
    st.ScrollbarSize = 8;
    st.GrabMinSize = 12;
    st.DisabledAlpha = 0.4f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = Text;
    c[ImGuiCol_TextDisabled] = Muted;
    c[ImGuiCol_WindowBg] = Bg;
    c[ImGuiCol_ChildBg] = Panel;
    c[ImGuiCol_PopupBg] = Panel2;
    c[ImGuiCol_Border] = Border;
    c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    c[ImGuiCol_FrameBg] = Panel2;
    c[ImGuiCol_FrameBgHovered] = Mix(Panel2, Border, 0.6f);
    c[ImGuiCol_FrameBgActive] = Border;
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = Bg;
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = Border;
    c[ImGuiCol_ScrollbarGrabHovered] = Mix(Border, Muted, 0.4f);
    c[ImGuiCol_ScrollbarGrabActive] = Violet;
    c[ImGuiCol_CheckMark] = Violet;
    c[ImGuiCol_SliderGrab] = Violet;
    c[ImGuiCol_SliderGrabActive] = Pink;
    c[ImGuiCol_Button] = Panel2;
    c[ImGuiCol_ButtonHovered] = Mix(Panel2, Border, 0.7f);
    c[ImGuiCol_ButtonActive] = Alpha(Violet, 0.55f);
    c[ImGuiCol_Header] = Alpha(Violet, 0.14f);
    c[ImGuiCol_HeaderHovered] = Alpha(Violet, 0.22f);
    c[ImGuiCol_HeaderActive] = Alpha(Violet, 0.30f);
    c[ImGuiCol_Separator] = Border;
    c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
    c[ImGuiCol_PlotHistogram] = Violet;
    c[ImGuiCol_TableRowBg] = c[ImGuiCol_TableRowBgAlt] = {0, 0, 0, 0};
    c[ImGuiCol_TextSelectedBg] = Alpha(Violet, 0.35f);
    c[ImGuiCol_NavCursor] = Violet;
    c[ImGuiCol_ModalWindowDimBg] = Alpha(Black, 0.55f);
    st.ScaleAllSizes(scale);
}

// ---------------------------------------------------------------- widgets

float ButtonWidth(const char* label) { return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2; }

// Next item on the same line, flush right.
void SameLineRight(float width)
{
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - width));
}

// Subtle highlight behind the row under the mouse. Call before drawing the row's widgets.
void RowHover(ImVec2 rowMin, float width)
{
    ImVec2 rowMax{rowMin.x + width, rowMin.y + ImGui::GetFrameHeight()};
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(rowMin, rowMax))
        ImGui::GetWindowDrawList()->AddRectFilled({rowMin.x - 8 * S(), rowMin.y}, {rowMax.x + 4 * S(), rowMax.y},
                                                  Raw(Alpha(Text, 0.035f)), 7 * S());
}

// Pill switch, vertically centred on a frame-height row. Fades with BeginDisabled like any widget.
bool Switch(const char* id, bool* v)
{
    float frameH = ImGui::GetFrameHeight(), h = frameH * 0.70f, w = h * 1.85f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, {w, frameH});
    if (clicked) *v = !*v;
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID sid = ImGui::GetID(id);
    float target = *v ? 1.f : 0.f, t = store->GetFloat(sid, target);
    t += (target - t) * std::min(1.f, ImGui::GetIO().DeltaTime * 16.f);
    store->SetFloat(sid, t);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a{p.x, p.y + (frameH - h) * 0.5f}, b{a.x + w, a.y + h};
    ImVec4 off = ImGui::IsItemHovered() ? Mix(Panel2, Border, 0.8f) : Panel2;
    dl->AddRectFilled(a, b, U32(Mix(off, Violet, t)), h * 0.5f);
    if (t < 0.99f) dl->AddRect(a, b, U32(Alpha(Border, 1 - t)), h * 0.5f);
    float r = h * 0.5f - 3.f * S();
    dl->AddCircleFilled({a.x + h * 0.5f + t * (w - h), a.y + h * 0.5f}, r, U32(Mix(Muted, Text, t)), 20);
    return clicked;
}

void Tooltip(ImVec2 rowMin, ImVec2 rowMax, const std::string& text)
{
    if (!text.empty() && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseHoveringRect(rowMin, rowMax)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22);
        ImGui::TextUnformatted(T(text.c_str()));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Section(const char* title)
{
    float s = S();
    ImGui::Dummy({0, 6 * s});
    ImGui::PushFont(bold, kBaseFont * 0.78f);
    ImGui::PushStyleColor(ImGuiCol_Text, Muted);
    ImGui::TextUnformatted(T(title));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y - 2 * s}, {p.x + ImGui::GetContentRegionAvail().x, p.y - 2 * s}, U32(Border));
    ImGui::Dummy({0, 3 * s});
}

// Small pill showing a key (hotkey hint).
void KeyBadge(const char* text)
{
    float s = S(), size = kBaseFont * 0.78f * s, frameH = ImGui::GetFrameHeight();
    ImVec2 ts = regular->CalcTextSizeA(size, FLT_MAX, 0, text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ts.y + 4 * s, w = ts.x + 12 * s, y = p.y + (frameH - h) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({p.x, y}, {p.x + w, y + h}, U32(Panel2), 5 * s);
    dl->AddRect({p.x, y}, {p.x + w, y + h}, U32(Border), 5 * s);
    dl->AddText(regular, size, {p.x + 6 * s, y + 2 * s}, U32(Muted), text);
    ImGui::Dummy({w, frameH});
}

void ToggleRow(Toggle t, const feat::Snapshot& snap)
{
    const feat::ToggleInfo& info = feat::kToggles[t];
    const std::string& why = snap.why[t];
    ImGui::PushID(t);
    ImVec2 rowMin = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
    RowHover(rowMin, avail);
    ImGui::BeginDisabled(!why.empty());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T(info.label));
    if (int key = g_hotkey[t]) {
        ImGui::SameLine();
        KeyBadge(KeyName(unsigned(key)).c_str());
    }
    float sw = ImGui::GetFrameHeight() * 0.70f * 1.85f;
    ImGui::SameLine(x0 + avail - sw);
    bool v = feat::g_on[t];
    if (Switch("##sw", &v)) {
        feat::g_on[t] = v;
        feat::Save();
    }
    ImGui::EndDisabled();
    Tooltip(rowMin, {rowMin.x + avail, rowMin.y + ImGui::GetFrameHeight()}, why.empty() ? info.help : why);
    ImGui::PopID();
}

// Label, slider (value printed inside it), Reset.
void SliderRow(Value v, const char* label, const char* fmt, const feat::Snapshot& snap, const char* help,
               const char* resetLabel = "Reset")
{
    const feat::ValueInfo& info = feat::kValues[v];
    const std::string& why = snap.valueWhy[v];
    ImGui::PushID(100 + v);
    ImVec2 rowMin = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
    RowHover(rowMin, avail);
    ImGui::BeginDisabled(!why.empty());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T(label));
    ImGui::SameLine(x0 + avail * 0.40f);
    resetLabel = T(resetLabel);
    ImGui::SetNextItemWidth(avail * 0.60f - ButtonWidth(resetLabel) - ImGui::GetStyle().ItemSpacing.x);
    float lo = v == feat::Fov ? 50.f : info.min; // FOV: 0 = game default, below any usable value
    float val = feat::g_value[v];
    const char* shown = v == feat::Fov && val < lo ? T("Game default") : fmt;
    if (ImGui::SliderFloat("##v", &val, lo, info.max, shown, ImGuiSliderFlags_AlwaysClamp)) feat::g_value[v] = val;
    if (ImGui::IsItemDeactivatedAfterEdit()) feat::Save();
    ImGui::SameLine();
    if (ImGui::Button(resetLabel)) {
        feat::g_value[v] = info.def;
        feat::Save();
    }
    ImGui::EndDisabled();
    Tooltip(rowMin, {rowMin.x + avail * 0.40f, rowMin.y + ImGui::GetFrameHeight()}, why.empty() ? help : why);
    ImGui::PopID();
}

// Muted help text that wraps instead of running off the panel.
void Hint(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, Muted);
    ImGui::TextWrapped("%s", T(text));
    ImGui::PopStyleColor();
}

// Label, then any control laid out from the 40% column.
float ControlColumn(const char* label, float avail, float x0)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T(label));
    ImGui::SameLine(x0 + avail * 0.40f);
    return avail * 0.60f;
}

// ---------------------------------------------------------------- tabs

void TabPlayer(const feat::Snapshot& s)
{
    Section("STATUS");
    { // health: drag to set, applied on release
        static float editing = -1;
        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        float avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
        RowHover(rowMin, avail);
        ImGui::BeginDisabled(!s.player || s.maxHealth <= 0);
        float w = ControlColumn("Health", avail, x0);
        ImGui::SetNextItemWidth(w - ButtonWidth(T("Heal")) - ImGui::GetStyle().ItemSpacing.x);
        float hp = editing >= 0 ? editing : s.health;
        char fmt[32];
        snprintf(fmt, sizeof(fmt), s.maxHealth > 0 ? "%%.0f / %.0f" : "-", s.maxHealth);
        if (ImGui::SliderFloat("##hp", &hp, 1, std::max(s.maxHealth, 1.f), fmt, ImGuiSliderFlags_AlwaysClamp)) editing = hp;
        if (ImGui::IsItemDeactivated() && editing >= 0) {
            feat::SetHealth(editing);
            editing = -1;
        }
        ImGui::SetItemTooltip("%s", T("Drag to set your health."));
        ImGui::SameLine();
        if (ImGui::Button(T("Heal"))) feat::Heal();
        ImGui::EndDisabled();
    }
    Section("SURVIVAL");
    for (Toggle t : {feat::GodMode, feat::InfiniteHealth, feat::NoSprintCooldown, feat::Invisible}) ToggleRow(t, s);
    Section("DAMAGE");
    ToggleRow(feat::OneHitKill, s);
    SliderRow(feat::DamageMult, "Damage multiplier", "x%.1f", s, "Multiplies the damage you deal to enemies.");
}

void TabCombat(const feat::Snapshot& s)
{
    Section("AIM");
    ToggleRow(feat::SilentAim, s);
    SliderRow(feat::AimFov, "Aim FOV", "%.0f°", s, "Size of the circle around the crosshair where enemies are targeted.");
    Section("WEAPONS");
    for (Toggle t : {feat::InfiniteAmmo, feat::Unbreakable}) ToggleRow(t, s);
    Section("GENESIS CONFIGURATION");
    {
        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        float avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
        RowHover(rowMin, avail);
        ImGui::BeginDisabled(!s.player || s.maxSuffering <= 0);
        float w = ControlColumn("Suffering", avail, x0);
        char text[32];
        snprintf(text, sizeof(text), s.maxSuffering > 0 ? "%.0f / %.0f" : "-", s.suffering, s.maxSuffering);
        ImGui::ProgressBar(s.maxSuffering > 0 ? s.suffering / s.maxSuffering : 0.f,
                           {w - ButtonWidth(T("Fill")) - ImGui::GetStyle().ItemSpacing.x, 0}, text);
        ImGui::SameLine();
        if (ImGui::Button(T("Fill"))) feat::FillSuffering();
        ImGui::EndDisabled();
    }
    for (Toggle t : {feat::NoSufferingCost, feat::NoPowerCooldown}) ToggleRow(t, s);
    for (int p = 0; p < feat::PowerCount; p++) {
        ImGui::PushID(p);
        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        float avail = ImGui::GetContentRegionAvail().x;
        RowHover(rowMin, avail);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(T(feat::kPowerNames[p]));
        if (s.powers[p]) {
            SameLineRight(ImGui::CalcTextSize(T("Unlocked")).x);
            ImGui::TextDisabled("%s", T("Unlocked"));
        } else {
            SameLineRight(ButtonWidth(T("Unlock")));
            ImGui::BeginDisabled(!s.powersWhy.empty());
            if (ImGui::Button(T("Unlock"))) feat::UnlockPower(feat::Power(p));
            ImGui::EndDisabled();
        }
        Tooltip(rowMin, {rowMin.x + avail, rowMin.y + ImGui::GetFrameHeight()},
                s.powers[p]                ? ""
                : !s.powersWhy.empty()     ? s.powersWhy
                                           : "Gives the power now, even before the story does. Save first: it can skip its tutorial.");
        ImGui::PopID();
    }
}

void TabMovement(const feat::Snapshot& s)
{
    Section("MOVEMENT");
    for (Toggle t : {feat::Fly, feat::Noclip, feat::FreeCam}) ToggleRow(t, s);
    SliderRow(feat::SpeedMult, "Speed", "x%.2f", s, "Movement speed multiplier (also flying and free camera speed).");
    SliderRow(feat::JumpMult, "Jump height", "x%.1f", s, "Jump velocity multiplier.");

    Section("SAVED POSITIONS");
    for (int i = 0; i < 3; i++) {
        ImGui::PushID(i);
        RowHover(ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(T("Slot %d"), i + 1);
        ImGui::SameLine(0, 12 * S());
        ImGui::TextDisabled("%s", s.slots[i].empty() ? T("empty") : s.slots[i].c_str());
        SameLineRight(ButtonWidth(T("Save")) + ImGui::GetStyle().ItemSpacing.x + ButtonWidth(T("Go")));
        ImGui::BeginDisabled(!s.player);
        if (ImGui::Button(T("Save"))) feat::SaveSlot(i);
        ImGui::SameLine();
        ImGui::BeginDisabled(s.slots[i] != s.map);
        if (ImGui::Button(T("Go"))) feat::LoadSlot(i);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    Section("LEVEL CHECKPOINTS");
    if (s.checkpoints.empty()) ImGui::TextDisabled("%s", T(s.player ? "No checkpoint in this level." : "Load a save first."));
    for (const feat::Named& cp : s.checkpoints) {
        ImGui::PushID(int(cp.fname));
        RowHover(ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(cp.name.c_str());
        SameLineRight(ButtonWidth(T("Go")));
        if (ImGui::Button(T("Go"))) feat::TeleportCheckpoint(cp.fname);
        ImGui::PopID();
    }
}

void TabEsp(const feat::Snapshot& s)
{
    Section("ESP");
    ToggleRow(feat::Esp, s);
    ToggleRow(feat::EspAimFov, s); // independent of the ESP switch
    ImGui::BeginDisabled(!feat::g_on[feat::Esp]);
    Section("ENEMIES");
    for (Toggle t : {feat::EspBox, feat::EspHealth, feat::EspName, feat::EspDistance, feat::EspSkeleton, feat::EspHead,
                     feat::EspChams, feat::EspSnaplines, feat::EspOffscreen})
        ToggleRow(t, s);
    SliderRow(feat::EspRange, "Enemy range", "%.0f m", s, "Enemies farther than this are not shown.");
    Section("WORLD");
    for (Toggle t : {feat::EspItems, feat::EspContainers, feat::EspSavePoints}) ToggleRow(t, s);
    SliderRow(feat::EspWorldRange, "Object range", "%.0f m", s, "Objects farther than this are not shown.");
    ImGui::EndDisabled();
}

void TabInventory(const feat::Snapshot& s)
{
    static int amount = 1000, giveCount = 1;
    static char filter[64];

    Section("MONEY");
    {
        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        float avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
        RowHover(rowMin, avail);
        ImGui::BeginDisabled(!s.player);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(T("Money"));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, Pink);
        ImGui::PushFont(bold, 0);
        ImGui::Text("%s", s.money >= 0 ? std::to_string(s.money).c_str() : "-");
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::SameLine(x0 + avail * 0.40f);
        float w = avail * 0.60f - ButtonWidth(" + ") - ButtonWidth(" - ") - ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SetNextItemWidth(w);
        ImGui::InputInt("##amount", &amount, 0, 0); // the + / - buttons next to it add or remove this amount
        amount = std::clamp(amount, 1, 1000000);
        ImGui::SameLine();
        if (ImGui::Button(" + ")) feat::AddMoney(amount);
        ImGui::SameLine();
        if (ImGui::Button(" - ")) feat::AddMoney(-amount);
        ImGui::EndDisabled();
    }

    Section("CRAFTING MATERIALS");
    Hint("Carried inventory only. Enter a total from 0 to 9999.");
    static std::array<std::string, resources::Count> resourceTargets{};
    static std::array<bool, resources::Count> resourceInitialized{};
    if (!s.player) resourceInitialized.fill(false);
    for (int i = 0; i < resources::Count; ++i) {
        ImGui::PushID(i);
        const int observed = s.resourceTotals[i];
        if (!resourceInitialized[i] && observed >= 0) {
            resourceTargets[i] = std::to_string(observed);
            resourceInitialized[i] = true;
        }
        const float avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
        RowHover(ImGui::GetCursorScreenPos(), avail);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(T(resources::Labels[i]));
        ImGui::SameLine(x0 + avail * 0.30f);
        if (observed >= 0) ImGui::Text(T("Current: %d"), observed);
        else ImGui::TextDisabled("%s", T("Unavailable"));
        ImGui::SameLine(x0 + avail * 0.57f);
        ImGui::BeginDisabled(!s.player || !s.reflection || observed < 0);
        ImGui::SetNextItemWidth(avail * 0.43f - ButtonWidth(T("Set")) - ImGui::GetStyle().ItemSpacing.x);
        // Resizable input preserves the whole pasted number so overflow is rejected rather than truncated.
        auto resizeTarget = [](ImGuiInputTextCallbackData* data) {
            auto& text = *static_cast<std::string*>(data->UserData);
            text.resize(size_t(data->BufTextLen));
            data->Buf = text.data();
            return 0;
        };
        ImGui::InputText("##resource-target", resourceTargets[i].data(), resourceTargets[i].capacity() + 1,
                         ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CallbackResize,
                         resizeTarget, &resourceTargets[i]);
        int target = 0;
        const bool valid = resources::ParseTarget(resourceTargets[i], target);
        ImGui::SameLine();
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button(T("Set"))) feat::SetResourceTotal(i, target);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!valid && observed >= 0) ImGui::TextDisabled("%s", T("Enter a whole number from 0 to 9999."));
        ImGui::PopID();
    }

    Section("GIVE ITEMS");
    Hint("Key items can break scripted sequences: save before giving them.");
    float qtyW = ImGui::GetFontSize() * 4;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - qtyW - ImGui::CalcTextSize(T("Quantity")).x - ImGui::GetStyle().ItemSpacing.x * 2);
    ImGui::InputTextWithHint("##filter", T("Search an item..."), filter, sizeof(filter));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", T("Quantity"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(qtyW);
    ImGui::InputInt("##qty", &giveCount, 0, 0);
    giveCount = std::clamp(giveCount, 1, 9999);
    if (s.items.empty()) ImGui::TextDisabled("%s", T(s.player ? "Loading item list..." : "Load a save first."));
    auto lower = [](std::string t) {
        for (char& c : t) c = char(std::tolower(uint8_t(c)));
        return t;
    };
    std::string needle = lower(filter);
    for (const auto& [category, list] : s.items) {
        if (!ImGui::CollapsingHeader((std::string(T(category.c_str())) + "###" + category).c_str())) continue;
        for (const feat::Named& item : list) {
            std::string label = Pretty(item.name);
            if (!needle.empty() && lower(label).find(needle) == std::string::npos) continue;
            ImGui::PushID(item.name.c_str());
            RowHover(ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label.c_str());
            SameLineRight(ButtonWidth(T("Give")));
            if (ImGui::Button(T("Give"))) feat::GiveItem(item.name, giveCount);
            ImGui::PopID();
        }
    }

    Section("COSMETICS");
    ImGui::BeginDisabled(!s.player || !s.cheatManager);
    if (ImGui::Button(T("Unlock weapon skins & charms"))) feat::GiveAllSkins();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("Check them in the weapon customisation screen."));
}

void TabWorld(const feat::Snapshot& s)
{
    Section("TIME");
    SliderRow(feat::TimeScale, "Time scale", "x%.2f", s, "Slow motion below 1, fast forward above.");
    Section("ENEMIES");
    ToggleRow(feat::FreezeAI, s);
    SliderRow(feat::KillRadius, "Kill radius", "%.0f m", s, "Range of the button below.");
    ImGui::BeginDisabled(!s.player);
    if (ImGui::Button(T("Kill enemies nearby"))) feat::KillNearby();
    ImGui::EndDisabled();
    Section("CAMERA & HUD");
    SliderRow(feat::Fov, "Field of view", "%.0f°", s, "Locks the camera field of view. Default gives control back to the game.",
              "Default");
    ToggleRow(feat::HideHUD, s);
    Section("LOCKS");
    ToggleRow(feat::AutoLockpick, s);
}

void TabProgress(const feat::Snapshot& s)
{
    Section("STEAM ACHIEVEMENTS");
    const auto achievements = ach::GetSnapshot();
    const bool busy = achievements.phase == ach::Phase::Loading || achievements.phase == ach::Phase::Saving;
    ImGui::TextWrapped("%s", T(achievements.message));
    if (achievements.total)
        ImGui::Text(T("Unlocked: %u / %u"), achievements.unlocked, achievements.total);
    if (achievements.accepted || achievements.rejected)
        ImGui::Text(T("Confirmed: %u | Sent: %u | Rejected: %u"),
                    achievements.confirmed, achievements.accepted, achievements.rejected);
    ImGui::BeginDisabled(!s.reflection || busy || achievements.accepted || achievements.rejected);
    if (ImGui::Button(T("Check achievements"))) ach::RequestRefresh();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.reflection || !achievements.canUnlock || busy);
    const std::string achievementPopup = std::string(T("Unlock achievements")) + "###ach-confirm";
    if (ImGui::Button(T("Unlock all achievements"))) ImGui::OpenPopup(achievementPopup.c_str());
    ImGui::EndDisabled();
    ImGui::SetNextWindowSize({480 * S(), 0}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal(achievementPopup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", T("This unlocks all remaining achievements for this game on your Steam account. Restoring a game save will not undo it."));
        ImGui::Spacing();
        if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(!s.reflection || !achievements.canUnlock);
        if (ImGui::Button(T("Confirm unlock"))) {
            ach::RequestUnlock();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    { // warning card
        float sc = S(), w = ImGui::GetContentRegionAvail().x;
        const char* warn = T("Save your game before changing objectives: skipping steps can break scripted sequences.");
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::CalcTextSize(warn, nullptr, false, w - 30 * sc);
        ImVec2 b{p.x + w, p.y + size.y + 20 * sc};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, b, U32(Alpha(Violet, 0.12f)), 8 * sc);
        dl->AddRectFilled(p, {p.x + 3 * sc, b.y}, U32(Violet), 8 * sc, ImDrawFlags_RoundCornersLeft);
        ImGui::SetCursorScreenPos({p.x + 16 * sc, p.y + 10 * sc});
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 30 * sc);
        ImGui::TextUnformatted(warn);
        ImGui::PopTextWrapPos();
        ImGui::SetCursorScreenPos({p.x, b.y + 6 * sc});
        ImGui::Dummy({0, 0});
    }

    Section("CURRENT OBJECTIVES");
    if (s.objectivesActive.empty()) ImGui::TextDisabled("%s", T(s.player ? "No active objective found." : "Load a save first."));
    for (const feat::Named& o : s.objectivesActive) {
        ImGui::PushID(o.name.c_str());
        RowHover(ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x);
        bool tracked = o.name == s.tracked;
        ImGui::AlignTextToFramePadding();
        if (tracked) ImGui::PushStyleColor(ImGuiCol_Text, Pink);
        ImGui::TextUnformatted(Pretty(o.name).c_str());
        if (tracked) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s%s%s", o.name.c_str(), tracked ? "  " : "", tracked ? T("(tracked)") : "");
        SameLineRight(ButtonWidth(T("Complete")));
        if (ImGui::Button(T("Complete"))) feat::CompleteObjective(o.fname);
        ImGui::PopID();
    }
    if (!s.objectivesDone.empty() && ImGui::CollapsingHeader((std::string(T("Completed objectives")) + "###done").c_str())) {
        for (const feat::Named& o : s.objectivesDone) {
            ImGui::TextDisabled("%s", Pretty(o.name).c_str());
            ImGui::SetItemTooltip("%s", o.name.c_str());
        }
    }

    Section("SHOP");
    static int level = 0;
    ImGui::BeginDisabled(s.shopLevel < 0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T("Shop level"));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, Pink);
    ImGui::Text("%s", s.shopLevel >= 0 ? std::to_string(s.shopLevel).c_str() : "-");
    ImGui::PopStyleColor();
    SameLineRight(ImGui::GetFontSize() * 7 + ImGui::GetStyle().ItemSpacing.x + ButtonWidth(T("Set")));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    ImGui::InputInt("##level", &level);
    level = std::clamp(level, 0, 99);
    ImGui::SameLine();
    if (ImGui::Button(T("Set"))) feat::SetShopLevel(level);
    ImGui::EndDisabled();
}

void BindButton(const char* id, int target, unsigned key)
{
    ImGui::PushID(id);
    bool waiting = g_binding == target;
    std::string label = waiting ? T("Press a key...") : KeyName(key);
    if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, Alpha(Violet, 0.4f));
    if (ImGui::Button(label.c_str(), {ImGui::GetFontSize() * 8, 0})) g_binding = waiting ? kNotBinding : target;
    if (waiting) ImGui::PopStyleColor();
    ImGui::PopID();
}

void TabSettings(const feat::Snapshot& s)
{
    Section("MENU");
    float avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
    ControlColumn("Menu keys", avail, x0);
    BindButton("menukey", kBindMenu, g_menuKey);
    ImGui::SameLine();
    BindButton("menukey2", kBindMenu2, g_menuKey2);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("Esc cancels"));

    ControlColumn("Language", avail, x0);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::BeginCombo("##lang", lang::kLangs[lang::Get()].name)) {
        for (int i = 0; i < lang::Count; i++)
            if (ImGui::Selectable(lang::kLangs[i].name, i == lang::Get())) {
                lang::Set(i);
                cfg::SetString("Menu", "Language", lang::kLangs[i].code);
            }
        ImGui::EndCombo();
    }

    // Buttons rather than a slider: dragging a slider that rescales the window under the mouse runs away.
    ControlColumn("UI scale", avail, x0);
    float current = g_scale > 0 ? g_scale : g_appliedScale;
    auto setScale = [](float v) {
        g_scale = v > 0 ? std::clamp(std::round(v * 10) / 10, 0.7f, 2.5f) : 0;
        cfg::SetFloat("Menu", "Scale", g_scale);
    };
    if (ImGui::Button(" - ")) setScale(current - 0.1f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f%%", current * 100);
    ImGui::SameLine();
    if (ImGui::Button(" + ")) setScale(current + 0.1f);
    ImGui::SameLine();
    ImGui::BeginDisabled(g_scale <= 0);
    if (ImGui::Button(T("Auto"))) setScale(0);
    ImGui::EndDisabled();

    Section("GAME");
    ToggleRow(feat::SkipIntros, s);

    Section("HOTKEYS");
    Hint("They work with the menu closed. Click a key, then press the new one. Backspace clears, Esc cancels.");
    if (ImGui::BeginTable("hotkeys", 2, ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed);
        for (int t = 0; t < feat::ToggleCount; t++) {
            if (t == feat::SkipIntros) continue; // only matters at startup
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s%s%s", t > feat::Esp ? T("ESP") : "", t > feat::Esp ? "  ·  " : "", T(feat::kToggles[t].label));
            ImGui::TableNextColumn();
            BindButton(feat::kToggles[t].key, t, unsigned(g_hotkey[t].load()));
        }
        ImGui::EndTable();
    }

    Section("CONFIGURATION");
    if (ImGui::Button(T("Reset configuration"))) ImGui::OpenPopup("###reset");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("BananMenu.ini and BananMenu.log sit next to the DLL."));
    if (ImGui::BeginPopupModal((std::string(T("Reset?")) + "###reset").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(T("Turn every option off and forget hotkeys, positions and settings?"));
        if (ImGui::Button(T("Reset"))) {
            cfg::Reset();
            cfg::SetString("Menu", "Language", lang::kLangs[lang::Get()].code); // keep the language the menu shows
            for (int t = 0; t < feat::ToggleCount; t++) feat::g_on[t] = feat::kToggles[t].def;
            for (int v = 0; v < feat::ValueCount; v++) feat::g_value[v] = feat::kValues[v].def;
            for (auto& k : g_hotkey) k = 0;
            g_menuKey = VK_INSERT;
            g_menuKey2 = VK_F8;
            g_scale = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(T("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- frame

void DrawToasts()
{
    std::lock_guard lock(g_toastMutex);
    double now = Now();
    while (!g_toasts.empty() && now - g_toasts.front().start > kToastLife) g_toasts.pop_front();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float s = S(), size = kBaseFont * s, pad = 12 * s, y = 24 * s, right = ImGui::GetIO().DisplaySize.x - 24 * s;
    for (const Toast& t : g_toasts) {
        double age = now - t.start;
        float alpha = float(std::clamp(std::min(age / 0.15, (kToastLife - age) / 0.35), 0.0, 1.0));
        const char* state = t.state < 0 ? "" : T(t.state ? "ON" : "OFF");
        ImVec2 ts = regular->CalcTextSizeA(size, FLT_MAX, 0, t.text.c_str()), ss = bold->CalcTextSizeA(size, FLT_MAX, 0, state);
        float w = pad * 2 + 4 * s + ts.x + (t.state < 0 ? 0 : ss.x + 12 * s), h = ts.y + pad * 1.4f;
        ImVec2 a{right - w, y}, b{right, y + h};
        dl->AddRectFilled(a, b, Raw(Alpha(Panel, 0.97f * alpha)), 8 * s);
        dl->AddRect(a, b, Raw(Alpha(Border, alpha)), 8 * s);
        dl->AddRectFilled(a, {a.x + 4 * s, b.y}, Raw(Alpha(Violet, alpha)), 8 * s, ImDrawFlags_RoundCornersLeft);
        ImVec2 tp{a.x + pad + 4 * s, a.y + (h - ts.y) * 0.5f};
        dl->AddText(regular, size, tp, Raw(Alpha(Text, alpha)), t.text.c_str());
        if (t.state >= 0)
            dl->AddText(bold, size, {tp.x + ts.x + 12 * s, tp.y}, Raw(Alpha(t.state ? Pink : Muted, alpha)), state);
        y = b.y + 8 * s;
    }
}

void Header(const feat::Snapshot& snap)
{
    float s = S();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(bold, kBaseFont * 1.45f);
    ImGui::TextUnformatted("Banan Menu");
    ImGui::PopFont();
    ImGui::TextDisabled("v%s  ·  %s", KM_VERSION, overlay::ApiName());

    // Status pill on the right of the title.
    const char* status = T(!snap.reflection ? "Engine not hooked" : snap.player ? "Player found" : "Load a save");
    float size = kBaseFont * 0.85f * s;
    ImVec2 ts = regular->CalcTextSizeA(size, FLT_MAX, 0, status);
    float h = ts.y + 12 * s, w = ts.x + 30 * s;
    ImVec2 a{p.x + avail - w, p.y + 4 * s}, b{a.x + w, a.y + h};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, Raw(Panel), h * 0.5f);
    dl->AddRect(a, b, Raw(Border), h * 0.5f);
    ImVec4 dot = !snap.reflection ? Muted : snap.player ? Violet : Pink;
    dl->AddCircleFilled({a.x + 13 * s, a.y + h * 0.5f}, 3.5f * s, Raw(dot), 12);
    dl->AddText(regular, size, {a.x + 22 * s, a.y + 6 * s}, Raw(Text), status);
    ImGui::Dummy({0, 4 * s});
}

void Sidebar()
{
    float s = S();
    ImGui::BeginChild("##nav", {168 * s, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 3 * s});
    for (int i = 0; i < kTabCount; i++) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() * 1.2f;
        if (ImGui::InvisibleButton(kTabs[i], {w, h}) && g_tab != i) {
            g_tab = i;
            cfg::SetInt("Menu", "Tab", i);
        }
        bool active = g_tab == i, hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (active || hovered)
            dl->AddRectFilled(p, {p.x + w, p.y + h}, Raw(active ? Alpha(Violet, 0.16f) : Panel2), 7 * s);
        if (active) dl->AddRectFilled({p.x, p.y + h * 0.25f}, {p.x + 3 * s, p.y + h * 0.75f}, Raw(Violet), 2 * s);
        const char* name = T(kTabs[i]);
        ImVec2 ts = ImGui::CalcTextSize(name);
        dl->AddText(active ? bold : regular, kBaseFont * s, {p.x + 14 * s, p.y + (h - ts.y) * 0.5f}, Raw(active ? Text : Muted), name);
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

// Spec line: GAME 1.1.1.227384   CPU Ryzen 7 9800X3D   GPU GeForce RTX 5090 32 GB   RAM 64 GB   DISPLAY 2560 × 1440   FPS 144
void Footer()
{
    float s = S(), size = kBaseFont * 0.78f * s, labelSize = kBaseFont * 0.66f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    ImGuiIO& io = ImGui::GetIO();

    std::string gpu = overlay::GpuName();
    for (size_t at; (at = gpu.find("  ·  ")) != std::string::npos;) gpu.replace(at, 5, " ");
    gpu = ShortHardware(gpu);
    char display[32], fps[16];
    snprintf(display, sizeof(display), "%.0f × %.0f", io.DisplaySize.x, io.DisplaySize.y);
    snprintf(fps, sizeof(fps), "%.0f", io.Framerate);
    const std::pair<const char*, std::string> items[] = {
        {"GAME", g_game}, {"CPU", g_cpu}, {"GPU", gpu}, {"RAM", g_ram}, {"DISPLAY", display}, {"FPS", fps}};

    std::string keys = KeyName(g_menuKey) + (g_menuKey2 ? " / " + KeyName(g_menuKey2) : "");
    char hintBuf[128];
    snprintf(hintBuf, sizeof(hintBuf), T("%s to close"), keys.c_str());
    std::string hint = hintBuf;
    ImVec2 hs = regular->CalcTextSizeA(size, FLT_MAX, 0, hint.c_str());
    float x = p.x, right = p.x + avail - hs.x - 20 * s, labelY = p.y + (size - labelSize) * 0.72f; // baseline-ish alignment with the values
    dl->PushClipRect(p, {right, p.y + size * 2}, true);
    for (const auto& [label, value] : items) {
        if (value.empty()) continue;
        dl->AddText(bold, labelSize, {x, labelY}, Raw(Muted), label);
        x += bold->CalcTextSizeA(labelSize, FLT_MAX, 0, label).x + 5 * s;
        dl->AddText(regular, size, {x, p.y}, Raw(Alpha(Text, 0.82f)), value.c_str());
        x += regular->CalcTextSizeA(size, FLT_MAX, 0, value.c_str()).x + 16 * s;
    }
    dl->PopClipRect();
    dl->AddText(regular, size, {p.x + avail - hs.x, p.y}, Raw(Muted), hint.c_str());
    ImGui::Dummy({avail, size});
}

void DrawWindow()
{
    feat::Snapshot snap = feat::GetSnapshot();
    float s = S();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize({880 * s, 620 * s}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    ImGui::Begin("Banan Menu", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    Header(snap);
    float footerH = kBaseFont * 0.78f * s + ImGui::GetStyle().ItemSpacing.y * 2 + 4 * s;
    ImGui::BeginChild("##body", {0, -footerH}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    Sidebar();
    ImGui::SameLine(0, 14 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {2 * s, 0}); // keeps switches clear of the scrollbar
    ImGui::BeginChild("##content", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    switch (g_tab) {
    case 0: TabPlayer(snap); break;
    case 1: TabCombat(snap); break;
    case 2: TabMovement(snap); break;
    case 3: TabEsp(snap); break;
    case 4: TabInventory(snap); break;
    case 5: TabWorld(snap); break;
    case 6: TabProgress(snap); break;
    default: TabSettings(snap); break;
    }
    ImGui::Dummy({0, 6 * s});
    ImGui::EndChild();
    ImGui::EndChild();
    Footer();
    ImGui::End();
}

} // namespace

void Init(void* dllModule, const std::string& gameVersion)
{
    g_module = static_cast<HMODULE>(dllModule);
    g_game = gameVersion.substr(0, gameVersion.find('_')); // "1.0.1.227219_HellraiserGame_Shipping..."
    if (g_game.empty() && GetEnvironmentVariableA("KM_PREVIEW", nullptr, 0)) g_game = "1.1.1.227384"; // test host
    g_menuKey = unsigned(cfg::GetInt("Menu", "Key", VK_INSERT));
    g_menuKey2 = unsigned(cfg::GetInt("Menu", "Key2", VK_F8));
    int language = lang::FromCode(cfg::GetString("Menu", "Language").c_str());
    lang::Set(language >= 0 ? language : lang::FromSystem()); // first run: the Windows display language
    g_scale = cfg::GetFloat("Menu", "Scale", 0);
    if (g_scale > 0) g_scale = std::clamp(g_scale, 0.7f, 2.5f);
    g_tab = std::clamp(cfg::GetInt("Menu", "Tab", 0), 0, kTabCount - 1);
    for (int t = 0; t < feat::ToggleCount; t++) {
        g_hotkey[t] = cfg::GetInt("Hotkeys", feat::kToggles[t].key, 0);
        if (g_hotkey[t] == int(g_menuKey2)) g_menuKey2 = 0; // F8 already bound to a cheat before 0.4.8: the cheat keeps it
    }
    ReadHardware();
    feat::Init();
}

void SetupImGui()
{
    regular = LoadFont(101); // first font added = default font
    bold = LoadFont(102);
    if (!regular) {
        km::Log("Embedded font missing, using ImGui's default");
        regular = ImGui::GetIO().Fonts->AddFontDefault();
    }
    if (!bold) bold = regular;
    MergeScriptFonts();
}

void Draw()
{
    float scale = g_scale > 0 ? g_scale : std::clamp(ImGui::GetIO().DisplaySize.y / 1080.f, 1.f, 2.f);
    if (scale != g_appliedScale) {
        ApplyTheme(scale);
        g_appliedScale = scale;
    }
    ImGui::GetIO().MouseDrawCursor = g_open;
    esp::Draw();
    if (g_open) DrawWindow();
    DrawToasts();
}

bool IsOpen() { return g_open; }

bool WantsFrame()
{
    if (g_open || feat::g_on[feat::Esp] || (feat::g_on[feat::SilentAim] && feat::g_on[feat::EspAimFov])) return true;
    std::lock_guard lock(g_toastMutex);
    return !g_toasts.empty();
}

void Toast(std::string text) { PushToast(std::move(text), -1); }

bool OnKeyDown(unsigned vk)
{
    int target = g_binding;
    if (target != kNotBinding) {
        g_binding = kNotBinding;
        if (vk == VK_ESCAPE) return true;
        bool clear = vk == VK_BACK || vk == VK_DELETE;
        if (target == kBindMenu) {
            if (!clear) {
                g_menuKey = vk;
                cfg::SetInt("Menu", "Key", int(vk));
            }
        } else if (target == kBindMenu2) {
            g_menuKey2 = clear ? 0 : vk;
            cfg::SetInt("Menu", "Key2", int(g_menuKey2.load()));
        } else {
            g_hotkey[target] = clear ? 0 : int(vk);
            cfg::SetInt("Hotkeys", feat::kToggles[target].key, g_hotkey[target]);
        }
        return true;
    }
    if (vk == g_menuKey || (g_menuKey2 && vk == g_menuKey2)) {
        bool open = !g_open;
        g_open = open;
        km::Log("Menu %s%s", open ? "opened" : "closed", overlay::Drawing() ? "" : " (overlay not drawing, see the DX lines)"); // a crash right after this line points at the overlay
        if (open) ClipCursor(nullptr); // the game confines the cursor; free it for the menu
        return true;
    }
    if (g_open) return false;
    for (int t = 0; t < feat::ToggleCount; t++) {
        if (g_hotkey[t] != int(vk)) continue;
        bool on = !feat::g_on[t];
        feat::g_on[t] = on;
        feat::Save();
        PushToast(T(feat::kToggles[t].label), on ? 1 : 0);
        return true;
    }
    return false;
}

} // namespace menu
