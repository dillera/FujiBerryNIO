/*
 * FujiNet Disks: browse the FujiNet's disk images and mount them, from the
 * classic Mac, through the floppy port.
 *
 * Every call is FujiBus carried in HD20 block I/O (fujinet-nio-lib, platform
 * mac68k): the FileService lists the directory, the DiskService mounts.
 * Slots 1-4 are the HD20 units (the Mac looks for them when it starts up),
 * slot 5 is the floppy, which appears in the external drive at once.
 *
 * Keys: up/down or j/k select (the Plus keyboard has no arrows), Return or
 * a double-click open a folder, Delete up a folder,
 * 1-5 mount the selected image in that slot, Command-1..5 eject it, Q quit.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>

#include "fujinet-nio.h"
#include "fn_raw.h"

/* The first of these the FujiNet can list: POSIX NIO has "host", an ESP32
 * has its internal flash and the SD card. */
static const char *const roots[] = {"host:/mac/", "flash:/mac/", "sd0:/"};
static const char *root = "host:/mac/";

#define FILE_SERVICE 0xFE
#define FILE_LIST 0x02
#define SLOTS 5
#define FLOPPY_SLOT 5
#define EXT_FLOPPY_DRIVE 2

#define MAX_ENTRIES 64
#define NAME_MAX_LEN 48
#define LINE_H 12
#define MARGIN 6
#define LIST_TOP 118
#define LIST_ROWS 14

struct entry {
    char name[NAME_MAX_LEN];
    unsigned char is_dir;
    unsigned long size;
};

static WindowPtr win;
static struct entry entries[MAX_ENTRIES];
static short n_entries, sel, top;
static char dir[160];
static char status_line[120];
static fn_disk_info_t slot_info[SLOTS];
static uint8_t slot_err[SLOTS];
static uint8_t reply[1024];

static void text_at(short x, short y, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    size_t n;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    n = strlen(buf);
    MoveTo(x, y);
    DrawText(buf, 0, (short)n);
}

static void set_status(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(status_line, sizeof(status_line), fmt, ap);
    va_end(ap);
}

/* GetDblTime() is glue Retro68 does not ship; read the low-memory global.
 * GCC flags any fixed-address access as out of bounds. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static long double_click_ticks(void)
{
    return LMGetDoubleTime();
}
#pragma GCC diagnostic pop

static unsigned short le16(const uint8_t *p) { return (unsigned short)(p[0] | (p[1] << 8)); }

/* FileService ListDirectory, compact and sorted, all pages. */
static uint8_t load_dir(void)
{
    uint8_t req[200];
    size_t ulen = strlen(dir);
    unsigned short start = 0;
    fn_raw_response_t resp;

    n_entries = 0;
    sel = top = 0;
    if (strcmp(dir, root) != 0) {
        strcpy(entries[0].name, "..");
        entries[0].is_dir = 1;
        n_entries = 1;
    }
    for (;;) {
        uint8_t r, flags;
        unsigned short count, len, i, off = 0;
        size_t p = 0;

        req[p++] = 1;
        req[p++] = (uint8_t)ulen;
        req[p++] = (uint8_t)(ulen >> 8);
        memcpy(req + p, dir, ulen);
        p += ulen;
        req[p++] = (uint8_t)start;
        req[p++] = (uint8_t)(start >> 8);
        req[p++] = (uint8_t)(sizeof(reply) - 64);
        req[p++] = (uint8_t)((sizeof(reply) - 64) >> 8);
        req[p++] = 0x03; /* compact, sorted */

        r = fn_raw_call(FILE_SERVICE, FILE_LIST, req, (uint16_t)p, reply, sizeof(reply), &resp);
        if (r != FN_OK || resp.status != 0) {
            set_status("Cannot list %s (%s, status %u)", dir, fn_error_string(r), resp.status);
            return 0;
        }
        flags = reply[1];
        count = le16(reply + 6);
        len = le16(reply + 8);
        for (i = 0; i < count && off < len && n_entries < MAX_ENTRIES; ++i) {
            const uint8_t *e = reply + 10 + off;
            uint8_t nlen = e[1];
            struct entry *d = &entries[n_entries];

            d->is_dir = e[0] & 1;
            if (nlen >= NAME_MAX_LEN)
                nlen = NAME_MAX_LEN - 1;
            memcpy(d->name, e + 2, nlen);
            d->name[nlen] = 0;
            d->size = 0;
            off = (unsigned short)(off + 2 + e[1]);
            if (d->name[0] != '.')
                ++n_entries;
        }
        start = (unsigned short)(start + count);
        if (!(flags & 1) || count == 0 || n_entries >= MAX_ENTRIES)
            break;
    }
    set_status("%d item(s) in %s", n_entries, dir);
    return 1;
}

static void load_slots(void)
{
    uint8_t s;

    for (s = 0; s < SLOTS; ++s) {
        memset(&slot_info[s], 0, sizeof(slot_info[s]));
        slot_err[s] = fn_disk_info((uint8_t)(s + 1), &slot_info[s]);
    }
}

