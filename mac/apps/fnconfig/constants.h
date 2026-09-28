/* Origin: fujinet-mac-da src/constants.h (commit d029216), adapted for fujinet-nio. */
#ifndef CONSTANTS_H
#define CONSTANTS_H

#define NUM_HOST_SLOTS   8
#define NUM_DEVICE_SLOTS 5      /* NIO: units 1-4 are HD20s, 5 the floppy */
#ifdef COMPACT_UI
#define ENTRIES_PER_PAGE 12
#else
#define ENTRIES_PER_PAGE 14
#endif
#define DIR_MAX_LEN      224
#define FILTER_MAX_LEN   32

#define MODE_READ  1
#define MODE_WRITE 2

#define EMPTY_SLOT 0xFF         /* DeviceSlot.hostSlot value for an empty slot */
#define DIR_EOF    0x7F         /* in-band end-of-directory sentinel */

/* WiFi status codes (fuji_get_wifi_status) */
#define WIFI_STATUS_NO_SSID    1
#define WIFI_STATUS_CONNECTED  3
#define WIFI_STATUS_FAILED     4
#define WIFI_STATUS_LOST       5

#endif /* CONSTANTS_H */
