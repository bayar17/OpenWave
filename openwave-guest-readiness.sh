#!/bin/sh
# Run INSIDE the booted OpenWave guest. Read-only diagnostic; no service changes.
set -u
pass=0
fail=0
warn=0
ok() { pass=$((pass + 1)); printf '[PASS] %s\n' "$*"; }
bad() { fail=$((fail + 1)); printf '[FAIL] %s\n' "$*"; }
note() { warn=$((warn + 1)); printf '[WARN] %s\n' "$*"; }

printf 'OpenWave guest readiness — no services will be started or stopped\n\n'
printf '%s\n' '=== BOOT / INIT ==='
if [ -r /proc/1/comm ]; then
    init=$(cat /proc/1/comm)
    printf 'PID 1: %s\n' "$init"
    case "$init" in openrc-init|init) ok 'PID 1 looks like the expected init process (verify command line)' ;; *) note "Unexpected PID 1: $init" ;; esac
else
    bad 'Cannot inspect PID 1'
fi
if command -v rc-status >/dev/null 2>&1; then
    rc-status -a || note 'rc-status could not list every service'
else
    bad 'rc-status not found'
fi
if command -v rc-update >/dev/null 2>&1; then
    rc-update show || note 'rc-update show failed'
else
    bad 'rc-update not found'
fi

printf '\n%s\n' '=== CRITICAL SERVICE STATUS ==='
if command -v rc-service >/dev/null 2>&1; then
    for name in dbus openwave-session-bus udevd NetworkManager bluetooth \
      pipewire wireplumber appd permissiond promptd audiod \
      connectivityd wallpaperagentd openwave-portal \
      xdg-desktop-portal-openwave openwave-ocean; do
        if rc-service "$name" status >/dev/null 2>&1; then
            ok "$name running"
        else
            note "$name not reported running; inspect rc-service $name status"
        fi
    done
else
    bad 'rc-service not available'
fi

printf '\n%s\n' '=== RUNTIME SOCKETS ==='
for socket in /run/dbus/system_bus_socket /run/user/1000/bus; do
    if [ -S "$socket" ]; then ok "socket exists: $socket"; else bad "missing socket: $socket"; fi
done

printf '\n%s\n' '=== D-BUS NAME OWNERS ==='
if command -v gdbus >/dev/null 2>&1; then
    for name in org.openwave.Applications org.openwave.Permissions org.openwave.Shell \
      org.openwave.Wallpaper; do
        result=$(gdbus call --system --dest org.freedesktop.DBus \
             --object-path /org/freedesktop/DBus \
             --method org.freedesktop.DBus.GetNameOwner "$name" 2>&1)
        if [ "$?" -eq 0 ]; then ok "$name owned: $result"; else bad "$name not owned: $result"; fi
    done
    if [ "$(id -u)" -eq 1000 ]; then
        for name in org.openwave.Prompt org.freedesktop.portal.Desktop; do
            result=$(DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
                gdbus call --session --dest org.freedesktop.DBus \
                --object-path /org/freedesktop/DBus \
                --method org.freedesktop.DBus.GetNameOwner "$name" 2>&1)
            if [ "$?" -eq 0 ]; then ok "session $name owned: $result"; else note "session $name not owned: $result"; fi
        done
    else
        note 'Run session-bus name checks as ocean (UID 1000)'
    fi
else
    bad 'gdbus utility unavailable'
fi

printf '\n%s\n' '=== DYNAMIC LOADER ==='
loader=/lib64/ld-linux-x86-64.so.2
if [ -x "$loader" ]; then
    for name in appd permissiond promptd audiod connectivityd wallpaperagentd; do
        path="/System/Services/$name"
        if [ ! -x "$path" ]; then bad "$name executable missing"; continue; fi
        output=$("$loader" --list "$path" 2>&1)
        if [ "$?" -eq 0 ]; then
            ok "$name dynamic libraries resolved"
        else
            bad "$name loader failure: $output"
        fi
    done
else
    bad "missing loader: $loader"
fi

printf '\n%s\n' '=== SELINUX ==='
if [ -r /sys/fs/selinux/enforce ]; then
    enforcing=$(cat /sys/fs/selinux/enforce)
    if [ "$enforcing" = 1 ]; then ok 'SELinux kernel enforcing'; else bad "SELinux not enforcing: $enforcing"; fi
else
    bad 'SELinux enforcement status unreadable (/sys/fs/selinux/enforce)'
fi
if [ -r /proc/self/attr/current ]; then
    printf 'Current process SELinux context: '; cat /proc/self/attr/current
else
    note 'Cannot read current SELinux context'
fi
if command -v python3 >/dev/null 2>&1; then
    python3 - <<'PYLABEL'
import os
for path in ('/System/Services/appd', '/System/Services/permissiond',
             '/System/Applications/com.openwave.shell/bin/ocean-launcher'):
    try:
        print('SELinux label', path, os.getxattr(path, 'security.selinux').decode())
    except OSError as e:
        print('SELinux label UNAVAILABLE', path, str(e))
PYLABEL
else
    note 'python3 missing: cannot query file security.selinux xattrs directly'
fi
if command -v dmesg >/dev/null 2>&1; then
    printf 'Recent kernel AVC evidence (may be empty or access-restricted):\n'
    dmesg 2>/dev/null | grep -i 'avc:.*denied' | tail -n 15 || :
fi

printf '\n%s\n' '=== AUDIO / GRAPHICS ==='
if [ -d /dev/dri ]; then
    ls -l /dev/dri
else
    bad '/dev/dri missing'
fi
if command -v wpctl >/dev/null 2>&1; then
    if [ "$(id -u)" -eq 1000 ]; then
        wpctl status || note 'WirePlumber control cannot connect to user session'
    else
        note 'Run wpctl status as the ocean user (UID 1000) to test the audio session'
    fi
else
    note 'wpctl command not present'
fi

printf '\nPASS=%s WARN=%s FAIL=%s\n' "$pass" "$warn" "$fail"
if [ "$fail" -gt 0 ]; then
    printf 'RESULT: GUEST READINESS BLOCKED\n'
    exit 1
fi
printf 'RESULT: NO HARD FAILURES; inspect WARN entries and service logs\n'
exit 0
