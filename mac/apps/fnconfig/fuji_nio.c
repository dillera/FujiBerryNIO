/*
 * fuji_nio.c: the fuji_* CONFIG contract (fuji.h) on fujinet-nio, through
 * fujinet-nio-lib (target mac68k: FujiBus in the HD20 mailbox blocks).
 *
 * NIO has no classic FujiDevice CONFIG commands (host slots, 0xF7 open
 * directory, ...), so each call maps onto an NIO service:
 *
 *   host slots        8 x 32-byte entries kept in the AppStore (0xF1),
 *                     namespace "mac-config", key "hosts". Until the user
 *                     saves a host, the list is built from the filesystems
 *                     that answer (host:/mac/, flash:/, sd0:/) plus a TNFS
 *                     server. An entry is a NIO URI base ("sd0:/games/",
 *                     "tnfs://server/") or a bare name, which means TNFS.
 *   directories       FileService (0xFE) ListDirectory (0x02), compact and
 *                     sorted, cached whole on open; reads page the cache and
 *                     report the in-band 0x7F end marker.
 *   device slots      DiskService (0xFC) units 1-4 (HD20s) and 5 (floppy):
 *                     Info for state, ListMounts (0x0D) for the image URIs.
 *   adapter / WiFi    WifiService through fn_wifi_*.
 */
#include <string.h>
#include <stdio.h>

#include <Files.h>

#include "fujinet-nio.h"
#include "fn_raw.h"

#include "fuji.h"
#include "constants.h"

const char *fuji_transport_name = "fujinet-nio";

#define APPSTORE_SERVICE 0xF1
#define DISK_SERVICE     0xFC
#define FILE_SERVICE     0xFE
#define APPSTORE_READ    0x02
#define APPSTORE_WRITE   0x03
#define DISK_LIST_MOUNTS 0x0D
#define FILE_LIST        0x02

#define FLOPPY_UNIT      5      /* DiskService unit of the 800K floppy */
#define EXT_FLOPPY_DRIVE 2      /* Mac drive number of the external drive */

static const char kNamespace[] = "mac-config";
static const char kHostsKey[] = "hosts";

static uint8_t g_up;            /* fn_init succeeded */
static uint8_t g_tried;
static uint8_t g_stored;        /* host list came from the AppStore */
static HostSlot g_hosts[NUM_HOST_SLOTS];

/* one buffer for every request and reply; word aligned for the 68000 */
static uint8_t g_req[320] __attribute__((aligned(4)));
static uint8_t g_reply[1024] __attribute__((aligned(4)));

/* runtime state of the device slots */
static DeviceSlot g_dev[NUM_DEVICE_SLOTS];
static char g_uri[NUM_DEVICE_SLOTS][200];

/* pending image per device slot, set by fuji_set_device_filename */
static char g_pend_uri[NUM_DEVICE_SLOTS][200];

/* directory cache: NUL-terminated names in one pool */
#define DIR_POOL  6144
#define DIR_MAXN  200
static char g_dir_pool[DIR_POOL];
static uint16_t g_dir_off[DIR_MAXN];
static uint16_t g_dir_n;
static uint16_t g_dir_pos;
static uint8_t g_dir_open;
static char g_dir_uri[256];     /* listing held in the cache */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static bool up(void)
{
    if (!g_tried) {
        g_tried = 1;
        g_up = fn_init() == FN_OK;
    }
    return g_up;
}

/* ---- hosts ------------------------------------------------------- */

/* NIO URI base for a host entry, always ending in '/' */
static bool host_base(uint8_t hs, char *out, size_t cap)
{
    const char *h;
    size_t n;

    if (hs >= NUM_HOST_SLOTS || g_hosts[hs][0] == 0)
        return false;
    h = (const char *)g_hosts[hs];
    if (strcmp(h, "SD") == 0 || strcmp(h, "sd") == 0)
        snprintf(out, cap, "sd0:/");
    else if (strchr(h, ':'))
        snprintf(out, cap, "%s", h);
    else
        snprintf(out, cap, "tnfs://%s/", h);
    n = strlen(out);
    if (n && out[n - 1] != '/' && n + 1 < cap) {
        out[n] = '/';
        out[n + 1] = 0;
    }
    return true;
}

