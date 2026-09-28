/* Origin: fujinet-mac-da src/da_main.c (commit d029216), adapted for fujinet-nio. */
/*
 * Desk Accessory (DRVR) wrapper around the CONFIG core.
 *
 * Built as a Retro68 flat code resource (-Wl,--mac-flat) with entry
 * point DRVRENTRY. tools/install_da.py prepends the 30-byte DRVR
 * header (all five routine offsets point at DRVRENTRY) and injects
 * the result into the System file as DRVR 21 "\0FujiConfig".
 *
 * The Device Manager calls the driver with A0 = ParamBlock, A1 = DCE,
 * result in D0. Open/Close return with RTS; Prime/Control/Status must
 * exit through JIODone ($08FC). The glue below dispatches on the low
 * three bits of ioTrap so a single entry point serves all five slots.
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Events.h>
#include <Devices.h>
#include <OSUtils.h>
#include <Retro68Runtime.h>

#include "ui.h"

asm(
    ".text\n"
    ".globl DRVRENTRY\n"
    "DRVRENTRY:\n"
    "   move.l  %d2,-(%sp)\n"     /* preserve caller d2 */
    "   move.l  %a1,-(%sp)\n"     /* keep the DCE for the JIODone exit */
    "   move.w  6(%a0),%d2\n"     /* ioTrap, kept across the C call */
    "   move.w  %d2,%d0\n"
    "   and.w   #7,%d0\n"         /* selector: 0=open 1=close 2/3=prime */
    "   move.w  %d0,-(%sp)\n"     /*           4=control 5=status 6=killIO */
    "   move.l  %a1,-(%sp)\n"
    "   move.l  %a0,-(%sp)\n"
    /* PC-relative: nothing is relocated yet at this point, an absolute
       jsr would jump to the link-time address */
    "   bsr.w   DADispatch\n"
    "   lea     10(%sp),%sp\n"
    "   move.l  (%sp)+,%a1\n"     /* A1 = DCE again */
    "   move.w  %d2,%d1\n"
    "   andi.w  #6,%d1\n"
    "   beq     1f\n"             /* Open/Close: plain RTS */
    "   btst    #9,%d2\n"         /* noQueueBit: immediate call? */
    "   bne     1f\n"             /* immediate: plain RTS */
    "   move.l  (%sp)+,%d2\n"
    "   move.l  0x08FC,%a0\n"     /* queued: exit through JIODone,   */
    "   jmp     (%a0)\n"          /* which expects A1 = the DCE      */
    "1: move.l  (%sp)+,%d2\n"
    "   rts\n"
);

enum { SEL_OPEN = 0, SEL_CLOSE = 1, SEL_CTL = 4 };

static unsigned long gLastClickWhen;
static Point gLastClickWhere;

static long daOpen(CntrlParam *pb, DCtlEntry *dce)
{
    GrafPtr savePort;
    WindowPtr w;
    Rect r;

    (void)pb;
    if (dce->dCtlWindow)
        return 0;                 /* already open: just come to front */

    GetPort(&savePort);
    SetRect(&r, 76, 48, 76 + CONFIG_WIN_WIDTH, 48 + CONFIG_WIN_HEIGHT);
    w = NewWindow(NULL, &r, "\pFujiNet CONFIG", true,
                  noGrowDocProc, (WindowPtr)-1, true, 0);
    if (!w) {
        SetPort(savePort);
        return openErr;
    }
    /* negative windowKind = system window owned by this driver */
    ((WindowPeek)w)->windowKind = dce->dCtlRefNum;
    dce->dCtlWindow = w;

    SetPort(w);
    ConfigInit(w);
    SetPort(savePort);
    return 0;
}

static long daClose(CntrlParam *pb, DCtlEntry *dce)
{
    (void)pb;
    if (dce->dCtlWindow) {
        DisposeWindow(dce->dCtlWindow);
        dce->dCtlWindow = NULL;
    }
    return 0;
}

static void daEvent(EventRecord *ev, DCtlEntry *dce)
{
    WindowPtr w = dce->dCtlWindow;
    GrafPtr savePort;

    if (!w)
        return;
    GetPort(&savePort);
    SetPort(w);

    switch (ev->what) {
    case updateEvt:
        BeginUpdate(w);
        ConfigRender();
        EndUpdate(w);
        break;
    case mouseDown: {
        Point p = ev->where;
        Boolean dbl;
        GlobalToLocal(&p);
        dbl = (ev->when - gLastClickWhen) <= LMGetDoubleTime()
              && (p.h - gLastClickWhere.h) < 5
              && (gLastClickWhere.h - p.h) < 5
              && (p.v - gLastClickWhere.v) < 5
              && (gLastClickWhere.v - p.v) < 5;
        gLastClickWhen = ev->when;
        gLastClickWhere = p;
        ConfigMouse(p, dbl);
        break;
    }
    case keyDown:
    case autoKey:
        /* close-box (not Q) quits a DA; ignore the quit request */
        ConfigKey((char)(ev->message & charCodeMask));
        break;
    }

    SetPort(savePort);
}

static long daControl(CntrlParam *pb, DCtlEntry *dce)
{
    switch (pb->csCode) {
    case accEvent:
        daEvent(*(EventRecord **)&pb->csParam[0], dce);
        break;
    default:
        break;                    /* accRun/accCursor/goodBye: nothing */
    }
    return 0;
}

long DADispatch(CntrlParam *pb, DCtlEntry *dce, short selector);

long DADispatch(CntrlParam *pb, DCtlEntry *dce, short selector)
{
    RETRO68_RELOCATE();

    switch (selector) {
    case SEL_OPEN:  return daOpen(pb, dce);
    case SEL_CLOSE: return daClose(pb, dce);
    case SEL_CTL:   return daControl(pb, dce);
    default:        return 0;     /* prime/status/killIO: noErr */
    }
}
