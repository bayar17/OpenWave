# OpenWave promptd

`promptd` is a supervised process on the OpenWave user session bus. `/init`
starts and restarts `/usr/libexec/promptd` independently of Ocean and its apps.
The shell renders dialogs; the daemon validates requests and owns their lifetime.
It does not grant permissions. Secret form values are returned only to the
requesting connection and are never persisted or broadcast.

The versioned public interface is `org.openwave.Prompt1` at
`/org/openwave/Prompt`, bus name `org.openwave.Prompt`. Its installed XML is
`/usr/share/dbus-1/interfaces/org.openwave.Prompt1.xml`.

## Qt applications

Link `openwave-prompt-client` and include `OpenWavePromptClient.h`. Keep the
client alive until `finished` arrives. An instance supports one outstanding
request; separate instances can request dialogs independently.

```cpp
connect(&prompts, &OpenWavePromptClient::finished, this,
    [this](const QString &, const QString &result, const QString &) {
        if (result == "accepted") performPreviouslyRequestedAction();
    });
prompts.confirmation(tr("Replace file?"), tr("Replace this file with an empty file?"),
    {{"acceptLabel", tr("Replace")}, {"cancelLabel", tr("Cancel")}});
```

`message`, `confirmation`, `textInput`, `passwordInput`, `form`, and `selection` return immediately. Transport runs
on a serialized worker; UI timers retrieve retained results asynchronously.
`cancel()` is safe before the initial request completes. Cancellation wins
locally over a response that arrives concurrently. Use `errorOccurred` to
report failures; only `accepted` authorizes the application's requested action.
Ocean Files uses `textInput` for New file, New folder, Rename file and Rename
folder, with the shared centered unfold transition and compact underline field.
Selected files and folders also use a frosted confirmation prompt for permanent
deletion; folder deletion includes hidden contents. Canceling or losing the
prompt does not delete anything.
Rename is available for exactly one selected item. Files checks the captured
directory before acting on a result, and atomically refuses existing rename
destinations. File creation never overwrites without a separate confirmation;
replacement commits atomically. Cancelled or interrupted requests change no files.

## Protocol

`Request(type, options)` returns a UUID. Types are `message`, `confirmation`,
`text_input`, `form`, and `selection`. Options are plain strings unless specified otherwise:

| Option | Limit / behavior |
| --- | --- |
| `title` | Required, nonempty, up to 160 Unicode characters |
| `message` | Up to 2048 characters; newline and tab permitted |
| `acceptLabel`, `cancelLabel` | Nonempty, up to 48 characters |
| `timeoutMs` | uint32, 1000–300000; default 120000, includes queue time |
| `placeholder`, `initialText` | Text input or selection, up to 512 characters |
| `minLength`, `maxLength` | Text input only, uint32; default 0 / 256; maximum 512 |
| `validationPattern` | Text input only, up to 128 characters; full-string regular expression |
| `secret` | Text input only, boolean; mask the input and disable predictive input |
| `allowPasswordReveal` | Secret text input only, boolean; show a small Show/Hide password button at the panel's bottom left |
| `fields` | Form only, `a{sv}` map of 1–6 named field definitions |
| `fieldOrder` | Form only, comma-separated field names, each appearing exactly once |
| `choices` | Selection only, `a{sv}` map of 1–1024 keys and plain string labels, each up to 128 characters |

`selection` renders a searchable, scrollable choice list in the same frosted
prompt. `initialText` optionally names the current choice; `placeholder` labels
the search field. Keys are returned through `finished` only after the user saves;
clicking a row changes the pending selection. Choices are validated by the daemon
and requests are bounded to 128 KiB (other prompt types retain their 16 KiB limit).
Search, keyboard navigation, Cancel, modal shielding, and reduced motion use the
shared renderer. Settings uses this for Time zone, retaining TimeBackend's zone
list and verified time-service updates. Device name uses `textInput`; Save updates
the existing preferences and Bluetooth adapter name. Leaving General cancels
either prompt, and canceled or stale results cannot apply settings.

Forms use `fieldsFinished(id, result, values)` instead of `finished`. Each field
definition is an `a{sv}` map supporting `label`, `placeholder`, `initialText`,
uint32 `minLength` / `maxLength` / `maxBytes`, and boolean `secret`. A `choices`
map (1–8 named string labels) renders a dropdown, with `initialText` naming its
initial choice. `activeWhenField` and `activeWhenValue` show a field only for
that choice; inactive fields return empty strings. Unknown fields, invalid
choices, and length/UTF-8 byte limits are validated again by the daemon.
Accepted form results use a GVariant `a{ss}` dictionary serialized into the
existing result text; the Qt client decodes it into a `QVariantMap`.

The SDK's `OpenWavePromptClient::passwordInput(title, body, options)` uses the
existing `text_input` protocol, forces `secret=true`, and enables
`allowPasswordReveal` by default. Set `allowPasswordReveal=false` to keep the
field permanently masked. It returns through `finished` only to the caller,
just like `textInput`. Reveal affects rendering only; input remains sensitive,
and the renderer clears its input and resets masking after closing.

