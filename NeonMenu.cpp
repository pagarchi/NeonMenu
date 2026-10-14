// NeonMenu.asi - GTA San Andreas (PC v1.0 US) neon underglow with ImGui menu
// Neon menu mod by : Parsa Rsi
// Type NEON in-game to open/close the menu (mouse cursor appears).
// Two style groups: NORMAL (250 classic patterns) and EMOJI (11 animated multi-colour neon shapes).
// While menu is open the game is frozen. Mouse keys: F6/F7 = slower/faster, F8 = flip up/down.
#include "plugin.h"
#include "common.h"
#include "CTimer.h"
#include "CVehicle.h"
#include "CModelInfo.h"
#include "CPointLights.h"
#include "CCoronas.h"
#include "CShadows.h"
#include "CTxdStore.h"
#include "CWorld.h"
#include <d3d9.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <cfloat>
#include <cstring>
#include <algorithm>
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

using namespace plugin;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static bool g_open = false, g_inited = false, g_enabled = true;
static int g_colorCount = 1, g_slot = 0, g_style = 1; // style 1..250
static float g_col[3][3] = {{0.0f, 0.6f, 1.0f}, {1.0f, 0.0f, 0.5f}, {0.2f, 1.0f, 0.2f}};
static WNDPROC g_oldProc = nullptr;
static HWND g_hwnd = nullptr;
static std::string g_typed;
static int g_mode = 0;          // 0 = normal styles, 1 = emoji styles
static int g_emoji = 0;         // 0..10
static float g_esize = 1.0f, g_estretch = 1.25f, g_eheight = 0.15f, g_ebright = 1.0f;
static ImFont* g_fontBody = nullptr;
static ImFont* g_fontTitle = nullptr;

// ---------- mouse ----------
// GTA uses exclusive DirectInput, so the Windows cursor is frozen. We read the mouse with Raw Input
// (WM_INPUT) which gives true relative movement on both axes. Old game-memory method is kept as fallback.
struct SAMouseState {
    unsigned char lmb, rmb, mmb, wheelUp, wheelDown, bmx1, bmx2, pad;
    float X, Y, Z;
};
static SAMouseState* const g_ms = (SAMouseState*)0xB73418;
static float g_mx = 0.0f, g_my = 0.0f, g_mouseSpeed = 1.0f;
static bool g_centerMouse = false, g_invertY = false, g_pausedByUs = false;
static POINT g_lastOs = {0, 0};
static float g_rawDX = 0.0f, g_rawDY = 0.0f;
static bool g_rawSeen = false;
static RAWINPUTDEVICE g_prevRid = {};
static bool g_hadPrevRid = false;

static void RawMouseOn() {
    g_hadPrevRid = false;
    UINT n = 0;
    GetRegisteredRawInputDevices(nullptr, &n, sizeof(RAWINPUTDEVICE));
    if (n) {
        std::vector<RAWINPUTDEVICE> devs(n);
        if (GetRegisteredRawInputDevices(devs.data(), &n, sizeof(RAWINPUTDEVICE)) != (UINT)-1) {
            for (UINT i = 0; i < n; i++)
                if (devs[i].usUsagePage == 1 && devs[i].usUsage == 2) { g_prevRid = devs[i]; g_hadPrevRid = true; }
        }
    }
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 1; rid.usUsage = 2; rid.dwFlags = 0; rid.hwndTarget = g_hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    g_rawDX = g_rawDY = 0.0f;
}

static void RawMouseOff() {
    if (g_hadPrevRid) {
        RegisterRawInputDevices(&g_prevRid, 1, sizeof(RAWINPUTDEVICE));
    } else {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = 1; rid.usUsage = 2; rid.dwFlags = RIDEV_REMOVE; rid.hwndTarget = nullptr;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }
}

static bool GetOsCursor(POINT& pt) {
    return GetCursorPos(&pt) && g_hwnd && ScreenToClient(g_hwnd, &pt);
}

static bool KeyPressedOnce(int vk, bool& prev) {
    bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool fired = now && !prev;
    prev = now;
    return fired;
}

static void UpdateMouse() {
    ImGuiIO& io = ImGui::GetIO();

    static bool f6p = false, f7p = false, f8p = false;
    if (KeyPressedOnce(VK_F6, f6p)) g_mouseSpeed = fmaxf(0.25f, g_mouseSpeed - 0.25f);
    if (KeyPressedOnce(VK_F7, f7p)) g_mouseSpeed = fminf(6.0f, g_mouseSpeed + 0.25f);
    if (KeyPressedOnce(VK_F8, f8p)) g_invertY = !g_invertY;

    POINT pt;
    bool haveOs = GetOsCursor(pt);

    if (g_centerMouse) {
        g_mx = io.DisplaySize.x * 0.5f;
        g_my = io.DisplaySize.y * 0.5f;
        g_centerMouse = false;
        g_rawDX = g_rawDY = 0.0f;
        if (haveOs) g_lastOs = pt;
    } else if (g_rawSeen) {
        g_mx += g_rawDX * g_mouseSpeed;
        g_my += (g_invertY ? -g_rawDY : g_rawDY) * g_mouseSpeed;
        g_rawDX = g_rawDY = 0.0f;
        if (haveOs) g_lastOs = pt;
    } else {
        // fallback (no raw input received yet)
        bool gameMoved = (g_ms->X != 0.0f || g_ms->Y != 0.0f);
        bool osMoved = haveOs && (abs(pt.x - g_lastOs.x) >= 2 || abs(pt.y - g_lastOs.y) >= 2);
        if (gameMoved) {
            g_mx += g_ms->X * g_mouseSpeed;
            g_my += (g_invertY ? -g_ms->Y : g_ms->Y) * g_mouseSpeed;
        } else if (osMoved) {
            g_mx = (float)pt.x;
            g_my = (float)pt.y;
        }
        if (haveOs) g_lastOs = pt;
    }

    if (g_mx < 0) g_mx = 0;
    if (g_my < 0) g_my = 0;
    if (g_mx > io.DisplaySize.x - 1) g_mx = io.DisplaySize.x - 1;
    if (g_my > io.DisplaySize.y - 1) g_my = io.DisplaySize.y - 1;

    bool ldown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool rdown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (!g_rawSeen) { ldown = ldown || g_ms->lmb; rdown = rdown || g_ms->rmb; }
    io.AddMousePosEvent(g_mx, g_my);
    io.AddMouseButtonEvent(0, ldown);
    io.AddMouseButtonEvent(1, rdown);
}

// freeze / unfreeze the game when the menu opens / closes (also works when closed with the X button)
static void ApplyOpenState() {
    static bool prevOpen = false;
    if (g_open == prevOpen) return;
    prevOpen = g_open;
    if (g_open) {
        g_centerMouse = true;
        if (!CTimer::m_CodePause) { CTimer::m_CodePause = true; g_pausedByUs = true; }
        RawMouseOn();
    } else {
        if (g_pausedByUs) { CTimer::m_CodePause = false; g_pausedByUs = false; }
        RawMouseOff();
    }
}

// ---------- effects: 25 patterns x 10 speeds = 250 styles ----------
static float Saw(float x) { return x - floorf(x); }
static float Tri(float x) { return 1.0f - fabsf(2.0f * Saw(x) - 1.0f); }
static float Hash(int a, int b) {
    unsigned h = (unsigned)(a * 73856093) ^ (unsigned)(b * 19349663);
    h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
    return (h & 1023) / 1023.0f;
}

// sample the palette at continuous index (smooth) or hard (stepped)
static void Palette(float idx, bool smooth, float out[3]) {
    int n = g_colorCount;
    if (n == 1) { for (int c = 0; c < 3; c++) out[c] = g_col[0][c]; return; }
    float f = fmodf(idx, (float)n); if (f < 0) f += n;
    int a = (int)f, b = (a + 1) % n;
    float u = smooth ? (f - a) : 0.0f;
    for (int c = 0; c < 3; c++) out[c] = g_col[a][c] * (1 - u) + g_col[b][c] * u;
}

// pt   = index of the light point (for random patterns)
// u    = position around the car outline, 0..4 (0=front-left, 1=front-right, 2=rear-right, 3=rear-left)
static void Eval(int style, float t, int pt, float u, bool left, bool front, float outRGB[3]) {
    int pat = (style - 1) / 10, spd = (style - 1) % 10;
    float ph = t * (0.3f + 0.4f * spd);
    float k = 1.0f, idx = 0.0f;
    bool smooth = false;
    switch (pat) {
    case 0: break;
    case 1: k = 0.35f + 0.65f * Tri(ph); break;
    case 2: k = Saw(ph) < 0.5f ? 1.0f : 0.05f; break;
    case 3: k = Saw(ph * 3) < 0.2f ? 1.0f : 0.05f; break;
    case 4: { float d = fabsf(Saw(ph) * 4 - u); d = fminf(d, 4 - d); k = fmaxf(0.1f, 1 - d * 0.7f); } break;
    case 5: { float d = fabsf(Saw(-ph) * 4 - u); d = fminf(d, 4 - d); k = fmaxf(0.1f, 1 - d * 0.7f); } break;
    case 6: k = (front == (Saw(ph) < 0.5f)) ? 1.0f : 0.05f; break;
    case 7: k = (left == (Saw(ph) < 0.5f)) ? 1.0f : 0.05f; break;
    case 8: k = ((((int)u) & 1) == 0) == (Saw(ph) < 0.5f) ? 1.0f : 0.05f; break;
    case 9: k = 0.5f + 0.5f * sinf(6.2832f * (ph + u / 4.0f)); break;
    case 10: { float d = fabsf(Saw(ph) * 4 - u); d = fminf(d, 4 - d); float d2 = fabsf(Saw(ph) * 4 + 2 - u); d2 = fminf(d2, 4 - d2); k = fmaxf(0.1f, 1 - fminf(d, d2) * 0.7f); } break;
    case 11: { float x = Saw(ph); k = (x < 0.1f || (x > 0.2f && x < 0.3f)) ? 1.0f : 0.15f; } break;
    case 12: k = 0.1f + 0.9f * Hash(pt, (int)(ph * 8)); break;
    case 13: idx = ph; smooth = true; break;
    case 14: idx = floorf(ph); break;
    case 15: idx = ph + u * 0.25f * g_colorCount; smooth = true; break;
    case 16: idx = floorf(u); break;
    case 17: idx = floorf(ph) + floorf(u); break;
    case 18: idx = ph; smooth = true; k = 0.35f + 0.65f * Tri(ph * 2); break;
    case 19: idx = floorf(ph * 2); break;
    case 20: idx = floorf(ph * 3); k = Saw(ph * 6) < 0.3f ? 1.0f : 0.05f; break;
    case 21: idx = ph + u * 0.5f; smooth = true; k = 0.5f + 0.5f * sinf(6.2832f * (ph + u / 4.0f)); break;
    case 22: idx = left ? 0.0f : 1.0f; k = (Saw(ph * 2) < 0.5f) == left ? 1.0f : 0.05f; break;
    case 23: idx = floorf(Hash(pt, (int)(ph * 6) + 7) * 3); k = 0.1f + 0.9f * Hash(pt, (int)(ph * 6)); break;
    case 24: { float r, g, b; ImGui::ColorConvertHSVtoRGB(Saw(ph * 0.5f + u * 0.25f), 1.0f, 1.0f, r, g, b); outRGB[0] = r; outRGB[1] = g; outRGB[2] = b; return; }
    }
    Palette(idx, smooth, outRGB);
    for (int c = 0; c < 3; c++) outRGB[c] *= k;
}

