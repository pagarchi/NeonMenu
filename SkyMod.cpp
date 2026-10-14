// SkyMod for GTA San Andreas (1.0 US) - ASI plugin using plugin-sdk (plugin_sa)
// Build: Visual Studio, x86 Release, plugin-sdk, output extension .asi
// Type the word SKY in game to open/close the menu. Mouse picks items (free cursor, camera frozen).
//
// Mode 1 "Natural": 56 presets of weather/time/wind/fog/clouds (uses game's own sky).
// Mode 2 "Animator": 20 textured sprites in the sky (put PNG->TXD "skyfx.txd" in /SkyMod/)
//                    + 20 Terminal / WatchDogs style text animations.
// NOTE: sprites are drawn as overlay (they are not hidden behind buildings).

#include "plugin.h"
#include "CWeather.h"
#include "CClock.h"
#include "CTimer.h"
#include "CFont.h"
#include "CSprite.h"
#include "CSprite2d.h"
#include "CTxdStore.h"
#include "CCamera.h"
#include "CMessages.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include "CPad.h"
#include <string>
#include <cstring>

using namespace plugin;

// ---------------- Mode 1: natural presets ----------------
struct Preset { const char* name; short wid; int hour; float wind, fog, cloud, rain; int msPerMin; };
// hour -1 = keep current. msPerMin: 1000 = normal, lower = time-lapse (clouds/sun/moon move faster)
static Preset P[] = {
 {"Clear Sunrise",0,6,0.1f,0,0.3f,0,1000},{"Clear Morning",0,9,0.1f,0,0.2f,0,1000},{"Clear Noon",0,12,0.1f,0,0.1f,0,1000},
 {"Clear Afternoon",0,15,0.2f,0,0.2f,0,1000},{"Golden Sunset",0,19,0.2f,0,0.4f,0,1000},{"Clear Dusk",0,20,0.1f,0,0.3f,0,1000},
 {"Starry Night",0,0,0.0f,0,0.0f,0,1000},{"Full Moon Night",0,1,0.0f,0,0.1f,0,1000},{"Late Night Calm",0,3,0.0f,0,0.0f,0,1000},
 {"Pre-Dawn",0,5,0.0f,0,0.1f,0,1000},
 {"Timelapse Day",0,6,0.3f,0,0.5f,0,60},{"Timelapse Night",0,21,0.3f,0,0.3f,0,60},{"Meteor Night (fast sky)",0,23,0.0f,0,0.0f,0,120},
 {"Fast Clouds Day",1,11,0.9f,0,1.0f,0,200},{"Fast Clouds Sunset",1,18,0.9f,0,1.0f,0,200},{"Slow Clouds",1,10,0.1f,0,0.8f,0,2500},
 {"Cloudy Morning",2,8,0.3f,0,0.8f,0,1000},{"Cloudy Noon",2,13,0.3f,0,0.9f,0,1000},{"Overcast Evening",2,18,0.3f,0,1.0f,0,1000},
 {"Overcast Night",2,22,0.2f,0,1.0f,0,1000},
 {"Light Rain",8,12,0.3f,0.1f,0.9f,0.3f,1000},{"Heavy Rain",8,14,0.6f,0.2f,1.0f,1.0f,1000},{"Rain Night",8,23,0.5f,0.2f,1.0f,0.7f,1000},
 {"Rain Sunset",8,19,0.4f,0.1f,0.9f,0.5f,1000},{"Drizzle Morning",8,7,0.2f,0.2f,0.8f,0.2f,1000},
 {"Thunderstorm",16,15,1.0f,0.3f,1.0f,1.0f,1000},{"Night Storm",16,1,1.0f,0.3f,1.0f,1.0f,1000},{"Storm Timelapse",16,12,1.0f,0.3f,1.0f,1.0f,150},
 {"Foggy Morning",9,6,0.0f,1.0f,0.8f,0,1000},{"Thick Fog Noon",9,12,0.0f,1.0f,0.9f,0,1000},{"Fog Night",9,23,0.0f,1.0f,0.8f,0,1000},
 {"Misty Dawn",9,5,0.0f,0.7f,0.5f,0,1000},{"Light Haze",9,16,0.1f,0.4f,0.4f,0,1000},
 {"Sandstorm",19,13,1.0f,0.8f,1.0f,0,1000},{"Sandstorm Sunset",19,18,1.0f,0.8f,1.0f,0,1000},{"Dusty Wind",19,10,0.8f,0.5f,0.7f,0,1000},
 {"Hot Desert Noon",11,12,0.1f,0.1f,0.0f,0,1000},{"Heat Wave",11,14,0.1f,0.2f,0.0f,0,1000},{"Desert Sunset",11,19,0.1f,0.1f,0.1f,0,1000},
 {"Desert Night",11,0,0.0f,0,0.0f,0,1000},{"Desert Sunrise",11,6,0.1f,0.1f,0.1f,0,1000},
 {"Smog Morning",4,8,0.0f,0.5f,0.8f,0,1000},{"Smog Noon",4,12,0.0f,0.6f,0.9f,0,1000},{"Smog Sunset",4,18,0.0f,0.6f,0.8f,0,1000},
 {"Smog Night",4,22,0.0f,0.5f,0.8f,0,1000},
 {"Sunny LS",10,11,0.1f,0,0.1f,0,1000},{"Sunny SF Breeze",3,11,0.5f,0.1f,0.3f,0,1000},{"Windy Clear",0,13,1.0f,0,0.4f,0,1000},
 {"Windy Evening",1,18,1.0f,0,0.7f,0,1000},{"Gale Night",2,2,1.0f,0.1f,0.9f,0,1000},
 {"Pink Evening",0,19,0.1f,0.05f,0.6f,0,1000},{"Blue Hour",0,20,0.1f,0.05f,0.2f,0,1000},{"Midnight Clouds",1,0,0.4f,0,0.8f,0,1000},
 {"Moonlit Rain",8,2,0.3f,0.1f,0.7f,0.4f,1000},{"Rainy Timelapse",8,6,0.5f,0.2f,1.0f,0.6f,120},
};
static const int NP = sizeof(P) / sizeof(P[0]);

