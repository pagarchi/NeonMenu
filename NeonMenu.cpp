// NeonMenu.asi - GTA San Andreas (PC v1.0 US) neon underglow with ImGui menu
// Type NEON in-game to open/close the menu (mouse cursor appears).
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
#include <d3d9.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
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
            // type 1 = additive (glowing) shadow, soft round texture
            if (glowTex) CShadows::StoreShadowToBeRendered(1, glowTex, &p,
                m->up.x * R, m->up.y * R, m->right.x * R, m->right.y * R,
                150, r8, g8, b8, 4.0f, false, 1.0f, nullptr, false);
            if ((n & 1) == 0) // real light on the surroundings
                CPointLights::AddLight(0, p, CVector(0, 0, 0), 5.0f, rgb[0] * 0.8f, rgb[1] * 0.8f, rgb[2] * 0.8f, 0, false, nullptr);
        }
    }
}

// ---------- menu ----------
static void DrawMenu() {
    ImGui::SetNextWindowSize(ImVec2(380, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("Neon Underglow", &g_open);
    ImGui::Checkbox("Neon on", &g_enabled);
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
    ImGui::Separator();
    ImGui::SliderInt("Style", &g_style, 1, 250);
    if (ImGui::Button("<")) g_style = g_style > 1 ? g_style - 1 : 250;
    ImGui::SameLine();
    if (ImGui::Button(">")) g_style = g_style < 250 ? g_style + 1 : 1;
    ImGui::SameLine();
    ImGui::Text("pattern %d/25  speed %d/10", (g_style - 1) / 10 + 1, (g_style - 1) % 10 + 1);
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

static void InitImGui() {
    IDirect3DDevice9* dev = (IDirect3DDevice9*)RwD3D9GetCurrentD3DDevice();
    D3DDEVICE_CREATION_PARAMETERS cp; dev->GetCreationParameters(&cp);
    g_hwnd = cp.hFocusWindow;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(dev);
    g_oldProc = (WNDPROC)SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
    g_inited = true;
}

static void OnDraw() {
    if (!g_inited) InitImGui();
    ApplyOpenState();
    DrawNeon();
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