// soft round glow texture used by the game itself for explosions/headlights ("shad_exp" in particle.txd)
static RwTexture* GetGlowTex() {
    static RwTexture* tex = nullptr;
    static bool tried = false;
    if (tex || tried) return tex;
    // 1) the game's own pointer, whichever way this plugin-sdk version declares it
    __if_exists(CShadows::gpShadowExplosionTex) { tex = CShadows::gpShadowExplosionTex; }
    __if_exists(gpShadowExplosionTex) { tex = gpShadowExplosionTex; }
    // 2) otherwise look it up by name in particle.txd
    if (!tex) {
        int slot = CTxdStore::FindTxdSlot("particle");
        if (slot >= 0) {
            CTxdStore::PushCurrentTxd();
            CTxdStore::SetCurrentTxd(slot);
            tex = RwTexDictionaryFindNamedTexture(RwTexDictionaryGetCurrent(), "shad_exp");
            CTxdStore::PopCurrentTxd();
        }
    }
    if (tex) tried = true; // keep retrying until found (textures may not be loaded yet)
    return tex;
}

// ---------- neon rendering: continuous soft glow projected on the ground under the car ----------
// Many large, heavily overlapping additive ground-glow blobs along the underside outline => one smooth halo.
// No coronas (they were the visible "dots").
static void DrawNeon() {
    if (!g_enabled) return;
    CVehicle* v = FindPlayerVehicle(-1, false);
    if (!v || !v->m_matrix) return;
    CBaseModelInfo* mi = CModelInfo::GetModelInfo(v->m_nModelIndex);
    if (!mi || !mi->m_pColModel) return;
    CVector mn = mi->m_pColModel->m_boundBox.m_vecMin, mx = mi->m_pColModel->m_boundBox.m_vecMax;
    float t = (float)(GetTickCount() % 600000u) / 1000.0f; // real time, keeps animating while game is frozen
    CMatrix* m = v->m_matrix;

    // outline corners: 0=front-left 1=front-right 2=rear-right 3=rear-left
    const float cxs[4] = {mn.x * 0.78f, mx.x * 0.78f, mx.x * 0.78f, mn.x * 0.78f};
    const float cys[4] = {mx.y * 0.80f, mx.y * 0.80f, mn.y * 0.80f, mn.y * 0.80f};
    const int cnt[4] = {3, 7, 3, 7}; // 20 blobs total (game shadow slots are limited)
    const float z = mn.z + 0.2f;
    const float midY = (mn.y + mx.y) * 0.5f;
    const float R = 1.35f; // blob radius in metres (big => blobs merge into one continuous glow)

    RwTexture* glowTex = GetGlowTex();
    int n = 0;
    for (int e = 0; e < 4; e++) {
        int a = e, b = (e + 1) & 3;
        for (int j = 0; j < cnt[e]; j++, n++) {
            float s = (float)j / (float)cnt[e];
            float x = cxs[a] + (cxs[b] - cxs[a]) * s;
            float y = cys[a] + (cys[b] - cys[a]) * s;
            float u = (float)e + s;
            CVector p = m->pos + m->right * x + m->up * y + m->at * z;
            float rgb[3];
            Eval(g_style, t, n, u, x < 0.0f, y > midY, rgb);
            unsigned char r8 = (unsigned char)(fminf(rgb[0], 1.0f) * 255);
            unsigned char g8 = (unsigned char)(fminf(rgb[1], 1.0f) * 255);
            unsigned char b8 = (unsigned char)(fminf(rgb[2], 1.0f) * 255);
            // shadow type: 1 = dark (multiply, caused black squares), 2 = additive (glow), 3 = inverse color
            if (glowTex) CShadows::StoreShadowToBeRendered(2, glowTex, &p,
                m->up.x * R, m->up.y * R, m->right.x * R, m->right.y * R,
                150, r8, g8, b8, 4.0f, false, 1.0f, nullptr, false);
            if ((n & 1) == 0) // real light on the surroundings
                CPointLights::AddLight(0, p, CVector(0, 0, 0), 5.0f, rgb[0] * 0.8f, rgb[1] * 0.8f, rgb[2] * 0.8f, 0, false, nullptr);
        }
    }
}

// =====================================================================================
//  EMOJI NEON ENGINE
//  Shapes are built from (a) translucent colour fills, (b) dense miniature neon dots with glow,
//  and drawn flat on the ground under the car with a depth test (the car body hides the middle,
//  the rest sticks out around the car). Every shape keeps its own fixed colours.
// =====================================================================================
namespace emo {

static const float PI2 = 6.2831853f;
static const float PI1 = 3.1415927f;

struct V2 { float x, y; };
struct Col { int r, g, b; };
struct Dot { float x, y; unsigned char hr, hg, hb, cr, cg, cb; float ha, ca, hrad, crad; };
struct Tri { float x[3], y[3]; unsigned char r, g, b, a; };
struct Vtx { float x, y, z; DWORD col; float u, v; };

static std::vector<Dot> g_dots;
static std::vector<Tri> g_tris;
static std::vector<V2> g_tmp;
static float g_bk = 5.0f, g_bky = 5.0f;   // base metres per unit (x / y)
static float g_k = 5.0f, g_ky = 5.0f;     // current (after per-shape pulse scale)
static float g_sumR = 0, g_sumG = 0, g_sumB = 0, g_sumW = 0;

static const Col cYEL  = {255, 214, 25};
static const Col cORG  = {255, 140, 20};
static const Col cRED  = {255, 35, 60};
static const Col cWHT  = {240, 248, 255};
static const Col cCYN  = {30, 230, 255};
static const Col cBLU  = {50, 120, 255};
static const Col cPNK  = {255, 80, 175};
static const Col cGRN  = {50, 255, 110};
static const Col cDRK  = {45, 22, 5};
static const Col cBLK  = {15, 5, 22};
static const Col cICE  = {150, 225, 255};
static const Col cGLD  = {255, 196, 30};

static inline float Sat01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static inline float Pls(float t, float f, float ph = 0.0f) { return 0.5f + 0.5f * sinf(PI2 * (t * f + ph)); }
static inline V2 pt2(float x, float y) { V2 v = {x, y}; return v; }
static inline Col MkC(int r, int g, int b) { Col c = {r, g, b}; return c; }
static Col Mix(Col a, Col b, float t) {
    Col c; c.r = (int)(a.r + (b.r - a.r) * t); c.g = (int)(a.g + (b.g - a.g) * t); c.b = (int)(a.b + (b.b - a.b) * t); return c;
}
static Col HsvC(float h) {
    float r, g, b; ImGui::ColorConvertHSVtoRGB(h - floorf(h), 0.9f, 1.0f, r, g, b);
    Col c; c.r = (int)(r * 255); c.g = (int)(g * 255); c.b = (int)(b * 255); return c;
}
static void SetScl(float s) { g_k = g_bk * s; g_ky = g_bky * s; }

// ---- neon dots ----
static void AddDot(float ux, float uy, Col c, float inten, float crad = 0.05f, float hrad = 0.27f) {
    Dot d; Col w = Mix(c, cWHT, 0.45f);
    d.x = ux * g_k; d.y = uy * g_ky;
    d.hr = (unsigned char)c.r; d.hg = (unsigned char)c.g; d.hb = (unsigned char)c.b;
    d.cr = (unsigned char)w.r; d.cg = (unsigned char)w.g; d.cb = (unsigned char)w.b;
    d.ha = 0.12f * inten; d.ca = Sat01(inten); d.hrad = hrad; d.crad = crad;
    g_dots.push_back(d);
    g_sumR += c.r * inten; g_sumG += c.g * inten; g_sumB += c.b * inten; g_sumW += inten;
}
static void Glow(float ux, float uy, Col c, float radM, float alpha) {
    Dot d;
    d.x = ux * g_k; d.y = uy * g_ky;
    d.hr = (unsigned char)c.r; d.hg = (unsigned char)c.g; d.hb = (unsigned char)c.b;
    d.cr = d.hr; d.cg = d.hg; d.cb = d.hb;
    d.ha = alpha; d.ca = 0.0f; d.hrad = radM; d.crad = 0.01f;
    g_dots.push_back(d);
}
static void StrokePts(const V2* p, int n, bool closed, Col c, float inten = 1.0f, float crad = 0.05f, float hrad = 0.27f, float stepM = 0.075f) {
    int segs = closed ? n : n - 1;
    float carry = 0.0f;
    for (int i = 0; i < segs; i++) {
        V2 a = p[i], b = p[(i + 1) % n];
        float dx = (b.x - a.x) * g_k, dy = (b.y - a.y) * g_ky;
        float L = sqrtf(dx * dx + dy * dy);
        if (L < 0.00001f) continue;
        float d = carry;
        while (d <= L) {
            float s = d / L;
            AddDot(a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, c, inten, crad, hrad);
            d += stepM;
        }
        carry = d - L;
    }
}
static void Line(float x0, float y0, float x1, float y1, Col c, float inten = 1.0f, float crad = 0.05f, float hrad = 0.27f) {
    V2 p[2] = {pt2(x0, y0), pt2(x1, y1)};
    StrokePts(p, 2, false, c, inten, crad, hrad);
}
static int AutoN(float rx, float ry, float span) {
    float rm = fmaxf(rx * g_k, ry * g_ky);
    int n = (int)(fabsf(span) * rm / 0.07f);
    return n < 12 ? 12 : (n > 160 ? 160 : n);
}
static void EllPts(float cx, float cy, float rx, float ry, float a0, float a1, int n) {
    g_tmp.resize(n + 1);
    for (int i = 0; i <= n; i++) { float a = a0 + (a1 - a0) * i / n; g_tmp[i] = pt2(cx + rx * cosf(a), cy + ry * sinf(a)); }
}
static void Ring(float cx, float cy, float rx, float ry, Col c, float inten = 1.0f, float a0 = 0.0f, float a1 = PI2) {
    int n = AutoN(rx, ry, a1 - a0);
    EllPts(cx, cy, rx, ry, a0, a1, n);
    StrokePts(g_tmp.data(), n + 1, false, c, inten);
}

// ---- translucent fills (colour-true, alpha blended) ----
static void AddTriM(float x0, float y0, float x1, float y1, float x2, float y2, Col c, float al) {
    Tri t; t.x[0] = x0; t.y[0] = y0; t.x[1] = x1; t.y[1] = y1; t.x[2] = x2; t.y[2] = y2;
    t.r = (unsigned char)c.r; t.g = (unsigned char)c.g; t.b = (unsigned char)c.b; t.a = (unsigned char)(Sat01(al) * 255);
    g_tris.push_back(t);
}
static void AddTriU(V2 a, V2 b, V2 c, Col col, float al) {
    AddTriM(a.x * g_k, a.y * g_ky, b.x * g_k, b.y * g_ky, c.x * g_k, c.y * g_ky, col, al);
}
static void FillFan(V2 ctr, const V2* p, int n, bool closed, Col col, float al) {
    int e = closed ? n : n - 1;
    for (int i = 0; i < e; i++) AddTriU(ctr, p[i], p[(i + 1) % n], col, al);
}
static void FillEll(float cx, float cy, float rx, float ry, Col col, float al, float a0 = 0.0f, float a1 = PI2) {
    int n = AutoN(rx, ry, a1 - a0);
    EllPts(cx, cy, rx, ry, a0, a1, n);
    FillFan(pt2(cx, cy), g_tmp.data(), n + 1, false, col, al);
}
static void FillDisc(float cx, float cy, float rM, Col col, float al) { FillEll(cx, cy, rM / g_k, rM / g_ky, col, al); }
static void FillStrip(const V2* A, const V2* B, int n, Col col, float al) {
    for (int i = 0; i < n - 1; i++) { AddTriU(A[i], A[i + 1], B[i + 1], col, al); AddTriU(A[i], B[i + 1], B[i], col, al); }
}
static bool InTri(V2 p, V2 a, V2 b, V2 c) {
    float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0), pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}