// ---------------- Mode 2a: animator sprites ----------------
// texture names inside skyfx.txd (you provide them: soda, ice monkey meme, cartoons, animals...)
struct Spr { const char* name; const char* tex; float size; float speed; float elev; };
static Spr S[] = {
 {"Soda Can","soda",90,0.02f,0.35f},{"Ice Monkey Meme","icemonkey",160,0.0f,0.6f},{"Cartoon Hero 1","toon1",140,0.03f,0.4f},
 {"Cartoon Hero 2","toon2",140,0.03f,0.5f},{"Cartoon Cat","cat",120,0.04f,0.45f},{"Cartoon Dog","dog",120,0.04f,0.4f},
 {"Flying Pig","pig",110,0.05f,0.5f},{"Duck","duck",100,0.06f,0.55f},{"Dancing Banana","banana",100,0.08f,0.35f},
 {"Pizza","pizza",100,0.03f,0.4f},{"Burger","burger",100,0.03f,0.45f},{"Rocket","rocket",90,0.15f,0.5f},
 {"UFO","ufo",120,0.1f,0.6f},{"Giant Face","face",220,0.0f,0.7f},{"Balloon","balloon",90,0.02f,0.5f},
 {"Shark","shark",150,0.05f,0.45f},{"Dragon","dragon",180,0.06f,0.55f},{"Robot","robot",130,0.04f,0.4f},
 {"Ghost","ghost",110,0.03f,0.5f},{"Emoji","emoji",100,0.05f,0.45f},
};
static const int NS = sizeof(S) / sizeof(S[0]);

// ---------------- Mode 2b: terminal / WatchDogs ----------------
static const char* T[] = {
 "Terminal: Matrix Rain","Terminal: Boot Log","Terminal: root@sky:~#","Terminal: Hex Dump","Terminal: Ping Flood",
 "Terminal: Compile Errors","Terminal: ASCII Clouds","Terminal: Sudo Rm Sky","Terminal: Git Log Sky","Terminal: Binary Stars",
 "WD: ctOS Scan","WD: Hack Profiler","WD: Glitch Sky","WD: Data Stream","WD: Traffic Hijack",
 "WD: Blackout Grid","WD: Access Granted","WD: Surveillance Eye","WD: Network Nodes","WD: Terminal+Glitch Mix",
};
static const int NT = sizeof(T) / sizeof(T[0]);

