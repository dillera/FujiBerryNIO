/* Origin: fujinet-mac-da src/ui.h (commit d029216), adapted for fujinet-nio. */
#ifndef UI_H
#define UI_H

/*
 * Event-driven CONFIG core. No blocking loops (arch.md section 3):
 * the host wrapper (application event loop today, DA accEvent
 * handler later) feeds events in; the current view/state lives in
 * a single ConfigState struct.
 */

#include <Quickdraw.h>
#include <Windows.h>

/* Window content size. COMPACT_UI (the DA build) keeps the window
   small enough to share a 9" screen with the host app's windows. */
#ifdef COMPACT_UI
#define CONFIG_WIN_WIDTH  360
#define CONFIG_WIN_HEIGHT 216
#else
#define CONFIG_WIN_WIDTH  508
#define CONFIG_WIN_HEIGHT 316
#endif

void ConfigInit(WindowPtr win);
void ConfigRender(void);

/* returns false when the user asked to quit/close */
Boolean ConfigKey(char key);
void ConfigMouse(Point localPt, Boolean dblClick);

#endif /* UI_H */