static size_t appstore_prefix(uint8_t *p)
{
    size_t n = 0;

    p[n++] = 1;
    put16(p + n, sizeof(kNamespace) - 1); n += 2;
    memcpy(p + n, kNamespace, sizeof(kNamespace) - 1); n += sizeof(kNamespace) - 1;
    put16(p + n, sizeof(kHostsKey) - 1); n += 2;
    memcpy(p + n, kHostsKey, sizeof(kHostsKey) - 1); n += sizeof(kHostsKey) - 1;
    return n;
}

static bool load_stored_hosts(void)
{
    fn_raw_response_t resp;
    size_t n = appstore_prefix(g_req);
    uint16_t len;

    memset(g_req + n, 0, 4); n += 4;                  /* offset 0 */
    put16(g_req + n, sizeof(g_hosts)); n += 2;        /* maxBytes */
    if (fn_raw_call(APPSTORE_SERVICE, APPSTORE_READ, g_req, (uint16_t)n,
                    g_reply, sizeof(g_reply), &resp) != FN_OK || resp.status != 0)
        return false;
    if (resp.payload_length < 10 || !(g_reply[1] & 2))  /* bit 1: exists */
        return false;
    len = le16(g_reply + 8);
    if (len != sizeof(g_hosts))
        return false;
    memcpy(g_hosts, g_reply + 10, sizeof(g_hosts));
    for (n = 0; n < NUM_HOST_SLOTS; ++n)
        g_hosts[n][sizeof(HostSlot) - 1] = 0;
    return true;
}

static bool store_hosts(void)
{
    fn_raw_response_t resp;
    size_t n = appstore_prefix(g_req);

    memset(g_req + n, 0, 4); n += 4;
    put16(g_req + n, sizeof(g_hosts)); n += 2;
    memcpy(g_req + n, g_hosts, sizeof(g_hosts)); n += sizeof(g_hosts);
    return fn_raw_call(APPSTORE_SERVICE, APPSTORE_WRITE, g_req, (uint16_t)n,
                       g_reply, sizeof(g_reply), &resp) == FN_OK && resp.status == 0;
}

/* FileService ListDirectory, one page; returns NIO status or 0xFF */
static uint8_t list_page(const char *uri, uint16_t start, uint16_t max)
{
    fn_raw_response_t resp;
    size_t ulen = strlen(uri), p = 0;

    if (ulen + 10 > sizeof(g_req))
        return 0xFF;
    g_req[p++] = 1;
    put16(g_req + p, (uint16_t)ulen); p += 2;
    memcpy(g_req + p, uri, ulen); p += ulen;
    put16(g_req + p, start); p += 2;
    put16(g_req + p, max); p += 2;
    g_req[p++] = 0x03;                                /* compact, sorted */
    if (fn_raw_call(FILE_SERVICE, FILE_LIST, g_req, (uint16_t)p,
                    g_reply, sizeof(g_reply), &resp) != FN_OK)
        return 0xFF;
    return resp.status;
}

static void default_hosts(void)
{
    static const char *const fs[] = {"host:/mac/", "flash:/", "sd0:/"};
    unsigned i, n = 0;

    memset(g_hosts, 0, sizeof(g_hosts));
    for (i = 0; i < sizeof(fs) / sizeof(fs[0]); ++i)
        if (list_page(fs[i], 0, 64) == 0)
            strcpy((char *)g_hosts[n++], fs[i]);
    strcpy((char *)g_hosts[n], "fujinet.online");
}

/* ---- WiFi and adapter -------------------------------------------- */

bool fuji_get_wifi_enabled(void)
{
    fn_wifi_config_t c;
    fn_wifi_status_t s;

    if (!up())
        return true;           /* the UI reports the missing FujiNet itself */
    if (fn_wifi_get_status(&s) == FN_OK && s.link_state == 2)
        return true;           /* a host network (POSIX) is always up */
    if (fn_wifi_get_config(&c) != FN_OK)
        return true;
    return c.enabled != 0;
}

bool fuji_get_wifi_status(uint8_t *status)
{
    fn_wifi_status_t s;

    *status = WIFI_STATUS_NO_SSID;
    if (!up() || fn_wifi_get_status(&s) != FN_OK)
        return false;
    switch (s.link_state) {
    case 2:  *status = WIFI_STATUS_CONNECTED; break;
    case 3:  *status = WIFI_STATUS_FAILED; break;
    default: *status = WIFI_STATUS_LOST; break;
    }
    return true;
}

bool fuji_scan_for_networks(uint8_t *count)
{
    *count = 0;
    return false;              /* not used by the UI */
}

