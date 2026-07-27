/* Path B loader/break test (display-independent).
 * Parses vulture_tiles.conf against real NetHack data and asserts, for every
 * category, that its ERROR tile: resolves to a valid index; owns the error PNG
 * (ptr == -1, file exists); is not V_TILE_NONE / tile 0 / ordinary game art.
 * Also verifies no game_tiles[] overflow (GAMETILECOUNT == sum of typecounts),
 * and (break mode) that an unmatched symbol redirects to its category ERROR tile.
 *
 * Usage: test_pathb_loader <config> <tiles_dir> [probe_cat probe_idx expect]
 *   expect = "error"  -> probe tile must redirect to its category ERROR
 *   expect = "own"    -> probe tile must own a file (ptr == -1)
 */
extern "C" {
#include "hack.h"
}
#include "vulture_tile.h"
#include "vulture_tileconfig.h"
#include "vulture_opt.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <sys/stat.h>

/* --- symbols referenced by vulture_tileconfig.o / the generated parser --- */
struct instance_flags iflags;             /* NetHack global (zeroed; window not inited) */
struct vulture_optstruct vulture_opts;    /* zeroed -> debug off, load loop skipped     */
void pline(const char *, ...) {}
void vulture_write_log(int, const char *, int, const char *, ...) {}
vulture_tile *vulture_load_tile(int) { return 0; }

struct catinfo { int tt; const char *name; int offset; };

static int fail = 0;
#define CHECK(cond, msg, ...) do { if (!(cond)) { \
    printf("  FAIL: " msg "\n", ##__VA_ARGS__); fail++; } } while (0)

static bool file_exists(const char *dir, const std::string &fn)
{
    size_t slash = fn.find_last_of('/');
    std::string base = (slash == std::string::npos) ? fn : fn.substr(slash + 1);
    std::string p = std::string(dir) + "/" + base;
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: %s <config> <tiles_dir> [cat idx expect]\n", argv[0]); return 2; }
    const char *conf = argv[1], *tiles_dir = argv[2];

    FILE *fp = fopen(conf, "r");
    if (!fp) { printf("cannot open %s\n", conf); return 2; }
    gametiles *gt = 0;
    vulture_parse_tileconf(fp, &gt);
    fclose(fp);
    if (!gt) { printf("parse returned no tiles\n"); return 2; }

    /* recompute the per-type offsets exactly as vulture_parse_tileconf does */
    int off[NUM_TILETYPES];
    off[0] = 0;
    int total = vulture_typecount[0];
    for (int i = 1; i < NUM_TILETYPES; i++) {
        off[i] = off[i - 1] + vulture_typecount[i - 1];
        total += vulture_typecount[i];
    }
    printf("parsed OK: GAMETILECOUNT=%d  sum(typecount)=%d\n", GAMETILECOUNT, total);
    CHECK(total == GAMETILECOUNT, "typecount sum %d != GAMETILECOUNT %d (overflow risk)",
          total, GAMETILECOUNT);

    struct catinfo cats[] = {
        { TT_OBJECT, "object", 0 }, { TT_OBJICON, "objicon", 0 },
        { TT_MONSTER, "monster", 0 }, { TT_STATUE, "statue", 0 },
        { TT_FIGURINE, "figurine", 0 }, { TT_MISC, "misc", 0 },
        { TT_FLOOR, "floor", 0 }, { TT_WALL, "wall", 0 },
        { TT_EXPL, "explosion", 0 }, { TT_CURSOR, "cursor", 0 },
        { TT_EDGE, "edge", 0 },
    };
    printf("--- per-category ERROR placeholder checks ---\n");
    for (unsigned c = 0; c < sizeof(cats) / sizeof(cats[0]); c++) {
        int tt = cats[c].tt;
        char errname[] = "ERROR";
        int idx = vulture_get_tile_index(tt, errname, 0);   /* within-category */
        CHECK(idx >= 0, "%s: ERROR name does not resolve to an index", cats[c].name);
        if (idx < 0) continue;
        int abs = off[tt] + idx;
        CHECK(abs != 0, "%s: ERROR resolves to tile 0", cats[c].name);
        CHECK(abs != V_TILE_NONE, "%s: ERROR == V_TILE_NONE", cats[c].name);
        CHECK(abs >= 0 && abs < GAMETILECOUNT, "%s: ERROR abs %d out of range", cats[c].name, abs);
        CHECK(gt[abs].ptr == -1, "%s: ERROR tile does not own a file (ptr=%d)", cats[c].name, gt[abs].ptr);
        bool named = gt[abs].filename.find(std::string(cats[c].name) + ".error.png") != std::string::npos;
        CHECK(named, "%s: ERROR filename '%s' is not <cat>.error.png", cats[c].name, gt[abs].filename.c_str());
        CHECK(file_exists(tiles_dir, gt[abs].filename),
              "%s: ERROR image '%s' not found on disk", cats[c].name, gt[abs].filename.c_str());
        printf("  OK  %-9s ERROR -> abs %d  '%s'\n", cats[c].name, abs, gt[abs].filename.c_str());
    }

    /* optional intentional-break probe: <cat> <within-idx> <expect> */
    if (argc >= 6) {
        const char *pc = argv[3]; int pidx = atoi(argv[4]); const char *expect = argv[5];
        int tt = -1;
        for (unsigned c = 0; c < sizeof(cats)/sizeof(cats[0]); c++)
            if (!strcmp(cats[c].name, pc)) tt = cats[c].tt;
        char errname[] = "ERROR";
        int err_abs = off[tt] + vulture_get_tile_index(tt, errname, 0);
        int probe_abs = off[tt] + pidx;
        printf("--- break probe: %s[%d] abs=%d ptr=%d (err_abs=%d), expect=%s ---\n",
               pc, pidx, probe_abs, gt[probe_abs].ptr, err_abs, expect);
        if (!strcmp(expect, "error"))
            CHECK(gt[probe_abs].ptr == err_abs,
                  "%s[%d] did not redirect to ERROR (ptr=%d, want %d)", pc, pidx, gt[probe_abs].ptr, err_abs);
        else
            CHECK(gt[probe_abs].ptr == -1, "%s[%d] expected to own a file but ptr=%d", pc, pidx, gt[probe_abs].ptr);
    }

    printf(fail ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", fail);
    return fail ? 1 : 0;
}
