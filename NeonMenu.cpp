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
        if (L < 1e-5f) continue;
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
            if ((cr > 0) != ccw || fabsf(cr) < 1e