bool fuji_get_scan_result(uint8_t n, SSIDInfo *info)
{
    (void)n;
    memset(info, 0, sizeof(*info));
    return false;
}

bool fuji_get_ssid(NetConfig *nc)
{
    fn_wifi_config_t c;

    memset(nc, 0, sizeof(*nc));
    if (!up() || fn_wifi_get_config(&c) != FN_OK)
        return false;
    strncpy(nc->ssid, c.ssid[0] ? c.ssid : "(none)", SSID_MAXLEN - 1);
    return true;
}

bool fuji_set_ssid(NetConfig *nc)
{
    (void)nc;
    return false;              /* WiFi is set up in NIO's own config */
}

bool fuji_get_adapter_config_extended(AdapterConfigExtended *ac)
{
    fn_wifi_status_t s;
    fn_wifi_config_t c;

    memset(ac, 0, sizeof(*ac));
    strcpy(ac->hostname, "fujinet-nio");
    strcpy(ac->fn_version, up() ? "fujinet-nio" : "not found");
    strcpy(ac->sMacAddress, "n/a");
    strcpy(ac->sBssid, "n/a");
    if (!up())
        return false;
    if (fn_wifi_get_config(&c) == FN_OK)
        strncpy(ac->ssid, c.ssid[0] ? c.ssid : "(none)", SSID_MAXLEN - 1);
    if (fn_wifi_get_status(&s) == FN_OK) {
        strncpy(ac->sLocalIP, s.ip[0] ? s.ip : "n/a", 15);
        strncpy(ac->sGateway, s.gateway[0] ? s.gateway : "n/a", 15);
        strncpy(ac->sNetmask, s.subnet[0] ? s.subnet : "n/a", 15);
        strncpy(ac->sDnsIP, s.dns[0] ? s.dns : "n/a", 15);
        if (s.bssid.valid)
            sprintf(ac->sBssid, "%02X:%02X:%02X:%02X:%02X:%02X",
                    s.bssid.bytes[0], s.bssid.bytes[1], s.bssid.bytes[2],
                    s.bssid.bytes[3], s.bssid.bytes[4], s.bssid.bytes[5]);
        if (s.backend_kind == FN_WIFI_BACKEND_POSIX_HOST ||
            s.backend_kind == FN_WIFI_BACKEND_POSIX_SIMULATED)
            strcpy(ac->fn_version, "fujinet-nio (POSIX)");
        else if (s.backend_kind == FN_WIFI_BACKEND_ESP32)
            strcpy(ac->fn_version, "fujinet-nio (ESP32)");
    }
    return true;
}

/* ---- hosts & device slots ---------------------------------------- */

bool fuji_get_host_slots(HostSlot *slots, uint8_t count)
{
    if (!up())
        return false;
    if (!g_stored) {
        g_stored = load_stored_hosts();
        if (!g_stored)
            default_hosts();
    }
    memcpy(slots, g_hosts, count * sizeof(HostSlot));
    return true;
}

bool fuji_put_host_slots(HostSlot *slots, uint8_t count)
{
    if (!up())
        return false;
    memcpy(g_hosts, slots, count * sizeof(HostSlot));
    g_stored = store_hosts();  /* a failed store keeps them for the session */
    return true;
}

/* image URIs of the active units, from DiskService ListMounts */
static void load_mount_uris(void)
{
    fn_raw_response_t resp;
    uint16_t start = 0;
    uint8_t i;

    for (i = 0; i < NUM_DEVICE_SLOTS; ++i)
        g_uri[i][0] = 0;
    for (;;) {
        uint16_t count, len, off = 0;
        uint8_t *p = g_req;

        p[0] = 1; p[1] = 1;                            /* version, formatted */
        put16(p + 2, 0); put16(p + 4, 0);              /* all units */
        put16(p + 6, start);
        put16(p + 8, sizeof(g_reply) - 64);
        if (fn_raw_call(DISK_SERVICE, DISK_LIST_MOUNTS, g_req, 10,
                        g_reply, sizeof(g_reply), &resp) != FN_OK ||
            resp.status != 0 || resp.payload_length < 10)
            return;
        count = le16(g_reply + 6);
        len = le16(g_reply + 8);
        /* lines: "<unit index>: <RO|AUTO> <uri>\n" */
        while (off < len) {
            char *line = (char *)g_reply + 10 + off;
            char *end = memchr(line, '\n', len - off);
            char *uri;
            unsigned unit = 0;

            if (!end)
                break;
            *end = 0;
            off = (uint16_t)(off + (end - line) + 1);
            while (*line >= '0' && *line <= '9')
                unit = unit * 10 + (unsigned)(*line++ - '0');
            uri = strchr(line, ' ');
            if (uri)
                uri = strchr(uri + 1, ' ');
            if (uri && unit < NUM_DEVICE_SLOTS) {
                strncpy(g_uri[unit], uri + 1, sizeof(g_uri[0]) - 1);
                g_uri[unit][sizeof(g_uri[0]) - 1] = 0;
            }
        }
        start = (uint16_t)(start + count);
        if (!(g_reply[1] & 1) || count == 0)
            break;
    }
}

