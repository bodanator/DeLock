#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

static void AutoScrollToBottom();
static void SendToAI(const std::string& imageB64 = "");

#define TIH_HOTKEY_ID   0xA1F0
#define TIH_KILL_ID     0xA1F1
#define TIH_NUKE_ID     0xA1F2
#define TIH_SCAN_ID     0xA1F3
#define TIH_WM_AI_DONE  (WM_APP + 50)
#define TIH_WM_AI_ERR   (WM_APP + 51)
#define TIH_WM_SEND     (WM_APP + 60)

struct KeyBind {
    int         id;
    const char* label;
    UINT        mod;
    UINT        vk;
};

static KeyBind g_binds[] = {
    { TIH_HOTKEY_ID, "Toggle Window",  MOD_CONTROL, VK_OEM_3 },
    { TIH_SCAN_ID,   "Scan Screen",    MOD_CONTROL | MOD_SHIFT, 'S' },
    { TIH_KILL_ID,   "Exit",           0, VK_END },
    { TIH_NUKE_ID,   "Self-Destruct",  MOD_CONTROL | MOD_SHIFT, VK_END },
};
static const int BIND_COUNT = 4;
static bool g_showBinds  = false;
static int  g_listenIdx  = -1;

static std::string VKName(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') { char b[2] = { (char)vk, 0 }; return b; }
    if (vk >= '0' && vk <= '9') { char b[2] = { (char)vk, 0 }; return b; }
    if (vk >= VK_F1 && vk <= VK_F12) { char b[4]; wsprintfA(b, "F%d", vk - VK_F1 + 1); return b; }
    switch (vk) {
        case VK_OEM_3:   return "`";
        case VK_OEM_1:   return ";";
        case VK_OEM_2:   return "/";
        case VK_OEM_4:   return "[";
        case VK_OEM_5:   return "\\";
        case VK_OEM_6:   return "]";
        case VK_OEM_7:   return "'";
        case VK_OEM_PLUS:  return "=";
        case VK_OEM_MINUS: return "-";
        case VK_OEM_COMMA: return ",";
        case VK_OEM_PERIOD:return ".";
        case VK_END:     return "End";
        case VK_HOME:    return "Home";
        case VK_DELETE:  return "Del";
        case VK_INSERT:  return "Ins";
        case VK_ESCAPE:  return "Esc";
        case VK_RETURN:  return "Enter";
        case VK_SPACE:   return "Space";
        case VK_TAB:     return "Tab";
        case VK_BACK:    return "Backspace";
        case VK_PRIOR:   return "PgUp";
        case VK_NEXT:    return "PgDn";
        case VK_UP:      return "Up";
        case VK_DOWN:    return "Down";
        case VK_LEFT:    return "Left";
        case VK_RIGHT:   return "Right";
        default: { char b[8]; wsprintfA(b, "0x%02X", vk); return b; }
    }
}

static std::string BindStr(UINT mod, UINT vk) {
    std::string s;
    if (mod & MOD_CONTROL) s += "Ctrl+";
    if (mod & MOD_SHIFT)   s += "Shift+";
    if (mod & MOD_ALT)     s += "Alt+";
    s += VKName(vk);
    return s;
}

static UINT ParseVK(const std::string& s) {
    if (s.size() == 1) {
        char c = s[0];
        if (c >= 'A' && c <= 'Z') return (UINT)c;
        if (c >= 'a' && c <= 'z') return (UINT)(c - 32);
        if (c >= '0' && c <= '9') return (UINT)c;
        if (c == '`')  return VK_OEM_3;
        if (c == ';')  return VK_OEM_1;
        if (c == '/')  return VK_OEM_2;
        if (c == '[')  return VK_OEM_4;
        if (c == '\\') return VK_OEM_5;
        if (c == ']')  return VK_OEM_6;
        if (c == '\'') return VK_OEM_7;
        if (c == '=')  return VK_OEM_PLUS;
        if (c == '-')  return VK_OEM_MINUS;
        if (c == ',')  return VK_OEM_COMMA;
        if (c == '.')  return VK_OEM_PERIOD;
    }
    if (s == "End")    return VK_END;
    if (s == "Home")   return VK_HOME;
    if (s == "Del")    return VK_DELETE;
    if (s == "Ins")    return VK_INSERT;
    if (s == "Esc")    return VK_ESCAPE;
    if (s == "Enter")  return VK_RETURN;
    if (s == "Space")  return VK_SPACE;
    if (s == "Tab")    return VK_TAB;
    if (s == "Backspace") return VK_BACK;
    if (s == "PgUp")   return VK_PRIOR;
    if (s == "PgDn")   return VK_NEXT;
    if (s == "Up")     return VK_UP;
    if (s == "Down")   return VK_DOWN;
    if (s == "Left")   return VK_LEFT;
    if (s == "Right")  return VK_RIGHT;
    if (s[0] == 'F' && s.size() <= 3) {
        int n = atoi(s.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    return 0;
}

static void ParseBindStr(const std::string& s, UINT& mod, UINT& vk) {
    mod = 0; vk = 0;
    std::string tok;
    std::string last;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == '+') {
            if (!tok.empty()) {
                if (tok == "Ctrl")       mod |= MOD_CONTROL;
                else if (tok == "Shift") mod |= MOD_SHIFT;
                else if (tok == "Alt")   mod |= MOD_ALT;
                else last = tok;
            }
            tok.clear();
        } else tok += s[i];
    }
    if (!last.empty()) vk = ParseVK(last);
}

static std::string g_iniPath;

static void SaveKeybinds() {
    if (g_iniPath.empty()) return;
    for (int i = 0; i < BIND_COUNT; i++) {
        std::string val = BindStr(g_binds[i].mod, g_binds[i].vk);
        WritePrivateProfileStringA("keybinds", g_binds[i].label,
            val.c_str(), g_iniPath.c_str());
    }
}