// ear clipping, for small simple polygons (star, bolt, crown)
static void FillPoly(const V2* p, int n, Col col, float al) {
    if (n < 3) return;
    std::vector<int> idx(n);
    float area = 0;
    for (int i = 0; i < n; i++) { idx[i] = i; int j = (i + 1) % n; area += p[i].x * p[j].y - p[j].x * p[i].y; }
    bool ccw = area > 0;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 500) {
        bool clipped = false;
        int m = (int)idx.size();
        for (int i = 0; i < m; i++) {
            int i0 = idx[(i + m - 1) % m], i1 = idx[i], i2 = idx[(i + 1) % m];
            V2 a = p[i0], b = p[i1], c = p[i2];
            float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if ((cr > 0) != ccw || fabsf(cr) < 0.000000001f) continue;
            bool inside = false;
            for (int k = 0; k < m; k++) {
                int ik = idx[k];
                if (ik == i0 || ik == i1 || ik == i2) continue;
                if (InTri(p[ik], a, b, c)) { inside = true; break; }
            }
            if (inside) continue;
            AddTriU(a, b, c, col, al);
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) AddTriU(p[idx[0]], p[idx[1]], p[idx[2]], col, al);
}
// thick segment, width in metres
static void ThickSeg(V2 a, V2 b, float wM, Col col, float al) {
    float ax = a.x * g_k, ay = a.y * g_ky, bx = b.x * g_k, by = b.y * g_ky;
    float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy);
    if (L < 0.0001f) return;
    float nx = -dy / L * wM * 0.5f, ny = dx / L * wM * 0.5f;
    AddTriM(ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, col, al);
    AddTriM(ax + nx, ay + ny, bx - nx, by - ny, ax - nx, ay - ny, col, al);
}
static void ThickPath(const V2* p, int n, float wM, Col col, float al, bool round = true) {
    for (int i = 0; i < n - 1; i++) ThickSeg(p[i], p[i + 1], wM, col, al);
    if (round) for (int i = 0; i < n; i++) FillDisc(p[i].x, p[i].y, wM * 0.5f, col, al);
}
static void Star5(float cx, float cy, float R, float rot, Col col, float al, bool outline) {
    V2 v[10];
    for (int i = 0; i < 10; i++) { float r = (i & 1) ? R * 0.42f : R; float a = rot + i * PI1 / 5.0f; v[i] = pt2(cx + r * cosf(a), cy + r * sinf(a)); }
    FillFan(pt2(cx, cy), v, 10, true, col, al);
    if (outline) StrokePts(v, 10, true, col, al, 0.04f, 0.2f);
}
static void Star4(float cx, float cy, float R, float rot, Col col, float al) {
    V2 v[8];
    for (int i = 0; i < 8; i++) { float r = (i & 1) ? R * 0.26f : R; float a = rot + i * PI1 / 4.0f; v[i] = pt2(cx + r * cosf(a), cy + r * sinf(a)); }
    FillFan(pt2(cx, cy), v, 8, true, col, al);
    Glow(cx, cy, col, R * g_k * 2.2f, 0.18f * al);
}
static void HeartPts(V2* out, int n, float cx, float cy, float sz) {
    for (int i = 0; i < n; i++) {
        float a = PI2 * i / n, s = sinf(a);
        out[i] = pt2(cx + sz * 16.0f * s * s * s / 17.0f,
                     cy + sz * (13.0f * cosf(a) - 5.0f * cosf(2 * a) - 2.0f * cosf(3 * a) - cosf(4 * a)) / 17.0f);
    }
}

// =============================== SHAPES ===============================

// 0 - laughing smiley: blinks, mouth opens/closes, joy tears
static void Smiley(float t) {
    SetScl(1.0f + 0.025f * sinf(t * 6.0f));
    float lp = Pls(t, 1.6f);
    float open = 0.14f + 0.36f * lp;
    float bph = fmodf(t, 3.0f);
    float blink = bph < 0.16f ? fabsf(bph - 0.08f) / 0.08f : 1.0f;
    float eyeRy = 0.02f + 0.16f * blink;
    Col mouthC = MkC(120, 10, 25), tongC = MkC(255, 95, 120);
    FillEll(0, 0, 0.93f, 0.93f, cYEL, 0.70f);
    FillEll(0, 0.10f, 0.75f, 0.72f, Mix(cYEL, cWHT, 0.35f), 0.30f);
    Ring(0, 0, 0.96f, 0.96f, cORG, 1.0f);
    Ring(0, 0, 0.88f, 0.88f, cYEL, 0.75f);
    Glow(0, 0, cYEL, 3.4f, 0.07f + 0.04f * lp);
    FillEll(-0.62f, -0.10f, 0.15f, 0.09f, cPNK, 0.45f);
    FillEll(0.62f, -0.10f, 0.15f, 0.09f, cPNK, 0.45f);
    for (int s = -1; s <= 1; s += 2) {
        float ex = s * 0.34f;
        FillEll(ex, 0.30f, 0.10f, eyeRy, cDRK, 0.97f);
        if (blink > 0.5f) FillEll(ex + 0.03f, 0.30f + eyeRy * 0.45f, 0.035f, eyeRy * 0.28f, cWHT, 0.95f);
        V2 br[3] = {pt2(ex - 0.13f, 0.58f + 0.03f * lp), pt2(ex, 0.64f + 0.04f * lp), pt2(ex + 0.13f, 0.58f + 0.03f * lp)};
        ThickPath(br, 3, 0.16f, cDRK, 0.9f);
        FillEll(s * 0.66f, 0.18f - 0.05f * lp, 0.04f, 0.07f, cCYN, 0.85f);
    }
    FillEll(0, -0.12f, 0.58f, open, mouthC, 0.97f, PI1, PI2);
    FillEll(0, -0.12f - open * 0.85f, 0.24f, open * 0.42f, tongC, 0.97f, 0, PI1);
    float td = fminf(0.09f, open * 0.6f);
    float tw = 0.58f * sqrtf(fmaxf(0.0f, 1.0f - (td / open) * (td / open)));
    AddTriU(pt2(-tw, -0.12f), pt2(tw, -0.12f), pt2(tw, -0.12f - td), cWHT, 0.97f);
    AddTriU(pt2(-tw, -0.12f), pt2(tw, -0.12f - td), pt2(-tw, -0.12f - td), cWHT, 0.97f);
    Ring(0, -0.12f, 0.58f, open, cRED, 1.0f, PI1, PI2);
    Line(-0.58f, -0.12f, 0.58f, -0.12f, cRED, 1.0f);
}