// ---------------- state ----------------
static bool menuOpen = false;
static int level = 0;        // 0 main, 1 natural, 2 animator main, 3 sprites, 4 terminal
static int activeSpr = -1, activeTerm = -1;
static float mx = 0, my = 0; // virtual mouse cursor in pixels
static std::string typed;
static bool lockedPad = false;
static CSprite2d tex[20];
static bool txdLoaded = false;
static int txdSlot = -1;
static int curPreset = -1;


// ---------------- helpers (text + mouse) ----------------
// PrintString takes char* in some plugin-sdk versions and 16-bit chars in others: support both.
template <class F>
static auto PrintImpl(F*, float x, float y, const char* s, int) -> decltype(F::PrintString(x, y, (char*)s), void()) {
    F::PrintString(x, y, (char*)s);
}
template <class F>
static void PrintImpl(F*, float x, float y, const char* s, long) {
    static unsigned short wbuf[256];
    int i = 0;
    for (; s[i] && i < 255; i++) wbuf[i] = (unsigned char)s[i];
    wbuf[i] = 0;
    F::PrintString(x, y, wbuf);
}

// named Txt because Win32 defines a DrawText macro
static void Txt(const char* s, float x, float y, float scale, CRGBA col) {
    float kx = SCREEN_WIDTH / 640.0f, ky = SCREEN_HEIGHT / 448.0f;
    CFont::SetFontStyle(1);
    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetOrientation(static_cast<eFontAlignment>(1));
    CFont::SetWrapx(SCREEN_WIDTH * 2.0f);
    CFont::SetDropShadowPosition(1);
    CFont::SetScale(0.5f * scale * kx, 1.0f * scale * ky);
    CFont::SetColor(col);
    PrintImpl((CFont*)nullptr, x, y, s, 0);
}

// mouse movement: works whatever the field names are in this plugin-sdk version
struct MTag0 {}; struct MTag1 : MTag0 {}; struct MTag2 : MTag1 {}; struct MTag3 : MTag2 {};
template <class S>
static auto MouseDelta(S& s, float& dx, float& dy, MTag3) -> decltype((void)(s.X + s.Y), void()) { dx = (float)s.X; dy = (float)s.Y; }
template <class S>
static auto MouseDelta(S& s, float& dx, float& dy, MTag2) -> decltype((void)(s.x + s.y), void()) { dx = (float)s.x; dy = (float)s.y; }
template <class S>
static auto MouseDelta(S& s, float& dx, float& dy, MTag1) -> decltype((void)(s.m_fX + s.m_fY), void()) { dx = (float)s.m_fX; dy = (float)s.m_fY; }
template <class S>
static void MouseDelta(S&, float& dx, float& dy, MTag0) {
    // fallback: Windows cursor movement
    POINT p; GetCursorPos(&p);
    static POINT last = p;
    dx = (float)(p.x - last.x); dy = (float)(p.y - last.y);
    last = p;
}

static bool Pressed(int vk) {
    static bool prev[256];
    bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool r = now && !prev[vk];
    prev[vk] = now;
    return r;
}

static void LoadTextures() {
    if (txdLoaded) return;
    txdLoaded = true;
    txdSlot = CTxdStore::AddTxdSlot("skyfx");
    CTxdStore::LoadTxd(txdSlot, "SkyMod\\skyfx.txd");
    CTxdStore::AddRef(txdSlot);
    CTxdStore::PushCurrentTxd();
    CTxdStore::SetCurrentTxd(txdSlot);
    for (int i = 0; i < NS; i++) tex[i].SetTexture((char*)S[i].tex);
    CTxdStore::PopCurrentTxd();
}

static void ApplyPreset(const Preset& p) {
    CWeather::ForceWeatherNow(p.wid);
    if (p.hour >= 0) { CClock::ms_nGameClockHours = p.hour; CClock::ms_nGameClockMinutes = 0; }
    CClock::ms_nMillisecondsPerGameMinute = p.msPerMin;
}

static void PerFramePreset(const Preset& p) {
    CWeather::Wind = p.wind;
    CWeather::Foggyness = p.fog;
    CWeather::CloudCoverage = p.cloud;
    CWeather::Rain = p.rain;
    CWeather::WetRoads = p.rain;
}

static bool Project(float az, float elev, float dist, float& x, float& y, float& sc) {
    CVector cam = TheCamera.GetPosition();
    CVector d(cosf(elev) * sinf(az), cosf(elev) * cosf(az), sinf(elev));
    RwV3d in = { cam.x + d.x * dist, cam.y + d.y * dist, cam.z + d.z * dist }, out;
    float w, h;
    if (!CSprite::CalcScreenCoors(in, &out, &w, &h, false, true)) return false;
    x = out.x; y = out.y; sc = w;
    return true;
}

