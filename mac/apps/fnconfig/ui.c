/* Origin: fujinet-mac-da src/ui.c (commit d029216), adapted for fujinet-nio. */
/*
 * FujiNet CONFIG for classic Mac — event-driven UI core.
 *
 * Views map to the original CONFIG states (arch.md section 5):
 *   VIEW_HOSTS = HOSTS_AND_DEVICES, VIEW_FILES = SELECT_FILE,
 *   VIEW_SLOT = SELECT_SLOT, VIEW_INFO = SHOW_INFO.
 * WiFi is a passive status check only (POC scope).
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <string.h>
#include <stdio.h>

#include "fuji.h"
#include "constants.h"
#include "ui.h"

enum {
    VIEW_HOSTS,
    VIEW_FILES,
    VIEW_SLOT,
    VIEW_INFO
};

enum { PANE_HOSTS, PANE_DEVICES };

#define WIN_W      CONFIG_WIN_WIDTH

#ifdef COMPACT_UI
/* DA layout: 360x200 window */
#define LINE_H      11
#define LEFT         6
#define ROW_RIGHT  (WIN_W - 8)
#define HEAD_Y      12
#define HOSTS_Y0    26
#define DEVICES_LBL 116
#define DEVICES_Y0 128
#define FILES_Y0    38
#define SLOT_Y0     68
#define HELP_Y     192
#define STATUS_Y   210
#else
/* application layout: fills a 9" 512x342 screen */
#define LINE_H      13
#define LEFT         8
#define ROW_RIGHT  (WIN_W - 8)
#define HEAD_Y      16
#define HOSTS_Y0    44
#define DEVICES_LBL 156
#define DEVICES_Y0 170
#define FILES_Y0    44
#define SLOT_Y0     86
#define HELP_Y     288
#define STATUS_Y   308
#endif

typedef struct {
    WindowPtr win;
    int view;

    /* hosts & devices */
    HostSlot hosts[NUM_HOST_SLOTS];
    DeviceSlot devices[NUM_DEVICE_SLOTS];
    int pane;
    int hostSel;
    int devSel;
    Boolean editing;
    char editBuf[sizeof(HostSlot)];
    int editLen;

    /* file browser */
    uint8_t curHost;
    char path[DIR_MAX_LEN];
    uint16_t pageBase;
    int fileSel;
    int pageCount;
    Boolean atEOF;
    char pageEntries[ENTRIES_PER_PAGE][40];

    /* slot pick */
    char selectedFile[256];
    int slotSel;
    uint8_t slotMode;

    char wifiLine[80];
    char status[100];
} ConfigState;

static ConfigState g;

/* ---- small drawing helpers -------------------------------------- */

static void drawCStr(short x, short y, const char *s)
{
    MoveTo(x, y);
    DrawText((Ptr)s, 0, (short)strlen(s));
}

static void invertRow(short baseline)
{
    Rect r;
    SetRect(&r, LEFT - 3, baseline - 10, ROW_RIGHT, baseline + 3);
    InvertRect(&r);
}

static void setStatus(const char *msg)
{
    strncpy(g.status, msg, sizeof(g.status) - 1);
    g.status[sizeof(g.status) - 1] = '\0';
}

static void redraw(void)
{
    InvalRect(&g.win->portRect);
}

/* NIO's units: 1-4 are HD20s, the last one is the 800K floppy */
static const char *slotName(int i)
{
    static char buf[8];
    if (i == NUM_DEVICE_SLOTS - 1)
        return "FD ";
    sprintf(buf, "HD%d", i + 1);
    return buf;
}

static const char *hostTag(uint8_t hs)
{
    static char buf[8];
    if (hs >= NUM_HOST_SLOTS)
        return "boot  ";           /* mounted by NIO's config, no host */
    sprintf(buf, "host %d", hs + 1);
    return buf;
}

