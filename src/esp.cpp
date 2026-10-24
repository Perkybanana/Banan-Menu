#include "esp.h"
#include "features.h"
#include "text.h"
#include "theme.h"
#include "ue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace esp {
namespace {

using feat::g_on;
using feat::Vec3;
using namespace theme;

constexpr double kDeg = 3.14159265358979 / 180.0;

struct View {
    Vec3 loc, fwd, right, up;
    float cx = 0, cy = 0, fx = 0, fy = 0; // in overlay pixels
    float focal = 0;                      // fx, for sizes that scale with distance
};

double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

// The camera and the engine's projection measured in the game frame the positions come from, scaled from the game's
// viewport to the overlay (normally the same size).
bool ReadView(const feat::EspFrame& f, View& v)
{
    if (!f.camera) return false;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float sx = float(ds.x / f.width), sy = float(ds.y / f.height);
    v.loc = f.camLoc;
    v.fwd = f.camFwd;
    v.right = f.camRight;
    v.up = f.camUp;
    v.cx = float(f.cx) * sx;
    v.cy = float(f.cy) * sy;
    v.fx = float(f.fx) * sx;
    v.fy = float(f.fy) * sy;
    v.focal = v.fx;
    return true;
}

// World -> screen with the engine's projection. `depth` = distance along the view axis (cm). False when behind.
bool Project(const View& v, const Vec3& w, ImVec2& out, double* depth = nullptr)
{
    Vec3 d = Sub(w, v.loc);
    double x = Dot(d, v.fwd);
    if (depth) *depth = x;
    if (x < 1) return false;
    out = {float(v.cx + Dot(d, v.right) / x * v.fx), float(v.cy - Dot(d, v.up) / x * v.fy)};
    return true;
}

bool OnScreen(ImVec2 p, float margin = 0)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    return p.x > -margin && p.y > -margin && p.x < ds.x + margin && p.y < ds.y + margin;
}

// Text with a 1 px black outline, readable on any background.
void Label(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImVec4 col, const char* text)
{
    ImU32 shadow = Raw(Alpha(Black, 0.9f));
    for (ImVec2 o : {ImVec2{-1, 0}, ImVec2{1, 0}, ImVec2{0, -1}, ImVec2{0, 1}})
        dl->AddText(font, size, {pos.x + o.x, pos.y + o.y}, shadow, text);
    dl->AddText(font, size, pos, Raw(col), text);
}

void LabelCentered(ImDrawList* dl, ImFont* font, float size, float cx, float y, ImVec4 col, const char* text)
{
    float w = font->CalcTextSizeA(size, FLT_MAX, 0, text).x;
    Label(dl, font, size, {cx - w * 0.5f, y}, col, text);
}

void Arrow(ImDrawList* dl, const View& v, const Vec3& w, float distance, float s)
{
    Vec3 d = Sub(w, v.loc);
    float ang = std::atan2(float(-Dot(d, v.up)), float(Dot(d, v.right)));
    float ring = std::min(v.cx, v.cy) * 0.80f, size = 10 * s;
    ImVec2 dir{std::cos(ang), std::sin(ang)}, n{-dir.y, dir.x};
    ImVec2 tip{v.cx + dir.x * (ring + size), v.cy + dir.y * (ring + size)}, base{v.cx + dir.x * ring, v.cy + dir.y * ring};
    ImVec2 l{base.x + n.x * size * 0.6f, base.y + n.y * size * 0.6f}, r{base.x - n.x * size * 0.6f, base.y - n.y * size * 0.6f};
    dl->AddTriangleFilled(tip, l, r, Raw(Pink));
    dl->AddTriangle(tip, l, r, Raw(Alpha(Black, 0.9f)), 1.2f * s);
    char text[16];
    snprintf(text, sizeof(text), "%.0fm", distance);
    LabelCentered(dl, regular, 11 * s, base.x - dir.x * 12 * s, base.y - dir.y * 12 * s - 6 * s, Pink, text);
}