static void DrawSprite(int i, float t) {
    float az = t * S[i].speed, x, y, sc;
    if (!Project(az, S[i].elev, 400.0f, x, y, sc)) return;
    float sz = S[i].size * (SCREEN_HEIGHT / 448.0f);
    CRect r(x - sz, y + sz, x + sz, y - sz);
    tex[i].Draw(r, CRGBA(255, 255, 255, 255));
}

static void DrawTerminal(int m, float t) {
    CRGBA green(0, 255, 70, 255), cyan(0, 200, 255, 255), red(255, 40, 40, 255);
    bool wd = m >= 10;
    CRGBA col = wd ? cyan : green;
    char buf[96];
    static const char* log[] = { "[ OK ] Started Sky Daemon", "[ OK ] Mounted /dev/clouds", "$ sudo rm -rf /sky/*", "0x7FFE 4A 65 6C 6C 6F",
        "git commit -m \"add moon\"", "ping sun.local: 8 minutes", "error: undefined reference to 'rain'", "ACCESS GRANTED", "ctOS: PROFILING...", "NODE 17 -> ONLINE" };
    float k = SCREEN_HEIGHT / 448.0f;
    for (int n = 0; n < 14; n++) {
        float x, y, sc;
        if (!Project(t * 0.01f + n * 0.35f, 0.25f + 0.05f * (n % 5), 300.0f, x, y, sc)) continue;
        if (m == 0 || m == 9) {
            for (int j = 0; j < 6; j++) {
                sprintf(buf, "%d", rand() % 2 ? 1 : 0);
                Txt(buf, x, y + (j * 18 + fmodf(t * 40, 120)) * k, 1.0f, CRGBA(0, 255, 70, 255 - j * 40));
            }
        } else {
            const char* line = log[(n + m) % 10];
            if (m == 10 || m == 11 || m == 16 || m == 17) line = wd ? log[(n % 3) + 7] : line;
            float jitter = (m == 12 || m == 19) && (rand() % 8 == 0) ? (float)(rand() % 30 - 15) : 0;
            Txt(line, x + jitter, y, 1.0f, (m == 12 && rand() % 10 == 0) ? red : col);
        }
    }
    Txt(T[m], 20 * k, 20 * k, 1.0f, col);
}

// ---------------- menu ----------------
static std::vector<const char*> Items() {
    std::vector<const char*> v;
    if (level == 0) { v.push_back("1. Natural Animated Sky"); v.push_back("2. Animator"); v.push_back("Off (restore game sky)"); v.push_back("X Close menu"); }
    else {
        v.push_back("< Back");
        if (level == 1) for (int i = 0; i < NP; i++) v.push_back(P[i].name);
        else if (level == 2) { v.push_back("Sprites (20)"); v.push_back("Terminal / Watch Dogs (20)"); v.push_back("Stop animation"); }
        else if (level == 3) for (int i = 0; i < NS; i++) v.push_back(S[i].name);
        else for (int i = 0; i < NT; i++) v.push_back(T[i]);
    }
    return v;
}

// item rectangle in screen pixels (columns of 14 rows)
static void ItemRect(int i, float& l, float& t, float& r, float& b) {
    float sx = SCREEN_WIDTH / 640.0f, sy = SCREEN_HEIGHT / 448.0f;
    int col = i / 14, row = i % 14;
    l = (30 + col * 150) * sx; t = (80 + row * 24) * sy;
    r = l + 145 * sx;          b = t + 22 * sy;
}

static void Select(int idx) {
    if (level == 0) {
        if (idx == 0) level = 1;
        else if (idx == 1) { level = 2; LoadTextures(); }
        else if (idx == 2) { curPreset = -1; activeSpr = activeTerm = -1; CWeather::ReleaseWeather(); CClock::ms_nMillisecondsPerGameMinute = 1000; }
        else menuOpen = false;
        return;
    }
    if (idx == 0) { level = (level >= 3) ? 2 : 0; return; }
    int k = idx - 1;
    if (level == 1) { curPreset = k; ApplyPreset(P[k]); activeSpr = activeTerm = -1; }
    else if (level == 2) {
        if (k == 0) level = 3; else if (k == 1) level = 4; else activeSpr = activeTerm = -1;
    }
    else if (level == 3) { activeSpr = k; activeTerm = -1; }
    else { activeTerm = k; activeSpr = -1; }
}

