// NeonMenu.asi - GTA San Andreas (PC v1.0 US) neon underglow with ImGui menu
// Type NEON in-game to open/close the menu (mouse cursor appears).
#include "plugin.h"
#include "common.h"
#include "CTimer.h"
#include "CVehicle.h"
#include "CModelInfo.h"
#include "CPointLights.h"
#include "CCoronas.h"
#include <d3d9.h>
#include <cmath>
#include <cstdio>
#include <string>
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

// corner i: 0=front-left 1=front-right 2=rear-right 3=rear-left
static void Eval(int style, float t, int i, float outRGB[3]) {
    int pat = (style - 1) / 10, spd = (style - 1) % 10;
    float ph = t * (0.3f + 0.4f * spd);
    float k = 1.0f, idx = 0.0f;
    bool smooth = false;
    bool left = (i == 0 || i == 3), front = (i < 2);
    switch (pat) {
    case 0: break;
    case 1: k = 0.35f + 0.65f * Tri(ph); break;
    case 2: k = Saw(ph) < 0.5f ? 1.0f : 0.05f; break;
    case 3: k = Saw(ph * 3) < 0.2f ? 1.0f : 0.05f; break;
    case 4: { float d = fabsf(Saw(ph) * 4 - i); d = fminf(d, 4 - d); k = fmaxf(0.1f, 1 - d * 0.7f); } break;
    case 5: { float d = fabsf(Saw(-ph) * 4 - i); d = fminf(d, 4 - d); k = fmaxf(0.1f, 1 - d * 0.7f); } break;
    case 6: k = (front == (Saw(ph) < 0.5f)) ? 1.0f : 0.05f; break;
    case 7: k = (left == (Saw(ph) < 0.5f)) ? 1.0f : 0.05f; break;
    case 8: k = ((i % 2 == 0) == (Saw(ph) < 0.5f)) ? 1.0f : 0.05f; break;
    case 9: k = 0.5f + 0.5f * sinf(6.2832f * (ph + i / 4.0f)); break;
    case 10: { float d = fabsf(Saw(ph) * 4 - i); d = fminf(d, 4 - d); float d2 = fabsf(Saw(ph) * 4 + 2 - i); d2 = fminf(d2, 4 - d2); k = fmaxf(0.1f, 1 - fminf(d, d2) * 0.7f); } break;
    case 11: { float x = Saw(ph); k = (x < 0.1f || (x > 0.2f && x < 0.3f)) ? 1.0f : 0.15f; } break;
    case 12: k = 0.1f + 0.9f * Hash(i, (int)(ph * 8)); break;
    case 13: idx = ph; smooth = true; break;
    case 14: idx = floorf(ph); break;
    case 15: idx = ph + i * 0.25f * g_colorCount; smooth = true; break;
    case 16: idx = (float)i; break;
    case 17: idx = floorf(ph) + i; break;
    case 18: idx = ph; smooth = true; k = 0.35f + 0.65f * Tri(ph * 2); break;
    case 19: idx = floorf(ph * 2); break;
    case 20: idx = floorf(ph * 3); k = Saw(ph * 6) < 0.3f ? 1.0f : 0.05f; break;
    case 21: idx = ph + i * 0.5f; smooth = true; k = 0.5f + 0.5f * sinf(6.2832f * (ph + i / 4.0f)); break;
    case 22: idx = left ? 0.0f : 1.0f; k = (Saw(ph * 2) < 0.5f) == left ? 1.0f : 0.05f; break;
    case 23: idx = floorf(Hash(i, (int)(ph * 6) + 7) * 3); k = 0.1f + 0.9f * Hash(i, (int)(ph * 6)); break;
    case 24: { float r, g, b; ImGui::ColorConvertHSVtoRGB(Saw(ph * 0.5f + i * 0.25f), 1.0f, 1.0f, r, g, b); outRGB[0] = r; outRGB[1] = g; outRGB[2] = b; return; }
    }
    Palette(idx, smooth, outRGB);
    for (int c = 0; c < 3; c++) outRGB[c] *= k;
}

// ---------- neon rendering ----------
static void DrawNeon() {
    if (!g_enabled) return;
    CVehicle* v = FindPlayerVehicle(-1, false);
    if (!v || !v->m_matrix) return;
    CBaseModelInfo* mi = CModelInfo::GetModelInfo(v->m_nModelIndex);
    if (!mi || !mi->m_pColModel) return;
    CVector mn = mi->m_pColModel->m_boundBox.m_vecMin, mx = mi->m_pColModel->m_boundBox.m_vecMax;
    float t = CTimer::m_snTimeInMilliseconds / 1000.0f;
    const float sx[4] = {-1, 1, 1, -1}, sy[4] = {1, 1, -1, -1};
    CMatrix* m = v->m_matrix;
    for (int i = 0; i < 4; i++) {
        float x = sx[i] * mx.x * 0.85f, y = sy[i] * (sy[i] > 0 ? mx.y : -mn.y) * 0.85f, z = mn.z + 0.2f;
        CVector p = m->pos + m->right * x + m->up * y + m->at * z;
        float rgb[3]; Eval(g_style, t, i, rgb);
        CPointLights::AddLight(0, p, CVector(0, 0, 0), 5.0f, rgb[0], rgb[1], rgb[2], 0, false, nullptr);
        CCoronas::RegisterCorona((unsigned)((uintptr_t)v & 0xFFFFFF) + i, nullptr,
            (unsigned char)(rgb[0] * 255), (unsigned char)(rgb[1] * 255), (unsigned char)(rgb[2] * 255), 255,
            p, 0.6f, 80.0f, CORONATYPE_SHINYSTAR, FLARETYPE_NONE, false, false, 0, 0.0f, false, 1.5f, 0, 15.0f, false, false);
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
    ImGui::End();
}

static LRESULT CALLBACK HookedWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CHAR) {
        g_typed += (char)toupper((int)w);
        if (g_typed.size() > 16) g_typed.erase(0, g_typed.size() - 16);
        if (g_typed.size() >= 4 && g_typed.compare(g_typed.size() - 4, 4, "NEON") == 0) {
            g_open = !g_open; g_typed.clear();
            ShowCursor(g_open ? TRUE : FALSE);
        }
    }
    if (g_open) {
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
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
    DrawNeon();
    if (!g_open) return;
    ImGui::GetIO().MouseDrawCursor = true;
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
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