static void fill_slot(uint8_t ds, const char *uri, bool readonly)
{
    const char *base = strrchr(uri, '/');
    char hb[80];
    uint8_t hs;

    g_dev[ds].hostSlot = 0xFE;                 /* mounted, no matching host */
    for (hs = 0; hs < NUM_HOST_SLOTS; ++hs)
        if (host_base(hs, hb, sizeof(hb)) && strncmp(uri, hb, strlen(hb)) == 0) {
            g_dev[ds].hostSlot = hs;
            break;
        }
    g_dev[ds].mode = readonly ? MODE_READ : MODE_WRITE;
    base = base ? base + 1 : uri;
    strncpy((char *)g_dev[ds].file, base[0] ? base : "(image)", FILE_MAXLEN - 1);
    g_dev[ds].file[FILE_MAXLEN - 1] = 0;
}

bool fuji_get_device_slots(DeviceSlot *slots, uint8_t count)
{
    uint8_t ds;

    for (ds = 0; ds < NUM_DEVICE_SLOTS; ++ds) {
        memset(&g_dev[ds], 0, sizeof(g_dev[ds]));
        g_dev[ds].hostSlot = EMPTY_SLOT;
    }
    if (up()) {
        load_mount_uris();
        for (ds = 0; ds < NUM_DEVICE_SLOTS; ++ds) {
            fn_disk_info_t info;

            if (fn_disk_info((uint8_t)(ds + 1), &info) != FN_OK ||
                !(info.flags & FN_DISK_FLAG_MOUNTED))
                continue;
            fill_slot(ds, g_uri[ds][0] ? g_uri[ds] : "", (info.flags & FN_DISK_FLAG_READONLY) != 0);
        }
    }
    memcpy(slots, g_dev, count * sizeof(DeviceSlot));
    return up();
}

static bool do_mount(uint8_t ds, const char *uri, uint8_t mode)
{
    fn_disk_info_t info;

    if (!up() || ds >= NUM_DEVICE_SLOTS || !uri[0])
        return false;
    return fn_disk_mount((uint8_t)(ds + 1), uri, mode != MODE_WRITE,
                         FN_DISK_TYPE_RAW, 512, &info) == FN_OK;
}

/* the UI's mode toggle: remount the image with the new mode */
bool fuji_put_device_slots(DeviceSlot *slots, uint8_t count)
{
    uint8_t ds;
    bool ok = true;

    for (ds = 0; ds < count && ds < NUM_DEVICE_SLOTS; ++ds) {
        if (slots[ds].hostSlot == EMPTY_SLOT || g_dev[ds].hostSlot == EMPTY_SLOT)
            continue;
        if (slots[ds].mode != g_dev[ds].mode && g_uri[ds][0])
            ok = do_mount(ds, g_uri[ds], slots[ds].mode) && ok;
    }
    return ok;
}

bool fuji_mount_host_slot(uint8_t hs)
{
    g_dir_uri[0] = 0;          /* entering a host lists afresh */
    return up() && hs < NUM_HOST_SLOTS && g_hosts[hs][0] != 0;
}

bool fuji_mount_disk_image(uint8_t ds, uint8_t mode)
{
    if (ds >= NUM_DEVICE_SLOTS)
        return false;
    if (g_pend_uri[ds][0]) {
        bool ok = do_mount(ds, g_pend_uri[ds], mode);
        if (ok)
            g_pend_uri[ds][0] = 0;
        return ok;
    }
    /* already mounted in NIO: nothing to do */
    return g_dev[ds].hostSlot != EMPTY_SLOT;
}