// 1 - crying face: sad brows, trembling frown, tear streams and falling drops
static void Crying(float t) {
    SetScl(1.0f + 0.02f * sinf(t * 9.0f));
    float bph = fmodf(t, 2.6f);
    float blink = bph < 0.16f ? fabsf(bph - 0.08f) / 0.08f : 1.0f;
    float quiver = sinf(t * 16.0f);
    Col mouthC = MkC(120, 10, 25), tearIn = Mix(cCYN, cWHT, 0.5f);
    FillEll(0, 0, 0.93f, 0.93f, cYEL, 0.66f);
    Ring(0, 0, 0.96f, 0.96f, cORG, 1.0f);
    Ring(0, 0, 0.88f, 0.88f, cYEL, 0.7f);
    Glow(0, 0, cBLU, 3.4f, 0.06f);
    for (int s = -1; s <= 1; s += 2) {
        V2 st[14];
        for (int i = 0; i < 14; i++) st[i] = pt2(s * (0.37f + 0.02f * i / 13.0f) + 0.012f * sinf(i * 0.9f - t * 7.0f), 0.05f - i * 0.07f);
        ThickPath(st, 14, 0.30f, cCYN, 0.40f, false);
        ThickPath(st, 14, 0.10f, tearIn, 0.55f, false);
        for (int i = 0; i < 14; i += 2) AddDot(st[i].x, st[i].y, cCYN, 0.5f, 0.03f, 0.18f);
    }
    for (int s = -1; s <= 1; s += 2) {
        float ex = s * 0.34f;
        FillEll(ex, 0.20f, 0.11f, 0.02f + 0.15f * blink, cDRK, 0.97f);
        FillEll(ex, 0.20f, 0.11f, 0.09f * blink, cCYN, 0.75f, PI1, PI2);
        FillEll(ex + 0.035f, 0.26f, 0.035f, 0.045f * blink, cWHT, 0.95f);
        V2 br[2] = {pt2(ex + s * 0.15f, 0.42f + 0.01f * quiver), pt2(ex - s * 0.15f, 0.58f)};
        ThickPath(br, 2, 0.15f, cDRK, 0.92f);
    }
    V2 mp[17];
    for (int i = 0; i <= 16; i++) {
        float a = 0.44f + 2.26f * i / 16.0f;
        mp[i] = pt2(0.36f * cosf(a), -0.62f + (0.20f + 0.015f * quiver) * sinf(a));
    }
    ThickPath(mp, 17, 0.20f, mouthC, 0.97f);
    for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < 3; k++) {
            float ph = Saw(t * 0.6f + k * 0.34f + (s > 0 ? 0.17f : 0.0f));
            float x = s * (0.40f + 0.02f * ph), y = -ph * 1.15f;
            float a = 1.0f - ph * 0.8f;
            FillEll(x, y, 0.035f, 0.055f, cCYN, a);
            FillEll(x - 0.01f, y + 0.015f, 0.01f, 0.02f, cWHT, a);
            AddDot(x, y, cCYN, a, 0.04f, 0.2f);
        }
}

// 2 - skull & crossbones: eyes blink, red pupils glow, jaw chatters
static void Skull(float t) {
    SetScl(1.0f);
    float jaw = 0.07f * Pls(t, 1.8f);
    float bph = fmodf(t, 3.4f);
    float openE = bph < 0.18f ? fabsf(bph - 0.09f) / 0.09f : 1.0f;
    Col bone = MkC(236, 228, 205);
    Glow(0, 0.05f, cICE, 3.6f, 0.07f);
    for (int s = -1; s <= 1; s += 2) {
        V2 a = pt2(-0.88f * s, -0.80f), b = pt2(0.88f * s, 0.80f);
        ThickSeg(a, b, 0.42f, bone, 0.92f);
        float dx = (b.x - a.x) * g_k, dy = (b.y - a.y) * g_ky, L = sqrtf(dx * dx + dy * dy);
        float nx = -dy / L, ny = dx / L;
        for (int e = 0; e < 2; e++) {
            V2 en = e ? b : a;
            for (int q = -1; q <= 1; q += 2) FillDisc(en.x + nx * 0.22f * q / g_k, en.y + ny * 0.22f * q / g_ky, 0.26f, bone, 0.92f);
        }
        for (int q = -1; q <= 1; q += 2)
            Line(a.x + nx * 0.19f * q / g_k, a.y + ny * 0.19f * q / g_ky, b.x + nx * 0.19f * q / g_k, b.y + ny * 0.19f * q / g_ky, cICE, 0.7f);
    }
    FillEll(0, 0.14f, 0.50f, 0.52f, bone, 0.97f);
    AddTriU(pt2(-0.32f, -0.18f), pt2(0.32f, -0.18f), pt2(0.29f, -0.52f - jaw), bone, 0.97f);
    AddTriU(pt2(-0.32f, -0.18f), pt2(0.29f, -0.52f - jaw), pt2(-0.29f, -0.52f - jaw), bone, 0.97f);
    Ring(0, 0.14f, 0.50f, 0.52f, cICE, 1.0f, -0.35f, PI1 + 0.35f);
    V2 jp[6] = {pt2(-0.47f, -0.04f), pt2(-0.33f, -0.20f), pt2(-0.29f, -0.52f - jaw), pt2(0.29f, -0.52f - jaw), pt2(0.33f, -0.20f), pt2(0.47f, -0.04f)};
    StrokePts(jp, 6, false, cICE, 1.0f);
    for (int s = -1; s <= 1; s += 2) {
        float ex = s * 0.21f;
        FillEll(ex, 0.06f, 0.15f, 0.17f * openE + 0.01f, cBLK, 1.0f);
        if (openE > 0.55f) {
            float pu = 0.85f + 0.15f * sinf(t * 7.0f + s);
            AddDot(ex, 0.06f, cRED, pu, 0.10f, 0.45f);
            Glow(ex, 0.06f, cRED, 1.0f, 0.22f * pu);
        }
    }
    AddTriU(pt2(0, -0.02f), pt2(-0.07f, -0.20f), pt2(0.07f, -0.20f), cBLK, 1.0f);
    for (int i = -2; i <= 2; i++) ThickSeg(pt2(i * 0.11f, -0.30f), pt2(i * 0.11f, -0.50f - jaw), 0.07f, cBLK, 0.9f);
    ThickSeg(pt2(-0.27f, -0.36f), pt2(0.27f, -0.36f), 0.06f, cBLK, 0.9f);
}

// 3 - spinning faceted star with orbiting twinkles
static void StarSpin(float t) {
    SetScl(1.0f);
    float ang = t * 1.5f + PI1 * 0.5f;
    V2 v[10];
    for (int i = 0; i < 10; i++) { float r = (i & 1) ? 0.40f : 0.95f; float a = ang + i * PI1 / 5.0f; v[i] = pt2(r * cosf(a), r * sinf(a)); }
    Glow(0, 0, cYEL, 3.4f, 0.09f + 0.06f * Pls(t, 1.0f));
    Col dk = MkC(255, 150, 15), lt = MkC(255, 238, 95);
    for (int i = 0; i < 10; i++) {
        float am = ang + (i + 0.5f) * PI1 / 5.0f;
        float sh = 0.5f + 0.5f * cosf(am - 1.0f);
        AddTriU(pt2(0, 0), v[i], v[(i + 1) % 10], Mix(dk, lt, sh), 0.93f);
    }
    StrokePts(v, 10, true, cGLD, 1.0f);
    for (int i = 0; i < 10; i += 2) Line(0, 0, v[i].x, v[i].y, cYEL, 0.45f, 0.04f, 0.2f);
    for (int k = 0; k < 4; k++) {
        float a = -t * 0.7f + k * PI1 * 0.5f;
        float tw = Sat01(sinf(PI2 * (t * 0.8f + k * 0.25f)) * 1.6f + 0.4f);
        Star4(1.08f * cosf(a), 1.08f * sinf(a), 0.10f + 0.05f * tw, t * 1.3f + k, (k & 1) ? cCYN : cWHT, tw);
    }
}

// 4 - crescent moon sticking out from under the car, surrounded by blinking stars
static void MoonStars(float t) {
    SetScl(1.0f);
    const float R = 0.85f, r = 0.80f, d = 0.30f;
    float phi = 0.55f + 0.05f * sinf(t * 0.9f);
    float th = acosf((R * R + d * d - r * r) / (2.0f * R * d));
    V2 c1 = pt2(d * cosf(phi), d * sinf(phi));
    const int N = 48;
    V2 outer[N + 1], inner[N + 1], mid[N + 1];
    float aP = phi + th, aQ = phi - th + PI2;
    for (int i = 0; i <= N; i++) { float a = aP + (aQ - aP) * i / N; outer[i] = pt2(R * cosf(a), R * sinf(a)); }
    float psiP = atan2f(outer[0].y - c1.y, outer[0].x - c1.x), psiQ = atan2f(outer[N].y - c1.y, outer[N].x - c1.x);
    auto rel = [&](float psi) -> float {
        float x = psi - (phi + PI1);
        while (x > PI1) x -= PI2;
        while (x <= -PI1) x += PI2;
        return x;
    };
    float relP = rel(psiP), relQ = rel(psiQ);
    for (int i = 0; i <= N; i++) {
        float a = phi + PI1 + relP + (relQ - relP) * i / N;
        inner[i] = pt2(c1.x + r * cosf(a), c1.y + r * sinf(a));
        mid[i] = pt2(outer[i].x * 0.55f + inner[i].x * 0.45f, outer[i].y * 0.55f + inner[i].y * 0.45f);
    }
    Col moon = MkC(255, 226, 140);
    float fa = phi + PI1;
    Glow(0.55f * cosf(fa), 0.55f * sinf(fa), moon, 2.8f, 0.09f + 0.05f * Pls(t, 0.5f));
    FillStrip(outer, inner, N + 1, moon, 0.93f);
    FillStrip(outer, mid, N + 1, Mix(moon, cWHT, 0.35f), 0.55f);
    StrokePts(outer, N + 1, false, MkC(255, 215, 100), 1.0f);
    StrokePts(inner, N + 1, false, MkC(255, 240, 175), 0.9f);
    float cr[3][3] = {{0.62f, 0.35f, 0.06f}, {0.66f, -0.40f, 0.045f}, {0.55f, 0.02f, 0.035f}};
    for (int i = 0; i < 3; i++)
        FillEll(cr[i][0] * cosf(fa + cr[i][1]), cr[i][0] * sinf(fa + cr[i][1]), cr[i][2], cr[i][2], MkC(225, 180, 85), 0.6f);
    static const float sx[7] = {0.62f, 0.18f, 0.98f, 0.45f, -0.85f, 0.88f, -0.30f};
    static const float sy[7] = {0.28f, 0.78f, -0.18f, -0.58f, 0.70f, 0.80f, -0.92f};
    static const float ss[7] = {0.15f, 0.10f, 0.11f, 0.13f, 0.12f, 0.09f, 0.10f};
    for (int i = 0; i < 7; i++) {
        float tw = Sat01(sinf(PI2 * (t * (0.45f + 0.13f * i) + i * 0.37f)) * 1.8f + 0.35f);
        if (tw < 0.03f) continue;
        Col sc = (i % 3 == 0) ? cWHT : ((i % 3 == 1) ? cCYN : cYEL);
        Star5(sx[i], sy[i], ss[i] * (0.8f + 0.4f * tw), t * 0.5f + i, sc, tw, true);
        Glow(sx[i], sy[i], sc, 0.9f, 0.15f * tw);
    }
}