/* the Mac Plus keyboard has no arrow keys and no Esc */
static char mapKey(char key)
{
    switch (key) {
    case 'j': case 'J': return 0x1F;          /* down */
    case 'k': case 'K': return 0x1E;          /* up */
    case ',': case '<': return 0x1C;          /* left: previous page */
    case '.': case '>': return 0x1D;          /* right: next page */
    case '`': case '~': return 0x1B;          /* Esc */
    }
    return key;
}

/* ---- data refresh ------------------------------------------------ */

static void refreshSlots(void)
{
    if (!fuji_get_host_slots(g.hosts, NUM_HOST_SLOTS)) {
        setStatus("No FujiNet found on the floppy port.");
        return;
    }
    fuji_get_device_slots(g.devices, NUM_DEVICE_SLOTS);
}

static void checkWifi(void)
{
    uint8_t s = 0;
    NetConfig nc;

    if (!fuji_get_wifi_enabled()) {
        strcpy(g.wifiLine, "WiFi: disabled");
        return;
    }
    fuji_get_wifi_status(&s);
    if (s == WIFI_STATUS_CONNECTED) {
        fuji_get_ssid(&nc);
        sprintf(g.wifiLine, "WiFi: connected (%s)", nc.ssid);
    } else {
        sprintf(g.wifiLine, "WiFi: not connected (status %d)", s);
    }
}

/* ---- file browser paging ----------------------------------------- */

/* Load one page of directory entries. Stateless per arch.md section 4:
   open, seek, read rows, one extra read to probe EOF, close. */
static Boolean loadPage(void)
{
    char buf[64];
    int i;

    g.pageCount = 0;
    g.atEOF = true;

    if (!fuji_open_directory2(g.curHost, g.path, "")) {
        setStatus("Could not open directory.");
        return false;
    }
    fuji_set_directory_position(g.pageBase);

    for (i = 0; i < ENTRIES_PER_PAGE; i++) {
        if (!fuji_read_directory(sizeof(g.pageEntries[0]), 0, buf))
            break;
        if (buf[1] == DIR_EOF)
            break;
        strncpy(g.pageEntries[i], buf, sizeof(g.pageEntries[0]) - 1);
        g.pageEntries[i][sizeof(g.pageEntries[0]) - 1] = '\0';
        g.pageCount++;
    }

    if (g.pageCount == ENTRIES_PER_PAGE) {
        /* probe one entry past the page to see if there are more */
        if (fuji_read_directory(sizeof(buf), 0, buf) && buf[1] != DIR_EOF)
            g.atEOF = false;
    }

    fuji_close_directory();

    if (g.fileSel >= g.pageCount)
        g.fileSel = g.pageCount ? g.pageCount - 1 : 0;
    return true;
}

static Boolean entryIsFolder(const char *name)
{
    size_t n = strlen(name);
    return n > 0 && name[n - 1] == '/';
}

/* ---- view transitions --------------------------------------------- */

static void enterHosts(void)
{
    /* the FujiNet is the source of truth: re-fetch on every entry */
    refreshSlots();
    g.view = VIEW_HOSTS;
    g.editing = false;
    redraw();
}

static void enterBrowser(uint8_t hs)
{
    if (g.hosts[hs][0] == '\0') {
        setStatus("Host slot is empty.");
        redraw();
        return;
    }
    if (!fuji_mount_host_slot(hs)) {
        setStatus("Could not mount host (host down / bad name).");
        redraw();
        return;
    }
    g.curHost = hs;
    strcpy(g.path, "/");
    g.pageBase = 0;
    g.fileSel = 0;
    if (loadPage()) {
        g.view = VIEW_FILES;
        setStatus("");
    }
    redraw();
}

