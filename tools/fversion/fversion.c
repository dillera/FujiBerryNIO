/*
 * fversion -- show the FujiNet devices currently registered in memory.
 *
 * Workbench 1.3's Version command cannot name a device, and the file's $VER
 * string is not necessarily what the loaded device reports (fujinet-nio.device
 * registers 0.5 while its $VER says 0.9).  This reads Exec's DeviceList, so
 * it shows what is actually live.  Uses only Kickstart 1.x calls.
 *
 *   fversion                       the three FujiNet devices
 *   fversion serial.device ...     any named devices
 *
 * Returns WARN (5) if a named device is not loaded.
 */
#include <dos/dos.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <proto/exec.h>

#include <stdio.h>

static const char version_tag[] __attribute__((used)) =
    "$VER: fversion 1.0 (27.9.2026)";

static const char *const default_devices[] = {
    "fujinet-nio.device",
    "fujinet-disk.device",
    "fujinet-serial.device",
};

#define ID_MAX 96

struct snapshot {
    UWORD version;
    UWORD revision;
    UWORD open_count;
    APTR address;
    char id[ID_MAX];
};

/* Copy what we need under Forbid: once we Permit, an expunge could free the
 * node and the ID string, which lives in the device's own code hunk. */
static int take_snapshot(const char *name, struct snapshot *out)
{
    struct Library *device;
    const char *id;
    unsigned i = 0;

    Forbid();
    device = (struct Library *)FindName(&SysBase->DeviceList, (STRPTR)name);
    if (device != NULL) {
        out->version = device->lib_Version;
        out->revision = device->lib_Revision;
        out->open_count = device->lib_OpenCnt;
        out->address = device;
        id = (const char *)device->lib_IdString;
        if (id != NULL) {
            /* Drop a "$VER: " prefix and the trailing CR/LF so the ID fits
             * on one 80-column line. */
            if (id[0] == '$' && id[1] == 'V' && id[2] == 'E' && id[3] == 'R' &&
                id[4] == ':')
                id += id[5] == ' ' ? 6 : 5;
            for (; i < ID_MAX - 1 && id[i] && id[i] != '\r' && id[i] != '\n'; ++i)
                out->id[i] = id[i];
        }
        out->id[i] = '\0';
    }
    Permit();
    return device != NULL;
}

int main(int argc, char **argv)
{
    const char *const *names = (const char *const *)argv + 1;
    int count = argc - 1;
    int missing = 0;
    int n;

    if (count <= 0) {
        names = default_devices;
        count = (int)(sizeof(default_devices) / sizeof(default_devices[0]));
    } else if (argv[1][0] == '?' || argv[1][0] == '-') {
        printf("Usage: fversion [device ...]\n"
               "Shows version, revision and ID of devices loaded in memory.\n");
        return RETURN_OK;
    }

    /* exec's own version; SoftVer is 0 on KS 1.3, so not a ROM revision. */
    printf("exec.library %u.%u\n", (unsigned)SysBase->LibNode.lib_Version,
           (unsigned)SysBase->LibNode.lib_Revision);
    for (n = 0; n < count; ++n) {
        struct snapshot snap;

        if (!take_snapshot(names[n], &snap)) {
            printf("%-22s not loaded\n", names[n]);
            ++missing;
            continue;
        }
        printf("%-22s %u.%u  open=%u  at $%08lx\n", names[n],
               (unsigned)snap.version, (unsigned)snap.revision,
               (unsigned)snap.open_count, (unsigned long)snap.address);
        if (snap.id[0])
            printf("  id: %s\n", snap.id);
    }
    return missing ? RETURN_WARN : RETURN_OK;
}
