/* NetHack may be freely redistributed.  See license for details. */

/*
 * Main menu / launcher (IMPLEMENTATION_LOG s65).
 *
 * Shown once at startup, after the intro and before the name prompt, when the
 * game was started WITHOUT a -nethackrc= (i.e. not from a per-hero shortcut).
 * Called from sys/windows/windmain.c just before plnamesuffix().
 *
 *  - Continue / Load: a save whose hero name matches a roster rc file
 *    (%USERPROFILE%\NetHack\*.nethackrc, "name:" option) is resumed by
 *    RELAUNCHING the exe with -nethackrc=<that file>, because NetHack reads
 *    its options before the window port exists and cannot re-read them in
 *    process.  A save with no matching rc is resumed in process with the
 *    default .nethackrc.
 *  - Roster hero: relaunch with that rc (resumes its save, or starts it).
 *  - New hero: asks for a name, then carries on in process (default rc).
 *  - High scores (record), past games (dumplogs), save backups (restore /
 *    archive - backups are what restore.c's vq_backup_savefile writes),
 *    the Guidebook, quit.
 *
 * The relaunched child gets VULTURE_SKIP_MENU and VULTURE_SKIP_INTRO; this
 * process clears VULTURE_SKIP_MENU from its own environment on the way in so
 * a later "Play again?" relaunch shows the menu again.
 */

#include "vulture_sdl.h" /* must be first, see vulture_gen.cpp */

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <algorithm>
#include <string>
#include <vector>

#ifdef _WIN32
#include "win32api.h"
#endif

#include "vulture_win.h"
#include "vulture_gra.h"
#include "vulture_gen.h"
#include "vulture_gfl.h"
#include "vulture_txt.h"
#include "vulture_main.h"
#include "vulture_types.h"

/* full-screen image that levelwin::draw() paints instead of black while no
 * map exists, so menus opening and closing over the title keep it intact */
SDL_Surface *vulture_backdrop = NULL;