/* canonical selected-name fetch + commit prep (arch.md section 4) */
static void chooseEntry(void)
{
    char name[64];
    const char *entry;

    if (g.fileSel >= g.pageCount)
        return;
    entry = g.pageEntries[g.fileSel];

    if (entryIsFolder(entry)) {
        if (strlen(g.path) + strlen(entry) < sizeof(g.path) - 1) {
            strcat(g.path, entry);
            g.pageBase = 0;
            g.fileSel = 0;
            loadPage();
        } else {
            setStatus("Path too long.");
        }
        redraw();
        return;
    }

    /* re-read the selected entry through the directory handle */
    if (!fuji_open_directory2(g.curHost, g.path, "")) {
        setStatus("Could not re-open directory.");
        redraw();
        return;
    }
    fuji_set_directory_position((uint16_t)(g.pageBase + g.fileSel));
    if (!fuji_read_directory((uint8_t)(255 - strlen(g.path)), 0, name)) {
        fuji_close_directory();
        setStatus("Could not read entry.");
        redraw();
        return;
    }
    fuji_close_directory();

    if (strlen(g.path) + strlen(name) < sizeof(g.selectedFile)) {
        strcpy(g.selectedFile, g.path);
        strcat(g.selectedFile, name);
    } else {
        setStatus("Filename too long.");
        redraw();
        return;
    }

    /* default to the first empty device slot */
    {
        int i;
        g.slotSel = 0;
        for (i = 0; i < NUM_DEVICE_SLOTS; i++) {
            if (g.devices[i].hostSlot == EMPTY_SLOT) {
                g.slotSel = i;
                break;
            }
        }
    }
    /* read/write by default: the Finder will not mount a locked disk
       that has no Desktop file yet */
    g.slotMode = MODE_WRITE;
    g.view = VIEW_SLOT;
    setStatus("");
    redraw();
}

static void upFolder(void)
{
    size_t n = strlen(g.path);

    if (n <= 1) {          /* already at root: leave the browser */
        enterHosts();
        return;
    }
    /* strip trailing '/' then truncate at the previous '/' */
    g.path[n - 1] = '\0';
    {
        char *slash = strrchr(g.path, '/');
        if (slash)
            slash[1] = '\0';
        else
            strcpy(g.path, "/");
    }
    g.pageBase = 0;
    g.fileSel = 0;
    loadPage();
    redraw();
}

static void commitSlot(void)
{
    if (!fuji_set_device_filename(g.slotMode, g.curHost,
                                  (uint8_t)g.slotSel, g.selectedFile)) {
        setStatus("Could not set device filename.");
        redraw();
        return;
    }
    if (!fuji_mount_disk_image((uint8_t)g.slotSel, g.slotMode)) {
        setStatus("Mount failed.");
    } else if (g.slotSel == NUM_DEVICE_SLOTS - 1) {
        setStatus("Mounted: the floppy is in the external drive.");
    } else {
        setStatus("Mounted in HD20 slot: restart the Mac to use it.");
    }
    enterHosts();
}

static void mountAll(void)
{
    int i, mounted = 0, failed = 0;
    char msg[80];

    for (i = 0; i < NUM_DEVICE_SLOTS; i++) {
        if (g.devices[i].hostSlot == EMPTY_SLOT)
            continue;
        if (!fuji_mount_host_slot(g.devices[i].hostSlot)) {
            failed++;
            continue;
        }
        if (fuji_mount_disk_image((uint8_t)i, g.devices[i].mode))
            mounted++;
        else
            failed++;
    }
    refreshSlots();
    sprintf(msg, "Mounted %d slot(s), %d failed.", mounted, failed);
    setStatus(msg);
    redraw();
}

static void ejectSlot(int ds)
{
    if (g.devices[ds].hostSlot == EMPTY_SLOT) {
        setStatus("Slot is already empty.");
        redraw();
        return;
    }
    fuji_unmount_disk_image((uint8_t)ds);
    refreshSlots();          /* firmware clears the slot; re-fetch */
    setStatus("Ejected.");
    redraw();
}

