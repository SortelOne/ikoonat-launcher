/*
 * TheXTech - A platform game engine ported from old source code for VB6
 *
 * Copyright (c) 2009-2011 Andrew Spinks, original VB6 code
 * Copyright (c) 2020-2025 Vitaly Novichkov <admin@wohlnet.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_events.h>
#if defined(__APPLE__)
#   include <SDL2/SDL_filesystem.h>
#endif

#include <FreeImageLite.h>

#include <AppPath/app_path.h>
#include <DirManager/dirman.h>
#include <Utils/files.h>
#include <Logger/logger.h>

#include "globals.h"
#include "game_main.h"
#include "main/game_save.h"
#include "sound.h"
#include "gfx.h"
#include "config.h"
#include "controls.h"
#include "screen.h"
#include "message.h"
#include "core/render.h"
#include "draw_planes.h"
#include "graphics.h"
#include "fontman/font_manager.h"
#include "fontman/ttf_font.h"
#include "main/menu_main.h"
#include "main/screen_options.h"
#include "main/screen_quickreconnect.h"
#include "main/screen_connect.h"
#include "main/screen_library.h"
#include "main/episode_library.h"
#include "main/smbx2_mode.h"
#include "main/library_import.h"
#include "main/library_picker.h"

#if !defined(__APPLE__) || defined(XTECH_MACOSX_TIGER)
// no picker on this system yet (Android's comes from the app)
extern "C" int LibraryPicker_available(void) { return 0; }
extern "C" char *LibraryPicker_pickImportSource(void) { return nullptr; }
#endif

namespace LibraryScreen
{

bool g_enabled = true;
bool g_classicMenuOnly = false;
bool g_LoopActive = false;
bool g_classicVisit = false;
bool g_returnRequested = false;


// ---- look ----

struct Theme
{
    const char *name;
    XTColor bg, card, cardHi, text, sub, line;
};

static const Theme s_themes[] =
{
    {"Dark",     {24, 25, 30},    {40, 42, 50},    {54, 57, 68},    {240, 240, 245}, {160, 164, 175}, {70, 72, 82}},
    {"Light",    {236, 236, 240}, {255, 255, 255}, {246, 246, 250}, {25, 25, 30},    {100, 100, 112}, {205, 205, 214}},
    {"Midnight", {12, 16, 32},    {24, 30, 54},    {34, 42, 74},    {235, 238, 255}, {150, 160, 200}, {50, 60, 96}},
};

struct Accent
{
    const char *name;
    XTColor color;
    XTColor onColor; // text on the accent
};

static const Accent s_accents[] =
{
    {"Leaf",  {98, 190, 80},  {16, 30, 12}},
    {"Sky",   {70, 150, 235}, {255, 255, 255}},
    {"Berry", {220, 80, 140}, {255, 255, 255}},
    {"Sun",   {240, 180, 50}, {40, 28, 4}},
};

static const int s_numThemes = (int)(sizeof(s_themes) / sizeof(s_themes[0]));
static const int s_numAccents = (int)(sizeof(s_accents) / sizeof(s_accents[0]));

static int s_theme = 0;
static int s_accent = 0;

static const Theme &T() { return s_themes[s_theme]; }
static const Accent &A() { return s_accents[s_accent]; }

static const char *const s_charNames[] = {"", "Mario", "Luigi", "Peach", "Toad", "Link"};


// ---- episodes ----

struct Card
{
    int world = 0;           // index in SelectWorld
    std::string name;
    std::string author;
    std::string dir;         // episode folder, with a slash
    std::string key;         // library.ini group
    LibraryEpisode_t lib;
    bool smbx2 = false;      // the mode it will play in
    std::string modeWhy;
    std::string picPath;
    std::shared_ptr<StdPicture> pic;
    bool picTried = false;
};

static std::vector<Card> s_cards;     // every episode
static std::vector<int> s_shown;      // indices into s_cards for the current filter

enum Filter { FILTER_ALL = 0, FILTER_SMBX2, FILTER_SMBX, FILTER_RECENT, FILTER_COUNT };
static const char *const s_filterNames[] = {"All", "SMBX2", "SMBX", "Recent"};
static int s_filter = FILTER_ALL;

enum View { VIEW_GRID = 0, VIEW_EPISODE, VIEW_SETTINGS, VIEW_OPTIONS, VIEW_IMPORT };
static View s_view = VIEW_GRID;

// grid focus: the top bar (filters, Import, Settings) or a card
static bool s_inTopBar = false;
static int s_topItem = 0;            // 0..3 filters, 4 Import, 5 Settings
static const int TOP_IMPORT = FILTER_COUNT, TOP_SETTINGS = FILTER_COUNT + 1, TOP_COUNT = FILTER_COUNT + 2;
static int s_sel = 0;                // position in s_shown (s_shown.size() = the "Add episode" card)
static int s_scrollRow = 0;

// episode page
enum EpItem { EP_CONTINUE = 0, EP_SLOT1, EP_SLOT2, EP_SLOT3, EP_MODE, EP_CHARACTER, EP_PLAYERS, EP_COUNT };
static int s_epItem = 0;
static int s_epCard = -1;
static int s_deleteArmed = 0;        // slot waiting for a second Erase press

// settings page
enum SetItem { SET_THEME = 0, SET_ACCENT, SET_OPTIONS, SET_CLASSIC, SET_QUIT, SET_COUNT };
static int s_setItem = 0;

// import
static SDL_Thread *s_importThread = nullptr;
static LibraryImportProgress *s_importProgress = nullptr;
static LibraryImportResult s_importResult;
static std::string s_importSource;
static std::vector<std::string> s_dropped;

// a short message at the bottom
static std::string s_toast;
static int s_toastFrames = 0;

static std::shared_ptr<StdPicture> s_leaf;

// input
static bool s_prevDo = true, s_prevBack = true, s_prevErase = true, s_prevClick = true;
static int s_holdFrames[4] = {0, 0, 0, 0};

// mouse targets from the last frame drawn
struct Hit
{
    int x, y, w, h;
    int kind;   // HIT_*
    int index;
};
enum { HIT_TOP = 0, HIT_CARD, HIT_EPITEM, HIT_SETITEM, HIT_BACK };
static std::vector<Hit> s_hits;

// testing: SMBX2COMPAT_LIBKEYS="60:right,90:do", SMBX2COMPAT_LIBSHOTS="30,120"
static int s_frame = 0;
static std::vector<std::pair<int, std::string>> s_testKeys;
static std::vector<int> s_testShots;
static std::vector<std::pair<int, std::string>> s_testDrops;
static int s_testShotCount = 0;

static void s_toastShow(const std::string &t)
{
    s_toast = t;
    s_toastFrames = 64 * 4;
}


// ---- text ----

// The asset pack's TTF fonts are pixel fonts for one size, so the library
// uses the system's own font (no font to ship or license): San Francisco on
// macOS, Roboto on Android; the asset pack's font if none is found.
static std::unique_ptr<TtfFont> s_font;

static void s_loadFont()
{
    if(s_font)
        return;

    static const char *const candidates[] =
    {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/system/fonts/Roboto-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };

    for(const char *path : candidates)
    {
        if(!Files::fileExists(path))
            continue;

        std::unique_ptr<TtfFont> f(new TtfFont());
        if(f->loadFont(std::string(path)))
        {
            f->setAntiAlias(true);
            s_font = std::move(f);
            pLogInfo("Library: font %s", path);
            return;
        }
    }

    pLogWarning("Library: no system font found; using the asset pack's");
}

static int s_px(int size)
{
    return std::max(10, size * XRender::TargetH / 600);
}

// height of a capital letter above the baseline (the font draws its baseline at y + size)
static int s_capH(int size)
{
    int px = s_px(size);
    if(s_font)
    {
        int top = s_font->getGlyphInfo("H", (uint32_t)px).top;
        if(top > 0)
            return top;
    }
    return px * 7 / 10;
}

// y is where the capital letters start
static void s_text(const std::string &t, int x, int y, int size, XTColor c)
{
    if(t.empty())
        return;

    int px = s_px(size);
    int fy = y + s_capH(size) - px;
    if(s_font)
        s_font->printText(t.c_str(), t.size(), x, fy, c, (uint32_t)px);
    else
        FontManager::printText(t.c_str(), t.size(), x, fy, FontManager::DefaultTTF_Font, c, (uint32_t)px);
}

// centered vertically in a box from y to y + h
static void s_textMid(const std::string &t, int x, int y, int h, int size, XTColor c)
{
    s_text(t, x, y + (h - s_capH(size)) / 2, size, c);
}

static int s_textW(const std::string &t, int size)
{
    if(t.empty())
        return 0;
    if(s_font)
        return s_font->textSize(t.c_str(), t.size(), (uint32_t)s_px(size)).w();
    return FontManager::textSize(t.c_str(), t.size(), FontManager::DefaultTTF_Font, (uint32_t)s_px(size)).w();
}

// cuts a text to fit a width, with "..." (UTF-8 safe: cuts at character starts)
static std::string s_fit(const std::string &t, int maxW, int size)
{
    if(s_textW(t, size) <= maxW)
        return t;

    std::string s = t;
    while(!s.empty())
    {
        s.pop_back();
        while(!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80)
            s.pop_back();
        if(!s.empty())
            s.pop_back();
        std::string c = s + "...";
        if(s_textW(c, size) <= maxW)
            return c;
    }

    return "...";
}

static void s_textCenter(const std::string &t, int cx, int y, int size, XTColor c)
{
    s_text(t, cx - s_textW(t, size) / 2, y, size, c);
}


// ---- pictures ----

static std::string s_thumbDir()
{
    return AppPathManager::settingsRoot() + "library/";
}

static std::string s_thumbPath(const std::string &key)
{
    std::string f;
    for(char c : key)
        f.push_back((std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_');
    return s_thumbDir() + f + ".png";
}

static void s_loadPic(Card &c)
{
    if(c.picTried)
        return;
    c.picTried = true;

    if(!Files::fileExists(c.picPath))
        return;

    c.pic = std::make_shared<StdPicture>();
    XRender::LoadPicture(*c.pic, c.picPath);
    if(!c.pic->inited)
        c.pic.reset();
}

static void s_unloadPics()
{
    for(Card &c : s_cards)
    {
        if(c.pic)
            XRender::unloadTexture(*c.pic);
        c.pic.reset();
        c.picTried = false;
    }
}

// draws a picture filling a rectangle, cropped to its shape
static void s_drawPicCover(StdPicture &p, int x, int y, int w, int h)
{
    int sw = p.w, sh = p.h;
    if(sw <= 0 || sh <= 0)
        return;

    // crop the source to the destination's aspect
    if((long long)sw * h > (long long)sh * w)
    {
        int cw = (int)((long long)sh * w / h);
        XRender::renderTextureScaleEx(x, y, w, h, p, (sw - cw) / 2, 0, cw, sh);
    }
    else
    {
        int ch = (int)((long long)sw * h / w);
        XRender::renderTextureScaleEx(x, y, w, h, p, 0, (sh - ch) / 2, sw, ch);
    }
}


// ---- episode list ----

static void s_applyFilter()
{
    s_shown.clear();
    for(size_t i = 0; i < s_cards.size(); i++)
    {
        const Card &c = s_cards[i];
        if(s_filter == FILTER_SMBX2 && !c.smbx2)
            continue;
        if(s_filter == FILTER_SMBX && c.smbx2)
            continue;
        if(s_filter == FILTER_RECENT && c.lib.lastPlayed <= 0)
            continue;
        s_shown.push_back((int)i);
    }

    if(s_filter == FILTER_RECENT)
    {
        std::stable_sort(s_shown.begin(), s_shown.end(), [](int a, int b)
        {
            return s_cards[a].lib.lastPlayed > s_cards[b].lib.lastPlayed;
        });
    }

    if(s_sel > (int)s_shown.size())
        s_sel = (int)s_shown.size();
}

static void s_buildCards(const std::string &focusDir = std::string())
{
    s_unloadPics();
    s_cards.clear();

    FindWorlds();

    for(int i = 1; i <= NumSelectWorld && i < (int)SelectWorld.size(); i++)
    {
        const SelectWorld_t &w = SelectWorld[i];
        const std::string &path = w.WorldFilePath;
        if(w.disabled || path.empty() || path == "battle" || path[0] == '@')
            continue;

        std::string lower = path;
        for(char &ch : lower)
            ch = (char)std::tolower((unsigned char)ch);
        if(lower.size() < 4 || (lower.compare(lower.size() - 4, 4, ".wld") != 0 && (lower.size() < 5 || lower.compare(lower.size() - 5, 5, ".wldx") != 0)))
            continue;

        Card c;
        c.world = i;
        c.name = w.WorldName.empty() ? Files::basenameNoSuffix(path) : w.WorldName;
        c.author = w.CreatorName;
        c.dir = Files::dirname(path) + "/";
        c.key = Library_key(c.dir);
        Library_get(c.key, c.lib);

        EpisodeMode m = EpisodeMode::Auto;
        if(g_episodeModeOverride != EpisodeMode::Auto)
            m = g_episodeModeOverride;
        else if(!EpisodeMode_parse(c.lib.mode, m) || m == EpisodeMode::Auto)
        {
            if(!EpisodeMode_parse(c.lib.detected, m) || m == EpisodeMode::Auto)
                m = EpisodeMode_detect(c.dir, c.lib.detectedWhy);
        }
        c.smbx2 = (m == EpisodeMode::Smbx2);
        c.picPath = s_thumbPath(c.key);

        s_cards.push_back(c);
    }

    s_applyFilter();

    if(!focusDir.empty())
    {
        for(size_t k = 0; k < s_shown.size(); k++)
        {
            if(s_cards[s_shown[k]].dir == focusDir)
                s_sel = (int)k;
        }
    }
}

static Card *s_selCard()
{
    if(s_sel >= 0 && s_sel < (int)s_shown.size())
        return &s_cards[s_shown[s_sel]];
    return nullptr;
}


// ---- starting an episode ----

static bool s_charAllowed(int ch)
{
    return ch >= 1 && ch <= 5 && (g_forceCharacter || !SelectWorld[selWorld].blockChar[ch]);
}

static int s_nextChar(int ch, int dir)
{
    for(int n = 0; n < 5; n++)
    {
        ch += dir;
        if(ch > 5)
            ch = 1;
        if(ch < 1)
            ch = 5;
        if(s_charAllowed(ch))
            return ch;
    }
    return 1;
}

static void s_startEpisode(Card &c, int slot)
{
    selWorld = c.world;
    selSave = slot;
    FindSaves();

    // gameplay style as TheXTech's menu picks it (menu_main.cpp, s_change_save_item)
    int cfg = SaveSlotInfo[slot].ConfigDefaults;
    int playstyle, speedrun = 0;
    if(cfg > 0)
        playstyle = cfg - 1;
    else if(cfg < 0)
    {
        speedrun = -cfg;
        playstyle = speedrun - 1;
    }
    else
    {
        playstyle = SelectWorld[selWorld].bugfixes_on_by_default ? Config_t::MODE_MODERN : Config_t::MODE_CLASSIC;
        speedrun = g_config.speedrun_mode.m_value;
    }

    g_config.playstyle.m_value = playstyle;
    g_config.playstyle.m_set = ConfigSetLevel::ep_config;
    if(g_config.speedrun_mode.m_set != ConfigSetLevel::cmdline)
    {
        g_config.speedrun_mode.m_value = speedrun;
        g_config.speedrun_mode.m_set = ConfigSetLevel::ep_config;
    }

    for(int a = 1; a <= numCharacters; a++)
        blockCharacter[a] = g_forceCharacter ? false : SelectWorld[selWorld].blockChar[a];

    int players = (c.lib.players == 2) ? 2 : 1;
    int ch1 = s_charAllowed(c.lib.character) ? c.lib.character : s_nextChar(c.lib.character, 1);
    int ch2 = s_nextChar(ch1, 1);

    ConnectScreen::g_charSelect = {};
    ConnectScreen::g_charSelect[0] = (uint8_t)ch1;
    if(players == 2)
        ConnectScreen::g_charSelect[1] = (uint8_t)ch2;

    numPlayers = players;

    // player 1 is whoever pressed a button here; ask for any missing controller
    if((int)Controls::g_InputMethods.size() < players)
    {
        while((int)Controls::g_InputMethods.size() < players)
            Controls::g_InputMethods.push_back(nullptr);
        QuickReconnectScreen::g_active = true;
    }

    XMessage::g_session.init_char_select = ConnectScreen::g_charSelect;

    c.lib.lastSlot = slot;
    Library_set(c.key, c.lib);

    PlaySoundMenu(SFX_Do);
    s_unloadPics();
    g_LoopActive = false;

    pLogInfo("Library: starting %s, slot %d, %d player(s), character %d", c.key.c_str(), slot, players, ch1);
    StartEpisode();
}


// ---- import ----

static int s_importThreadFunc(void *)
{
    s_importResult = LibraryImport_run(s_importSource, s_importProgress);
    return 0;
}

static void s_importStart(const std::string &source)
{
    if(s_importThread)
        return;

    s_importSource = source;
    delete s_importProgress;
    s_importProgress = new LibraryImportProgress();
    s_importResult = LibraryImportResult();
    s_view = VIEW_IMPORT;

    s_importThread = SDL_CreateThread(s_importThreadFunc, "library_import", nullptr);
    if(!s_importThread)
    {
        s_importResult.message = "The import couldn't start.";
        s_importProgress->finished = true;
    }
}

static void s_importPoll()
{
    if(!s_importThread || !s_importProgress || !s_importProgress->finished)
        return;

    SDL_WaitThread(s_importThread, nullptr);
    s_importThread = nullptr;

    std::string focus = s_importResult.episodes.empty() ? std::string() : s_importResult.episodes[0];
    s_filter = FILTER_ALL;
    s_buildCards(focus);
}

static void s_pickAndImport()
{
    if(!LibraryPicker_available())
    {
        s_toastShow("Drag a zip or folder onto this window to import it.");
        return;
    }

    char *path = LibraryPicker_pickImportSource();
    if(path)
    {
        s_importStart(path);
        std::free(path);
    }

    // the picker took the keyboard; don't let the release act again
    s_prevDo = s_prevBack = s_prevErase = s_prevClick = true;
}

void dropFile(const std::string &path)
{
    s_dropped.push_back(path);
}


// ---- input ----

enum Dir { DIR_UP = 0, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

// a direction just pressed, or held long enough to repeat
static bool s_dirPressed(int d, bool held)
{
    if(!held)
    {
        s_holdFrames[d] = 0;
        return false;
    }

    int f = ++s_holdFrames[d];
    return f == 1 || (f > 20 && (f - 20) % 5 == 0);
}

static void s_testInput(MenuControls_t &mc)
{
    for(const auto &k : s_testKeys)
    {
        // held for 2 frames, so edge detection sees one press
        if(s_frame != k.first && s_frame != k.first + 1)
            continue;
        if(s_frame == k.first + 1)
            continue;

        const std::string &n = k.second;
        if(n == "up") mc.Up = true;
        else if(n == "down") mc.Down = true;
        else if(n == "left") mc.Left = true;
        else if(n == "right") mc.Right = true;
        else if(n == "do") mc.Do = true;
        else if(n == "back") mc.Back = true;
        else if(n == "erase") mc.Erase = true;
    }
}


// ---- layout ----

struct Layout
{
    int W, H, m, gap, top, gridTop, gridBottom, cols, cardW, picH, cardH, rowsVisible;
};

static Layout s_layout()
{
    Layout l;
    l.W = XRender::TargetW;
    l.H = XRender::TargetH;
    l.m = std::max(16, l.W / 40);
    l.gap = std::max(10, l.W / 64);
    l.top = l.m;
    l.gridTop = l.m + s_px(48);
    l.gridBottom = l.H - l.m - s_px(22);
    l.cols = std::max(2, std::min(5, l.W / 230));
    l.cardW = (l.W - 2 * l.m - (l.cols - 1) * l.gap) / l.cols;
    l.picH = l.cardW * 9 / 16;
    l.cardH = l.picH + s_px(46);
    l.rowsVisible = std::max(1, (l.gridBottom - l.gridTop + l.gap) / (l.cardH + l.gap));
    return l;
}

static void s_keepSelVisible(const Layout &l)
{
    int row = s_sel / l.cols;
    if(row < s_scrollRow)
        s_scrollRow = row;
    if(row >= s_scrollRow + l.rowsVisible)
        s_scrollRow = row - l.rowsVisible + 1;
    if(s_scrollRow < 0)
        s_scrollRow = 0;
}


// ---- logic ----

static void s_openEpisode()
{
    Card *c = s_selCard();
    if(!c)
        return;

    s_epCard = s_shown[s_sel];
    selWorld = c->world;
    FindSaves();
    s_epItem = EP_CONTINUE;
    s_deleteArmed = 0;
    s_view = VIEW_EPISODE;
    PlaySoundMenu(SFX_Do);
}

static void s_activateTop(int item)
{
    if(item < FILTER_COUNT)
    {
        s_filter = item;
        s_sel = 0;
        s_scrollRow = 0;
        s_applyFilter();
        PlaySoundMenu(SFX_Slide);
    }
    else if(item == TOP_IMPORT)
    {
        PlaySoundMenu(SFX_Do);
        s_pickAndImport();
    }
    else if(item == TOP_SETTINGS)
    {
        PlaySoundMenu(SFX_Do);
        s_setItem = 0;
        s_view = VIEW_SETTINGS;
    }
}

static int s_continueSlot(const Card &c)
{
    if(c.lib.lastSlot >= 1 && c.lib.lastSlot <= maxSaveSlots)
        return c.lib.lastSlot;

    for(int s = 1; s <= maxSaveSlots; s++)
    {
        if(SaveSlotInfo[s].Progress >= 0)
            return s;
    }

    return 1;
}

static void s_episodeChange(Card &c, int item, int dir)
{
    if(item == EP_MODE)
    {
        static const char *const modes[] = {"auto", "smbx", "smbx2"};
        int cur = 0;
        for(int i = 0; i < 3; i++)
        {
            if(c.lib.mode == modes[i])
                cur = i;
        }
        cur = (cur + dir + 3) % 3;
        c.lib.mode = modes[cur];

        EpisodeMode m = EpisodeMode::Auto;
        EpisodeMode_parse(c.lib.mode, m);
        if(m == EpisodeMode::Auto)
            EpisodeMode_parse(c.lib.detected.empty() ? "smbx" : c.lib.detected, m);
        c.smbx2 = (m == EpisodeMode::Smbx2);
    }
    else if(item == EP_CHARACTER)
        c.lib.character = s_nextChar(c.lib.character, dir);
    else if(item == EP_PLAYERS)
        c.lib.players = (c.lib.players == 2) ? 1 : 2;
    else
        return;

    Library_set(c.key, c.lib);
    PlaySoundMenu(SFX_Slide);
}

static bool s_logicEpisode(const MenuControls_t &mc, bool doP, bool backP, bool eraseP, bool up, bool down, bool left, bool right)
{
    Card &c = s_cards[s_epCard];

    if(backP)
    {
        s_view = VIEW_GRID;
        s_deleteArmed = 0;
        PlaySoundMenu(SFX_Slide);
        return false;
    }

    if(up)
    {
        s_epItem = (s_epItem + EP_COUNT - 1) % EP_COUNT;
        s_deleteArmed = 0;
        PlaySoundMenu(SFX_Slide);
    }
    if(down)
    {
        s_epItem = (s_epItem + 1) % EP_COUNT;
        s_deleteArmed = 0;
        PlaySoundMenu(SFX_Slide);
    }

    if(left || right)
        s_episodeChange(c, s_epItem, right ? 1 : -1);

    if(eraseP && s_epItem >= EP_SLOT1 && s_epItem <= EP_SLOT3)
    {
        int slot = s_epItem - EP_SLOT1 + 1;
        if(SaveSlotInfo[slot].Progress >= 0)
        {
            if(s_deleteArmed == slot)
            {
                DeleteSave(selWorld, slot);
                FindSaves();
                s_deleteArmed = 0;
                s_toastShow("Save " + std::to_string(slot) + " deleted.");
                PlaySoundMenu(SFX_Slide);
            }
            else
            {
                s_deleteArmed = slot;
                PlaySoundMenu(SFX_Slide);
            }
        }
    }

    if(doP)
    {
        if(s_epItem == EP_CONTINUE)
        {
            s_startEpisode(c, s_continueSlot(c));
            return true;
        }
        else if(s_epItem >= EP_SLOT1 && s_epItem <= EP_SLOT3)
        {
            s_startEpisode(c, s_epItem - EP_SLOT1 + 1);
            return true;
        }
        else
            s_episodeChange(c, s_epItem, 1);
    }

    UNUSED(mc);
    return false;
}

static bool s_logicSettings(bool doP, bool backP, bool up, bool down, bool left, bool right)
{
    if(backP)
    {
        s_view = VIEW_GRID;
        PlaySoundMenu(SFX_Slide);
        return false;
    }

    if(up)
    {
        s_setItem = (s_setItem + SET_COUNT - 1) % SET_COUNT;
        PlaySoundMenu(SFX_Slide);
    }
    if(down)
    {
        s_setItem = (s_setItem + 1) % SET_COUNT;
        PlaySoundMenu(SFX_Slide);
    }

    int dir = right ? 1 : (left ? -1 : (doP ? 1 : 0));

    if(dir && s_setItem == SET_THEME)
    {
        s_theme = (s_theme + dir + s_numThemes) % s_numThemes;
        Library_setSetting("theme", s_themes[s_theme].name);
        PlaySoundMenu(SFX_Slide);
    }
    else if(dir && s_setItem == SET_ACCENT)
    {
        s_accent = (s_accent + dir + s_numAccents) % s_numAccents;
        Library_setSetting("accent", s_accents[s_accent].name);
        PlaySoundMenu(SFX_Slide);
    }
    else if(doP && s_setItem == SET_OPTIONS)
    {
        PlaySoundMenu(SFX_Do);
        OptionsScreen::Init();
        s_view = VIEW_OPTIONS;
    }
    else if(doP && s_setItem == SET_CLASSIC)
    {
        PlaySoundMenu(SFX_Do);
        s_unloadPics();
        g_classicVisit = true;
        g_LoopActive = false;
        return true;
    }
    else if(doP && s_setItem == SET_QUIT)
    {
        PlaySoundMenu(SFX_Do);
        s_unloadPics();
        g_LoopActive = false;
        GracefulQuit(true);
        return true;
    }

    return false;
}

static bool s_logicGrid(bool doP, bool up, bool down, bool left, bool right, const Layout &l)
{
    int count = (int)s_shown.size() + 1; // + "Add episode"

    if(s_inTopBar)
    {
        if(left)
            s_topItem = (s_topItem + TOP_COUNT - 1) % TOP_COUNT;
        if(right)
            s_topItem = (s_topItem + 1) % TOP_COUNT;
        if(left || right)
            PlaySoundMenu(SFX_Slide);
        if(down)
        {
            s_inTopBar = false;
            PlaySoundMenu(SFX_Slide);
        }
        if(doP)
            s_activateTop(s_topItem);
        return false;
    }

    int prev = s_sel;
    if(left && s_sel > 0)
        s_sel--;
    if(right && s_sel < count - 1)
        s_sel++;
    if(down && s_sel + l.cols < count)
        s_sel += l.cols;
    else if(down && s_sel / l.cols < (count - 1) / l.cols)
        s_sel = count - 1;
    if(up)
    {
        if(s_sel >= l.cols)
            s_sel -= l.cols;
        else
        {
            s_inTopBar = true;
            s_topItem = s_filter;
        }
        PlaySoundMenu(SFX_Slide);
    }
    if(s_sel != prev)
        PlaySoundMenu(SFX_Slide);

    s_keepSelVisible(l);

    if(doP)
    {
        if(s_sel == (int)s_shown.size())
        {
            PlaySoundMenu(SFX_Do);
            s_pickAndImport();
        }
        else
            s_openEpisode();
    }

    return false;
}

// true: the screen is closing (an episode started, Classic menu, Quit)
static bool s_logic()
{
    s_frame++;
    if(s_toastFrames > 0)
        s_toastFrames--;

    s_importPoll();

    for(const auto &d : s_testDrops)
    {
        if(d.first == s_frame)
            dropFile(d.second);
    }

    if(!s_dropped.empty() && s_view != VIEW_IMPORT)
    {
        std::string path = s_dropped.front();
        s_dropped.clear();
        s_importStart(path);
    }

    if(s_view == VIEW_OPTIONS)
    {
        if(OptionsScreen::Logic())
        {
            s_view = VIEW_SETTINGS;
            s_prevDo = s_prevBack = true;
        }
        return false;
    }

    MenuControls_t mc = Controls::GetMenuControls();
    s_testInput(mc);

    bool click = SharedCursor.Primary;
    bool clickP = click && !s_prevClick;
    s_prevClick = click;

    bool doP = mc.Do && !s_prevDo;
    bool backP = (mc.Back || SharedCursor.Secondary) && !s_prevBack;
    bool eraseP = mc.Erase && !s_prevErase;
    s_prevDo = mc.Do;
    s_prevBack = mc.Back || SharedCursor.Secondary;
    s_prevErase = mc.Erase;

    bool up = s_dirPressed(DIR_UP, mc.Up);
    bool down = s_dirPressed(DIR_DOWN, mc.Down);
    bool left = s_dirPressed(DIR_LEFT, mc.Left);
    bool right = s_dirPressed(DIR_RIGHT, mc.Right);

    Layout l = s_layout();

    // the mouse: hovering focuses, clicking activates
    if(SharedCursor.Move || clickP)
    {
        int mx = (int)SharedCursor.X, my = (int)SharedCursor.Y;
        for(const Hit &h : s_hits)
        {
            if(mx < h.x || my < h.y || mx >= h.x + h.w || my >= h.y + h.h)
                continue;

            if(h.kind == HIT_TOP && s_view == VIEW_GRID)
            {
                s_inTopBar = true;
                s_topItem = h.index;
                if(clickP)
                    s_activateTop(h.index);
            }
            else if(h.kind == HIT_CARD && s_view == VIEW_GRID)
            {
                s_inTopBar = false;
                s_sel = h.index;
                if(clickP)
                    doP = true;
            }
            else if(h.kind == HIT_EPITEM && s_view == VIEW_EPISODE)
            {
                s_epItem = h.index;
                if(clickP)
                    doP = true;
            }
            else if(h.kind == HIT_SETITEM && s_view == VIEW_SETTINGS)
            {
                s_setItem = h.index;
                if(clickP)
                    doP = true;
            }
            else if(h.kind == HIT_BACK && clickP)
                backP = true;
            break;
        }
    }

    if(s_view == VIEW_GRID && (SharedCursor.ScrollDown || SharedCursor.ScrollUp))
    {
        int maxRow = std::max(0, ((int)s_shown.size()) / l.cols - l.rowsVisible + 1);
        s_scrollRow = std::max(0, std::min(maxRow, s_scrollRow + (SharedCursor.ScrollDown ? 1 : -1)));
    }

    switch(s_view)
    {
    case VIEW_GRID:
        return s_logicGrid(doP && !(clickP && s_inTopBar), up, down, left, right, l);
    case VIEW_EPISODE:
        return s_logicEpisode(mc, doP, backP, eraseP, up, down, left, right);
    case VIEW_SETTINGS:
        return s_logicSettings(doP, backP, up, down, left, right);
    case VIEW_IMPORT:
        if(s_importThread == nullptr && (doP || backP))
        {
            s_view = VIEW_GRID;
            s_inTopBar = false;
            PlaySoundMenu(SFX_Do);
        }
        return false;
    default:
        return false;
    }
}


// ---- drawing ----

static void s_box(int x, int y, int w, int h, XTColor c)
{
    XRender::renderRect(x, y, w, h, c, true);
}

static void s_frameRect(int x, int y, int w, int h, int t, XTColor c)
{
    s_box(x, y, w, t, c);
    s_box(x, y + h - t, w, t, c);
    s_box(x, y, t, h, c);
    s_box(x + w - t, y, t, h, c);
}

static void s_badge(const std::string &t, int x, int y, bool smbx2)
{
    int size = 11;
    int w = s_textW(t, size) + s_px(10);
    int h = s_px(16);
    XTColor bg = smbx2 ? A().color : T().line;
    XTColor fg = smbx2 ? A().onColor : T().text;
    s_box(x, y, w, h, bg);
    s_textMid(t, x + s_px(5), y, h, size, fg);
}

static std::string s_timeText(int64_t frames)
{
    // TheXTech's play time counts game frames (65 a second)
    int64_t secs = frames / 65;
    int64_t h = secs / 3600, m = (secs / 60) % 60;
    if(h > 0)
        return std::to_string(h) + " h " + std::to_string(m) + " min";
    return std::to_string(m) + " min";
}

static void s_drawTopBar(const Layout &l)
{
    int x = l.m, y = l.top;
    int iconSize = s_px(28);

    if(s_leaf && s_leaf->inited)
        XRender::renderTextureScale(x, y, iconSize, iconSize, *s_leaf);
    else
        s_box(x, y, iconSize, iconSize, A().color);

    x += iconSize + s_px(10);
    s_textMid("Ikoonat", x, y, iconSize, 20, T().text);
    x += s_textW("Ikoonat", 20) + s_px(24);

    for(int i = 0; i < TOP_COUNT; i++)
    {
        std::string label = (i < FILTER_COUNT) ? s_filterNames[i] : (i == TOP_IMPORT ? "+ Import" : "Settings");
        int size = 13;
        int w = s_textW(label, size) + s_px(20);
        int h = s_px(26);

        if(i == TOP_IMPORT)
            x = std::max(x, l.W - l.m - w - (s_textW("Settings", size) + s_px(20)) - s_px(8));

        bool active = (i == s_filter);
        bool focus = s_inTopBar && s_topItem == i && s_view == VIEW_GRID;

        if(active)
            s_box(x, y + s_px(1), w, h, A().color);
        else if(focus)
            s_box(x, y + s_px(1), w, h, T().cardHi);

        if(focus)
            s_frameRect(x - 2, y + s_px(1) - 2, w + 4, h + 4, 2, A().color);

        s_textMid(label, x + s_px(10), y + s_px(1), h, size, active ? A().onColor : (focus ? T().text : T().sub));
        s_hits.push_back({x, y, w, h, HIT_TOP, i});
        x += w + s_px(8);
    }
}

static void s_drawGrid(const Layout &l)
{
    s_drawTopBar(l);

    int count = (int)s_shown.size() + 1;
    for(int k = s_scrollRow * l.cols; k < count; k++)
    {
        int row = k / l.cols - s_scrollRow;
        if(row >= l.rowsVisible)
            break;

        int col = k % l.cols;
        int x = l.m + col * (l.cardW + l.gap);
        int y = l.gridTop + row * (l.cardH + l.gap);
        bool focus = (!s_inTopBar && k == s_sel);

        s_hits.push_back({x, y, l.cardW, l.cardH, HIT_CARD, k});

        if(k == (int)s_shown.size())
        {
            // "Add episode"
            s_box(x, y, l.cardW, l.cardH, focus ? T().cardHi : T().bg);
            s_frameRect(x, y, l.cardW, l.cardH, focus ? 3 : 1, focus ? A().color : T().line);
            s_textCenter("+", x + l.cardW / 2, y + l.cardH / 2 - s_px(30), 28, T().sub);
            s_textCenter("Add episode", x + l.cardW / 2, y + l.cardH / 2 + s_px(4), 13, T().sub);
            continue;
        }

        Card &c = s_cards[s_shown[k]];
        s_loadPic(c);

        s_box(x, y, l.cardW, l.cardH, focus ? T().cardHi : T().card);

        if(c.pic)
            s_drawPicCover(*c.pic, x, y, l.cardW, l.picH);
        else
        {
            s_box(x, y, l.cardW, l.picH, c.smbx2 ? XTColor(A().color, 70) : T().line);
            s_textCenter(s_fit(c.name, l.cardW - s_px(16), 15), x + l.cardW / 2, y + l.picH / 2 - s_px(9), 15, T().text);
        }

        s_badge(c.smbx2 ? "SMBX2" : "SMBX", x + s_px(6), y + s_px(6), c.smbx2);

        s_text(s_fit(c.name, l.cardW - s_px(16), 14), x + s_px(8), y + l.picH + s_px(6), 14, T().text);

        std::string info = c.lib.lastPlayed > 0 ? "Played" : "New";
        if(c.lib.lastPlayed > 0)
        {
            int64_t days = ((int64_t)std::time(nullptr) - c.lib.lastPlayed) / 86400;
            info = days <= 0 ? "Played today" : (days == 1 ? "Played yesterday" : "Played " + std::to_string(days) + " days ago");
        }
        s_text(s_fit(info, l.cardW - s_px(16), 11), x + s_px(8), y + l.picH + s_px(26), 11, T().sub);

        if(focus)
            s_frameRect(x - 3, y - 3, l.cardW + 6, l.cardH + 6, 3, A().color);
    }

    // status line
    std::string status = SMBX2Data_dir().empty() ? "SMBX2's data: not installed (SMBX episodes only)" : "SMBX2's data: installed";
    status += "  \xC2\xB7  " + std::to_string(s_cards.size()) + (s_cards.size() == 1 ? " episode" : " episodes");
    s_text(status, l.m, l.H - l.m - s_px(14), 11, T().sub);

    if(s_cards.empty())
        s_textCenter("Import an episode or SMBX2's data to start: + Import, or drag a zip onto this window.", l.W / 2, l.gridTop + l.cardH + l.gap * 2, 13, T().sub);
}

static void s_drawListItem(int x, int y, int w, int h, bool focus, const std::string &label, const std::string &value, int kind, int index, bool primary = false)
{
    if(primary)
        s_box(x, y, w, h, A().color);
    else
        s_box(x, y, w, h, focus ? T().cardHi : T().card);

    if(focus)
        s_frameRect(x - 2, y - 2, w + 4, h + 4, 2, primary ? T().text : A().color);

    s_textMid(label, x + s_px(12), y, h, 13, primary ? A().onColor : T().text);

    if(!value.empty())
        s_textMid(value, x + w - s_px(12) - s_textW(value, 12), y, h, 12, primary ? A().onColor : T().sub);

    s_hits.push_back({x, y, w, h, kind, index});
}

static void s_drawBackButton(const Layout &l)
{
    std::string t = "< Back";
    int w = s_textW(t, 13) + s_px(16), h = s_px(24);
    s_box(l.m, l.top, w, h, T().card);
    s_textMid(t, l.m + s_px(8), l.top, h, 13, T().sub);
    s_hits.push_back({l.m, l.top, w, h, HIT_BACK, 0});
}

static void s_drawEpisode(const Layout &l)
{
    Card &c = s_cards[s_epCard];
    s_drawBackButton(l);

    int top = l.top + s_px(36);
    int leftW = (l.W - 2 * l.m - l.gap) * 11 / 21;
    int picH = leftW * 9 / 16;

    s_loadPic(c);
    if(c.pic)
        s_drawPicCover(*c.pic, l.m, top, leftW, picH);
    else
    {
        s_box(l.m, top, leftW, picH, c.smbx2 ? XTColor(A().color, 70) : T().line);
        s_textCenter(s_fit(c.name, leftW - s_px(20), 18), l.m + leftW / 2, top + picH / 2 - s_px(10), 18, T().text);
    }

    // under the picture: what auto-detect saw
    int ty = top + picH + s_px(10);
    std::string why = c.lib.detectedWhy.empty() ? std::string() : "Detected: " + c.lib.detectedWhy;
    s_text(s_fit(why, leftW, 11), l.m, ty, 11, T().sub);
    s_text(s_fit(c.key, leftW, 10), l.m, ty + s_px(18), 10, T().sub);

    // right column
    int x = l.m + leftW + l.gap;
    int w = l.W - l.m - x;
    int y = top;

    s_text(s_fit(c.name, w, 20), x, y, 20, T().text);
    y += s_px(28);

    std::string sub = c.author.empty() ? std::string() : "by " + c.author;
    std::string mode = c.smbx2 ? "SMBX2 mode" : "SMBX mode";
    mode += (c.lib.mode == "auto") ? " (auto)" : " (your choice)";
    s_text(s_fit(sub.empty() ? mode : sub + "  \xC2\xB7  " + mode, w, 12), x, y, 12, T().sub);
    y += s_px(24);

    int ih = s_px(30), ig = s_px(6);

    int cs = s_continueSlot(c);
    std::string contValue = "Slot " + std::to_string(cs);
    if(SaveSlotInfo[cs].Progress < 0)
        contValue += ": new game";
    s_drawListItem(x, y, w, ih, s_epItem == EP_CONTINUE, "Continue", contValue, HIT_EPITEM, EP_CONTINUE, true);
    y += ih + ig;

    for(int s = 1; s <= maxSaveSlots; s++)
    {
        const SaveSlotInfo_t &info = SaveSlotInfo[s];
        std::string v;
        if(info.Progress < 0)
            v = "Empty";
        else
        {
            v = std::to_string(info.Stars) + " stars";
            if(info.Progress > 0)
                v += "  \xC2\xB7  " + std::to_string(info.Progress) + "%";
            if(info.Time > 0)
                v += "  \xC2\xB7  " + s_timeText(info.Time);
        }

        bool focus = (s_epItem == EP_SLOT1 + s - 1);
        if(focus && s_deleteArmed == s)
            v = "Press Erase again to delete";

        s_drawListItem(x, y, w, ih, focus, "Save slot " + std::to_string(s), v, HIT_EPITEM, EP_SLOT1 + s - 1);
        y += ih + ig;
    }

    y += s_px(6);

    std::string modeV = (c.lib.mode == "smbx2") ? "SMBX2" : (c.lib.mode == "smbx" ? "SMBX" : std::string("Auto (") + (c.smbx2 ? "SMBX2" : "SMBX") + ")");
    s_drawListItem(x, y, w, ih, s_epItem == EP_MODE, "Mode", "< " + modeV + " >", HIT_EPITEM, EP_MODE);
    y += ih + ig;

    int ch = s_charAllowed(c.lib.character) ? c.lib.character : s_nextChar(c.lib.character, 1);
    s_drawListItem(x, y, w, ih, s_epItem == EP_CHARACTER, "Character", std::string("< ") + s_charNames[ch] + " >", HIT_EPITEM, EP_CHARACTER);
    y += ih + ig;

    s_drawListItem(x, y, w, ih, s_epItem == EP_PLAYERS, "Players", c.lib.players == 2 ? "< 2 >" : "< 1 >", HIT_EPITEM, EP_PLAYERS);
    y += ih + ig;

    std::string hint = (s_epItem >= EP_SLOT1 && s_epItem <= EP_SLOT3) ? "Erase: delete this save" : "Left/Right: change";
    s_text(hint, x, y + s_px(4), 11, T().sub);
}

static void s_drawSettings(const Layout &l)
{
    s_drawBackButton(l);

    int w = std::min(l.W - 2 * l.m, s_px(460));
    int x = (l.W - w) / 2;
    int y = l.top + s_px(44);

    s_text("Settings", x, y, 20, T().text);
    y += s_px(36);

    int ih = s_px(32), ig = s_px(8);
    s_drawListItem(x, y, w, ih, s_setItem == SET_THEME, "Theme", std::string("< ") + T().name + " >", HIT_SETITEM, SET_THEME);
    y += ih + ig;
    s_drawListItem(x, y, w, ih, s_setItem == SET_ACCENT, "Accent color", std::string("< ") + A().name + " >", HIT_SETITEM, SET_ACCENT);
    y += ih + ig + s_px(8);
    s_drawListItem(x, y, w, ih, s_setItem == SET_OPTIONS, "Game options", "Video, sound, controls, SMBX2", HIT_SETITEM, SET_OPTIONS);
    y += ih + ig;
    s_drawListItem(x, y, w, ih, s_setItem == SET_CLASSIC, "Classic menu", "Battle mode, editor", HIT_SETITEM, SET_CLASSIC);
    y += ih + ig;
    s_drawListItem(x, y, w, ih, s_setItem == SET_QUIT, "Quit", "", HIT_SETITEM, SET_QUIT);

    // accent swatch
    s_box(x + w + s_px(10), l.top + s_px(80) + ih + ig, s_px(14), s_px(14), A().color);
}

static void s_drawImport(const Layout &l)
{
    int w = std::min(l.W - 2 * l.m, s_px(520));
    int x = (l.W - w) / 2;
    int y = l.H / 2 - s_px(60);

    s_text("Import", x, y, 20, T().text);
    y += s_px(34);

    if(s_importThread)
    {
        std::string stage = s_importProgress ? s_importProgress->getStage() : std::string();
        s_text(s_fit(stage.empty() ? "Starting" : stage, w, 13), x, y, 13, T().sub);
        y += s_px(26);

        int total = s_importProgress ? (int)s_importProgress->total : 0;
        int done = s_importProgress ? (int)s_importProgress->done : 0;
        s_box(x, y, w, s_px(10), T().line);
        if(total > 0)
            s_box(x, y, (int)((long long)w * std::min(done, total) / total), s_px(10), A().color);
        else
        {
            // unknown length: a moving block
            int bw = w / 5;
            int bx = (int)((s_frame * 4) % (w + bw)) - bw;
            int x0 = std::max(0, bx), x1 = std::min(w, bx + bw);
            if(x1 > x0)
                s_box(x + x0, y, x1 - x0, s_px(10), A().color);
        }
        y += s_px(22);

        if(total > 0)
            s_text(std::to_string(done) + " of " + std::to_string(total) + " files", x, y, 11, T().sub);
    }
    else
    {
        s_text(s_fit(s_importResult.message, w, 14), x, y, 14, s_importResult.ok ? T().text : XTColor(230, 90, 80));
        y += s_px(40);
        s_drawListItem(x, y, s_px(120), s_px(30), true, "OK", "", HIT_BACK, 0, true);
    }
}

static void s_saveFrame(const std::string &path, int maxW);

static void s_render()
{
    XRender::setTargetTexture();
    XRender::resetViewport();
    XRender::clearBuffer();
    XRender::setDrawPlane(PLANE_GAME_MENUS);

    Layout l = s_layout();
    s_hits.clear();

    s_box(0, 0, l.W, l.H, T().bg);

    switch(s_view)
    {
    case VIEW_GRID:
        s_drawGrid(l);
        break;
    case VIEW_EPISODE:
        s_drawEpisode(l);
        break;
    case VIEW_SETTINGS:
        s_drawSettings(l);
        break;
    case VIEW_OPTIONS:
        // TheXTech's own options screen, on the library's background
        OptionsScreen::Render();
        break;
    case VIEW_IMPORT:
        s_drawImport(l);
        break;
    }

    if(s_toastFrames > 0 && !s_toast.empty())
    {
        int w = s_textW(s_toast, 13) + s_px(24);
        int x = (l.W - w) / 2, y = l.H - l.m - s_px(46);
        s_box(x, y, w, s_px(28), T().text);
        s_textMid(s_toast, x + s_px(12), y, s_px(28), 13, T().bg);
    }

    if(SharedCursor.Move || SharedCursor.Primary)
        XRender::renderTextureBasic(int(SharedCursor.X), int(SharedCursor.Y), GFX.ECursor[2]);

    XRender::repaint();

    if(TakeScreen)
        ScreenShot();

    // testing: frames as libshot-N.png in the log folder
    if(std::find(s_testShots.begin(), s_testShots.end(), s_frame) != s_testShots.end())
        s_saveFrame(AppPathManager::logsDir() + "libshot-" + std::to_string(++s_testShotCount) + ".png", 0);
}


// ---- screen ----

static void s_parseTestEnv()
{
    s_testKeys.clear();
    s_testShots.clear();

    if(const char *k = std::getenv("SMBX2COMPAT_LIBKEYS"))
    {
        std::string s = k;
        size_t i = 0;
        while(i < s.size())
        {
            size_t comma = s.find(',', i);
            if(comma == std::string::npos)
                comma = s.size();
            std::string item = s.substr(i, comma - i);
            size_t colon = item.find(':');
            if(colon != std::string::npos)
                s_testKeys.push_back({std::atoi(item.substr(0, colon).c_str()), item.substr(colon + 1)});
            i = comma + 1;
        }
    }

    // SMBX2COMPAT_LIBDROP="60:/path/file.zip": as if dropped on the window then
    s_testDrops.clear();
    if(const char *k = std::getenv("SMBX2COMPAT_LIBDROP"))
    {
        std::string item = k;
        size_t colon = item.find(':');
        if(colon != std::string::npos)
            s_testDrops.push_back({std::atoi(item.substr(0, colon).c_str()), item.substr(colon + 1)});
    }

    if(const char *k = std::getenv("SMBX2COMPAT_LIBSHOTS"))
    {
        std::string s = k;
        size_t i = 0;
        while(i < s.size())
        {
            size_t comma = s.find(',', i);
            if(comma == std::string::npos)
                comma = s.size();
            s_testShots.push_back(std::atoi(s.substr(i, comma - i).c_str()));
            i = comma + 1;
        }
    }
}

void Init()
{
    g_LoopActive = true;
    g_returnRequested = false;
    s_view = VIEW_GRID;
    s_inTopBar = false;
    s_frame = 0;
    s_testShotCount = 0;
    s_prevDo = s_prevBack = s_prevErase = s_prevClick = true;
    MenuCursorCanMove = false;

    std::string theme = Library_setting("theme", "Dark");
    std::string accent = Library_setting("accent", "Leaf");
    for(int i = 0; i < s_numThemes; i++)
    {
        if(theme == s_themes[i].name)
            s_theme = i;
    }
    for(int i = 0; i < s_numAccents; i++)
    {
        if(accent == s_accents[i].name)
            s_accent = i;
    }

    s_parseTestEnv();
    s_loadFont();

    if(!s_leaf)
    {
        std::string leaf;
#if defined(__APPLE__)
        char *base = SDL_GetBasePath();
        if(base)
        {
            leaf = std::string(base) + "ikoonat-leaf.png";
            SDL_free(base);
        }
#endif
        if(!leaf.empty() && Files::fileExists(leaf))
        {
            s_leaf = std::make_shared<StdPicture>();
            XRender::LoadPicture(*s_leaf, leaf);
        }
    }

    // files dropped on the window (main.cpp turns these off after startup)
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    // the episode played last comes first
    std::string recentKey;
    LibraryEpisode_t recent;
    std::string focus;
    s_buildCards();
    if(Library_mostRecent(recentKey, recent))
    {
        for(size_t k = 0; k < s_shown.size(); k++)
        {
            if(s_cards[s_shown[k]].key == recentKey)
                s_sel = (int)k;
        }
    }
    if(s_sel > (int)s_shown.size())
        s_sel = 0;
    s_keepSelVisible(s_layout());

    StopMusic();
    pLogInfo("Library: %d episodes", (int)s_cards.size());
}

void Loop()
{
    Controls::PollInputMethod();
    Controls::Update(false);

    if(s_logic())
        return;

    UpdateSound();
    s_render();
}


// ---- episode pictures ----

static std::string s_thumbArmed;
static int s_thumbFrames = 0;

// the frame just drawn as a PNG, scaled down to at most maxW wide (0: as is)
static void s_saveFrame(const std::string &path, int maxW)
{
    const int w = XRender::TargetW, h = XRender::TargetH;
    std::vector<uint8_t> px((size_t)w * h * 4);
    XRender::setTargetTexture();
    g_render->getScreenPixelsRGBA(0, 0, w, h, px.data());
    XRender::setTargetScreen();

    int f = (maxW > 0 && w > maxW) ? (w + maxW - 1) / maxW : 1;
    int ow = w / f, oh = h / f;

    FIBITMAP *img = FreeImage_AllocateT(FIT_BITMAP, ow, oh, 32);
    if(!img)
        return;

    // average f x f blocks (as AbstractRender_t::makeShot, rows bottom-up)
    for(int y = 0; y < oh; ++y)
    {
        for(int x = 0; x < ow; ++x)
        {
            unsigned r = 0, g = 0, b = 0;
            for(int dy = 0; dy < f; dy++)
            {
                const uint8_t *p = &px[(((size_t)(y * f + dy)) * w + (size_t)x * f) * 4];
                for(int dx = 0; dx < f; dx++, p += 4)
                {
                    r += p[0];
                    g += p[1];
                    b += p[2];
                }
            }
            RGBQUAD q;
            q.rgbRed = (BYTE)(r / (f * f));
            q.rgbGreen = (BYTE)(g / (f * f));
            q.rgbBlue = (BYTE)(b / (f * f));
            q.rgbReserved = 255;
            FreeImage_SetPixelColor(img, x, (oh - 1) - y, &q);
        }
    }

    if(!FreeImage_Save(FIF_PNG, img, path.c_str(), 0))
        pLogWarning("Library: couldn't save %s", path.c_str());
    FreeImage_Unload(img);
}

void thumbArm(const std::string &episodeDir)
{
    std::string path = s_thumbPath(Library_key(episodeDir));
    if(Files::fileExists(path))
    {
        s_thumbArmed.clear();
        return;
    }

    s_thumbArmed = path;
    s_thumbFrames = 0;
}

void thumbAfterFrame()
{
    if(s_thumbArmed.empty() || GameMenu || LevelEditor || GamePaused != PauseCode::None || LoadingInProcess)
        return;

    // ~45 seconds of play: past the loading screens and the episode's own
    // menus (SMATRS asks for players first), usually on the map or in a level
    int after = 64 * 45;
    if(const char *t = std::getenv("SMBX2COMPAT_THUMBFRAMES")) // testing
        after = std::atoi(t);
    if(++s_thumbFrames < after)
        return;

    DirMan::mkAbsPath(s_thumbDir());
    s_saveFrame(s_thumbArmed, 480);
    pLogInfo("Library: saved the episode's picture %s", s_thumbArmed.c_str());
    s_thumbArmed.clear();
}

} // namespace LibraryScreen
