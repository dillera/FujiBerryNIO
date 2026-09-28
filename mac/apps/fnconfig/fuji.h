/* Origin: fujinet-mac-da src/fuji.h (commit d029216), adapted for fujinet-nio. */
#ifndef FUJI_H
#define FUJI_H

/*
 * The fuji_* device-protocol contract (arch.md section 4).
 * Implemented by fuji_mock.c today; swap in a real transport
 * (floppy-port DCD or serial to FujiNet-PC) later without
 * touching the UI code.
 */

#include <stdbool.h>
#include <stdint.h>
#include "fuji_typedefs_io.h"

/* short transport name for the UI ("mock", "serial") */
extern const char *fuji_transport_name;

/* WiFi */
bool fuji_get_wifi_enabled(void);
bool fuji_get_wifi_status(uint8_t *status);
bool fuji_scan_for_networks(uint8_t *count);
bool fuji_get_scan_result(uint8_t n, SSIDInfo *info);
bool fuji_get_ssid(NetConfig *nc);
bool fuji_set_ssid(NetConfig *nc);

/* Adapter info */
bool fuji_get_adapter_config_extended(AdapterConfigExtended *ac);

/* Hosts & device slots */
bool fuji_get_host_slots(HostSlot *slots, uint8_t count);
bool fuji_put_host_slots(HostSlot *slots, uint8_t count);
bool fuji_get_device_slots(DeviceSlot *slots, uint8_t count);
bool fuji_put_device_slots(DeviceSlot *slots, uint8_t count);
bool fuji_mount_host_slot(uint8_t hs);
bool fuji_mount_disk_image(uint8_t ds, uint8_t mode);
bool fuji_unmount_disk_image(uint8_t ds);
bool fuji_set_device_filename(uint8_t mode, uint8_t hs, uint8_t ds,
                              const char *fullpath);
bool fuji_get_device_filename(uint8_t ds, char *buf);
bool fuji_set_boot_config(uint8_t enabled);
bool fuji_create_new(NewDisk *nd);
bool fuji_copy_file(uint8_t src_slot, uint8_t dst_slot, const char *spec);

/* Directory browsing — one shared handle; always close before other cmds */
bool fuji_open_directory2(uint8_t hs, const char *path, const char *filter);
bool fuji_set_directory_position(uint16_t pos);
bool fuji_read_directory(uint8_t maxlen, uint8_t aux2, char *buffer);
bool fuji_close_directory(void);

#endif /* FUJI_H */