static void toggleMode(int ds)
{
    uint8_t newMode;

    if (g.devices[ds].hostSlot == EMPTY_SLOT)
        return;
    newMode = (g.devices[ds].mode == MODE_READ) ? MODE_WRITE : MODE_READ;
    g.devices[ds].mode = newMode;
    fuji_put_device_slots(g.devices, NUM_DEVICE_SLOTS);
    refreshSlots();
    setStatus(newMode == MODE_WRITE ? "Slot set to read/write."
                                    : "Slot set to read-only.");
    redraw();
}

/* ---- host editing -------------------------------------------------- */

static void beginEditHost(void)
{
    g.editing = true;
    strncpy(g.editBuf, (const char *)g.hosts[g.hostSel],
            sizeof(g.editBuf) - 1);
    g.editBuf[sizeof(g.editBuf) - 1] = '\0';
    g.editLen = (int)strlen(g.editBuf);
    setStatus("Editing host — Return saves, Esc cancels.");
    redraw();
}

static void commitEditHost(void)
{
    Boolean changed =
        strncmp((const char *)g.hosts[g.hostSel], g.editBuf,
                sizeof(HostSlot)) != 0;
    int i;

    memset(g.hosts[g.hostSel], 0, sizeof(HostSlot));
    strncpy((char *)g.hosts[g.hostSel], g.editBuf, sizeof(HostSlot) - 1);
    fuji_put_host_slots(g.hosts, NUM_HOST_SLOTS);

    if (changed) {
        /* stale mounts: eject device slots that referenced this host */
        for (i = 0; i < NUM_DEVICE_SLOTS; i++) {
            if (g.devices[i].hostSlot == g.hostSel)
                fuji_unmount_disk_image((uint8_t)i);
        }
    }
    g.editing = false;
    refreshSlots();
    setStatus("Host saved.");
    redraw();
}

/* ---- rendering ------------------------------------------------------ */

static void renderHosts(void)
{
    char line[96];
    int i;
    short y;

    TextFace(underline);
    drawCStr(LEFT, HEAD_Y, "Hosts");
    TextFace(0);
    drawCStr(WIN_W / 2 - 20, HEAD_Y, g.wifiLine);

    for (i = 0; i < NUM_HOST_SLOTS; i++) {
        y = HOSTS_Y0 + i * LINE_H;
        if (g.editing && g.pane == PANE_HOSTS && i == g.hostSel)
            sprintf(line, "%d: %s_", i + 1, g.editBuf);
        else
            sprintf(line, "%d: %s", i + 1,
                    g.hosts[i][0] ? (const char *)g.hosts[i] : "<empty>");
        drawCStr(LEFT, y, line);
        if (g.pane == PANE_HOSTS && i == g.hostSel)
            invertRow(y);
    }

    TextFace(underline);
    drawCStr(LEFT, DEVICES_LBL, "Device Slots");
    TextFace(0);

    for (i = 0; i < NUM_DEVICE_SLOTS; i++) {
        y = DEVICES_Y0 + i * LINE_H;
        if (g.devices[i].hostSlot == EMPTY_SLOT) {
            sprintf(line, "%s: <empty>", slotName(i));
        } else {
            sprintf(line, "%s: %s  %c  %s",
                    slotName(i),
                    hostTag(g.devices[i].hostSlot),
                    g.devices[i].mode == MODE_WRITE ? 'W' : 'R',
                    g.devices[i].file);
        }
        drawCStr(LEFT, y, line);
        if (g.pane == PANE_DEVICES && i == g.devSel)
            invertRow(y);
    }

#ifdef COMPACT_UI
    drawCStr(LEFT, HELP_Y,
             g.pane == PANE_HOSTS
             ? "j/k  Ret browse  E edit  Tab  I info"
             : "j/k  E eject  R/W mode  Tab  I info");
#else
    drawCStr(LEFT, HELP_Y,
             g.pane == PANE_HOSTS
             ? "Return browse  E edit  Tab devices  I info  M mount all  Q quit"
             : "E eject  R/W toggle mode  Tab hosts  I info  M mount all  Q quit");
#endif
}