// 5 - galloping horse (legs animated, rainbow mane + tail, speed lines)
static void Horse(float t) {
    SetScl(1.0f);
    float w = t * 8.0f;
    float by = 0.03f * sinf(w * 2.0f);
    Col brown = MkC(170, 92, 36), dark = MkC(118, 60, 22), tan = MkC(224, 168, 108), hoof = MkC(50, 30, 15);
    struct Leg { float ax, ay, ph; bool isFar; };
    Leg legs[4] = {{0.20f, -0.12f, 0.9f, true}, {-0.48f, -0.10f, PI1 + 0.9f, true}, {0.30f, -0.10f, 0.0f, false}, {-0.36f, -0.08f, PI1, false}};
    auto drawLeg = [&](const Leg& L) {
        float th = 0.75f * sinf(w + L.ph);
        float fold = 0.95f * Sat01(cosf(w + L.ph));
        V2 A = pt2(L.ax, L.ay + by);
        V2 K = pt2(A.x + 0.30f * sinf(th), A.y - 0.30f * cosf(th));
        float tl = th - fold;
        V2 F = pt2(K.x + 0.33f * sinf(tl), K.y - 0.33f * cosf(tl));
        Col lc = L.isFar ? dark : brown;
        float ni = L.isFar ? 0.5f : 0.9f;
        FillDisc(A.x, A.y, 0.23f, lc, 0.97f);
        ThickSeg(A, K, 0.46f, lc, 0.97f);
        FillDisc(K.x, K.y, 0.23f, lc, 0.97f);
        ThickSeg(K, F, 0.32f, lc, 0.97f);
        FillDisc(F.x, F.y, 0.20f, hoof, 0.98f);
        Line(A.x, A.y, K.x, K.y, cORG, ni);
        Line(K.x, K.y, F.x, F.y, cORG, ni);
    };
    Glow(0, 0.1f, cORG, 3.6f, 0.05f);
    for (int i = 0; i < 4; i++) if (legs[i].isFar) drawLeg(legs[i]);
    FillEll(-0.05f, 0.05f + by, 0.52f, 0.22f, brown, 0.97f);
    FillEll(0.26f, 0.09f + by, 0.24f, 0.22f, brown, 0.97f);
    ThickSeg(pt2(0.32f, 0.12f + by), pt2(0.60f, 0.55f + by), 0.85f, brown, 0.97f);
    FillDisc(0.60f, 0.58f + by, 0.32f, brown, 0.97f);
    ThickSeg(pt2(0.60f, 0.58f + by), pt2(0.84f, 0.43f + by), 0.64f, brown, 0.97f);
    ThickSeg(pt2(0.80f, 0.46f + by), pt2(0.94f, 0.34f + by), 0.50f, tan, 0.97f);
    FillDisc(0.94f, 0.34f + by, 0.25f, tan, 0.97f);
    AddTriU(pt2(0.56f, 0.66f + by), pt2(0.59f, 0.84f + by), pt2(0.68f, 0.67f + by), dark, 0.97f);
    FillDisc(0.70f, 0.55f + by, 0.09f, cBLK, 1.0f);
    AddDot(0.705f, 0.555f + by, cWHT, 0.8f, 0.025f, 0.08f);
    FillDisc(0.935f, 0.345f + by, 0.06f, hoof, 1.0f);
    Ring(-0.05f, 0.05f + by, 0.52f, 0.22f, cORG, 0.9f, 1.4f, PI2 + 0.7f);
    Line(0.395f, 0.075f + by, 0.675f, 0.52f + by, cORG, 0.9f);
    Line(0.60f, 0.645f + by, 0.84f, 0.495f + by, cORG, 0.9f);
    for (int i = 0; i < 4; i++) if (!legs[i].isFar) drawLeg(legs[i]);
    for (int st = 0; st < 3; st++)
        for (int i = 0; i < 26; i++) {
            float s = i / 25.0f;
            float bx = 0.31f + 0.25f * s, byy = 0.17f + 0.55f * s + by;
            float off = -0.075f - 0.05f * sinf(w * 1.5f - s * 5.0f) * s - st * 0.035f;
            AddDot(bx + off, byy + 0.02f * st, HsvC(t * 0.25f + s * 0.7f + st * 0.05f), 0.9f, 0.045f, 0.22f);
        }
    for (int st = 0; st < 3; st++)
        for (int i = 0; i < 28; i++) {
            float s = i / 27.0f;
            float tx = -0.55f - 0.42f * s - st * 0.02f;
            float ty = 0.10f + by - 0.28f * s * s + 0.09f * sinf(w * 1.2f - s * 6.0f) * s + st * 0.03f;
            AddDot(tx, ty, HsvC(t * 0.25f + 0.5f + s * 0.7f), 0.9f, 0.045f, 0.22f);
        }
    for (int k = 0; k < 3; k++) {
        float ph = Saw(t * 1.5f + k * 0.3f);
        float x = 0.2f - ph * 1.4f, y = 0.30f - 0.16f * k;
        float a = sinf(ph * PI1);
        Line(x, y, x - 0.18f, y, cCYN, a, 0.03f, 0.15f);
    }
}

// 6 - beating heart with rising mini hearts
static void HeartBeat(float t) {
    float bp = fmodf(t, 0.9f) / 0.9f;
    float beat = expf(-powf((bp - 0.08f) * 9.0f, 2.0f)) + 0.6f * expf(-powf((bp - 0.28f) * 9.0f, 2.0f));
    SetScl(1.0f + 0.14f * beat);
    V2 h[80], hi[80];
    HeartPts(h, 80, 0, 0.15f, 1.0f);
    HeartPts(hi, 80, 0, 0.12f, 0.72f);
    Glow(0, 0.1f, cRED, 3.8f, 0.08f + 0.08f * beat);
    FillFan(pt2(0, 0.10f), h, 80, true, MkC(215, 20, 60), 0.92f);
    FillFan(pt2(0, 0.10f), hi, 80, true, MkC(255, 70, 105), 0.65f);
    FillEll(-0.45f, 0.52f, 0.13f, 0.07f, MkC(255, 205, 225), 0.75f);
    StrokePts(h, 80, true, cPNK, 1.0f);
    V2 h2[80]; HeartPts(h2, 80, 0, 0.14f, 0.86f);
    StrokePts(h2, 80, true, cRED, 0.6f, 0.04f, 0.2f);
    for (int k = 0; k < 5; k++) {
        float s = Saw(t * 0.35f + k * 0.2f);
        float x = (k - 2) * 0.42f + 0.1f * sinf(t * 2.0f + k);
        float y = -0.9f + s * 2.0f;
        float a = sinf(s * PI1);
        V2 mh[24]; HeartPts(mh, 24, x, y + 0.06f, 0.10f + 0.03f * (k & 1));
        FillFan(pt2(x, y + 0.04f), mh, 24, true, cPNK, a * 0.9f);
        AddDot(x, y, cPNK, a, 0.04f, 0.22f);
    }
}

// 7 - lightning bolt: flashing, with jagged electric arcs
static void Bolt(float t) {
    SetScl(1.0f);
    int tk = (int)(t * 16.0f);
    float fl = Hash(tk, 3) > 0.72f ? 1.0f : 0.62f;
    V2 b[7] = {pt2(0.22f, 1.00f), pt2(-0.45f, 0.05f), pt2(-0.08f, 0.05f), pt2(-0.30f, -1.00f), pt2(0.48f, -0.15f), pt2(0.08f, -0.15f), pt2(0.45f, 0.55f)};
    V2 bi[7];
    for (int i = 0; i < 7; i++) bi[i] = pt2(b[i].x * 0.58f, b[i].y * 0.58f);
    Glow(0, 0, MkC(60, 150, 255), 3.6f, 0.12f * fl);
    FillPoly(b, 7, cYEL, 0.95f);
    FillPoly(bi, 7, MkC(255, 250, 205), 0.9f);
    StrokePts(b, 7, true, MkC(255, 235, 90), fl, 0.05f, 0.30f);
    int idxs[4] = {0, 1, 3, 4};
    for (int k = 0; k < 6; k++) {
        if (Hash(tk, 40 + k) < 0.45f) continue;
        V2 s0 = b[idxs[k % 4]];
        float ang = Hash(tk, 70 + k) * PI2;
        V2 arc[6];
        for (int j = 0; j < 6; j++)
            arc[j] = pt2(s0.x + cosf(ang) * j * 0.11f + (Hash(tk, 80 + k * 7 + j) - 0.5f) * 0.10f,
                         s0.y + sinf(ang) * j * 0.11f + (Hash(tk, 120 + k * 7 + j) - 0.5f) * 0.10f);
        StrokePts(arc, 6, false, MkC(170, 230, 255), 1.0f, 0.035f, 0.2f);
    }
}

