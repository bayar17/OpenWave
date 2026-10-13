# timed

`timed` owns OpenWave's system timezone. PID 1 starts it after the system D-Bus.
It exposes `org.openwave.Time1` at `/org/openwave/Time` with `GetTimezone`,
`ListTimezones`, `SetTimezone`, and `TimezoneChanged`. Zone names come from the
installed IANA `zone.tab`; arbitrary paths and unknown zones are rejected.

`SetTimezone` atomically replaces `/etc/localtime` with a relative link to the
selected TZif file and updates `/etc/timezone` for human-readable configuration.
The localtime link is authoritative. This service does not set the system clock
or provide NTP synchronization.