static void renderFiles(void)
{
    char line[96];
    int i;
    short y;

    TextFace(bold);
    sprintf(line, "Browse: %s", (const char *)g.hosts[g.curHost]);
    drawCStr(LEFT, HEAD_Y, line);
    TextFace(0);
    drawCStr(LEFT, HEAD_Y + LINE_H + 1, g.path);
    sprintf(line, "pg %d%s", g.pageBase / ENTRIES_PER_PAGE + 1,
            g.atEOF ? "*" : "");
    drawCStr(WIN_W - 50, HEAD_Y, line);

    if (g.pageCount == 0)
        drawCStr(LEFT, FILES_Y0, "<empty directory>");

    for (i = 0; i < g.pageCount; i++) {
        y = FILES_Y0 + i * LINE_H;
        drawCStr(LEFT, y, g.pageEntries[i]);
        if (i == g.fileSel)
            invertRow(y);
    }

#ifdef COMPACT_UI
    drawCStr(LEFT, HELP_Y, "j/k  Ret open  , . page  Del up  ` hosts");
#else
    drawCStr(LEFT, HELP_Y,
             "Return open  <- -> page  Bksp up folder  Esc hosts");
#endif
}

static void renderSlot(void)
{
    char line[96];
    int i;
    short y;

    TextFace(bold);
    drawCStr(LEFT, HEAD_Y, "Mount Image");
    TextFace(0);

    sprintf(line, "File: %s", g.selectedFile);
    drawCStr(LEFT, HEAD_Y + LINE_H + 3, line);
    sprintf(line, "Mode: %s   (R/W changes)",
            g.slotMode == MODE_WRITE ? "read/write" : "read-only");
    drawCStr(LEFT, HEAD_Y + 2 * LINE_H + 4, line);

    TextFace(underline);
    drawCStr(LEFT, SLOT_Y0 - LINE_H - 3, "Device Slot");
    TextFace(0);

    for (i = 0; i < NUM_DEVICE_SLOTS; i++) {
        y = SLOT_Y0 + i * LINE_H;
        if (g.devices[i].hostSlot == EMPTY_SLOT)
            sprintf(line, "%s: <empty>", slotName(i));
        else
            sprintf(line, "%s: %s  %s", slotName(i),
                    hostTag(g.devices[i].hostSlot), g.devices[i].file);
        drawCStr(LEFT, y, line);
        if (i == g.slotSel)
            invertRow(y);
    }

    drawCStr(LEFT, HELP_Y, "j/k  Ret mount  R/W mode  ` back");
}

static void renderInfo(void)
{
    AdapterConfigExtended ac;
    char line[96];
    short y = HEAD_Y + 2 * LINE_H;

    fuji_get_adapter_config_extended(&ac);

    TextFace(bold);
    drawCStr(LEFT, HEAD_Y, "FujiNet Adapter Info");
    TextFace(0);

    sprintf(line, "SSID:      %s", ac.ssid);      drawCStr(LEFT, y, line); y += LINE_H;
    sprintf(line, "Hostname:  %s", ac.hostname);  drawCStr(LEFT, y, line); y += LINE_H;
    sprintf(line, "IP:        %s", ac.sLocalIP);  drawCStr(LEFT, y, line); y += LINE_H;
    sprintf(line, "Gateway:   %s", ac.sGateway);  drawCStr(LEFT, y, line); y += LINE_H;
    sprintf(line, "Netmask:   %s", ac.sNetmask);  drawCStr(LEFT, y, line); y += LINE_H;
    sprintf(line, "DNS:       %s", ac.sDnsIP);    drawCStr(LEFT, y, line); y += LINE_H;
    sprintf(line, "MAC:       %s", ac.sMacAddress); drawCStr(LEFT, y, line); y += LINE_H;
    sprintf(line, "BSSID:     %s", ac.sBssid);    drawCStr(LEFT, y, line); y += LINE_H;
    sprintf(line, "Firmware:  %s", ac.fn_version); drawCStr(LEFT, y, line); y += LINE_H;

    drawCStr(LEFT, HELP_Y, "Esc back");
}

