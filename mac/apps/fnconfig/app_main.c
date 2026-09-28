/* Origin: fujinet-mac-da src/app_main.c (commit d029216), adapted for fujinet-nio. */
/*
 * Application wrapper around the CONFIG core, for developing and
 * demoing in an emulator. The core is event-driven so the same
 * ui.c can later be driven from a DA's accEvent handler instead.
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <OSUtils.h>
#include <ToolUtils.h>

#include "ui.h"

static Boolean gRunning = true;
static WindowPtr gWin;

static unsigned long gLastClickWhen;
static Point gLastClickWhere;

static void handleMouseDown(EventRecord *ev)
{
    WindowPtr win;
    short part = FindWindow(ev->where, &win);

    switch (part) {
    case inGoAway:
        if (win == gWin && TrackGoAway(win, ev->where))
            gRunning = false;
        break;
    case inDrag:
        if (win == gWin)
            DragWindow(win, ev->where, &qd.screenBits.bounds);
        break;
    case inContent:
        if (win == gWin) {
            Point p = ev->where;
            Boolean dbl;
            SetPort(win);
            GlobalToLocal(&p);
            dbl = (ev->when - gLastClickWhen) <= LMGetDoubleTime()
                  && (p.h - gLastClickWhere.h) < 5
                  && (gLastClickWhere.h - p.h) < 5
                  && (p.v - gLastClickWhere.v) < 5
                  && (gLastClickWhere.v - p.v) < 5;
            gLastClickWhen = ev->when;
            gLastClickWhere = p;
            ConfigMouse(p, dbl);
        }
        break;
    }
}

int main(void)
{
    Rect bounds;
    EventRecord ev;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    /* fills a 9" 512x342 screen below the menu bar */
    SetRect(&bounds, 2, 24, 510, 340);
    gWin = NewWindow(NULL, &bounds, "\pFujiNet CONFIG", true,
                     noGrowDocProc, (WindowPtr)-1, true, 0);
    SetPort(gWin);

    ConfigInit(gWin);

    while (gRunning) {
        SystemTask();
        if (GetNextEvent(everyEvent, &ev)) {
            switch (ev.what) {
            case mouseDown:
                handleMouseDown(&ev);
                break;
            case keyDown:
            case autoKey: {
                char c = (char)(ev.message & charCodeMask);
                if ((ev.modifiers & cmdKey) && (c == 'q' || c == 'Q'))
                    gRunning = false;
                else if (!ConfigKey(c))
                    gRunning = false;
                break;
            }
            case updateEvt:
                if ((WindowPtr)ev.message == gWin) {
                    BeginUpdate(gWin);
                    SetPort(gWin);
                    ConfigRender();
                    EndUpdate(gWin);
                }
                break;
            case activateEvt:
                SetPort(gWin);
                break;
            }
        }
    }

    DisposeWindow(gWin);
    return 0;
}