static const COLORREF C_BG_MAIN     = RGB(33, 33, 33);
static const COLORREF C_BG_SIDEBAR  = RGB(23, 23, 23);
static const COLORREF C_BG_INPUT    = RGB(47, 47, 47);
static const COLORREF C_BG_INPUT_BD = RGB(86, 86, 86);
static const COLORREF C_BG_USER     = RGB(47, 47, 47);
static const COLORREF C_BG_AI       = RGB(33, 33, 33);
static const COLORREF C_TEXT_MAIN   = RGB(236, 236, 236);
static const COLORREF C_TEXT_DIM    = RGB(142, 142, 160);
static const COLORREF C_TEXT_USER   = RGB(236, 236, 236);
static const COLORREF C_TEXT_AI     = RGB(209, 213, 219);
static const COLORREF C_ACCENT      = RGB(25, 195, 125);
static const COLORREF C_BORDER      = RGB(65, 65, 65);
static const COLORREF C_SCROLL_THUMB= RGB(86, 86, 86);
static const COLORREF C_SCROLL_TRACK= RGB(33, 33, 33);
static const COLORREF C_BTN_SEND    = RGB(103, 103, 115);
static const COLORREF C_BTN_SEND_ACT= RGB(25, 195, 125);
static const COLORREF C_BTN_SCAN    = RGB(55, 114, 175);
static const COLORREF C_MAC_CLOSE   = RGB(255, 95, 87);
static const COLORREF C_MAC_MIN     = RGB(255, 189, 46);
static const COLORREF C_MAC_MAX     = RGB(39, 201, 63);
static const COLORREF C_MAC_IDLE    = RGB(75, 75, 75);
static const COLORREF C_SCAN_GLOW   = RGB(16, 163, 127);
static const COLORREF C_SCAN_BG     = RGB(25, 195, 125);
static const COLORREF C_SCAN_BORDER = RGB(20, 160, 105);

static DWORD GetSeed() {
    DWORD s = 0;
    GetVolumeInformationA("C:\\", NULL, 0, &s, NULL, NULL, NULL, 0);
    return s;
}

static std::string MakeClassName() {
    DWORD h = GetSeed();
    const char* t = "TIHost";
    for (const char* p = t; *p; p++) h = h * 31 + *p;
    char b[32];
    wsprintfA(b, "TI%08X", h);
    return b;
}

enum Role { R_USER, R_AI, R_SYS };

struct Msg {
    Role        role;
    std::string text;
    int         measuredH;
};

static HWND              g_hWnd       = NULL;
static HFONT             g_fBody      = NULL;
static HFONT             g_fBold      = NULL;
static HFONT             g_fSmall     = NULL;
static HFONT             g_fLabel     = NULL;
static HBRUSH            g_brBg       = NULL;
static HBRUSH            g_brInput    = NULL;
static bool              g_vis        = false;
static int               g_scrollY    = 0;
static int               g_contentH   = 0;
static std::string       g_apiKey;
static std::string       g_model      = "sol-6.1";
static std::string       g_pendReply;
static bool              g_thinking   = false;
static std::vector<Msg>  g_msgs;
static std::mutex        g_mtx;
static std::string       g_inputBuf;
static int               g_inputPos   = 0;
static bool              g_cursorOn   = true;
static HHOOK             g_hookKb     = NULL;

static const int PW = 460;
static const int PH = 620;
static const int TITLE_H = 44;
static const int INPUT_H = 44;
static const int PAD = 14;
static const int MSG_PAD = 10;
static const int BUBBLE_RAD = 12;
static const int SCROLLBAR_W = 6;

static void RegisterAllHotkeys() {
    for (int i = 0; i < BIND_COUNT; i++)
        RegisterHotKey(g_hWnd, g_binds[i].id, g_binds[i].mod, g_binds[i].vk);
}

static void UnregisterAllHotkeys() {
    for (int i = 0; i < BIND_COUNT; i++)
        UnregisterHotKey(g_hWnd, g_binds[i].id);
}

typedef BOOL(WINAPI* pfnSWDA)(HWND, DWORD);
typedef BOOL(WINAPI* pfnGWDA)(HWND, DWORD*);
static pfnSWDA pSetWDA = nullptr;
static pfnGWDA pGetWDA = nullptr;

static void ResolveAffinityAPI() {
    if (pSetWDA) return;
    HMODULE u = GetModuleHandleA("user32.dll");
    if (!u) return;
    pSetWDA = (pfnSWDA)GetProcAddress(u, "SetWindowDisplayAffinity");
    pGetWDA = (pfnGWDA)GetProcAddress(u, "GetWindowDisplayAffinity");
}

static void ApplyAffinity(HWND hw) {
    ResolveAffinityAPI();
    if (!pSetWDA) return;
    if (pSetWDA(hw, 0x00000011)) {
        if (pGetWDA) {
            DWORD cur = 0;
            pGetWDA(hw, &cur);
            if (cur == 0x00000011) return;
        } else return;
    }
    pSetWDA(hw, 0x00000001);
}

static void LoadCfg() {
    char ep[MAX_PATH];
    GetModuleFileNameA(NULL, ep, MAX_PATH);
    std::string ini(ep);
    auto sl = ini.find_last_of("\\/");
    if (sl != std::string::npos) ini = ini.substr(0, sl + 1);
    ini += "config.ini";
    g_iniPath = ini;

    char v[512];
    GetPrivateProfileStringA("api", "key", "", v, sizeof(v), ini.c_str());
    if (v[0]) g_apiKey = v;

    GetPrivateProfileStringA("api", "model", "sol-6.1", v, sizeof(v), ini.c_str());
    if (v[0]) g_model = v;

    if (g_apiKey.empty()) {
        char* env = nullptr;
        size_t len = 0;
        if (_dupenv_s(&env, &len, "OPENAI_API_KEY") == 0 && env) {
            g_apiKey = env;
            free(env);
        }
    }

    for (int i = 0; i < BIND_COUNT; i++) {
        GetPrivateProfileStringA("keybinds", g_binds[i].label, "",
            v, sizeof(v), ini.c_str());
        if (v[0]) {
            UINT mod = 0, vk = 0;
            ParseBindStr(v, mod, vk);
            if (vk) { g_binds[i].mod = mod; g_binds[i].vk = vk; }
        }
    }
}

static std::string JEsc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) {
                char h[8]; wsprintfA(h, "\\u%04x", (unsigned char)c);
                o += h;
            } else o += c;
        }
    }
    return o;
}