// flame helper
static void Flame(float cx, float baseY, float W, float H, float seed, float t, Col col, float al, bool stroke) {
    const int K = 14;
    V2 left[K + 1], right[K + 1], pts[2 * K + 2];
    for (int i = 0; i <= K; i++) {
        float h = (float)i / K;
        float hw = h < 0.30f ? W * sqrtf(fmaxf(0.0f, 1.0f - ((h - 0.30f) / 0.30f) * ((h - 0.30f) / 0.30f)))
                             : W * powf(1.0f - (h - 0.30f) / 0.70f, 1.25f);
        float sx = 0.30f * (W + 0.15f) * sinf(t * 9.0f + seed + h * 5.0f) * h * h;
        float y = baseY + H * h;
        left[i] = pt2(cx + sx - hw, y);
        right[i] = pt2(cx + sx + hw, y);
    }
    int n = 0;
    for (int i = 0; i <= K; i++) pts[n++] = left[i];
    for (int i = K - 1; i >= 0; i--) pts[n++] = right[i];
    FillFan(pt2(cx, baseY + H * 0.30f), pts, n, true, col, al);
    if (stroke) StrokePts(pts, n, true, MkC(255, 120, 25), 1.0f, 0.05f, 0.28f);
}
// 8 - flickering fire with rising embers
static void Fire(float t) {
    SetScl(1.0f);
    float fk = 1.0f + 0.07f * sinf(t * 13.0f);
    Glow(0, -0.1f, cORG, 3.8f, 0.10f + 0.05f * Pls(t, 3.0f));
    Flame(-0.42f, -0.78f, 0.17f, 0.85f * fk, 1.7f, t, cRED, 0.9f, false);
    Flame(0.42f, -0.78f, 0.17f, 0.80f * fk, 3.1f, t, cRED, 0.9f, false);
    Flame(0, -0.90f, 0.50f, 1.70f * fk, 0.0f, t, MkC(255, 60, 10), 0.93f, true);
    Flame(0, -0.88f, 0.37f, 1.30f * fk, 0.9f, t, MkC(255, 150, 10), 0.95f, false);
    Flame(0, -0.85f, 0.23f, 0.85f * fk, 2.0f, t, MkC(255, 235, 90), 0.97f, false);
    Flame(0, -0.82f, 0.11f, 0.45f * fk, 2.6f, t, MkC(255, 255, 210), 0.98f, false);
    for (int k = 0; k < 9; k++) {
        float ph = Saw(t * 0.55f + k * 0.111f);
        float ex = 0.45f * sinf(k * 2.3f + t * 2.0f) * (0.3f + ph);
        float ey = -0.4f + ph * 1.3f;
        AddDot(ex, ey, Mix(cYEL, cORG, ph), 1.0f - ph, 0.035f, 0.15f);
    }
}

// 9 - ghost: floats, blinks, mouth "oooh", waving arms, wavy hem
static void Ghost(float t) {
    SetScl(1.0f);
    float dy = 0.05f * sinf(t * 2.2f);
    float fl = 0.86f + 0.14f * sinf(t * 17.0f);
    float bph = fmodf(t, 3.0f);
    float blink = bph < 0.16f ? fabsf(bph - 0.08f) / 0.08f : 1.0f;
    Col body = MkC(222, 238, 255), inkC = MkC(40, 25, 80);
    V2 pts[60]; int n = 0;
    for (int i = 0; i <= 16; i++) { float a = PI1 * i / 16.0f; pts[n++] = pt2(0.56f * cosf(a), 0.15f + dy + 0.56f * sinf(a)); }
    for (int i = 0; i <= 24; i++) { float x = -0.56f + 1.12f * i / 24.0f; pts[n++] = pt2(x, -0.60f + dy + 0.09f * sinf(x * 13.0f + t * 5.0f)); }
    Glow(0, 0.1f, cCYN, 3.2f, 0.09f * fl);
    for (int s = -1; s <= 1; s += 2)
        FillEll(s * 0.64f, -0.10f + dy + 0.09f * sinf(t * 4.0f + (s > 0 ? PI1 : 0.0f)), 0.10f, 0.07f, body, 0.8f * fl);
    FillFan(pt2(0, -0.1f + dy), pts, n, true, body, 0.80f * fl);
    StrokePts(pts, n, true, MkC(90, 230, 255), fl);
    FillEll(-0.34f, 0.02f + dy, 0.07f, 0.045f, MkC(255, 140, 180), 0.6f);
    FillEll(0.34f, 0.02f + dy, 0.07f, 0.045f, MkC(255, 140, 180), 0.6f);
    for (int s = -1; s <= 1; s += 2) {
        FillEll(s * 0.20f, 0.22f + dy, 0.08f, 0.02f + 0.11f * blink, inkC, 0.97f);
        if (blink > 0.5f) FillEll(s * 0.20f + 0.02f, 0.26f + dy, 0.025f, 0.035f, cWHT, 0.95f);
    }
    float mo = Pls(t, 1.2f);
    FillEll(0, -0.10f + dy, 0.07f + 0.03f * mo, 0.08f + 0.07f * mo, inkC, 0.95f);
    static const float gx[3] = {0.82f, -0.88f, 0.72f}, gy[3] = {0.70f, 0.30f, -0.72f};
    for (int i = 0; i < 3; i++) {
        float tw = Sat01(sinf(PI2 * (t * 0.7f + i * 0.33f)) * 1.8f + 0.3f);
        if (tw > 0.03f) Star4(gx[i], gy[i], 0.12f, t + i, cCYN, tw);
    }
}

// 10 - crown: gems pulse, pearls glow, sparkles blink
static void Crown(float t) {
    SetScl(1.0f);
    V2 c[7] = {pt2(-0.78f, -0.50f), pt2(-0.84f, 0.42f), pt2(-0.42f, -0.02f), pt2(0.0f, 0.72f), pt2(0.42f, -0.02f), pt2(0.84f, 0.42f), pt2(0.78f, -0.50f)};
    Glow(0, 0.1f, cGLD, 3.6f, 0.08f + 0.04f * Pls(t, 1.0f));
    FillPoly(c, 7, cGLD, 0.95f);
    AddTriU(pt2(-0.79f, -0.50f), pt2(0.79f, -0.50f), pt2(0.78f, -0.22f), MkC(220, 150, 10), 0.95f);
    AddTriU(pt2(-0.79f, -0.50f), pt2(0.78f, -0.22f), pt2(-0.78f, -0.22f), MkC(220, 150, 10), 0.95f);
    StrokePts(c, 7, true, MkC(255, 170, 10), 1.0f, 0.05f, 0.28f);
    Line(-0.78f, -0.22f, 0.78f, -0.22f, MkC(255, 200, 40), 0.9f);
    struct Gem { float x; Col c; };
    Gem gems[3] = {{0.0f, cRED}, {-0.45f, cBLU}, {0.45f, cGRN}};
    for (int i = 0; i < 3; i++) {
        float pu = 0.6f + 0.4f * Pls(t, 1.2f, i * 0.33f);
        FillDisc(gems[i].x, -0.36f, 0.34f, gems[i].c, 0.97f);
        FillDisc(gems[i].x - 0.02f, -0.33f, 0.12f, Mix(gems[i].c, cWHT, 0.6f), 0.9f);
        Glow(gems[i].x, -0.36f, gems[i].c, 1.1f, 0.22f * pu);
        AddDot(gems[i].x, -0.36f, gems[i].c, pu, 0.08f, 0.3f);
    }
    V2 tips[3] = {c[1], c[3], c[5]};
    for (int i = 0; i < 3; i++) {
        float pu = 0.6f + 0.4f * Pls(t, 1.5f, i * 0.2f);
        FillDisc(tips[i].x, tips[i].y + 0.03f, 0.26f, cWHT, 0.97f);
        Glow(tips[i].x, tips[i].y + 0.03f, cWHT, 1.0f, 0.18f * pu);
    }
    static const float px[4] = {-0.60f, 0.25f, 0.62f, -0.15f}, py[4] = {0.10f, 0.40f, -0.05f, -0.42f};
    for (int k = 0; k < 4; k++) {
        float tw = Sat01(sinf(PI2 * (t * 0.7f + k * 0.27f)) * 2.0f);
        if (tw > 0.03f) Star4(px[k], py[k], 0.13f, 0.0f, cWHT, tw);
    }
}

// =============================== RENDERING ===============================
static IDirect3DTexture9* g_glowTex = nullptr;
static IDirect3DTexture9* g_discTex = nullptr;
static std::vector<Vtx> g_vF, g_vH, g_vC;

static IDirect3DTexture9* MakeTex(IDirect3DDevice9* dev, bool glow) {
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex) return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
    for (int y = 0; y < 64; y++) {
        DWORD* row = (DWORD*)((BYTE*)lr.pBits + y * lr.Pitch);
        for (int x = 0; x < 64; x++) {
            float dx = (x + 0.5f - 32.0f) / 32.0f, dy = (y + 0.5f - 32.0f) / 32.0f;
            float r = sqrtf(dx * dx + dy * dy), a;
            if (glow) a = expf(-5.0f * r * r) * (1.0f - Sat01((r - 0.85f) / 0.15f));
            else a = 1.0f - Sat01((r - 0.55f) / 0.45f);
            row[x] = ((DWORD)(Sat01(a) * 255.0f) << 24) | 0x00FFFFFF;
        }
    }
    tex->UnlockRect(0);
    return tex;
}

static void QuadOut(std::vector<Vtx>& out, float cx, float cy, float cz, float rx, float ry, float fx, float fy, float rad, DWORD col) {
    float ax = rx * rad, ay = ry * rad, bx = fx * rad, by = fy * rad;
    Vtx v0 = {cx - ax - bx, cy - ay - by, cz, col, 0.0f, 1.0f};
    Vtx v1 = {cx + ax - bx, cy + ay - by, cz, col, 1.0f, 1.0f};
    Vtx v2 = {cx + ax + bx, cy + ay + by, cz, col, 1.0f, 0.0f};
    Vtx v3 = {cx - ax + bx, cy - ay + by, cz, col, 0.0f, 0.0f};
    out.push_back(v0); out.push_back(v1); out.push_back(v2);
    out.push_back(v0); out.push_back(v2); out.push_back(v3);
}

static void FlushBatch(IDirect3DDevice9* dev, std::vector<Vtx>& v) {
    size_t i = 0;
    while (i < v.size()) {
        size_t cnt = v.size() - i;
        if (cnt > 9000) cnt = 9000;
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(cnt / 3), &v[i], sizeof(Vtx));
        i += cnt;
    }
}