void ConfigRender(void)
{
    EraseRect(&g.win->portRect);
    TextFont(4 /* Monaco */);
    TextSize(9);

    switch (g.view) {
    case VIEW_HOSTS: renderHosts(); break;
    case VIEW_FILES: renderFiles(); break;
    case VIEW_SLOT:  renderSlot();  break;
    case VIEW_INFO:  renderInfo();  break;
    }

    MoveTo(0, STATUS_Y - 12);
    LineTo(WIN_W, STATUS_Y - 12);
    drawCStr(LEFT, STATUS_Y, g.status);
}

/* ---- input dispatch -------------------------------------------------- */

enum {
    KEY_RETURN = 0x0D,
    KEY_ENTER  = 0x03,
    KEY_ESC    = 0x1B,
    KEY_TAB    = 0x09,
    KEY_BKSP   = 0x08,
    KEY_LEFT   = 0x1C,
    KEY_RIGHT  = 0x1D,
    KEY_UP     = 0x1E,
    KEY_DOWN   = 0x1F
};

static Boolean keyHosts(char key)
{
    if (g.editing) {
        if (key == KEY_RETURN || key == KEY_ENTER) {
            commitEditHost();
        } else if (key == KEY_ESC) {
            g.editing = false;
            setStatus("Edit cancelled.");
            redraw();
        } else if (key == KEY_BKSP) {
            if (g.editLen > 0)
                g.editBuf[--g.editLen] = '\0';
            redraw();
        } else if (key >= 0x20 && key < 0x7F &&
                   g.editLen < (int)sizeof(g.editBuf) - 1) {
            g.editBuf[g.editLen++] = key;
            g.editBuf[g.editLen] = '\0';
            redraw();
        }
        return true;
    }

    switch (key) {
    case KEY_TAB:
        g.pane = (g.pane == PANE_HOSTS) ? PANE_DEVICES : PANE_HOSTS;
        redraw();
        break;
    case KEY_UP:
        if (g.pane == PANE_HOSTS) {
            if (g.hostSel > 0) g.hostSel--;
            else { g.pane = PANE_DEVICES; g.devSel = NUM_DEVICE_SLOTS - 1; }
        } else {
            if (g.devSel > 0) g.devSel--;
            else { g.pane = PANE_HOSTS; g.hostSel = NUM_HOST_SLOTS - 1; }
        }
        redraw();
        break;
    case KEY_DOWN:
        if (g.pane == PANE_HOSTS) {
            if (g.hostSel < NUM_HOST_SLOTS - 1) g.hostSel++;
            else { g.pane = PANE_DEVICES; g.devSel = 0; }
        } else {
            if (g.devSel < NUM_DEVICE_SLOTS - 1) g.devSel++;
            else { g.pane = PANE_HOSTS; g.hostSel = 0; }
        }
        redraw();
        break;
    case KEY_RETURN:
    case KEY_ENTER:
        if (g.pane == PANE_HOSTS)
            enterBrowser((uint8_t)g.hostSel);
        break;
    case 'e': case 'E':
        if (g.pane == PANE_HOSTS)
            beginEditHost();
        else
            ejectSlot(g.devSel);
        break;
    case 'r': case 'R': case 'w': case 'W':
        if (g.pane == PANE_DEVICES)
            toggleMode(g.devSel);
        break;
    case 'i': case 'I':
        g.view = VIEW_INFO;
        redraw();
        break;
    case 'm': case 'M':
        mountAll();
        break;
    case 'q': case 'Q':
    case KEY_ESC:
        return false;
    }
    return true;
}