static std::string JExtract(const std::string& j, const char* k) {
    std::string needle = std::string("\"") + k + "\"";
    size_t kp = j.find(needle);
    if (kp == std::string::npos) return "";
    size_t col = j.find(':', kp + needle.size());
    if (col == std::string::npos) return "";
    size_t qs = j.find('"', col + 1);
    if (qs == std::string::npos) return "";
    qs++;
    std::string r;
    for (size_t i = qs; i < j.size(); i++) {
        if (j[i] == '\\' && i + 1 < j.size()) {
            i++;
            switch (j[i]) {
            case '"':  r += '"';  break;
            case '\\': r += '\\'; break;
            case 'n':  r += '\n'; break;
            case 'r':  r += '\r'; break;
            case 't':  r += '\t'; break;
            default:   r += j[i]; break;
            }
        } else if (j[i] == '"') break;
        else r += j[i];
    }
    return r;
}

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string B64Enc(const BYTE* d, size_t len) {
    std::string o;
    o.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)d[i] << 16;
        if (i + 1 < len) n |= (uint32_t)d[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)d[i + 2];
        o += B64[(n >> 18) & 0x3F];
        o += B64[(n >> 12) & 0x3F];
        o += (i + 1 < len) ? B64[(n >> 6) & 0x3F] : '=';
        o += (i + 2 < len) ? B64[n & 0x3F] : '=';
    }
    return o;
}

static bool GetJpegClsid(CLSID* pClsid) {
    UINT num = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&num, &sz);
    if (!sz) return false;
    auto* pi = (Gdiplus::ImageCodecInfo*)malloc(sz);
    if (!pi) return false;
    Gdiplus::GetImageEncoders(num, sz, pi);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(pi[i].MimeType, L"image/jpeg") == 0) {
            *pClsid = pi[i].Clsid;
            free(pi);
            return true;
        }
    }
    free(pi);
    return false;
}

static std::string CaptureScreen() {
    bool was = g_vis;
    if (was) { ShowWindow(g_hWnd, SW_HIDE); Sleep(60); }

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    HDC hScr = GetDC(NULL);
    HDC hMem = CreateCompatibleDC(hScr);
    HBITMAP hBmp = CreateCompatibleBitmap(hScr, w, h);
    SelectObject(hMem, hBmp);
    BitBlt(hMem, 0, 0, w, h, hScr, 0, 0, SRCCOPY);

    if (was) ShowWindow(g_hWnd, SW_SHOWNOACTIVATE);

    Gdiplus::Bitmap bmp(hBmp, NULL);
    CLSID jClsid;
    std::string result;

    if (GetJpegClsid(&jClsid)) {
        Gdiplus::EncoderParameters ep;
        ep.Count = 1;
        ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
        ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1;
        ULONG q = 45;
        ep.Parameter[0].Value = &q;

        IStream* ps = NULL;
        CreateStreamOnHGlobal(NULL, TRUE, &ps);
        if (ps) {
            bmp.Save(ps, &jClsid, &ep);
            HGLOBAL hg = NULL;
            GetHGlobalFromStream(ps, &hg);
            if (hg) {
                SIZE_T sz = GlobalSize(hg);
                void* ptr = GlobalLock(hg);
                if (ptr && sz) result = B64Enc((const BYTE*)ptr, sz);
                GlobalUnlock(hg);
            }
            ps->Release();
        }
    }

    DeleteObject(hBmp);
    DeleteDC(hMem);
    ReleaseDC(NULL, hScr);
    return result;
}

static std::string BuildPayload(const std::string& img) {
    std::string p = "{\"model\":\"" + g_model + "\",\"messages\":[";
    p += "{\"role\":\"system\",\"content\":\"You are a concise, helpful assistant. "
         "Give direct answers. Use short paragraphs. If shown a screenshot of an exam or quiz, "
         "answer the visible questions accurately and concisely.\"},";

    std::lock_guard<std::mutex> lk(g_mtx);
    int start = 0;
    if ((int)g_msgs.size() > 20) start = (int)g_msgs.size() - 20;

    for (int i = start; i < (int)g_msgs.size(); i++) {
        auto& m = g_msgs[i];
        if (m.role == R_SYS) continue;
        const char* r = (m.role == R_USER) ? "user" : "assistant";

        if (m.role == R_USER && !img.empty() && i == (int)g_msgs.size() - 1) {
            p += "{\"role\":\"user\",\"content\":[";
            p += "{\"type\":\"text\",\"text\":\"" + JEsc(m.text) + "\"},";
            p += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
            p += img;
            p += "\"}}]}";
        } else {
            p += "{\"role\":\"" + std::string(r) + "\",";
            p += "\"content\":\"" + JEsc(m.text) + "\"}";
        }
        if (i < (int)g_msgs.size() - 1) p += ",";
    }
    p += "],\"max_completion_tokens\":2048}";
    return p;
}

static std::string CallAPI(const std::string& payload) {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return "[Connection error]";

    HINTERNET hC = WinHttpConnect(hS, L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) { WinHttpCloseHandle(hS); return "[Connection error]"; }

    HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/v1/chat/completions",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return "[Request error]"; }

    std::wstring auth = L"Authorization: Bearer ";
    for (char c : g_apiKey) auth += (wchar_t)c;
    WinHttpAddRequestHeaders(hR, auth.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hR, L"Content-Type: application/json", -1, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (LPVOID)payload.c_str(), (DWORD)payload.size(), (DWORD)payload.size(), 0);

    std::string resp;
    if (ok && WinHttpReceiveResponse(hR, NULL)) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
            std::vector<char> buf(avail + 1, 0);
            DWORD rd = 0;
            WinHttpReadData(hR, buf.data(), avail, &rd);
            resp.append(buf.data(), rd);
        }
    } else {
        resp = "[Request failed]";
    }

    WinHttpCloseHandle(hR);
    WinHttpCloseHandle(hC);
    WinHttpCloseHandle(hS);

    std::string content = JExtract(resp, "content");
    if (content.empty()) {
        std::string err = JExtract(resp, "message");
        if (!err.empty()) return "[Error: " + err + "]";
        if (resp.size() < 300) return "[" + resp + "]";
        return "[Parse error]";
    }
    return content;
}

struct WorkItem { HWND hw; std::string payload; };