static void DrawEmoji() {
    if (!g_enabled) return;
    CVehicle* v = FindPlayerVehicle(-1, false);
    if (!v || !v->m_matrix) return;
    CBaseModelInfo* mi = CModelInfo::GetModelInfo(v->m_nModelIndex);
    if (!mi || !mi->m_pColModel) return;
    CVector mn = mi->m_pColModel->m_boundBox.m_vecMin, mx = mi->m_pColModel->m_boundBox.m_vecMax;
    IDirect3DDevice9* dev = (IDirect3DDevice9*)RwD3D9GetCurrentD3DDevice();
    if (!dev) return;
    if (!g_glowTex) g_glowTex = MakeTex(dev, true);
    if (!g_discTex) g_discTex = MakeTex(dev, false);
    if (!g_glowTex || !g_discTex) return;

    float t = (float)(GetTickCount() % 600000u) / 1000.0f;
    CMatrix* m = v->m_matrix;
    float rx = m->right.x, ry = m->right.y, fx = m->up.x, fy = m->up.y;
    float rl = sqrtf(rx * rx + ry * ry), fln = sqrtf(fx * fx + fy * fy);
    if (rl < 0.0001f || fln < 0.0001f) return;
    rx /= rl; ry /= rl; fx /= fln; fy /= fln;

    float S = fmaxf(4.2f, mx.y * 1.9f) * g_esize;
    g_bk = S; g_bky = S * g_estretch;
    SetScl(1.0f);
    g_dots.clear(); g_tris.clear();
    g_sumR = g_sumG = g_sumB = g_sumW = 0.0f;
    switch (g_emoji) {
    case 0: Smiley(t); break;
    case 1: Crying(t); break;
    case 2: Skull(t); break;
    case 3: StarSpin(t); break;
    case 4: MoonStars(t); break;
    case 5: Horse(t); break;
    case 6: HeartBeat(t); break;
    case 7: Bolt(t); break;
    case 8: Fire(t); break;
    case 9: Ghost(t); break;
    default: Crown(t); break;
    }

    // sample the ground on a coarse grid so the picture hugs the road
    const int GN = 7;
    static float gz[GN][GN];
    float baseZ = m->pos.z + mn.z + 0.05f;
    float exX = g_bk * 1.25f, exY = g_bky * 1.25f;
    for (int j = 0; j < GN; j++)
        for (int i = 0; i < GN; i++) {
            float lx = -exX + 2.0f * exX * i / (GN - 1), ly = -exY + 2.0f * exY * j / (GN - 1);
            float wx = m->pos.x + rx * lx + fx * ly, wy = m->pos.y + ry * lx + fy * ly;
            float z = CWorld::FindGroundZForCoord(wx, wy);
            if (z < baseZ - 3.0f || z > baseZ + 1.1f) z = baseZ;
            gz[j][i] = z;
        }
    auto groundAt = [&](float lx, float ly) -> float {
        float u = (lx / exX * 0.5f + 0.5f) * (GN - 1), q = (ly / exY * 0.5f + 0.5f) * (GN - 1);
        if (u < 0) u = 0; if (u > GN - 1.001f) u = GN - 1.001f;
        if (q < 0) q = 0; if (q > GN - 1.001f) q = GN - 1.001f;
        int i0 = (int)u, j0 = (int)q; float fu = u - i0, fq = q - j0;
        return (gz[j0][i0] * (1 - fu) + gz[j0][i0 + 1] * fu) * (1 - fq) + (gz[j0 + 1][i0] * (1 - fu) + gz[j0 + 1][i0 + 1] * fu) * fq;
    };

    g_vF.clear(); g_vH.clear(); g_vC.clear();
    for (size_t n = 0; n < g_tris.size(); n++) {
        const Tri& tr = g_tris[n];
        DWORD col = D3DCOLOR_ARGB(tr.a, tr.r, tr.g, tr.b);
        for (int k = 0; k < 3; k++) {
            Vtx vx;
            vx.x = m->pos.x + rx * tr.x[k] + fx * tr.y[k];
            vx.y = m->pos.y + ry * tr.x[k] + fy * tr.y[k];
            vx.z = groundAt(tr.x[k], tr.y[k]) + g_eheight;
            vx.col = col; vx.u = 0.0f; vx.v = 0.0f;
            g_vF.push_back(vx);
        }
    }
    for (size_t n = 0; n < g_dots.size(); n++) {
        const Dot& d = g_dots[n];
        float wx = m->pos.x + rx * d.x + fx * d.y, wy = m->pos.y + ry * d.x + fy * d.y;
        float wz = groundAt(d.x, d.y) + g_eheight;
        if (d.ha > 0.002f)
            QuadOut(g_vH, wx, wy, wz, rx, ry, fx, fy, d.hrad, D3DCOLOR_ARGB((int)(Sat01(d.ha * g_ebright) * 255.0f), d.hr, d.hg, d.hb));
        if (d.ca > 0.01f)
            QuadOut(g_vC, wx, wy, wz, rx, ry, fx, fy, d.crad, D3DCOLOR_ARGB((int)(Sat01(d.ca) * 255.0f), d.cr, d.cg, d.cb));
    }

    IDirect3DStateBlock9* sb = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &sb))) sb = nullptr;

    D3DMATRIX id; ZeroMemory(&id, sizeof(id)); id._11 = id._22 = id._33 = id._44 = 1.0f;
    dev->SetTransform(D3DTS_WORLD, &id);
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    // pass 1: colour fills (no texture)
    if (!g_vF.empty()) {
        dev->SetTexture(0, nullptr);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        FlushBatch(dev, g_vF);
    }
    // textured passes
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    // pass 2: additive glow halos
    if (!g_vH.empty()) {
        dev->SetTexture(0, g_glowTex);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        FlushBatch(dev, g_vH);
    }
    // pass 3: bright neon-tube cores (colour-true, crisp)
    if (!g_vC.empty()) {
        dev->SetTexture(0, g_discTex);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        FlushBatch(dev, g_vC);
    }
    if (sb) { sb->Apply(); sb->Release(); }

    // real light on the surroundings, tinted with the shape's own colours
    if (g_sumW > 0.01f) {
        float lr = g_sumR / g_sumW / 255.0f * 0.8f, lg = g_sumG / g_sumW / 255.0f * 0.8f, lb = g_sumB / g_sumW / 255.0f * 0.8f;
        const float lx[4] = {mn.x * 0.8f, mx.x * 0.8f, mx.x * 0.8f, mn.x * 0.8f};
        const float ly[4] = {mx.y * 0.8f, mx.y * 0.8f, mn.y * 0.8f, mn.y * 0.8f};
        for (int i = 0; i < 4; i++) {
            CVector p = m->pos + m->right * lx[i] + m->up * ly[i] + m->at * (mn.z + 0.2f);
            CPointLights::AddLight(0, p, CVector(0, 0, 0), 5.0f, lr, lg, lb, 0, false, nullptr);
        }
    }
}

} // namespace emo

// ---------- glitchy rainbow text (Watch Dogs 2 style) ----------
static ImU32 HsvCol(float h, float s, float v, float a) {
    float r, g, b; ImGui::ColorConvertHSVtoRGB(h - floorf(h), s, v, r, g, b);
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(fminf(fmaxf(a, 0.0f), 1.0f) * 255));
}

// strength 0..1 : how often / how hard it glitches. rainbow=false uses 'solid' colour.
static void GlitchText(const char* text, float size, bool rainbow, ImU32 solid, float strength, int seed, float maxW) {
    ImFont* f = g_fontTitle ? g_fontTitle : ImGui::GetFont();
    float nat = f->FontSize;
    ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    if (ts.x > maxW - 12.0f) { size *= (maxW - 12.0f) / ts.x; ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, text); }
    ImVec2 p = ImGui::GetCursorScreenPos();
    float pad = size * 0.15f;
    ImGui::Dummy(ImVec2(maxW, ts.y + pad * 2.0f));
    p.x += 6.0f; p.y += pad;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = (float)(GetTickCount() % 600000u) / 1000.0f;
    int tick = (int)(t * 16.0f);
    bool burst = Hash(tick / 5 + seed * 31, 7) > (1.0f - 0.30f * strength);

    char buf[96];
    strncpy(buf, text, 95); buf[95] = 0;
    int len = (int)strlen(buf);
    if (burst && len > 0) {
        static const char kGl[] = "#@$%&*?!<>/\\|01XZ";
        for (int k = 0; k < 2; k++) {
            int idx = (int)(Hash(tick, seed * 5 + k) * len) % len;
            if (buf[idx] != ' ') buf[idx] = kGl[(int)(Hash(tick, seed * 9 + k + 40) * 18.0f) % 18];
        }
    }

    auto layer = [&](float ox, float oy, int mode, float alpha) {
        float x = p.x + ox;
        for (const char* s = buf; *s; ++s) {
            float adv = f->GetCharAdvance((ImWchar)(unsigned char)*s) * (size / nat);
            if (*s != ' ') {
                int i = (int)(s - buf);
                ImU32 col;
                if (mode == 1) col = IM_COL32(255, 20, 90, (int)(alpha * 200));
                else if (mode == 2) col = IM_COL32(0, 240, 255, (int)(alpha * 200));
                else if (rainbow) col = HsvCol(t * 0.3f + i * 0.05f, 0.85f, 1.0f, alpha);
                else col = (solid & 0x00FFFFFF) | ((ImU32)(fminf(fmaxf(alpha, 0.0f), 1.0f) * 255.0f) << 24);
                char one[2] = {*s, 0};
                dl->AddText(f, size, ImVec2(x, p.y + oy), col, one, one + 1);
            }
            x += adv;
        }
    };

    // soft neon glow behind the letters
    for (int k = 0; k < 6; k++) layer(cosf(k * 1.047f) * 2.2f, sinf(k * 1.047f) * 2.2f, 0, 0.14f);

    float mainA = 1.0f;
    if (!burst && Hash((int)(t * 40.0f), seed + 3) > 0.97f) mainA = 0.45f;   // flicker
    int bands = burst ? 6 : 1;
    float bh = ts.y / bands;
    for (int b = 0; b < bands; b++) {
        float shift = 0.0f;
        if (burst && Hash(tick, seed * 101 + b * 7) > 0.45f)
            shift = (Hash(tick, seed * 131 + b * 11) - 0.5f) * 2.0f * 16.0f * strength;
        float split = burst ? (3.0f + 3.0f * Hash(tick, seed + 17)) * strength : 1.0f;
        ImVec2 c0(p.x - 30.0f, p.y + b * bh - (b == 0 ? 10.0f : 0.0f));
        ImVec2 c1(p.x + ts.x + 30.0f, p.y + (b + 1) * bh + (b == bands - 1 ? 10.0f : 0.0f));
        dl->PushClipRect(c0, c1, true);
        layer(shift - split, 0.0f, 1, 1.0f);   // red ghost
        layer(shift + split, 0.0f, 2, 1.0f);   // cyan ghost
        layer(shift, 0.0f, 0, mainA);          // main text
        dl->PopClipRect();
    }
    // scanlines + sweeping bar
    for (float yy = p.y; yy < p.y + ts.y; yy += 3.0f) dl->AddLine(ImVec2(p.x - 4.0f, yy), ImVec2(p.x + ts.x + 4.0f, yy), IM_COL32(0, 0, 0, 40));
    float sy = p.y + Saw(t * 0.6f + seed * 0.3f) * ts.y;
    dl->AddRectFilled(ImVec2(p.x - 4.0f, sy), ImVec2(p.x + ts.x + 4.0f, sy + 2.0f), IM_COL32(255, 255, 255, 45));
    // data-corruption blocks
    if (burst) {
        for (int k = 0; k < 5; k++) {
            float bx = p.x + Hash(tick, seed * 17 + k) * ts.x, by = p.y + Hash(tick, seed * 19 + k) * ts.y;
            float bw = 6.0f + Hash(tick, seed * 23 + k) * 28.0f, bhh = 2.0f + Hash(tick, seed * 29 + k) * 4.0f;
            ImU32 col = (k % 3 == 0) ? IM_COL32(0, 240, 255, 170) : ((k % 3 == 1) ? IM_COL32(255, 20, 140, 170) : IM_COL32(255, 255, 255, 140));
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bhh), col);
        }
    }
}