static Boolean keyFiles(char key)
{
    switch (key) {
    case KEY_UP:
        if (g.fileSel > 0) g.fileSel--;
        redraw();
        break;
    case KEY_DOWN:
        if (g.fileSel < g.pageCount - 1) g.fileSel++;
        redraw();
        break;
    case KEY_RIGHT:
        if (!g.atEOF) {
            g.pageBase += ENTRIES_PER_PAGE;
            g.fileSel = 0;
            loadPage();
            redraw();
        }
        break;
    case KEY_LEFT:
        if (g.pageBase >= ENTRIES_PER_PAGE) {
            g.pageBase -= ENTRIES_PER_PAGE;
            g.fileSel = 0;
            loadPage();
            redraw();
        }
        break;
    case KEY_RETURN:
    case KEY_ENTER:
        chooseEntry();
        break;
    case KEY_BKSP:
        upFolder();
        break;
    case KEY_ESC:
        enterHosts();
        break;
    }
    return true;
}

static Boolean keySlot(char key)
{
    switch (key) {
    case KEY_UP:
        if (g.slotSel > 0) g.slotSel--;
        redraw();
        break;
    case KEY_DOWN:
        if (g.slotSel < NUM_DEVICE_SLOTS - 1) g.slotSel++;
        redraw();
        break;
    case 'r': case 'R':
        g.slotMode = MODE_READ;
        redraw();
        break;
    case 'w': case 'W':
        g.slotMode = MODE_WRITE;
        redraw();
        break;
    case KEY_RETURN:
    case KEY_ENTER:
        commitSlot();
        break;
    case KEY_ESC:
    case KEY_BKSP:
        g.view = VIEW_FILES;
        redraw();
        break;
    }
    return true;
}

Boolean ConfigKey(char key)
{
    if (!(g.view == VIEW_HOSTS && g.editing))
        key = mapKey(key);
    switch (g.view) {
    case VIEW_HOSTS: return keyHosts(key);
    case VIEW_FILES: return keyFiles(key);
    case VIEW_SLOT:  return keySlot(key);
    case VIEW_INFO:
        g.view = VIEW_HOSTS;
        redraw();
        return true;
    }
    return true;
}

/* ---- mouse: click selects a row, double-click activates it ---------- */

static int rowHit(short y0, int count, Point p)
{
    int i;
    for (i = 0; i < count; i++) {
        short base = y0 + i * LINE_H;
        if (p.v >= base - 10 && p.v < base + 3)
            return i;
    }
    return -1;
}

void ConfigMouse(Point p, Boolean dblClick)
{
    int hit;

    switch (g.view) {
    case VIEW_HOSTS:
        hit = rowHit(HOSTS_Y0, NUM_HOST_SLOTS, p);
        if (hit >= 0) {
            g.pane = PANE_HOSTS;
            g.hostSel = hit;
            redraw();
            if (dblClick)
                enterBrowser((uint8_t)hit);
            return;
        }
        hit = rowHit(DEVICES_Y0, NUM_DEVICE_SLOTS, p);
        if (hit >= 0) {
            g.pane = PANE_DEVICES;
            g.devSel = hit;
            redraw();
        }
        break;
    case VIEW_FILES:
        hit = rowHit(FILES_Y0, g.pageCount, p);
        if (hit >= 0) {
            g.fileSel = hit;
            redraw();
            if (dblClick)
                chooseEntry();
        }
        break;
    case VIEW_SLOT:
        hit = rowHit(SLOT_Y0, NUM_DEVICE_SLOTS, p);
        if (hit >= 0) {
            g.slotSel = hit;
            redraw();
            if (dblClick)
                commitSlot();
        }
        break;
    case VIEW_INFO:
        g.view = VIEW_HOSTS;
        redraw();
        break;
    }
}

/* ---- init ------------------------------------------------------------ */

void ConfigInit(WindowPtr win)
{
    memset(&g, 0, sizeof(g));
    g.win = win;
    checkWifi();
    sprintf(g.status, "Welcome (%s transport).", fuji_transport_name);
    enterHosts();
}