bool fuji_unmount_disk_image(uint8_t ds)
{
    if (!up() || ds >= NUM_DEVICE_SLOTS)
        return false;
    g_pend_uri[ds][0] = 0;
    if (ds + 1 == FLOPPY_UNIT) {
        /* Eject from the Mac's side, as the Finder does: the drive's eject
         * reaches NIO, which unmounts the slot. Unmount the volume first so
         * the Finder drops its icon. */
        UnmountVol(NULL, EXT_FLOPPY_DRIVE);
        if (Eject(NULL, EXT_FLOPPY_DRIVE) == noErr)
            return true;
    }
    return fn_disk_unmount((uint8_t)(ds + 1)) == FN_OK;
}

bool fuji_set_device_filename(uint8_t mode, uint8_t hs, uint8_t ds,
                              const char *fullpath)
{
    char base[80];
    const char *p = fullpath;

    if (ds >= NUM_DEVICE_SLOTS || !host_base(hs, base, sizeof(base)))
        return false;
    while (*p == '/')
        ++p;
    if (strlen(base) + strlen(p) >= sizeof(g_pend_uri[0]))
        return false;
    strcpy(g_pend_uri[ds], base);
    strcat(g_pend_uri[ds], p);
    g_dev[ds].hostSlot = hs;
    g_dev[ds].mode = mode;
    return true;
}

bool fuji_get_device_filename(uint8_t ds, char *buf)
{
    if (ds >= NUM_DEVICE_SLOTS)
        return false;
    strcpy(buf, g_pend_uri[ds][0] ? g_pend_uri[ds] : g_uri[ds]);
    return true;
}

bool fuji_set_boot_config(uint8_t enabled)
{
    (void)enabled;
    return false;
}

bool fuji_create_new(NewDisk *nd)
{
    (void)nd;
    return false;
}

bool fuji_copy_file(uint8_t src_slot, uint8_t dst_slot, const char *spec)
{
    (void)src_slot; (void)dst_slot; (void)spec;
    return false;
}

/* ---- directory browsing -------------------------------------------- */

bool fuji_open_directory2(uint8_t hs, const char *path, const char *filter)
{
    char uri[256];
    uint16_t start = 0, used = 0;

    (void)filter;
    g_dir_open = 0;
    g_dir_pos = 0;
    if (!up() || !host_base(hs, uri, sizeof(uri)))
        return false;
    while (*path == '/')
        ++path;
    if (strlen(uri) + strlen(path) >= sizeof(uri))
        return false;
    strcat(uri, path);
    if (strcmp(uri, g_dir_uri) == 0) {
        g_dir_open = 1;
        return true;
    }
    g_dir_uri[0] = 0;
    g_dir_n = 0;

    for (;;) {
        uint16_t count, len, i, off = 0;

        if (list_page(uri, start, sizeof(g_reply) - 64) != 0)
            return false;
        count = le16(g_reply + 6);
        len = le16(g_reply + 8);
        for (i = 0; i < count && off < len; ++i) {
            const uint8_t *e = g_reply + 10 + off;
            uint8_t nlen = e[1];
            bool dir = e[0] & 1;

            off = (uint16_t)(off + 2 + nlen);
            if (nlen == 0 || e[2] == '.' || g_dir_n >= DIR_MAXN ||
                used + nlen + 2 > DIR_POOL)
                continue;
            g_dir_off[g_dir_n++] = used;
            memcpy(g_dir_pool + used, e + 2, nlen);
            used = (uint16_t)(used + nlen);
            if (dir)
                g_dir_pool[used++] = '/';
            g_dir_pool[used++] = 0;
        }
        start = (uint16_t)(start + count);
        if (!(g_reply[1] & 1) || count == 0 || g_dir_n >= DIR_MAXN)
            break;
    }
    strcpy(g_dir_uri, uri);
    g_dir_open = 1;
    return true;
}

bool fuji_set_directory_position(uint16_t pos)
{
    if (!g_dir_open)
        return false;
    g_dir_pos = pos;
    return true;
}

bool fuji_read_directory(uint8_t maxlen, uint8_t aux2, char *buffer)
{
    const char *name;
    size_t len;

    (void)aux2;
    if (!g_dir_open || maxlen < 4)
        return false;
    if (g_dir_pos >= g_dir_n) {
        buffer[0] = buffer[1] = buffer[2] = DIR_EOF;
        return true;
    }
    name = g_dir_pool + g_dir_off[g_dir_pos++];
    len = strlen(name);
    if (len > (size_t)maxlen - 1)
        len = (size_t)maxlen - 1;
    memcpy(buffer, name, len);
    buffer[len] = 0;
    return true;
}

bool fuji_close_directory(void)
{
    g_dir_open = 0;
    return true;
}