static void RainbowBar(float w) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = (float)(GetTickCount() % 600000u) / 1000.0f;
    const int segs = 48;
    float sw = w / segs;
    for (int i = 0; i < segs; i++)
        dl->AddRectFilled(ImVec2(p.x + i * sw, p.y), ImVec2(p.x + (i + 1) * sw + 1.0f, p.y + 3.0f), HsvCol(t * 0.3f + i * 0.02f, 1.0f, 1.0f, 1.0f));
    ImGui::Dummy(ImVec2(w, 6.0f));
}

// ---------- menu ----------
static const char* kEmojiNames[11] = {
    "Laughing Smiley", "Crying Face", "Skull & Bones", "Spinning Star", "Crescent Moon + Stars", "Galloping Horse",
    "Beating Heart", "Lightning Bolt", "Fire", "Ghost", "Crown"};

static void DrawMenu() {
    ImGui::SetNextWindowSize(ImVec2(480, 800), ImGuiCond_FirstUseEver);
    ImGui::Begin("NEON MENU", &g_open);
    float w = ImGui::GetContentRegionAvail().x;
    GlitchText("Neon menu mod by : Parsa Rsi", 46.0f, true, 0, 1.0f, 1, w);
    RainbowBar(w);
    ImGui::Spacing();
    ImGui::Checkbox("Neon on", &g_enabled);
    ImGui::Separator();

    GlitchText("// COLORS  (normal styles)", 20.0f, false, IM_COL32(0, 235, 255, 255), 0.6f, 2, w);
    ImGui::BeginDisabled(g_mode != 0);
    ImGui::Text("Colors:"); ImGui::SameLine();
    ImGui::RadioButton("1", &g_colorCount, 1); ImGui::SameLine();
    ImGui::RadioButton("2", &g_colorCount, 2); ImGui::SameLine();
    ImGui::RadioButton("3", &g_colorCount, 3);
    if (g_slot >= g_colorCount) g_slot = g_colorCount - 1;
    if (g_colorCount > 1) {
        for (int i = 0; i < g_colorCount; i++) {
            char b[16]; sprintf(b, "Color %d", i + 1);
            if (i) ImGui::SameLine();
            if (ImGui::RadioButton(b, g_slot == i)) g_slot = i;
        }
    }
    ImGui::ColorPicker3("##wheel", g_col[g_slot],
        ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayRGB);
    ImGui::EndDisabled();
    ImGui::Separator();

    GlitchText("// STYLES", 20.0f, false, IM_COL32(255, 60, 200, 255), 0.6f, 3, w);

    // ---- group 1: NORMAL (all the old 250 styles) ----
    bool nrm = (g_mode == 0);
    if (ImGui::Checkbox("NORMAL STYLES", &nrm)) g_mode = 0;
    ImGui::BeginDisabled(g_mode != 0);
    ImGui::Indent();
    ImGui::SliderInt("Style", &g_style, 1, 250);
    if (ImGui::Button("<")) g_style = g_style > 1 ? g_style - 1 : 250;
    ImGui::SameLine();
    if (ImGui::Button(">")) g_style = g_style < 250 ? g_style + 1 : 1;
    ImGui::SameLine();
    ImGui::Text("pattern %d/25  speed %d/10", (g_style - 1) / 10 + 1, (g_style - 1) % 10 + 1);
    ImGui::Unindent();
    ImGui::EndDisabled();
    ImGui::Spacing();

    // ---- group 2: EMOJI ----
    bool emj = (g_mode == 1);
    if (ImGui::Checkbox("EMOJI STYLES  (animated multi-colour neon shapes)", &emj)) g_mode = 1;
    ImGui::BeginDisabled(g_mode != 1);
    ImGui::Indent();
    for (int i = 0; i < 11; i++) {
        if (i & 1) ImGui::SameLine(235.0f);
        if (ImGui::RadioButton(kEmojiNames[i], g_emoji == i)) g_emoji = i;
    }
    ImGui::SliderFloat("Size", &g_esize, 0.6f, 1.6f, "%.2f");
    ImGui::SliderFloat("Stretch", &g_estretch, 0.8f, 2.0f, "%.2f");
    ImGui::SliderFloat("Glow", &g_ebright, 0.4f, 2.5f, "%.2f");
    ImGui::SliderFloat("Height", &g_eheight, 0.03f, 0.60f, "%.2f m");
    ImGui::TextWrapped("Emoji colours are fixed per shape. If part of the picture sinks into the road, raise Height.");
    ImGui::Unindent();
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextWrapped("Get in a car. Type NEON again to close.");
    ImGui::TextWrapped("Mouse: F6/F7 slower/faster (x%.2f), F8 flip up/down (%s)", g_mouseSpeed, g_invertY ? "flipped" : "normal");
    ImGui::End();
}

static LRESULT CALLBACK HookedWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CHAR) {
        g_typed += (char)toupper((int)w);
        if (g_typed.size() > 16) g_typed.erase(0, g_typed.size() - 16);
        if (g_typed.size() >= 4 && g_typed.compare(g_typed.size() - 4, 4, "NEON") == 0) {
            g_open = !g_open; g_typed.clear();
        }
    }
    if (g_open && m == WM_INPUT) {
        RAWINPUT ri;
        UINT sz = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE) {
            if (!(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                g_rawDX += (float)ri.data.mouse.lLastX;
                g_rawDY += (float)ri.data.mouse.lLastY;
            }
            g_rawSeen = true;
        }
    }
    if (g_open) {
        if (m != WM_MOUSEMOVE) ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) return TRUE;
    }
    return CallWindowProc(g_oldProc, h, m, w, l);
}

static bool FileExistsA(const char* p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static ImFont* TryFont(const char* name, float px) {
    char win[MAX_PATH];
    if (!GetWindowsDirectoryA(win, MAX_PATH)) return nullptr;
    char path[MAX_PATH * 2];
    sprintf(path, "%s\\Fonts\\%s", win, name);
    if (!FileExistsA(path)) return nullptr;
    ImFontConfig cfg; cfg.OversampleH = 3; cfg.OversampleV = 2;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, px, &cfg);
}
static void SetupFontsAndStyle() {
    ImGuiIO& io = ImGui::GetIO();
    const char* bodyNames[] = {"bahnschrift.ttf", "segoeui.ttf", "tahoma.ttf", "arial.ttf"};
    for (int i = 0; i < 4 && !g_fontBody; i++) g_fontBody = TryFont(bodyNames[i], 17.0f);
    if (!g_fontBody) g_fontBody = io.Fonts->AddFontDefault();
    const char* titleNames[] = {"impact.ttf", "ariblk.ttf", "bahnschrift.ttf", "arialbd.ttf"};
    for (int i = 0; i < 4 && !g_fontTitle; i++) g_fontTitle = TryFont(titleNames[i], 52.0f);

    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 8.0f; st.FrameRounding = 5.0f; st.GrabRounding = 5.0f; st.WindowBorderSize = 1.0f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.03f, 0.09f, 0.94f);
    c[ImGuiCol_Border] = ImVec4(0.0f, 0.9f, 1.0f, 0.55f);
    c[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.0f, 0.25f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.0f, 0.40f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.08f, 0.22f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.8f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.2f, 0.7f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.5f, 0.85f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.35f, 0.10f, 0.55f, 0.90f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.20f, 0.80f, 1.0f);
}

static void InitImGui() {
    IDirect3DDevice9* dev = (IDirect3DDevice9*)RwD3D9GetCurrentD3DDevice();
    D3DDEVICE_CREATION_PARAMETERS cp; dev->GetCreationParameters(&cp);
    g_hwnd = cp.hFocusWindow;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    SetupFontsAndStyle();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(dev);
    g_oldProc = (WNDPROC)SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
    g_inited = true;
}

static void OnDraw() {
    if (!g_inited) InitImGui();
    ApplyOpenState();
    if (g_mode == 0) DrawNeon(); else emo::DrawEmoji();
    if (!g_open) return;
    ImGui::GetIO().MouseDrawCursor = true;
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    UpdateMouse();
    ImGui::NewFrame();
    DrawMenu();
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

class NeonMenu {
public:
    NeonMenu() {
        Events::drawingEvent += OnDraw;
        Events::d3dLostEvent += [] { if (g_inited) ImGui_ImplDX9_InvalidateDeviceObjects(); };
        Events::d3dResetEvent += [] { if (g_inited) ImGui_ImplDX9_CreateDeviceObjects(); };
    }
} g_neonMenu;