static void draw(void)
{
    Rect r = win->portRect;
    short i, y;

    EraseRect(&r);
    TextFace(bold);
    text_at(MARGIN, 14, "FujiNet Disks  -  over the floppy port");
    TextFace(0);
    for (i = 0; i < SLOTS; ++i) {
        const fn_disk_info_t *d = &slot_info[i];

        y = 32 + i * LINE_H;
        if (slot_err[i] == FN_OK && (d->flags & FN_DISK_FLAG_MOUNTED))
            text_at(MARGIN, y, "%d %-7s %lu blocks, %s", i + 1, i == 4 ? "Floppy" : "HD20",
                    (unsigned long)d->sector_count * d->sector_size / 512,
                    (d->flags & FN_DISK_FLAG_READONLY) ? "read only" : "read/write");
        else
            text_at(MARGIN, y, "%d %-7s empty", i + 1, i == 4 ? "Floppy" : "HD20");
    }
    MoveTo(MARGIN, 96);
    LineTo(r.right - MARGIN, 96);
    text_at(MARGIN, 110, "%s", dir);

    for (i = 0; i < LIST_ROWS && top + i < n_entries; ++i) {
        const struct entry *e = &entries[top + i];
        Rect row;

        y = LIST_TOP + (i + 1) * LINE_H;
        text_at(MARGIN + 8, y, "%s%s", e->name, e->is_dir ? "/" : "");
        if (top + i == sel) {
            SetRect(&row, MARGIN, y - LINE_H + 3, r.right - MARGIN, y + 3);
            InvertRect(&row);
        }
    }
    y = r.bottom - 22;
    MoveTo(MARGIN, y - 10);
    LineTo(r.right - MARGIN, y - 10);
    text_at(MARGIN, y, "%s", status_line);
    text_at(MARGIN, r.bottom - 8,
            "j/k select  Return open  Delete up  1-5 mount  Cmd-1..5 eject");
}

static void open_selected(void)
{
    struct entry *e = &entries[sel];

    if (n_entries == 0 || !e->is_dir)
        return;
    if (strcmp(e->name, "..") == 0) {
        size_t n = strlen(dir);

        if (n > 1)
            dir[n - 1] = 0;            /* drop the trailing '/' */
        *(strrchr(dir, '/') + 1) = 0;  /* and the last component */
    } else if (strlen(dir) + strlen(e->name) + 2 < sizeof(dir)) {
        strcat(dir, e->name);
        strcat(dir, "/");
    }
    load_dir();
}

static void mount_selected(uint8_t slot)
{
    struct entry *e = &entries[sel];
    char uri[sizeof(dir) + NAME_MAX_LEN];
    fn_disk_info_t info;
    uint8_t r;

    if (n_entries == 0 || e->is_dir) {
        set_status("Select a disk image first");
        return;
    }
    snprintf(uri, sizeof(uri), "%s%s", dir, e->name);
    r = fn_disk_mount(slot, uri, 0, FN_DISK_TYPE_RAW, 512, &info);
    if (r != FN_OK)
        set_status("Mount %s in %d failed: %s", e->name, slot, fn_error_string(r));
    else if (slot == FLOPPY_SLOT)
        set_status("%s is in the external drive", e->name);
    else
        set_status("%s in HD20 slot %d: restart the Mac to use it", e->name, slot);
    load_slots();
}

static void eject_slot(uint8_t slot)
{
    uint8_t r;

    if (slot == FLOPPY_SLOT) {
        /* Eject from the Mac's side, like the Finder: the drive's eject
         * reaches the FujiNet, which unmounts the slot. */
        OSErr err = Eject(NULL, EXT_FLOPPY_DRIVE);
        set_status(err == noErr ? "Floppy ejected" : "Eject failed (%d)", err);
    } else {
        r = fn_disk_unmount(slot);
        set_status(r == FN_OK ? "HD20 slot %d emptied: restart the Mac" : "Unmount failed",
                   slot);
    }
    load_slots();
}

int main(void)
{
    Rect bounds;
    EventRecord ev;
    long last_click = 0;
    uint8_t r;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    bounds = qd.screenBits.bounds;
    InsetRect(&bounds, 8, 8);
    bounds.top += 30;
    win = NewWindow(NULL, &bounds, "\pFujiNet Disks", true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(win);
    TextFont(4); /* Monaco */
    TextSize(9);

    r = fn_init();
    if (r != FN_OK) {
        set_status("No FujiNet found: %s", fn_error_string(r));
    } else {
        unsigned i;

        load_slots();
        for (i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
            root = roots[i];
            strcpy(dir, root);
            if (load_dir())
                break;
        }
    }
    draw();

    for (;;) {
        if (!GetNextEvent(everyEvent, &ev))
            continue;
        if (ev.what == updateEvt) {
            BeginUpdate(win);
            draw();
            EndUpdate(win);
        } else if (ev.what == mouseDown) {
            Point p = ev.where;
            short row;

            GlobalToLocal(&p);
            row = (short)((p.v - LIST_TOP) / LINE_H);
            if (p.v > LIST_TOP && row < LIST_ROWS && top + row < n_entries) {
                /* a second click on the selected row opens a folder */
                if (sel == top + row && ev.when - last_click <= double_click_ticks())
                    open_selected();
                else
                    sel = (short)(top + row);
                last_click = ev.when;
                draw();
            }
        } else if (ev.what == keyDown || ev.what == autoKey) {
            char c = (char)(ev.message & charCodeMask);

            if (c >= '1' && c <= '5') {
                if (ev.modifiers & cmdKey)
                    eject_slot((uint8_t)(c - '0'));
                else
                    mount_selected((uint8_t)(c - '0'));
            } else if (c == 'q' || c == 'Q') {
                break;
            } else if ((c == 0x1E || c == 'k') && sel > 0) {             /* up */
                --sel;
            } else if ((c == 0x1F || c == 'j') && sel + 1 < n_entries) { /* down */
                ++sel;
            } else if (c == '\r') {
                open_selected();
            } else if (c == 8 && strcmp(dir, root) != 0) {
                sel = 0;
                open_selected();
            }
            if (sel < top)
                top = sel;
            if (sel >= top + LIST_ROWS)
                top = (short)(sel - LIST_ROWS + 1);
            draw();
        }
    }
    return 0;
}