static DWORD WINAPI AIThread(LPVOID p) {
    WorkItem* w = (WorkItem*)p;
    std::string reply = CallAPI(w->payload);
    { std::lock_guard<std::mutex> lk(g_mtx); g_pendReply = reply; }
    PostMessage(w->hw, TIH_WM_AI_DONE, 0, 0);
    delete w;
    return 0;
}

static void SendToAI(const std::string& imageB64) {
    if (g_apiKey.empty()) {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_msgs.push_back({ R_AI,
            "No API key. Set OPENAI_API_KEY or add key= to [api] in config.ini", 0 });
        InvalidateRect(g_hWnd, NULL, TRUE);
        return;
    }
    g_thinking = true;
    InvalidateRect(g_hWnd, NULL, TRUE);
    std::string payload = BuildPayload(imageB64);
    WorkItem* w = new WorkItem{ g_hWnd, payload };
    CreateThread(NULL, 0, AIThread, w, 0, NULL);
}

static void FillRoundRect(HDC hdc, int x, int y, int w, int h, int r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, x, y, x + w, y + h, r, r);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static int MeasureWrap(HDC hdc, const char* text, int width) {
    RECT rc = { 0, 0, width, 0 };
    DrawTextA(hdc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    return rc.bottom;
}

static void Paint(HWND hw, HDC hdc) {
    RECT rc;
    GetClientRect(hw, &rc);
    int w = rc.right, h = rc.bottom;

    RECT bgRc = { 0, 0, w, h };
    HBRUSH bgBr = CreateSolidBrush(C_BG_MAIN);
    FillRect(hdc, &bgRc, bgBr);
    DeleteObject(bgBr);

    SetBkMode(hdc, TRANSPARENT);

    RECT titleRc = { 0, 0, w, TITLE_H };
    HBRUSH titleBr = CreateSolidBrush(C_BG_SIDEBAR);
    FillRect(hdc, &titleRc, titleBr);
    DeleteObject(titleBr);

    SelectObject(hdc, g_fBold);
    SetTextColor(hdc, C_TEXT_MAIN);
    RECT titleTxt = { PAD + 8, 0, w - 80, TITLE_H };
    DrawTextA(hdc, "ChatGPT", -1, &titleTxt, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    int infoCX = PAD + 90, infoCY = TITLE_H / 2, infoR = 10;
    {
        HPEN ip = CreatePen(PS_SOLID, 1, C_TEXT_DIM);
        HBRUSH ib = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN oip = (HPEN)SelectObject(hdc, ip);
        HBRUSH oib = (HBRUSH)SelectObject(hdc, ib);
        Ellipse(hdc, infoCX - infoR, infoCY - infoR, infoCX + infoR, infoCY + infoR);
        SelectObject(hdc, oip); SelectObject(hdc, oib);
        DeleteObject(ip);
    }
    SetTextColor(hdc, C_TEXT_DIM);
    SelectObject(hdc, g_fSmall);
    RECT infoTR = { infoCX - infoR, infoCY - infoR, infoCX + infoR, infoCY + infoR };
    DrawTextA(hdc, "i", -1, &infoTR, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    int dotR = 6, dotCY = TITLE_H / 2, dotGap = 20;
    int dotRightX = w - PAD - 12;
    COLORREF dots[3] = { C_MAC_MAX, C_MAC_MIN, C_MAC_CLOSE };
    for (int i = 0; i < 3; i++) {
        int cx = dotRightX - i * dotGap;
        HBRUSH db = CreateSolidBrush(dots[i]);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, db);
        HPEN dp = CreatePen(PS_SOLID, 1, RGB(
            GetRValue(dots[i]) * 3 / 4,
            GetGValue(dots[i]) * 3 / 4,
            GetBValue(dots[i]) * 3 / 4));
        HPEN op = (HPEN)SelectObject(hdc, dp);
        Ellipse(hdc, cx - dotR, dotCY - dotR, cx + dotR, dotCY + dotR);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(db); DeleteObject(dp);
    }

    HPEN sepPen = CreatePen(PS_SOLID, 1, C_BORDER);
    HPEN oldPen = (HPEN)SelectObject(hdc, sepPen);
    MoveToEx(hdc, 0, TITLE_H, NULL);
    LineTo(hdc, w, TITLE_H);

    int inputTop = h - INPUT_H - PAD * 2;
    MoveToEx(hdc, 0, inputTop - 6, NULL);
    LineTo(hdc, w, inputTop - 6);
    SelectObject(hdc, oldPen);
    DeleteObject(sepPen);

    int chatTop = TITLE_H + 2;
    int chatBot = inputTop - 10;
    int chatW = w - PAD * 2 - SCROLLBAR_W - 4;
    int bubbleW = chatW - 20;

    HRGN clip = CreateRectRgn(0, chatTop, w, chatBot);
    SelectClipRgn(hdc, clip);

    int y = chatTop + 8 - g_scrollY;
    g_contentH = 8;

    SelectObject(hdc, g_fBody);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (auto& m : g_msgs) {
            if (m.role == R_SYS) {
                SetTextColor(hdc, C_TEXT_DIM);
                SelectObject(hdc, g_fSmall);
                int th = MeasureWrap(hdc, m.text.c_str(), chatW - 40);
                if (y + th + 16 > chatTop - 50 && y < chatBot + 50) {
                    RECT tr = { PAD + 20, y + 4, PAD + chatW - 20, y + 4 + th };
                    DrawTextA(hdc, m.text.c_str(), -1, &tr, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
                }
                m.measuredH = th + 16;
                SelectObject(hdc, g_fBody);
            }
            else if (m.role == R_USER) {
                int th = MeasureWrap(hdc, m.text.c_str(), bubbleW - 24);
                int bH = th + 16;
                int bW = (std::min)(bubbleW, chatW - 40);
                int bX = w - PAD - SCROLLBAR_W - 8 - bW;
                if (y + bH + MSG_PAD > chatTop - 50 && y < chatBot + 50) {
                    FillRoundRect(hdc, bX, y, bW, bH, BUBBLE_RAD, C_BG_USER);
                    SetTextColor(hdc, C_TEXT_USER);
                    RECT tr = { bX + 12, y + 8, bX + bW - 12, y + 8 + th };
                    DrawTextA(hdc, m.text.c_str(), -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
                }
                m.measuredH = bH + MSG_PAD;
            }
            else {
                int th = MeasureWrap(hdc, m.text.c_str(), bubbleW - 24);
                int bH = th + 16;
                int bX = PAD + 4;
                int bW = (std::min)(bubbleW, chatW - 20);
                if (y + bH + MSG_PAD > chatTop - 50 && y < chatBot + 50) {
                    FillRoundRect(hdc, bX, y + 2, 3, bH - 4, 2, C_ACCENT);
                    SetTextColor(hdc, C_TEXT_AI);
                    RECT tr = { bX + 14, y + 8, bX + bW - 12, y + 8 + th };
                    DrawTextA(hdc, m.text.c_str(), -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
                }
                m.measuredH = bH + MSG_PAD;
            }
            y += m.measuredH;
            g_contentH += m.measuredH;
        }
    }

    if (g_thinking) {
        SetTextColor(hdc, C_TEXT_DIM);
        SelectObject(hdc, g_fSmall);
        int dotX = PAD + 18;
        DWORD tick = GetTickCount();
        char dots[8] = "";
        int nd = ((tick / 400) % 3) + 1;
        for (int i = 0; i < nd; i++) dots[i] = '.';
        dots[nd] = '\0';
        std::string think = std::string("Thinking") + dots;
        RECT tr = { dotX, y + 4, dotX + 200, y + 24 };
        DrawTextA(hdc, think.c_str(), -1, &tr, DT_SINGLELINE | DT_NOPREFIX);
        g_contentH += 28;
        SelectObject(hdc, g_fBody);
        SetTimer(hw, 99, 400, NULL);
    } else {
        KillTimer(hw, 99);
    }

    SelectClipRgn(hdc, NULL);
    DeleteObject(clip);

    int chatViewH = chatBot - chatTop;
    if (g_contentH > chatViewH) {
        int trackH = chatViewH - 8;
        float ratio = (float)chatViewH / (float)g_contentH;
        int thumbH = (std::max)((int)(trackH * ratio), 20);
        int maxScroll = g_contentH - chatViewH;
        float scrollRatio = (maxScroll > 0) ? (float)g_scrollY / (float)maxScroll : 0.0f;
        int thumbY = chatTop + 4 + (int)((trackH - thumbH) * scrollRatio);
        int sbX = w - SCROLLBAR_W - 4;
        FillRoundRect(hdc, sbX, thumbY, SCROLLBAR_W, thumbH, 3, C_SCROLL_THUMB);
    }

    if (g_showBinds) {
        int panelW = 320, rowH = 42;
        int panelH = BIND_COUNT * rowH + 52;
        int px = (w - panelW) / 2;
        int py = (chatTop + chatBot - panelH) / 2;

        FillRoundRect(hdc, px, py, panelW, panelH, 12, RGB(28, 28, 28));
        HPEN bp = CreatePen(PS_SOLID, 1, C_BORDER);
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN obp2 = (HPEN)SelectObject(hdc, bp);
        HBRUSH obb2 = (HBRUSH)SelectObject(hdc, nb);
        RoundRect(hdc, px, py, px + panelW, py + panelH, 12, 12);
        SelectObject(hdc, obp2); SelectObject(hdc, obb2);
        DeleteObject(bp);

        SelectObject(hdc, g_fBold);
        SetTextColor(hdc, C_TEXT_MAIN);
        RECT hdrRc = { px + 16, py + 10, px + panelW - 16, py + 38 };
        DrawTextA(hdc, "Keybinds", -1, &hdrRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        int ry = py + 46;
        for (int i = 0; i < BIND_COUNT; i++) {
            bool listening = (g_listenIdx == i);
            if (i > 0) {
                HPEN sp = CreatePen(PS_SOLID, 1, C_BORDER);
                HPEN osp = (HPEN)SelectObject(hdc, sp);
                MoveToEx(hdc, px + 16, ry - 1, NULL);
                LineTo(hdc, px + panelW - 16, ry - 1);
                SelectObject(hdc, osp);
                DeleteObject(sp);
            }
            SelectObject(hdc, g_fBody);
            SetTextColor(hdc, C_TEXT_MAIN);
            RECT lr = { px + 16, ry, px + 160, ry + rowH };
            DrawTextA(hdc, g_binds[i].label, -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

            std::string ks = listening ? "Press key..." : BindStr(g_binds[i].mod, g_binds[i].vk);
            COLORREF chipBg = listening ? C_ACCENT : RGB(55, 55, 55);
            int chipW = (int)ks.size() * 8 + 20;
            if (chipW < 60) chipW = 60;
            int chipX = px + panelW - 16 - chipW;
            int chipY = ry + (rowH - 24) / 2;
            FillRoundRect(hdc, chipX, chipY, chipW, 24, 6, chipBg);
            SelectObject(hdc, g_fSmall);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT cr = { chipX, chipY, chipX + chipW, chipY + 24 };
            DrawTextA(hdc, ks.c_str(), -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            ry += rowH;
        }
    }

    FillRoundRect(hdc, PAD, inputTop, w - PAD * 2, INPUT_H, 22, C_BG_INPUT);

    HPEN bdPen = CreatePen(PS_SOLID, 1, C_BG_INPUT_BD);
    HBRUSH nullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN obp = (HPEN)SelectObject(hdc, bdPen);
    HBRUSH obb = (HBRUSH)SelectObject(hdc, nullBr);
    RoundRect(hdc, PAD, inputTop, w - PAD, inputTop + INPUT_H, 22, 22);
    SelectObject(hdc, obp);
    SelectObject(hdc, obb);
    DeleteObject(bdPen);

    int scanSz = INPUT_H - 16;
    int scanX = PAD + 8;
    int scanY = inputTop + 8;
    {
        FillRoundRect(hdc, scanX, scanY, scanSz, scanSz, scanSz / 2, C_SCAN_BG);
        SetTextColor(hdc, RGB(255, 255, 255));
        SelectObject(hdc, g_fBody);
        RECT iconRc = { scanX, scanY, scanX + scanSz, scanY + scanSz };
        DrawTextW(hdc, L"\x2316", -1, &iconRc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }

    {
        int editL = scanX + scanSz + 8;
        int editR = w - PAD - 38 - 4;
        int editY = inputTop + 4;
        int editH = INPUT_H - 8;
        SelectObject(hdc, g_fBody);
        if (g_inputBuf.empty()) {
            SetTextColor(hdc, C_TEXT_DIM);
            RECT pr = { editL, editY, editR, editY + editH };
            DrawTextA(hdc, "Message ChatGPT...", -1, &pr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        } else {
            SetTextColor(hdc, C_TEXT_MAIN);
            RECT tr = { editL, editY, editR, editY + editH };
            DrawTextA(hdc, g_inputBuf.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        }
        if (g_vis && g_cursorOn) {
            SIZE sz = {};
            std::string before = g_inputBuf.substr(0, g_inputPos);
            GetTextExtentPoint32A(hdc, before.c_str(), (int)before.size(), &sz);
            int cx = editL + sz.cx;
            int cy1 = editY + 6, cy2 = editY + editH - 6;
            HPEN cp = CreatePen(PS_SOLID, 1, C_TEXT_MAIN);
            HPEN ocp = (HPEN)SelectObject(hdc, cp);
            MoveToEx(hdc, cx, cy1, NULL);
            LineTo(hdc, cx, cy2);
            SelectObject(hdc, ocp);
            DeleteObject(cp);
        }
    }

    bool hasText = !g_inputBuf.empty();
    COLORREF sendClr = hasText ? C_BTN_SEND_ACT : C_BTN_SEND;
    int sendX = w - PAD - 38;
    int sendY = inputTop + 8;
    int sendSz = INPUT_H - 16;
    FillRoundRect(hdc, sendX, sendY, sendSz, sendSz, sendSz / 2, sendClr);
    SetTextColor(hdc, RGB(255, 255, 255));
    SelectObject(hdc, g_fBody);
    RECT arrowRc = { sendX, sendY, sendX + sendSz, sendY + sendSz };
    DrawTextW(hdc, L"\x2191", -1, &arrowRc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

static char VKToChar(UINT vk, bool shift) {
    if (vk >= 'A' && vk <= 'Z') return shift ? (char)vk : (char)(vk + 32);
    if (vk >= '0' && vk <= '9') {
        if (!shift) return (char)vk;
        static const char sh[] = ")!@#$%^&*(";
        return sh[vk - '0'];
    }
    if (!shift) {
        switch (vk) {
            case VK_OEM_3: return '`'; case VK_OEM_1: return ';';
            case VK_OEM_2: return '/'; case VK_OEM_4: return '[';
            case VK_OEM_5: return '\\'; case VK_OEM_6: return ']';
            case VK_OEM_7: return '\''; case VK_OEM_PLUS: return '=';
            case VK_OEM_MINUS: return '-'; case VK_OEM_COMMA: return ',';
            case VK_OEM_PERIOD: return '.'; case VK_SPACE: return ' ';
        }
    } else {
        switch (vk) {
            case VK_OEM_3: return '~'; case VK_OEM_1: return ':';
            case VK_OEM_2: return '?'; case VK_OEM_4: return '{';
            case VK_OEM_5: return '|'; case VK_OEM_6: return '}';
            case VK_OEM_7: return '"'; case VK_OEM_PLUS: return '+';
            case VK_OEM_MINUS: return '_'; case VK_OEM_COMMA: return '<';
            case VK_OEM_PERIOD: return '>'; case VK_SPACE: return ' ';
        }
    }
    return 0;
}

static LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode != HC_ACTION || !g_vis)
        return CallNextHookEx(g_hookKb, nCode, wp, lp);

    KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lp;
    if (wp != WM_KEYDOWN && wp != WM_SYSKEYDOWN)
        return CallNextHookEx(g_hookKb, nCode, wp, lp);

    UINT vk = kb->vkCode;
    bool ctrl  = GetAsyncKeyState(VK_CONTROL) & 0x8000;
    bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    bool alt   = GetAsyncKeyState(VK_MENU) & 0x8000;

    UINT hhMod = 0;
    if (ctrl)  hhMod |= MOD_CONTROL;
    if (shift) hhMod |= MOD_SHIFT;
    if (alt)   hhMod |= MOD_ALT;
    for (int i = 0; i < BIND_COUNT; i++) {
        if (g_binds[i].vk == vk && g_binds[i].mod == hhMod)
            return CallNextHookEx(g_hookKb, nCode, wp, lp);
    }

    if (g_showBinds && g_listenIdx >= 0) {
        if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU ||
            vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LMENU || vk == VK_RMENU)
            return CallNextHookEx(g_hookKb, nCode, wp, lp);
        if (vk == VK_ESCAPE) {
            g_listenIdx = -1;
            InvalidateRect(g_hWnd, NULL, TRUE);
            return 1;
        }
        g_binds[g_listenIdx].mod = hhMod;
        g_binds[g_listenIdx].vk  = vk;
        g_listenIdx = -1;
        SaveKeybinds();
        InvalidateRect(g_hWnd, NULL, TRUE);
        return 1;
    }

    if (g_showBinds) return 1;

    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU ||
        vk == VK_LCONTROL || vk == VK_RCONTROL ||
        vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL ||
        vk == VK_LWIN || vk == VK_RWIN)
        return CallNextHookEx(g_hookKb, nCode, wp, lp);

    if (vk == VK_RETURN && !shift) {
        if (!g_inputBuf.empty() && !g_thinking)
            PostMessage(g_hWnd, TIH_WM_SEND, 0, 0);
        return 1;
    }
    if (vk == VK_BACK) {
        if (g_inputPos > 0 && !g_inputBuf.empty()) {
            g_inputBuf.erase(g_inputPos - 1, 1);
            g_inputPos--;
            InvalidateRect(g_hWnd, NULL, TRUE);
        }
        return 1;
    }
    if (vk == VK_DELETE) {
        if (g_inputPos < (int)g_inputBuf.size()) {
            g_inputBuf.erase(g_inputPos, 1);
            InvalidateRect(g_hWnd, NULL, TRUE);
        }
        return 1;
    }
    if (vk == VK_LEFT)  { if (g_inputPos > 0) { g_inputPos--; InvalidateRect(g_hWnd, NULL, TRUE); } return 1; }
    if (vk == VK_RIGHT) { if (g_inputPos < (int)g_inputBuf.size()) { g_inputPos++; InvalidateRect(g_hWnd, NULL, TRUE); } return 1; }
    if (vk == VK_HOME && !ctrl)  { g_inputPos = 0; InvalidateRect(g_hWnd, NULL, TRUE); return 1; }
    if (vk == VK_END && !ctrl && !shift) { g_inputPos = (int)g_inputBuf.size(); InvalidateRect(g_hWnd, NULL, TRUE); return 1; }

    if (ctrl && vk == 'V') {
        if (OpenClipboard(NULL)) {
            HANDLE h = GetClipboardData(CF_TEXT);
            if (h) {
                char* txt = (char*)GlobalLock(h);
                if (txt) {
                    std::string paste;
                    for (char* p = txt; *p; p++) {
                        if (*p >= 32 && *p < 127) paste += *p;
                    }
                    g_inputBuf.insert(g_inputPos, paste);
                    g_inputPos += (int)paste.size();
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            InvalidateRect(g_hWnd, NULL, TRUE);
        }
        return 1;
    }
    if (ctrl && vk == 'A') { g_inputBuf.clear(); g_inputPos = 0; InvalidateRect(g_hWnd, NULL, TRUE); return 1; }

    if (ctrl || alt) return CallNextHookEx(g_hookKb, nCode, wp, lp);

    char c = VKToChar(vk, shift);
    if (c) {
        g_inputBuf.insert(g_inputPos, 1, c);
        g_inputPos++;
        InvalidateRect(g_hWnd, NULL, TRUE);
        return 1;
    }

    return 1;
}

static void InstallKeyHook() {
    if (!g_hookKb)
        g_hookKb = SetWindowsHookExA(WH_KEYBOARD_LL, LLKeyboardProc, GetModuleHandle(NULL), 0);
}

static void RemoveKeyHook() {
    if (g_hookKb) { UnhookWindowsHookEx(g_hookKb); g_hookKb = NULL; }
}

static bool g_drag = false;
static POINT g_dragPt = {};

static void DoScan() {
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        std::string prompt = g_inputBuf.empty()
            ? "What questions are shown on screen? Answer them."
            : g_inputBuf;
        g_msgs.push_back({ R_USER, "[Scan] " + prompt, 0 });
        g_inputBuf.clear();
        g_inputPos = 0;
    }
    std::string jpeg = CaptureScreen();
    if (jpeg.empty()) {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_msgs.push_back({ R_AI, "Screen capture failed.", 0 });
        InvalidateRect(g_hWnd, NULL, TRUE);
        return;
    }
    AutoScrollToBottom();
    SendToAI(jpeg);
}

static void AutoScrollToBottom() {
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    int inputTop = rc.bottom - INPUT_H - PAD * 2;
    int chatViewH = inputTop - 10 - TITLE_H - 2;
    if (g_contentH > chatViewH)
        g_scrollY = g_contentH - chatViewH + 40;
    else
        g_scrollY = 0;
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hw, 20, &dark, sizeof(dark));
        SetTimer(hw, 101, 530, NULL);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        RECT rc;
        GetClientRect(hw, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        SelectObject(mem, bmp);
        Paint(hw, mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == 99 && g_thinking)
            InvalidateRect(hw, NULL, TRUE);
        if (wp == 100)
            ApplyAffinity(hw);
        if (wp == 101) {
            g_cursorOn = !g_cursorOn;
            if (g_vis) InvalidateRect(hw, NULL, TRUE);
        }
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g_scrollY -= delta / 2;
        if (g_scrollY < 0) g_scrollY = 0;
        RECT rc;
        GetClientRect(hw, &rc);
        int inputTop = rc.bottom - INPUT_H - PAD * 2;
        int chatViewH = inputTop - 10 - TITLE_H - 2;
        int maxScroll = g_contentH - chatViewH;
        if (maxScroll < 0) maxScroll = 0;
        if (g_scrollY > maxScroll) g_scrollY = maxScroll;
        InvalidateRect(hw, NULL, TRUE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lp), y = HIWORD(lp);
        RECT rc;
        GetClientRect(hw, &rc);
        int inputTop = rc.bottom - INPUT_H - PAD * 2;

        int dotRightX = rc.right - PAD - 12, dotGap = 20, dotHitR = 10;
        for (int i = 0; i < 3; i++) {
            int cx = dotRightX - i * dotGap;
            int cy = TITLE_H / 2;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= dotHitR * dotHitR) {
                if (i >= 1) { g_vis = false; ShowWindow(hw, SW_HIDE); }
                return 0;
            }
        }

        {
            int infoCX = PAD + 90, infoCY = TITLE_H / 2, infoHitR = 12;
            int dx = x - infoCX, dy = y - infoCY;
            if (dx * dx + dy * dy <= infoHitR * infoHitR) {
                g_showBinds = !g_showBinds;
                g_listenIdx = -1;
                if (g_showBinds) UnregisterAllHotkeys();
                else RegisterAllHotkeys();
                InvalidateRect(hw, NULL, TRUE);
                return 0;
            }
        }

        if (g_showBinds) {
            int chatTop = TITLE_H + 2;
            int chatBot = inputTop - 10;
            int panelW = 320, rowH = 42;
            int panelH = BIND_COUNT * rowH + 52;
            int px = (rc.right - panelW) / 2;
            int py = (chatTop + chatBot - panelH) / 2;

            if (x >= px && x <= px + panelW && y >= py + 46 && y <= py + panelH) {
                int idx = (y - py - 46) / rowH;
                if (idx >= 0 && idx < BIND_COUNT) {
                    g_listenIdx = idx;
                    InvalidateRect(hw, NULL, TRUE);
                }
                return 0;
            }

            if (x < px || x > px + panelW || y < py || y > py + panelH) {
                g_showBinds = false;
                g_listenIdx = -1;
                RegisterAllHotkeys();
                InvalidateRect(hw, NULL, TRUE);
                return 0;
            }
            return 0;
        }

        int scanSz = INPUT_H - 16;
        int scanBX = PAD + 8, scanBY = inputTop + 8;
        if (x >= scanBX && x <= scanBX + scanSz &&
            y >= scanBY && y <= scanBY + scanSz && !g_thinking) {
            DoScan();
            return 0;
        }

        int sendX = rc.right - PAD - 38;
        if (y >= inputTop + 8 && y <= inputTop + INPUT_H - 8 &&
            x >= sendX && x <= sendX + INPUT_H - 16 && !g_thinking) {
            if (!g_inputBuf.empty()) {
                { std::lock_guard<std::mutex> lk(g_mtx); g_msgs.push_back({ R_USER, g_inputBuf, 0 }); }
                g_inputBuf.clear(); g_inputPos = 0;
                AutoScrollToBottom();
                SendToAI();
            }
            return 0;
        }

        if (y < TITLE_H && x < rc.right - PAD - 12 - 2 * 20 - 14) {
            g_drag = true;
            g_dragPt = { x, y };
            SetCapture(hw);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_drag) {
            POINT pt;
            GetCursorPos(&pt);
            SetWindowPos(hw, NULL, pt.x - g_dragPt.x, pt.y - g_dragPt.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_drag) { g_drag = false; ReleaseCapture(); }
        return 0;

    case TIH_WM_SEND: {
        std::string text;
        { std::lock_guard<std::mutex> lk(g_mtx);
          text = g_inputBuf;
          g_msgs.push_back({ R_USER, text, 0 });
          g_inputBuf.clear(); g_inputPos = 0;
        }
        AutoScrollToBottom();
        SendToAI();
        InvalidateRect(hw, NULL, TRUE);
        return 0;
    }

    case WM_HOTKEY:
        if (wp == TIH_HOTKEY_ID) {
            g_vis = !g_vis;
            if (g_vis) {
                ShowWindow(hw, SW_SHOWNOACTIVATE);
                InstallKeyHook();
            } else {
                ShowWindow(hw, SW_HIDE);
                RemoveKeyHook();
            }
        }
        if (wp == TIH_SCAN_ID) {
            if (!g_vis) {
                g_vis = true;
                ShowWindow(hw, SW_SHOWNOACTIVATE);
                InstallKeyHook();
            }
            if (!g_thinking) DoScan();
        }
        if (wp == TIH_KILL_ID) {
            RemoveKeyHook();
            UnregisterAllHotkeys();
            ExitProcess(0);
        }
        if (wp == TIH_NUKE_ID) {
            RemoveKeyHook();
            UnregisterAllHotkeys();

            char exePath[MAX_PATH] = {};
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            std::string dir(exePath);
            auto sl = dir.find_last_of("\\/");
            std::string folder = (sl != std::string::npos) ? dir.substr(0, sl) : dir;
            std::string ini = folder + "\\config.ini";

            std::string ps = "powershell.exe -NoP -W Hidden -C \"";
            ps += "Start-Sleep 2;";
            ps += "function S($p){";
            ps +=   "if(!(Test-Path $p)){return};";
            ps +=   "$z=(Get-Item $p).Length;";
            ps +=   "if($z -lt 1){$z=1};";
            ps +=   "$b=New-Object byte[] $z;";
            ps +=   "[IO.File]::WriteAllBytes($p,$b);";
            ps +=   "for($i=0;$i -lt $z;$i++){$b[$i]=0xFF};";
            ps +=   "[IO.File]::WriteAllBytes($p,$b);";
            ps +=   "(New-Object Random).NextBytes($b);";
            ps +=   "[IO.File]::WriteAllBytes($p,$b);";
            ps +=   "[IO.File]::WriteAllBytes($p,(New-Object byte[] 0));";
            ps +=   "$n=Join-Path(Split-Path $p)([guid]::NewGuid().ToString());";
            ps +=   "Rename-Item $p $n -Force;";
            ps +=   "Remove-Item $n -Force";
            ps += "};";
            ps += "S '" + std::string(exePath) + "';";
            ps += "S '" + ini + "';";
            ps += "Remove-Item '" + folder + "' -Force -Recurse -ErrorAction SilentlyContinue";
            ps += "\"";

            STARTUPINFOA si = {}; si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            CreateProcessA(NULL, (LPSTR)ps.c_str(), NULL, NULL, FALSE,
                CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);

            SecureZeroMemory(exePath, sizeof(exePath));
            ExitProcess(0);
        }
        return 0;

    case TIH_WM_AI_DONE: {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_msgs.push_back({ R_AI, g_pendReply, 0 });
        g_pendReply.clear();
        g_thinking = false;
        AutoScrollToBottom();
        InvalidateRect(hw, NULL, TRUE);
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hw, NULL, TRUE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hw, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdipTok;
    Gdiplus::GdiplusStartup(&gdipTok, &gsi, NULL);

    LoadCfg();

    g_brBg    = CreateSolidBrush(C_BG_MAIN);
    g_brInput = CreateSolidBrush(C_BG_INPUT);

    g_fBody  = CreateFontA(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_fBold  = CreateFontA(-16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_fSmall = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_fLabel = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    std::string cls = MakeClassName();

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = cls.c_str();
    RegisterClassExA(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        cls.c_str(), "",
        WS_POPUP,
        sx - PW - 20, (sy - PH) / 2,
        PW, PH,
        NULL, NULL, hInst, NULL);

    SetLayeredWindowAttributes(g_hWnd, 0, 255, LWA_ALPHA);

    ShowWindow(g_hWnd, SW_SHOWNOACTIVATE);
    ApplyAffinity(g_hWnd);
    ShowWindow(g_hWnd, SW_HIDE);

    SetTimer(g_hWnd, 100, 3000, NULL);

    RegisterAllHotkeys();

    ShowWindow(g_hWnd, SW_HIDE);
    g_vis = false;

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    RemoveKeyHook();
    UnregisterAllHotkeys();
    DeleteObject(g_brBg);
    DeleteObject(g_brInput);
    DeleteObject(g_fBody);
    DeleteObject(g_fBold);
    DeleteObject(g_fSmall);
    DeleteObject(g_fLabel);
    UnregisterClassA(cls.c_str(), hInst);
    Gdiplus::GdiplusShutdown(gdipTok);
    return 0;
}