static void SetMenu(bool open) {
    menuOpen = open; level = 0;
    CPad* pad = CPad::GetPad(0);
    if (open) { mx = SCREEN_WIDTH / 2; my = SCREEN_HEIGHT / 2; pad->DisablePlayerControls = 1; lockedPad = true; }
    else if (lockedPad) { pad->DisablePlayerControls = 0; TheCamera.m_bUseMouse3rdPerson = true; lockedPad = false; }
}

static void Update() {
    // type "sky" to toggle the menu
    for (int vk = 'A'; vk <= 'Z'; vk++) if (Pressed(vk)) {
        typed += (char)('a' + vk - 'A');
        if (typed.size() > 3) typed.erase(0, typed.size() - 3);
        if (typed == "sky") { typed.clear(); SetMenu(!menuOpen); }
    }
    if (curPreset >= 0) PerFramePreset(P[curPreset]);
    if (!menuOpen) return;

    CPad* pad = CPad::GetPad(0);
    CMouseControllerState& ms = pad->NewMouseControllerState;
    // move our own cursor with raw mouse deltas, then zero them so the camera stays still
    float mdx = 0, mdy = 0;
    MouseDelta(ms, mdx, mdy, MTag3());
    mx += mdx * 1.5f; my += mdy * 1.5f;
    if (mx < 0) mx = 0; if (mx > SCREEN_WIDTH) mx = (float)SCREEN_WIDTH;
    if (my < 0) my = 0; if (my > SCREEN_HEIGHT) my = (float)SCREEN_HEIGHT;
    bool click = ms.lmb && !pad->OldMouseControllerState.lmb;
    bool back = ms.rmb && !pad->OldMouseControllerState.rmb;
    memset(&ms, 0, sizeof(ms));
    pad->OldMouseControllerState.lmb = pad->OldMouseControllerState.rmb = 0;
    TheCamera.m_bUseMouse3rdPerson = false;
    pad->DisablePlayerControls = 1;

    if (back) { Select(level == 0 ? 3 : 0); return; }
    if (click) {
        int n = (int)Items().size();
        for (int i = 0; i < n; i++) {
            float l, t, r, b; ItemRect(i, l, t, r, b);
            if (mx >= l && mx <= r && my >= t && my <= b) { Select(i); break; }
        }
    }
}

static void Draw() {
    float t = CTimer::m_snTimeInMilliseconds * 0.001f;
    if (activeSpr >= 0) DrawSprite(activeSpr, t);
    if (activeTerm >= 0) DrawTerminal(activeTerm, t);
    if (!menuOpen) return;

    float sx = SCREEN_WIDTH / 640.0f, sy = SCREEN_HEIGHT / 448.0f;
    CSprite2d::DrawRect(CRect(10 * sx, 440 * sy, 630 * sx, 20 * sy), CRGBA(0, 0, 0, 150)); // panel
    Txt("SKY MOD  (type SKY to close | right click = back)", 30 * sx, 40 * sy, 1.1f, CRGBA(120, 200, 255, 255));
    std::vector<const char*> v = Items();
    for (int i = 0; i < (int)v.size(); i++) {
        float l, tp, r, b; ItemRect(i, l, tp, r, b);
        bool hot = mx >= l && mx <= r && my >= tp && my <= b;
        if (hot) CSprite2d::DrawRect(CRect(l, b, r, tp), CRGBA(255, 200, 0, 90));
        Txt(v[i], l + 4 * sx, tp + 2 * sy, 0.7f, hot ? CRGBA(255, 255, 0, 255) : CRGBA(255, 255, 255, 255));
    }
    // cursor
    float c = 6 * sy;
    CSprite2d::DrawRect(CRect(mx - c, my + c, mx + c, my - c), CRGBA(255, 255, 255, 255));
    CSprite2d::DrawRect(CRect(mx - c + 2, my + c - 2, mx + c - 2, my - c + 2), CRGBA(255, 60, 60, 255));
}

class SkyMod {
public:
    SkyMod() {
        srand(GetTickCount());
        Events::gameProcessEvent += Update;
        Events::drawingEvent += Draw;
    }
} skyMod;