Settings uses `passwordInput` to connect to protected Wi-Fi networks. Saved
network details expose Forget network even while disconnected; confirmation
calls the SDK's `ConnectivityBackend::forgetWifi(ssid)`, which deletes the
NetworkManager profile through the existing connectivity service. Cancel,
leaving the page, or losing the prompt never connects or forgets a network.

Settings' Add Network form collects SSID, Security (WPA Personal / Open), and
a masked password for WPA networks. Its dropdown slides down through a clipping
container over 180 ms and closes over 140 ms, with instant reduced-motion
behavior. The form shares the existing frosted panel, modal shield, and centered
unfold transition. Cancel or service loss never submits a network configuration.

Unknown, duplicate, wrong-typed, or invalid options are rejected. Single-line
strings and submitted input cannot contain control characters. Initial text
may be incomplete but must fit the maximum length. Input validation runs in
the renderer and again in the daemon.

`GetResult(id)` returns `(finished, result, text)` only to the originating
connection. Results are retained for 60 seconds, including across renderer
polling, until the caller uses `Acknowledge(id)`. `Cancel(id)` completes with
`cancelled`. The daemon also emits a unicast `Response(id, result)` notification;
text is retrieved through `GetResult`, not broadcast.

Results are `accepted`, `cancelled`, `timed_out`, `renderer_lost`, or
`service_lost`. Text is nonempty only for accepted text input, selections or forms. After a hard
service restart an old UUID is unknown; clients report `service_lost` and never
replay the requested action. Requests are not persisted.

FIFO scheduling allows one active dialog. Repeated `Next()` calls return the
same UUID until it finishes. Limits are four retained requests per process,
12 new requests per process per minute, 32 pending requests and 64 retained
requests in the session. Timeouts prevent abandoned requests from blocking
the queue indefinitely. A disconnected caller's requests are removed,
including its active dialog. A renderer disconnect completes all pending
requests with `renderer_lost`; a new shell starts with a clean queue.

## Trust and lifecycle

Only the registered shell connection may call `Next` and `Respond`. Registration
uses D-Bus `GetConnectionCredentials` (`UnixUserID` and `ProcessID`), the
supervisor's `/run/openwave/ocean.pid`, and `/proc/PID/exe` matching the installed
shell. Client and renderer transport likewise verifies the daemon's credentials,
supervised PID and executable, and addresses its unique bus name. Caller IDs
cannot be supplied as options: the daemon derives an optional application ID
from installed desktop metadata and the verified executable. Ocean matches the
owning PID with its Wayland client before bringing that app forward.

Titles and body text are untrusted content rendered as plain text. D-Bus
connection ownership controls results even when two connections have the same
PID. A stale or queued request cannot be answered as the active request. Modal
input is shielded for the full opening and closing transitions, independently
of panel scaling. Home, drawer, quick menu and app input are blocked while a
prompt is visible; cancellation and queued replacement retain the shield.
The renderer overlays the requesting app with a fading black dimmer, retaining
the app's current contents underneath.
The translucent panel samples and blurs only the app region beneath it, with
a rounded mask and a neutral tint. The frost shares the panel's unfold and fade;
the surrounding dimmer remains independent. Sampling stops while hidden.
The daemon supplies its authenticated owner PID; the shell matches that PID
to the app's Wayland surface and waits
until that app can be presented. Queued prompts retain their previous owner
through closing before the next app is presented. System prompts without an
app surface use the shell viewport. Prompt width, font sizes, spacing, controls
and corner radii scale with that viewport. Source Sans Pro ExtraLight, safe-area
centering and bounded scrolling accommodate orientation changes.
`VerticalUnfoldTransition`
shares timing logic with the file chooser. Prompts unfold from 0.02 to 1 over
220 ms (OutCubic) and collapse over 160 ms (InCubic), with matching opacity and
backdrop fades. The panel, text, fields and buttons share one centered vertical
transform and opacity, unfolding and collapsing together on every frame.
Transition input gating does not dim the contents; only invalid input dims the
accept action, so opening and closing introduce no separate brightness jump.
Reduced-motion settings use an 80 ms opacity
transition with no scaling; dismissal restores the owning app's keyboard
focus. The top-bar chevron remains visible but inactive over an app.

These checks fit the current OpenWave session model. Apps currently launch as
root, so this is not a security sandbox against a fully privileged process that
can alter system executables, supervisor state or another process. A future
unprivileged session must preserve trusted ownership of the runtime PID files
and executable/desktop metadata. Privileged access remains enforced by the
relevant services, never by `promptd`.

## Validation

Build the target through the existing userspace CMake project. The private-bus
test daemon is compiled with `PROMPTD_TESTING`; only that build accepts a test
renderer executable and PID-file path. Production accepts no override arguments.

```sh
cc -DPROMPTD_TESTING main.c $(pkg-config --cflags --libs gio-2.0 glib-2.0) -o promptd-test
/usr/bin/python3 tests/test_service.py ./promptd-test
```

The host tests require Python GObject introspection and GLib development files.
Qt Quick tests are in `OceanLauncher/OceanLauncher/tests/tst_prompt.qml`;
selection, rename and file replacement safety tests are in Ocean Files' backend
test suite; prompt integration tests are in Ocean Files' QML menu suite.
