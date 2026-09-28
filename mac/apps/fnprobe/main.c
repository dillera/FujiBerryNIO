/*
 * FujiNet NIO probe for the classic Macintosh.
 *
 * Talks to fujinet-nio through the floppy port: every call below is a
 * FujiBus packet carried in HD20 (DCD) block I/O by the ROM .Sony driver
 * (fujinet-nio-lib, platform mac68k). Shows the FujiNet's clock and an
 * HTTP GET made by the FujiNet. A plain Toolbox window, no console
 * library, so it stays small enough for a 512Ke and a 400K floppy.
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

#include "fujinet-nio.h"
#include "fn_platform.h"

#ifndef PROBE_URL
#define PROBE_URL "http://api.open-meteo.com/v1/forecast?latitude=40.71&longitude=-74.01&current=temperature_2m,wind_speed_10m&temperature_unit=fahrenheit"
#endif

#define LINE_H 12
#define MARGIN 6

static WindowPtr win;
static short line_y;
static uint8_t buf[512];
static char line[256];
static short col;

/* A minimal teletype into the window: wraps at the right edge. */
static void newline(void)
{
    line_y += LINE_H;
    col = 0;
    if (line_y > win->portRect.bottom - 4) {
        Rect r = win->portRect;
        RgnHandle rgn = NewRgn();

        ScrollRect(&r, 0, -LINE_H, rgn);
        DisposeRgn(rgn);
        line_y -= LINE_H;
    }
    MoveTo(MARGIN, line_y);
}

/* UTF-8 from the web -> MacRoman for the few characters worth showing. */
static unsigned char mac_roman(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;

    if (p[0] < 0x80)
        return p[0];
    if (p[0] == 0xC2 && p[1] == 0xB0) {       /* degree sign */
        *s += 1;
        return 0xA1;
    }
    while ((p[1] & 0xC0) == 0x80) {           /* skip the rest of the sequence */
        ++p;
        *s += 1;
    }
    return '?';
}

static void put_text(const char *s)
{
    short width = win->portRect.right - 2 * MARGIN;

    for (; *s; ++s) {
        unsigned char c = mac_roman(&s);

        if (c == '\n') {
            newline();
            continue;
        }
        if (c < 32)
            continue;
        if (col + CharWidth(c) > width)
            newline();
        DrawChar(c);
        col += CharWidth(c);
    }
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    put_text(line);
}

static void show_time(void)
{
    FN_TIME_T now;
    uint8_t r = fn_clock_get(&now);
    unsigned long secs, days;
    long y, m, d, era, doe, yoe, doy, mp;

    if (r != FN_OK) {
        say("Clock:   error %s\n", fn_error_string(r));
        return;
    }
    /* Unix seconds -> civil date (Howard Hinnant's days_from_civil inverse). */
    secs = (unsigned long)now;
    days = secs / 86400UL;
    secs %= 86400UL;
    {
        long z = (long)days + 719468L;
        era = z / 146097L;
        doe = z - era * 146097L;
        yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        y = yoe + era * 400;
        doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        mp = (5 * doy + 2) / 153;
        d = doy - (153 * mp + 2) / 5 + 1;
        m = mp < 10 ? mp + 3 : mp - 9;
        if (m <= 2)
            ++y;
    }
    say("Clock:   %04ld-%02ld-%02ld %02lu:%02lu:%02lu UTC\n", y, m, d,
        secs / 3600UL, (secs / 60UL) % 60UL, secs % 60UL);
}

static void http_get(const char *url)
{
    fn_handle_t h;
    uint16_t got, status = 0;
    uint32_t total = 0, length = 0;
    uint8_t flags, info = 0, r;
    long start = TickCount();

    say("\nHTTP GET %s\n", url);
    r = fn_open(&h, FN_METHOD_GET, url, 0);
    if (r != FN_OK) {
        say("open failed: %s\n", fn_error_string(r));
        return;
    }
    if (fn_info(h, &status, &length, &info) == FN_OK && (info & FN_INFO_HAS_STATUS))
        say("HTTP %u\n", status);

    for (;;) {
        r = fn_read(h, total, buf, sizeof(buf) - 1, &got, &flags);
        if (r == FN_ERR_NOT_READY || r == FN_ERR_BUSY) {
            if (TickCount() - start > 60L * 30) {
                say("\ntimed out\n");
                break;
            }
            continue;
        }
        if (r != FN_OK) {
            say("\nread failed: %s\n", fn_error_string(r));
            break;
        }
        buf[got] = 0;
        put_text((char *)buf);
        total += got;
        if (got == 0 || (flags & FN_READ_EOF))
            break;
    }
    say("\n[%lu bytes, %ld ticks]\n", (unsigned long)total, TickCount() - start);
    fn_close(h);
}

int main(void)
{
    Rect bounds;
    EventRecord ev;
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
    win = NewWindow(NULL, &bounds, "\pFujiNet NIO", true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(win);
    TextFont(4); /* Monaco */
    TextSize(9);
    line_y = LINE_H + 2;
    MoveTo(MARGIN, line_y);

    say("FujiNet NIO probe - Macintosh floppy port\n");
    say("fujinet-nio-lib %s, platform %s\n\n", fn_version(), fn_platform_name());
    say("Looking for a FujiNet on the floppy port...\n");

    r = fn_init();
    if (r != FN_OK) {
        say("No FujiNet found: %s\n", fn_error_string(r));
        say("(needs an HD20 served by fujinet-nio)\n");
    } else {
        say("FujiNet: found (HD20 mailbox)\n");
        show_time();
        http_get(PROBE_URL);
    }

    say("\nClick or press a key to quit.");
    FlushEvents(everyEvent, 0);
    for (;;) {
        if (GetNextEvent(mDownMask | keyDownMask, &ev))
            break;
    }
    return 0;
}
