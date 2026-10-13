#include <gio/gio.h>
#include <stdio.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    if (!fork()) { execl("/init.openwave", "/init.openwave", NULL); _exit(127); }
    sleep(12);
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (bus) {
        const char *arguments[] = { NULL };
        GVariant *reply = g_dbus_connection_call_sync(bus,
            "org.openwave.Applications", "/org/openwave/Applications", "org.openwave.Applications1",
            "Launch", g_variant_new("(s^as)", "input-test", arguments), NULL,
            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
        if (reply) g_variant_unref(reply);
        g_object_unref(bus);
    }
    if (error) fprintf(stderr, "BROWSER_INPUT_LAUNCH_ERROR %s\n", error->message);
    int result = -1;
    for (int i = 0; i < 600; ++i) {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {}
        FILE *file = fopen("/tmp/browser-input-result", "r");
        if (file) {
            if (fscanf(file, "%d", &result) != 1) result = -1;
            fclose(file);
            if (result >= 0) break;
        }
        usleep(100000);
    }
    printf("BROWSER_INPUT_QA_EXIT=%d\n", result);
    fflush(stdout); sync(); reboot(RB_POWER_OFF);
    for (;;) pause();
}