void DrawEnemy(ImDrawList* dl, const View& v, const feat::EspEnemy& e, float s)
{
    ImVec2 top, feet;
    bool visible = Project(v, e.top, top) && Project(v, e.feet, feet) && (OnScreen(top) || OnScreen(feet));
    if (!visible) {
        if (g_on[feat::EspOffscreen]) Arrow(dl, v, e.top, e.distance, s);
        return;
    }
    float h = std::max(feet.y - top.y, 6.f), w = h * 0.45f;
    ImVec2 a{std::floor(top.x - w * 0.5f), std::floor(top.y)}, b{std::floor(top.x + w * 0.5f), std::floor(feet.y)};

    auto bone = [&](const Vec3& p0, const Vec3& p1, ImVec2& s0, ImVec2& s1, double& depth) {
        double d0, d1;
        if (!Project(v, p0, s0, &d0) || !Project(v, p1, s1, &d1)) return false;
        depth = (d0 + d1) * 0.5;
        return true;
    };
    if (g_on[feat::EspChams] && !e.bones.empty()) { // see-through body silhouette built from the bones
        ImU32 fill = Raw(Alpha(Violet, 0.42f));
        for (const auto& [p0, p1] : e.bones) {
            ImVec2 s0, s1;
            double depth;
            if (!bone(p0, p1, s0, s1, depth)) continue;
            float th = std::clamp(float(12.0 * v.focal / std::max(depth, 1.0)), 2.f, 60.f);
            dl->AddLine(s0, s1, fill, th);
            dl->AddCircleFilled(s0, th * 0.5f, fill, 12);
            dl->AddCircleFilled(s1, th * 0.5f, fill, 12);
        }
        ImVec2 hp;
        double hd;
        if (e.hasHead && Project(v, e.head, hp, &hd)) dl->AddCircleFilled(hp, std::clamp(float(12.0 * v.focal / hd), 3.f, 70.f), fill, 20);
    }
    if (g_on[feat::EspSkeleton])
        for (const auto& [p0, p1] : e.bones) {
            ImVec2 s0, s1;
            double depth;
            if (!bone(p0, p1, s0, s1, depth)) continue;
            dl->AddLine(s0, s1, Raw(Alpha(Black, 0.6f)), 3.f * s);
            dl->AddLine(s0, s1, Raw(Text), 1.4f * s);
        }
    if (g_on[feat::EspHead] && e.hasHead) {
        ImVec2 hp;
        double hd;
        if (Project(v, e.head, hp, &hd)) {
            float r = std::clamp(float(10.0 * v.focal / hd), 3.f, 40.f);
            dl->AddCircle(hp, r, Raw(Alpha(Black, 0.8f)), 20, 3.f * s);
            dl->AddCircle(hp, r, Raw(Pink), 20, 1.5f * s);
        }
    }
    if (g_on[feat::EspSnaplines]) dl->AddLine({v.cx, ImGui::GetIO().DisplaySize.y},{(a.x + b.x) * 0.5f, b.y}, Raw(Alpha(Violet, 0.8f)), 1.f * s);
    if (g_on[feat::EspBox]) {
        dl->AddRect({a.x - 1, a.y - 1}, {b.x + 1, b.y + 1}, Raw(Alpha(Black, 0.85f)), 0, 0, 3.f * s);
        dl->AddRect(a, b, Raw(Violet), 0, 0, 1.4f * s);
    }
    if (g_on[feat::EspHealth] && e.maxHealth > 0) {
        float frac = std::clamp(e.health / e.maxHealth, 0.f, 1.f), bw = 3.f * s, x1 = a.x - 4 * s, x0 = x1 - bw;
        dl->AddRectFilled({x0 - 1, a.y - 1}, {x1 + 1, b.y + 1}, Raw(Alpha(Black, 0.85f)));
        dl->AddRectFilled({x0, b.y - (b.y - a.y) * frac}, {x1, b.y}, Raw(Pink));
    }
    float size = 13 * s, cx = (a.x + b.x) * 0.5f;
    if (g_on[feat::EspName]) LabelCentered(dl, bold, size, cx, a.y - size - 4 * s, Text, e.name.c_str());
    if (g_on[feat::EspDistance]) {
        char text[16];
        snprintf(text, sizeof(text), "%.0fm", e.distance);
        LabelCentered(dl, regular, size * 0.92f, cx, b.y + 3 * s, Pink, text);
    }
}

void DrawThing(ImDrawList* dl, const View& v, const feat::EspThing& t, float s)
{
    ImVec2 p;
    if (!Project(v, t.pos, p) || !OnScreen(p, 20)) return;
    ImVec4 col = t.kind == feat::EspKind::Item ? Pink : t.kind == feat::EspKind::Container ? Violet : Text;
    float r = 3.5f * s;
    dl->AddCircleFilled(p, r + 1.2f * s, Raw(Alpha(Black, 0.9f)), 12);
    dl->AddCircleFilled(p, r, Raw(col), 12);
    std::string label = t.kind == feat::EspKind::Item ? Pretty(t.label) : t.label;
    char dist[16];
    snprintf(dist, sizeof(dist), "  %.0fm", t.distance);
    float size = 12 * s;
    ImVec2 at{p.x + r + 5 * s, p.y - size * 0.55f};
    Label(dl, regular, size, at, Text, label.c_str());
    Label(dl, regular, size, {at.x + regular->CalcTextSizeA(size, FLT_MAX, 0, label.c_str()).x, at.y}, col, dist);
}

} // namespace

void Draw()
{
    bool circle = g_on[feat::SilentAim] && g_on[feat::EspAimFov];
    if (!g_on[feat::Esp] && !circle) return;
    feat::EspFrame f = feat::GetEspFrame();
    View v;
    if (!ReadView(f, v)) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float s = ImGui::GetStyle().FontScaleMain;

    if (circle) { // the aim FOV: enemies whose head is inside the circle can be targeted
        float t = float(std::tan(feat::g_value[feat::AimFov] * kDeg));
        ImVec2 c{v.cx, v.cy}, rad{t * v.fx, t * v.fy}; // the same angle in both directions
        dl->AddEllipse(c, rad, Raw(Alpha(Black, 0.5f)), 0, 96, 3.f * s);
        dl->AddEllipse(c, rad, Raw(Alpha(Violet, 0.85f)), 0, 96, 1.3f * s);
    }
    if (!g_on[feat::Esp] || !f.active) return;

    std::sort(f.things.begin(), f.things.end(), [](auto& x, auto& y) { return x.distance < y.distance; });
    if (f.things.size() > 60) f.things.resize(60);
    for (auto it = f.things.rbegin(); it != f.things.rend(); ++it) DrawThing(dl, v, *it, s);

    std::sort(f.enemies.begin(), f.enemies.end(), [](auto& x, auto& y) { return x.distance > y.distance; }); // near on top
    for (const auto& e : f.enemies) DrawEnemy(dl, v, e, s);
}

} // namespace esp