namespace {

struct mm_save {
    std::string name;   /* hero name */
    std::string desc;   /* "Tourist, human female neutral" */
    std::string path;   /* save file */
    time_t mtime;
};

struct mm_hero {
    std::string rcpath; /* full path of the rc file */
    std::string file;   /* just the file name */
    std::string name;
    std::string role, race, gender, align, pet;
};

struct mm_file {
    std::string path, file;
    time_t mtime;
};

const char *const mm_tips[] = {
    "Ctrl+> walks to the down stairs you've seen and goes down; Ctrl+< goes up.",
    "Alt+K walks to the nearest enemy, or attacks it if it's next to you.",
    "Alt+Q fires your quiver at the nearest enemy in a straight line.",
    "Alt+Z rests until your HP and power are full.",
    "Alt+H writes Elbereth in the dust.",
    "PageUp / PageDown turn the dungeon view a quarter at a time.",
    "The ' key explores the level for you.",
    "Tourists: Ctrl+S points the camera at a monster you haven't photographed.",
    "#photos lists every species in your photo album.",
    "#stash adds a note about what you left here to the dungeon overview.",
    "Every saved game you load is backed up first - see Save backups.",
    "The red messages are the ones that will kill you if you ignore them.",
    "Wear a new helmet over the old one and the swap happens in one go.",
    "Elbereth scares most monsters - but not @ humans or minotaurs.",
    "Never eat a cockatrice corpse. Never.",
    "Pray when you're in real trouble, and not too often.",
    "A unicorn of your own alignment is worth a gem; others are worth a fight.",
    "Engrave-test unknown wands: the message tells you a lot.",
};

const char *mm_tip = NULL;

std::string mm_dirname(const std::string &p)
{
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string("") : p.substr(0, s + 1);
}

std::string mm_basename(const std::string &p)
{
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

bool mm_exists(const std::string &p, time_t *mt = NULL)
{
    struct stat st;

    if (stat(p.c_str(), &st) != 0)
        return false;
    if (mt)
        *mt = st.st_mtime;
    return true;
}

std::string mm_when(time_t t)
{
    char buf[64];
    struct tm *tm = localtime(&t);

    if (!tm)
        return "";
    strftime(buf, sizeof buf, "%b %d, %H:%M", tm);
    return buf;
}

std::string mm_ago(time_t t)
{
    long d = (long) difftime(time(NULL), t);
    char buf[64];

    if (d < 120)
        return "just now";
    if (d < 7200)
        snprintf(buf, sizeof buf, "%ld minutes ago", d / 60);
    else if (d < 2 * 86400)
        snprintf(buf, sizeof buf, "%ld hours ago", d / 3600);
    else
        snprintf(buf, sizeof buf, "%ld days ago", d / 86400);
    return buf;
}

/* full path of the save file for hero name `name` (plname is borrowed) */
std::string mm_save_path(const char *name)
{
    char saved[PL_NSIZ];
    std::string p;

    Strcpy(saved, svp.plname);
    (void) strncpy(svp.plname, name, PL_NSIZ - 1);
    svp.plname[PL_NSIZ - 1] = '\0';
    set_savefile_name(TRUE);
    p = fqname(gs.SAVEF, SAVEPREFIX, 0);
    Strcpy(svp.plname, saved);
#ifdef COMPRESS_EXTENSION
    if (!mm_exists(p) && mm_exists(p + COMPRESS_EXTENSION))
        p += COMPRESS_EXTENSION;
#endif
    return p;
}

std::string mm_savedir(void)
{
    return mm_dirname(mm_save_path("x"));
}

const char *mm_lookup(const char *code, int which)
{
    int i;

    switch (which) {
    case 0:
        for (i = 0; roles[i].name.m; i++)
            if (!strcmpi(code, roles[i].filecode))
                return roles[i].name.m;
        break;
    case 1:
        for (i = 0; races[i].noun; i++)
            if (!strcmpi(code, races[i].filecode))
                return races[i].adj;
        break;
    case 2:
        for (i = 0; i < ROLE_GENDERS; i++)
            if (!strcmpi(code, genders[i].filecode))
                return genders[i].adj;
        break;
    default:
        for (i = 0; i < ROLE_ALIGNS; i++)
            if (!strcmpi(code, aligns[i].filecode))
                return aligns[i].adj;
        break;
    }
    return code;
}

std::vector<mm_save> mm_list_saves(void)
{
    std::vector<mm_save> out;
    char saved[PL_NSIZ];
    char **list;
    int i;

    Strcpy(saved, svp.plname); /* get_saved_games() writes "*" there */
    list = get_saved_games();
    Strcpy(svp.plname, saved);
    if (!list)
        return out;

    for (i = 0; list[i]; i++) {
        /* "name-Rol-Rac-Gen-Ali" (the name itself may contain '-') */
        std::string s = list[i];
        std::vector<std::string> f;
        mm_save sv;
        size_t cut = s.size();
        int k;

        for (k = 0; k < 4; k++) {
            size_t d = s.rfind('-', cut - 1);

            if (d == std::string::npos || d == 0)
                break;
            f.insert(f.begin(), s.substr(d + 1, cut - d - 1));
            cut = d;
        }
        sv.name = s.substr(0, cut);
        if (f.size() == 4)
            sv.desc = std::string(mm_lookup(f[0].c_str(), 0)) + ", "
                      + mm_lookup(f[1].c_str(), 1) + " "
                      + mm_lookup(f[2].c_str(), 2) + " "
                      + mm_lookup(f[3].c_str(), 3);
        sv.path = mm_save_path(sv.name.c_str());
        sv.mtime = 0;
        (void) mm_exists(sv.path, &sv.mtime);
        out.push_back(sv);
    }
    free_saved_games(list);
    std::sort(out.begin(), out.end(), [](const mm_save &a, const mm_save &b) {
        return a.mtime > b.mtime;
    });
    return out;
}

std::string mm_trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");

    return a == std::string::npos ? std::string("") : s.substr(a, b - a + 1);
}

std::string mm_configdir(void)
{
    const char *up = getenv("USERPROFILE");

    if (gf.fqn_prefix[CONFIGPREFIX] && *gf.fqn_prefix[CONFIGPREFIX])
        return gf.fqn_prefix[CONFIGPREFIX];
    return up ? std::string(up) + "\\NetHack\\" : std::string("");
}

std::vector<mm_file> mm_find(const std::string &dir, const char *pattern)
{
    std::vector<mm_file> out;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    std::string d = dir;

    if (!d.empty() && d[d.size() - 1] != '\\' && d[d.size() - 1] != '/')
        d += "\\";
    h = FindFirstFileA((d + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do {
        mm_file f;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        f.file = fd.cFileName;
        f.path = d + f.file;
        f.mtime = 0;
        (void) mm_exists(f.path, &f.mtime);
        out.push_back(f);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#endif
    std::sort(out.begin(), out.end(), [](const mm_file &a, const mm_file &b) {
        return a.mtime > b.mtime;
    });
    return out;
}

std::vector<mm_hero> mm_list_heroes(void)
{
    std::vector<mm_hero> out;
    std::vector<mm_file> files = mm_find(mm_configdir(), "*.nethackrc");
    size_t i;

    for (i = 0; i < files.size(); i++) {
        FILE *fp;
        char line[BUFSZ * 4];
        mm_hero h;

        if (files[i].file == ".nethackrc") /* the default settings */
            continue;
        if (!(fp = fopen(files[i].path.c_str(), "r")))
            continue;
        h.rcpath = files[i].path;
        h.file = files[i].file;
        while (fgets(line, sizeof line, fp)) {
            std::string l = mm_trim(line);
            size_t p;

            if (l.size() < 8 || strncmpi(l.c_str(), "OPTIONS", 7))
                continue;
            if ((p = l.find_first_of("=:")) == std::string::npos)
                continue;
            l = l.substr(p + 1);
            while (!l.empty()) {
                size_t c = l.find(',');
                std::string opt = mm_trim(l.substr(0, c));
                size_t colon = opt.find_first_of(":=");

                l = (c == std::string::npos) ? "" : l.substr(c + 1);
                if (colon == std::string::npos)
                    continue;
                std::string key = mm_trim(opt.substr(0, colon));
                std::string val = mm_trim(opt.substr(colon + 1));

                if (!strcmpi(key.c_str(), "name"))
                    h.name = val;
                else if (!strcmpi(key.c_str(), "role")
                         || !strcmpi(key.c_str(), "character"))
                    h.role = val;
                else if (!strcmpi(key.c_str(), "race"))
                    h.race = val;
                else if (!strcmpi(key.c_str(), "gender"))
                    h.gender = val;
                else if (!strcmpi(key.c_str(), "align")
                         || !strcmpi(key.c_str(), "alignment"))
                    h.align = val;
                else if (!strcmpi(key.c_str(), "pettype"))
                    h.pet = val;
            }
        }
        (void) fclose(fp);
        if (!h.name.empty())
            out.push_back(h);
    }
    std::sort(out.begin(), out.end(), [](const mm_hero &a, const mm_hero &b) {
        return strcmpi(a.name.c_str(), b.name.c_str()) < 0
               || (!strcmpi(a.name.c_str(), b.name.c_str()) && a.file < b.file);
    });
    return out;
}

std::string mm_hero_line(const mm_hero &h)
{
    std::string s = h.name;
    std::string what;

    if (!h.race.empty())
        what += h.race + " ";
    what += h.role.empty() ? std::string("(choose a role)") : h.role;
    s += " - " + what;
    if (!strcmpi(h.pet.c_str(), "none"))
        s += ", no pet";
    else if (!h.pet.empty())
        s += ", " + h.pet;
    return s;
}

/* ---------------------------------------------------------------- drawing */

void mm_compose_backdrop(void)
{
    SDL_Surface *img;
    int lh;

    if (vulture_backdrop && (vulture_backdrop->w != vulture_screen->w
                             || vulture_backdrop->h != vulture_screen->h)) {
        SDL_FreeSurface(vulture_backdrop);
        vulture_backdrop = NULL;
    }
    if (!vulture_backdrop)
        vulture_backdrop = SDL_CreateRGBSurface(
            SDL_SWSURFACE, vulture_screen->w, vulture_screen->h,
            vulture_px_format->BitsPerPixel, vulture_px_format->Rmask,
            vulture_px_format->Gmask, vulture_px_format->Bmask, 0);
    if (!vulture_backdrop)
        return;

    SDL_FillRect(vulture_backdrop, NULL, CLR32_BLACK);
    if ((img = vulture_load_graphic_ui("entrance")) != NULL) {
        SDL_Rect r;

        r.x = (vulture_screen->w - img->w) / 2;
        r.y = (vulture_screen->h - img->h) / 2;
        r.w = img->w;
        r.h = img->h;
        SDL_BlitSurface(img, NULL, vulture_backdrop, &r);
        SDL_FreeSurface(img);
    }

    lh = vulture_get_lineheight(V_FONT_LARGE);
    {
        std::string title = "NetHack 5.0";
        int tw = vulture_text_length(V_FONT_LARGE, title);

        vulture_put_text_shadow(V_FONT_LARGE, title, vulture_backdrop,
                                (vulture_screen->w - tw) / 2, lh,
                                CLR32_WHITE, CLR32_BLACK);
    }
    if (mm_tip) {
        std::string tip = std::string("Tip: ") + mm_tip;
        int tw = vulture_text_length(V_FONT_SMALL, tip);

        vulture_put_text_shadow(V_FONT_SMALL, tip, vulture_backdrop,
                                (vulture_screen->w - tw) / 2,
                                vulture_screen->h - 2 * lh,
                                CLR32_GRAY77, CLR32_BLACK);
    }
}

void mm_show_backdrop(void)
{
    mm_compose_backdrop();
    if (vulture_backdrop)
        SDL_BlitSurface(vulture_backdrop, NULL, vulture_screen, NULL);
    else
        SDL_FillRect(vulture_screen, NULL, CLR32_BLACK);
    vulture_refresh();
}

void mm_drop_backdrop(void)
{
    if (vulture_backdrop)
        SDL_FreeSurface(vulture_backdrop);
    vulture_backdrop = NULL;
}

/* ------------------------------------------------------------ menu helpers */

winid mm_start(void)
{
    winid w = vulture_create_nhwindow(NHW_MENU);

    vulture_start_menu(w, MENU_BEHAVE_STANDARD);
    return w;
}

void mm_item(winid w, int id, char acc, const std::string &text)
{
    anything any;

    any = cg.zeroany;
    any.a_int = id;
    vulture_add_menu(w, vulture_menu_glyph(NO_GLYPH), &any, acc, 0, ATR_NONE,
                     NO_COLOR, text.c_str(), MENU_ITEMFLAGS_NONE);
}

void mm_text(winid w, const std::string &text, int attr = ATR_NONE)
{
    anything any = cg.zeroany;

    vulture_add_menu(w, vulture_menu_glyph(NO_GLYPH), &any, 0, 0, attr,
                     NO_COLOR, text.c_str(), MENU_ITEMFLAGS_NONE);
}

/* finish, show, return the picked id (0 = nothing / Esc) */
int mm_pick(winid w, const char *prompt)
{
    menu_item *sel = NULL;
    int id = 0;

    vulture_end_menu(w, prompt);
    if (vulture_select_menu(w, PICK_ONE, &sel) > 0 && sel) {
        id = sel[0].item.a_int;
        free((genericptr_t) sel);
    }
    vulture_destroy_nhwindow(w);
    return id;
}

char mm_acc(int i)
{
    return i < 26 ? (char) ('a' + i) : i < 52 ? (char) ('A' + i - 26) : 0;
}

void mm_message(const std::string &msg)
{
    vulture_messagebox(msg.c_str());
}

bool mm_yes(const std::string &q)
{
    return vulture_yn_function(q.c_str(), "yn", 'n') == 'y';
}

void mm_show_textfile(const std::string &path, const std::string &title)
{
    FILE *fp = fopen(path.c_str(), "r");
    char line[BUFSZ * 2];
    winid w;

    if (!fp) {
        mm_message("Can't open " + path);
        return;
    }
    w = vulture_create_nhwindow(NHW_TEXT);
    if (!title.empty())
        vulture_putstr(w, ATR_BOLD, title.c_str());
    while (fgets(line, sizeof line, fp)) {
        size_t n = strlen(line);

        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        vulture_putstr(w, ATR_NONE, line);
    }
    (void) fclose(fp);
    vulture_display_nhwindow(w, TRUE);
    vulture_destroy_nhwindow(w);
}

bool mm_copy(const std::string &from, const std::string &to)
{
    FILE *in = fopen(from.c_str(), "rb"), *out;
    char buf[8192];
    size_t n;
    bool ok = true;

    if (!in)
        return false;
    if (!(out = fopen(to.c_str(), "wb"))) {
        (void) fclose(in);
        return false;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    (void) fclose(in);
    if (fclose(out) != 0)
        ok = false;
    return ok;
}

std::string mm_stamp(void)
{
    return yyyymmddhhmmss(getnow());
}

/* ----------------------------------------------------------------- actions */

/* start a fresh copy of the game with `rcpath`, then exit this one */
extern "C" void vulture_trace(const char *); /* s98 */
void mm_relaunch(const std::string &rcpath)
{
    vulture_trace(("menu: relaunching with " + rcpath).c_str());
#ifdef _WIN32
    char exepath[MAX_PATH];
    std::string exedir, cmd;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    std::vector<char> cmdbuf;
    DWORD n = GetModuleFileNameA(NULL, exepath, MAX_PATH);

    if (n == 0 || n >= MAX_PATH) {
        mm_message("Couldn't find the game's own program file.");
        return;
    }
    exedir = mm_dirname(exepath);
    cmd = std::string("\"") + exepath + "\" -nethackrc=\"" + rcpath + "\"";
    cmdbuf.assign(cmd.begin(), cmd.end());
    cmdbuf.push_back('\0');

    SetEnvironmentVariableA("VULTURE_SKIP_MENU", "1");
    SetEnvironmentVariableA("VULTURE_SKIP_INTRO", "1");
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(exepath, &cmdbuf[0], NULL, NULL, FALSE,
                        DETACHED_PROCESS, NULL,
                        exedir.empty() ? NULL : exedir.c_str(), &si, &pi)) {
        SetEnvironmentVariableA("VULTURE_SKIP_MENU", NULL);
        SetEnvironmentVariableA("VULTURE_SKIP_INTRO", NULL);
        mm_message("Couldn't start the game with " + mm_basename(rcpath));
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    mm_drop_backdrop();
    vulture_fade_out(0.2);
    exit(EXIT_SUCCESS); /* no lock or level files exist yet */
#else
    (void) rcpath;
#endif
}

/* resume hero `name`: with its roster rc if it has one (relaunch), else in
   process.  Returns true when the caller should leave the menu. */
bool mm_resume(const std::string &name)
{
    std::vector<mm_hero> heroes = mm_list_heroes(), match;
    size_t i;

    for (i = 0; i < heroes.size(); i++)
        if (!strcmpi(heroes[i].name.c_str(), name.c_str()))
            match.push_back(heroes[i]);

    if (match.size() == 1) {
        mm_relaunch(match[0].rcpath);
        return false; /* only reached if the relaunch failed */
    }
    if (match.size() > 1) {
        winid w = mm_start();
        int id;

        mm_text(w, "This hero has more than one settings file.");
        mm_text(w, "");
        for (i = 0; i < match.size(); i++)
            mm_item(w, (int) i + 1, mm_acc((int) i),
                    mm_hero_line(match[i]) + "   (" + match[i].file + ")");
        id = mm_pick(w, ("Which settings for " + name + "?").c_str());
        if (id > 0)
            mm_relaunch(match[id - 1].rcpath);
        return false;
    }
    (void) strncpy(svp.plname, name.c_str(), PL_NSIZ - 1);
    svp.plname[PL_NSIZ - 1] = '\0';
    return true;
}

bool mm_load_menu(const std::vector<mm_save> &saves)
{
    winid w = mm_start();
    size_t i;
    int id;

    for (i = 0; i < saves.size(); i++)
        mm_item(w, (int) i + 1, mm_acc((int) i),
                saves[i].name + " - " + saves[i].desc + "   (saved "
                    + mm_when(saves[i].mtime) + ")");
    id = mm_pick(w, "Load which game?");
    return id > 0 && mm_resume(saves[id - 1].name);
}

void mm_roster_menu(const std::vector<mm_save> &saves)
{
    std::vector<mm_hero> heroes = mm_list_heroes();
    winid w;
    size_t i, k;
    int id;

    if (heroes.empty()) {
        mm_message("No hero settings files found in " + mm_configdir()
                   + " (files named <hero>.nethackrc with a name: option).");
        return;
    }
    w = mm_start();
    for (i = 0; i < heroes.size(); i++) {
        std::string line = mm_hero_line(heroes[i]);

        for (k = 0; k < saves.size(); k++)
            if (!strcmpi(saves[k].name.c_str(), heroes[i].name.c_str())) {
                line += "   [saved game]";
                break;
            }
        mm_item(w, (int) i + 1, mm_acc((int) i), line);
    }
    id = mm_pick(w, "Play which hero?");
    if (id > 0)
        mm_relaunch(heroes[id - 1].rcpath);
}

bool mm_new_hero(void)
{
    char buf[BUFSZ];

    buf[0] = '\0';
    if (!vulture_get_input(-1, vulture_screen->h - V_UI(170),
                           "What is your name?  (leave it blank to be Nameless)", buf, ""))
        return false;
    std::string name = mm_trim(buf);
    /* s77: blank -> "Nameless"; newgame() asks the local model for a name
     * from the starting kit, stats and role (llm_name_hero, sounds.c) */
    if (name.empty())
        name = "Nameless";
    if (mm_exists(mm_save_path(name.c_str()))
        && !mm_yes(name + " already has a saved game. Resume it?"))
        return false;
    (void) strncpy(svp.plname, name.c_str(), PL_NSIZ - 1);
    svp.plname[PL_NSIZ - 1] = '\0';
    return true;
}

void mm_scores(void)
{
    std::string path = fqname(RECORD, SCOREPREFIX, 0);
    FILE *fp = fopen(path.c_str(), "r");
    char line[BUFSZ * 2];
    winid w;
    int rank = 0;

    if (!fp) {
        mm_message("No high scores yet.");
        return;
    }
    w = mm_start();
    mm_text(w, "Rank\tPoints\tHero\tDate\tFate", ATR_BOLD);
    while (rank < 40 && fgets(line, sizeof line, fp)) {
        int vmaj, vmin, vpat, dnum, dlev, maxlvl, hp, maxhp, deaths, uid;
        long points, ddate, bdate;
        char role[16], race[16], gend[16], algn[16], rest[BUFSZ * 2];

        if (sscanf(line,
                   "%d.%d.%d %ld %d %d %d %d %d %d %ld %ld %d %15s %15s "
                   "%15s %15s %[^\n]",
                   &vmaj, &vmin, &vpat, &points, &dnum, &dlev, &maxlvl, &hp,
                   &maxhp, &deaths, &ddate, &bdate, &uid, role, race, gend,
                   algn, rest) != 18)
            continue;
        std::string r = rest, nm = r, fate;
        size_t comma = r.find(',');

        if (comma != std::string::npos)
            nm = r.substr(0, comma), fate = r.substr(comma + 1);
        char row[BUFSZ * 3];
        Snprintf(row, sizeof row,
                 "%d.\t%ld\t%s (%s %s)\t%04ld-%02ld-%02ld\t%s on DL%d",
                 ++rank, points, nm.c_str(), race, mm_lookup(role, 0),
                 ddate / 10000, (ddate / 100) % 100, ddate % 100,
                 fate.c_str(), dlev);
        mm_text(w, row);
    }
    (void) fclose(fp);
    if (!rank)
        mm_text(w, "No games on record yet.");
    (void) mm_pick(w, "High scores");
}

/* "You died in Sokoban on dungeon level 4 with 4888 points," */
std::string mm_dumplog_fate(const std::string &path)
{
    FILE *fp = fopen(path.c_str(), "r");
    char line[BUFSZ * 2];
    std::string fate;

    if (!fp)
        return fate;
    while (fgets(line, sizeof line, fp)) {
        std::string l = mm_trim(line);

        if (!strncmp(l.c_str(), "You ", 4) && l.find(" points") != std::string::npos
            && l.find(" with ") != std::string::npos) {
            fate = l;
            if (!fate.empty() && fate[fate.size() - 1] == ',')
                fate.erase(fate.size() - 1);
        }
    }
    (void) fclose(fp);
    return fate;
}

void mm_graveyard(void)
{
    std::vector<mm_file> logs;
    winid w;
    size_t i;
    int id;

    if (!sysopt.dumplogfile || !*sysopt.dumplogfile) {
        mm_message("Dumplogs are turned off (DUMPLOGFILE in sysconf).");
        return;
    }
    logs = mm_find(mm_dirname(sysopt.dumplogfile), "*.log");
    if (logs.empty()) {
        mm_message("No past games recorded yet.");
        return;
    }
    if (logs.size() > 52)
        logs.resize(52);
    w = mm_start();
    for (i = 0; i < logs.size(); i++) {
        std::string nm = logs[i].file, fate = mm_dumplog_fate(logs[i].path);
        size_t dash = nm.rfind('-');

        if (dash != std::string::npos)
            nm = nm.substr(0, dash);
        mm_item(w, (int) i + 1, mm_acc((int) i),
                nm + " - " + mm_when(logs[i].mtime)
                    + (fate.empty() ? std::string("") : " - " + fate));
    }
    id = mm_pick(w, "Read which game's log?");
    if (id > 0)
        mm_show_textfile(logs[id - 1].path, "");
}

void mm_backups(const std::vector<mm_save> &saves)
{
    std::string sdir = mm_savedir(), bdir = sdir + "backups";
    std::vector<mm_file> files = mm_find(bdir, "*");
    winid w;
    size_t i;
    int id;

    w = mm_start();
    mm_text(w, "Every saved game is copied here when you load it.");
    mm_text(w, "Restoring one rewinds that hero to that moment.");
    mm_text(w, "");
    if (!saves.empty())
        mm_item(w, 1000, '-', "Archive a saved game (move it in here)...");
    if (files.size() > 50)
        files.resize(50);
    for (i = 0; i < files.size(); i++) {
        std::string nm = files[i].file;
        size_t cut = nm.find(".NetHack-saved-game");

        if (cut != std::string::npos)
            nm = nm.substr(0, cut);
        mm_item(w, (int) i + 1, mm_acc((int) i),
                "Restore " + nm + " from " + mm_when(files[i].mtime));
    }
    if (files.empty())
        mm_text(w, "(no backups yet)");
    id = mm_pick(w, "Save backups");

    if (id == 1000) {
        winid w2 = mm_start();
        int id2;

        for (i = 0; i < saves.size(); i++)
            mm_item(w2, (int) i + 1, mm_acc((int) i),
                    saves[i].name + " - " + saves[i].desc);
        id2 = mm_pick(w2, "Archive which saved game?");
        if (id2 > 0) {
            const mm_save &s = saves[id2 - 1];
            std::string to = bdir + "\\" + mm_basename(s.path) + "-" + mm_stamp();

            if (mm_yes("Move " + s.name + "'s saved game into backups?")) {
#ifdef _WIN32
                CreateDirectoryA(bdir.c_str(), NULL);
#endif
                if (rename(s.path.c_str(), to.c_str()) == 0)
                    mm_message(s.name + "'s game was moved to backups.");
                else
                    mm_message("Couldn't move the saved game.");
            }
        }
        return;
    }
    if (id <= 0)
        return;

    {
        const mm_file &b = files[id - 1];
        std::string base = b.file;
        size_t dash = base.rfind('-');

        if (dash == std::string::npos || base.size() - dash - 1 != 14) {
            mm_message("That doesn't look like a save backup.");
            return;
        }
        base = base.substr(0, dash);
        std::string target = sdir + base;

        if (mm_exists(target)) {
            if (!mm_yes("Replace the current saved game? (It is backed up first.)"))
                return;
            if (!mm_copy(target, bdir + "\\" + base + "-" + mm_stamp())) {
                mm_message("Couldn't back up the current save; nothing changed.");
                return;
            }
        } else if (!mm_yes("Restore this backup as a saved game?")) {
            return;
        }
        if (mm_copy(b.path, target))
            mm_message("Restored. It's now under Continue / Load a saved game.");
        else
            mm_message("Couldn't restore the backup.");
    }
}

void mm_guidebook(void)
{
    char exepath[MAX_PATH];
    std::string p;

#ifdef _WIN32
    if (GetModuleFileNameA(NULL, exepath, MAX_PATH))
        p = mm_dirname(exepath) + "Guidebook.txt";
#endif
    if (p.empty() || !mm_exists(p))
        p = std::string(gf.fqn_prefix[HACKPREFIX] ? gf.fqn_prefix[HACKPREFIX] : "")
            + "Guidebook.txt";
    mm_show_textfile(p, "");
}

enum {
    MM_CONTINUE = 1, MM_LOAD, MM_ROSTER, MM_NEW, MM_SCORES, MM_GRAVE,
    MM_BACKUPS, MM_GUIDE, MM_QUIT
};

} /* namespace */

extern "C" void vulture_trace(const char *); /* s98 */

extern "C" void vulture_main_menu(int launched_with_rc)
{
    bool first = true;

    if (launched_with_rc || getenv("VULTURE_SKIP_MENU")) {
#ifdef _WIN32
        SetEnvironmentVariableA("VULTURE_SKIP_MENU", NULL);
#endif
        return;
    }

    srand((unsigned) time(NULL));
    mm_tip = mm_tips[rand() % (int) (sizeof mm_tips / sizeof mm_tips[0])];

    for (;;) {
        vulture_trace("menu: listing saves");
        std::vector<mm_save> saves = mm_list_saves();
        winid w;
        int id;

        vulture_trace("menu: drawing backdrop");
        mm_show_backdrop();
        if (first) {
            /* fade the title in once; later passes just redraw it */
            vulture_fade_in(0.4);
            first = false;
        }

        w = mm_start();
        if (!saves.empty()) {
            mm_text(w, "Last played: " + saves[0].name + ", "
                           + mm_ago(saves[0].mtime), ATR_BOLD);
            mm_text(w, "");
            mm_item(w, MM_CONTINUE, 'c',
                    "Continue - " + saves[0].name + ", " + saves[0].desc);
            mm_item(w, MM_LOAD, 'l', "Load a saved game...");
        }
        mm_item(w, MM_ROSTER, 'r', "Play a hero from your roster...");
        mm_item(w, MM_NEW, 'n', "Create a new hero");
        mm_text(w, "");
        mm_item(w, MM_SCORES, 'h', "High scores");
        mm_item(w, MM_GRAVE, 'p', "Past games (dumplogs)");
        mm_item(w, MM_BACKUPS, 'b', "Save backups...");
        mm_item(w, MM_GUIDE, 'g', "Read the Guidebook");
        mm_text(w, "");
        mm_item(w, MM_QUIT, 'q', "Quit");
        vulture_trace("menu: waiting for a choice");
        id = mm_pick(w, "Main menu");
        {
            char tb[64];

            snprintf(tb, sizeof tb, "menu: picked %d", id);
            vulture_trace(tb);
        }

        switch (id) {
        case MM_CONTINUE:
            if (mm_resume(saves[0].name))
                goto done;
            break;
        case MM_LOAD:
            if (mm_load_menu(saves))
                goto done;
            break;
        case MM_ROSTER:
            mm_roster_menu(saves);
            break;
        case MM_NEW:
            if (mm_new_hero())
                goto done;
            break;
        case MM_SCORES:
            mm_scores();
            break;
        case MM_GRAVE:
            mm_graveyard();
            break;
        case MM_BACKUPS:
            mm_backups(saves);
            break;
        case MM_GUIDE:
            mm_guidebook();
            break;
        case MM_QUIT:
            mm_drop_backdrop();
            vulture_fade_out(0.2);
            exit(EXIT_SUCCESS); /* nothing to save or unlock yet */
        default:
            break; /* Esc: show the menu again */
        }
    }
done:
    mm_drop_backdrop();
    vulture_fade_out(0.2);
}
