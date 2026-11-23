#include "appd.h"
#include "android-cache.h"
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <glib-unix.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr const char *Helper = "/usr/libexec/openwave-waydroid/android-apps";
constexpr gsize MaxOutput = 16 * 1024 * 1024;
constexpr guint HelperTimeoutMs = 30000;
constexpr guint RemoveTimeoutMs = 130000;
constexpr guint RemovePresentMs = 1500;
constexpr gint64 RemoveHoldUs = 135 * G_USEC_PER_SEC;
constexpr int HelperCancelled = 4;
constexpr guint RefreshSeconds = 10;
constexpr int MaxApps = 512;
constexpr int MaxIconBytes = 256 * 1024;
constexpr int MaxEventLine = 16384;
constexpr gint64 StateHoldUs = 6 * G_USEC_PER_SEC;

enum class Operation { List, Launch, Watch, Close, Remove };

struct Job {
    Manager *manager = nullptr;
    Operation operation = Operation::List;
    GPid pid = 0;
    int fd = -1;
    guint reader = 0;
    guint timeout = 0;
    QByteArray output;
    bool eof = false, exited = false, overflow = false;
    int status = 0;
    QString appId;
    GDBusMethodInvocation *call = nullptr;
    bool settled = false;
};
}

struct AndroidState {
    QString session;
    Job *list = nullptr;
    bool listAgain = false;
    Job *watch = nullptr;
    Job *close = nullptr;
    QStringList closeQueue;
    GHashTable *launches = nullptr;
    bool settled = false;
    QByteArray remembered;
    guint burst[3] = {0, 0, 0};
};

namespace {
bool validPackage(const QString &package)
{
    static const QRegularExpression pattern(QStringLiteral(
        "\\A[A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z][A-Za-z0-9_]*)+\\z"));
    return package.size() <= 128 - int(strlen(APPD_ANDROID_PREFIX)) && pattern.match(package).hasMatch();
}

QString cleanName(const QString &name)
{
    QString clean;
    for (const QChar character : name)
        if (character.isPrint()) clean.append(character);
    return clean.trimmed().left(64);
}

bool plausiblePng(const QByteArray &data)
{
    if (data.size() < 33 || data.size() > MaxIconBytes || !data.startsWith("\x89PNG\r\n\x1a\n")
        || data.mid(12, 4) != "IHDR") return false;
    const auto byte = [&](int i) { return quint32(quint8(data[i])); };
    const quint32 width = byte(16) << 24 | byte(17) << 16 | byte(18) << 8 | byte(19);
    const quint32 height = byte(20) << 24 | byte(21) << 16 | byte(22) << 8 | byte(23);
    return width && height && width <= 512 && height <= 512;
}

bool available(const AndroidState *state)
{
    return state->session == QLatin1String("RUNNING") || state->session == QLatin1String("FROZEN");
}

void freeApp(gpointer data)
{
    appd_app_free(data);
}

void childSetup(gpointer)
{
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
}

void finishJob(Job *job);
void startList(Manager *m);
void startClose(Manager *m, const QString &package);
void watchEvent(Job *job, const QByteArray &line);

gboolean readOutput(gint fd, GIOCondition, gpointer data)
{
    Job *job = static_cast<Job *>(data);
    char buffer[16384];
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0 && job->operation == Operation::Watch) {
        job->output.append(buffer, count);
        qsizetype end;
        while ((end = job->output.indexOf('\n')) >= 0) {
            const QByteArray line = job->output.left(end);
            job->output.remove(0, end + 1);
            if (line.size() <= MaxEventLine) watchEvent(job, line);
        }
        if (job->output.size() > MaxEventLine && job->pid > 0 && !job->exited) {
            g_warning("appd: Android event line too long; stopping the watcher");
            job->output.clear();
            kill(job->pid, SIGKILL);
        }
        return G_SOURCE_CONTINUE;
    }
    if (count > 0) {
        if (job->output.size() + count > qsizetype(MaxOutput)) job->overflow = true;
        else job->output.append(buffer, count);
        return G_SOURCE_CONTINUE;
    }
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) return G_SOURCE_CONTINUE;
    job->eof = true; job->reader = 0;
    close(job->fd); job->fd = -1;
    finishJob(job);
    return G_SOURCE_REMOVE;
}

gboolean jobTimeout(gpointer data)
{
    Job *job = static_cast<Job *>(data);
    job->timeout = 0;
    g_warning("appd: Android helper timed out operation=%d pid=%d", int(job->operation), job->pid);
    if (job->pid > 0 && !job->exited) kill(job->pid, SIGKILL);
    return G_SOURCE_REMOVE;
}

bool spawn(Manager *m, Job *job, const QStringList &arguments)
{
    QList<QByteArray> storage{QByteArray(Helper)};
    for (const auto &argument : arguments) storage.append(argument.toUtf8());
    QVector<char *> argv;
    for (auto &value : storage) argv.append(value.data());
    argv.append(nullptr);
    char *envp[] = {const_cast<char *>("PATH=/usr/bin:/bin"), const_cast<char *>("LANG=C.UTF-8"), nullptr};
    GError *error = nullptr;
    int out = -1;
    if (!g_spawn_async_with_pipes("/", argv.data(), envp, GSpawnFlags(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL),
            childSetup, nullptr, &job->pid, nullptr, &out, nullptr, &error)) {
        g_warning("appd: cannot run Android helper: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }
    job->manager = m; job->fd = out;
    g_unix_set_fd_nonblocking(out, TRUE, nullptr);
    job->reader = g_unix_fd_add(out, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), readOutput, job);
    if (job->operation != Operation::Watch)
        job->timeout = g_timeout_add(job->operation == Operation::Remove ? RemoveTimeoutMs : HelperTimeoutMs, jobTimeout, job);
    return true;
}

GVariant *infoFor(Manager *, App *app)
{
    GVariantBuilder info;
    g_variant_builder_init(&info, G_VARIANT_TYPE("a{sv}"));
    const bool launchable = !app->removing;
    g_variant_builder_add(&info, "{sv}", "launchable", g_variant_new_boolean(launchable));
    g_variant_builder_add(&info, "{sv}", "appId", g_variant_new_string(app->id));
    g_variant_builder_add(&info, "{sv}", "manifestDigest", g_variant_new_string(app->digest));
    g_variant_builder_add(&info, "{sv}", "declaredPermissions", g_variant_new_strv(nullptr, 0));
    g_variant_builder_add(&info, "{sv}", "name", g_variant_new_string(app->name));
    g_variant_builder_add(&info, "{sv}", "icon", g_variant_new_string(app->icon));
    g_variant_builder_add(&info, "{sv}", "bundlePath", g_variant_new_string(""));
    g_variant_builder_add(&info, "{sv}", "isolationProfile", g_variant_new_string("android-container"));
    g_variant_builder_add(&info, "{sv}", "namespaceSandboxed", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&info, "{sv}", "seccompFiltered", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&info, "{sv}", "executable", g_variant_new_string(""));
    g_variant_builder_add(&info, "{sv}", "state", g_variant_new_string(app->state));
    g_variant_builder_add(&info, "{sv}", "error", g_variant_new_string(app->error));
    g_variant_builder_add(&info, "{sv}", "pid", g_variant_new_uint32(0));
    g_variant_builder_add(&info, "{sv}", "trusted", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&info, "{sv}", "canUninstall", g_variant_new_boolean(!app->removing && app->removable));
    g_variant_builder_add(&info, "{sv}", "singleInstance", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&info, "{sv}", "androidPackage", g_variant_new_string(app->package));
    appd_info_desktop(&info, app);
    return g_variant_builder_end(&info);
}

QByteArray mergedCatalogue(const QByteArray &remembered, const QByteArray &live)
{
    const auto rememberedDocument = QJsonDocument::fromJson(remembered);
    const auto liveDocument = QJsonDocument::fromJson(live);
    if (!rememberedDocument.isObject() || !liveDocument.isObject()) return live;
    QJsonArray merged = liveDocument.object().value(QStringLiteral("apps")).toArray();
    QSet<QString> seen;
    for (const auto &value : merged) seen.insert(value.toObject().value(QStringLiteral("package")).toString());
    for (const auto &value : rememberedDocument.object().value(QStringLiteral("apps")).toArray()) {
        if (merged.size() >= MaxApps) break;
        const QString package = value.toObject().value(QStringLiteral("package")).toString();
        if (seen.contains(package)) continue;
        seen.insert(package);
        merged.append(value);
    }
    QJsonObject root;
    root.insert(QStringLiteral("apps"), merged);
    const QByteArray result = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return gsize(result.size()) <= MaxOutput ? result : live;
}

void applyCatalogue(Manager *m, const QByteArray &received, bool remembered = false, bool authoritative = false)
{
    QByteArray output = received;
    if (!remembered && !authoritative && !m->android->remembered.isEmpty()) output = mergedCatalogue(m->android->remembered, received);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        g_warning("appd: rejected Android catalogue: not a JSON object");
        return;
    }
    const auto entries = document.object().value(QStringLiteral("apps")).toArray();
    if (!remembered && entries.isEmpty() && g_hash_table_size(m->android_apps)) {
        g_message("appd: Android reported no applications; keeping the %u remembered ones", g_hash_table_size(m->android_apps));
        return;
    }
    GHashTable *fresh = g_hash_table_new_full(g_str_hash, g_str_equal, nullptr, freeApp);
    for (const auto &value : entries) {
        if (g_hash_table_size(fresh) >= guint(MaxApps)) break;
        const auto entry = value.toObject();
        const QString package = entry.value(QStringLiteral("package")).toString();
        if (!validPackage(package)) continue;
        const QString id = QStringLiteral(APPD_ANDROID_PREFIX) + package;
        const QByteArray idBytes = id.toUtf8();
        if (g_hash_table_contains(fresh, idBytes.constData())) continue;
        QString name = cleanName(entry.value(QStringLiteral("name")).toString());
        if (name.isEmpty()) name = package;
        const QByteArray icon = QByteArray::fromBase64(entry.value(QStringLiteral("icon")).toString().toLatin1(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
        const QByteArray iconUrl = plausiblePng(icon) ? "data:image/png;base64," + icon.toBase64() : QByteArray();
        const auto declared = [&entry](const char *key, int limit, const QRegularExpression &pattern) {
            QStringList kept;
            const auto values = entry.value(QLatin1String(key));
            if (!values.isArray()) return kept;
            for (const auto &item : values.toArray()) {
                if (kept.size() >= limit) break;
                if (item.isString() && pattern.match(item.toString()).hasMatch() && !kept.contains(item.toString())) kept.append(item.toString());
            }
            return kept;
        };
        static const QRegularExpression mimePattern(QRegularExpression::anchoredPattern(
            "[a-z0-9][a-z0-9!#$&^_.+-]{0,126}/[a-z0-9][a-z0-9!#$&^_.+-]{0,126}"));
        static const QRegularExpression schemePattern(QRegularExpression::anchoredPattern("[a-z][a-z0-9+.-]{0,31}"));
        const QStringList mimeTypes = declared("mimeTypes", 64, mimePattern);
        const QStringList schemes = declared("schemes", 16, schemePattern);
        App *app = g_new0(App, 1);
        app->android = TRUE;
        app->id = g_strdup(idBytes.constData());
        app->package = g_strdup(package.toUtf8().constData());
        app->name = g_strdup(name.toUtf8().constData());
        app->icon = g_strdup(iconUrl.constData());
        app->manifest = g_strdup("");
        app->bundle = g_strdup("");
        app->mime_types = g_new0(char *, mimeTypes.size() + 1);
        for (int i = 0; i < mimeTypes.size(); ++i) app->mime_types[i] = g_strdup(mimeTypes[i].toUtf8().constData());
        app->schemes = g_new0(char *, schemes.size() + 1);
        for (int i = 0; i < schemes.size(); ++i) app->schemes[i] = g_strdup(schemes[i].toUtf8().constData());
        app->digest = g_strdup(QCryptographicHash::hash(idBytes + '\0' + name.toUtf8() + '\0' + iconUrl
                                                       + '\0' + mimeTypes.join(';').toUtf8() + '\0' + schemes.join(';').toUtf8()
                                                       + (app->removable ? "\0R" : "\0-"),
                                                       QCryptographicHash::Sha256).toHex().constData());
        app->permissions = g_new0(char *, 1);
        app->argv = g_new0(char *, 2);
        app->argv[0] = g_strdup("");
        app->single = TRUE;
        app->removable = entry.value(QStringLiteral("removable")).toBool();
        App *old = static_cast<App *>(g_hash_table_lookup(m->android_apps, app->id));
        app->android_hold = old ? old->android_hold : 0;
        app->state = g_strdup(old ? old->state : "Stopped");
        app->error = g_strdup(old ? old->error : "");
        g_hash_table_insert(fresh, app->id, app);
    }
    bool changed = g_hash_table_size(fresh) != g_hash_table_size(m->android_apps);
    GHashTableIter iterator; gpointer key, value;
    g_hash_table_iter_init(&iterator, fresh);
    while (!changed && g_hash_table_iter_next(&iterator, &key, &value)) {
        App *old = static_cast<App *>(g_hash_table_lookup(m->android_apps, key));
        changed = !old || g_strcmp0(old->digest, static_cast<App *>(value)->digest);
    }
    if (!changed) { g_hash_table_unref(fresh); return; }
    m->android->remembered = output;
    g_hash_table_unref(m->android_apps);
    m->android_apps = fresh;
    appd_desktop_attach_android(m);
    appd_desktop_sync_android(m);
    if (!remembered && !appd_android_cache_save(APPD_ANDROID_ROOT, reinterpret_cast<const guchar *>(output.constData()), gsize(output.size())))
        g_warning("appd: could not remember the Android catalogue");
    g_message("appd: Android catalogue %s, %u applications", remembered ? "restored from memory" : "updated", g_hash_table_size(fresh));
    appd_emit(m, "RegistryChanged", nullptr);
}

void setSession(Manager *m, const QString &session)
{
    if (m->android->session == session) return;
    const bool before = available(m->android);
    m->android->session = session;
    if (!available(m->android)) m->android->settled = false;
    g_message("appd: Android session %s", session.toUtf8().constData());
    if (before != available(m->android) && g_hash_table_size(m->android_apps))
        appd_emit(m, "RegistryChanged", nullptr);
}

void finishJob(Job *job)
{
    if (!job->eof || !job->exited) return;
    if (job->reader) g_source_remove(job->reader);
    if (job->timeout) g_source_remove(job->timeout);
    Manager *m = job->manager;
    const bool success = !job->overflow && WIFEXITED(job->status) && WEXITSTATUS(job->status) == 0;
    if (job->operation == Operation::Watch) {
        m->android->watch = nullptr;
        g_message("appd: Android event watcher stopped (status %d)", job->status);
    } else if (job->operation == Operation::Close) {
        m->android->close = nullptr;
        g_message("appd: Android close %s result=%s", job->appId.isEmpty() ? "all" : job->appId.toUtf8().constData(),
                  success ? "requested" : "failed");
        if (!m->android->closeQueue.isEmpty()) startClose(m, m->android->closeQueue.takeFirst());
    } else if (job->operation == Operation::List) {
        m->android->list = nullptr;
        if (success) applyCatalogue(m, job->output, false, job->settled);
        else g_warning("appd: Android catalogue query failed (status %d)", job->status);
        if (m->android->listAgain) {
            m->android->listAgain = false;
            startList(m);
        }
    } else if (job->operation == Operation::Remove) {
        const QByteArray id = job->appId.toUtf8();
        g_hash_table_remove(m->android->launches, id.constData());
        App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, id.constData()));
        const bool cancelled = !job->overflow && WIFEXITED(job->status) && WEXITSTATUS(job->status) == HelperCancelled;
        if (app) {
            app->removing = FALSE;
            app->android_hold = 0;
        }
        if (success) {
            GError *cleared = nullptr;
            if (app) appd_clear_file_defaults(m, app->id, &cleared);
            g_clear_error(&cleared);
            startList(m);
            if (job->call) g_dbus_method_invocation_return_value(job->call, nullptr);
        } else if (cancelled) {
            if (app) appd_state(m, app, "Stopped", nullptr);
            if (job->call) g_dbus_method_invocation_return_value(job->call, nullptr);
        } else if (job->call) {
            if (app) appd_state(m, app, "Stopped", nullptr);
            QString detail;
            const auto document = QJsonDocument::fromJson(job->output);
            if (document.isObject()) {
                for (const QChar character : document.object().value(QStringLiteral("detail")).toString().left(120))
                    if (character.unicode() >= 32 && character.unicode() < 127) detail.append(character);
                const auto code = document.object().value(QStringLiteral("code"));
                if (code.isDouble()) detail += QStringLiteral(" (Android answered %1)").arg(code.toInt());
            }
            if (detail.isEmpty()) {
                if (job->overflow) detail = QStringLiteral("the helper replied with too much data");
                else if (WIFSIGNALED(job->status)) detail = QStringLiteral("the helper was stopped by signal %1").arg(WTERMSIG(job->status));
                else if (WIFEXITED(job->status)) detail = QStringLiteral("the helper exited with status %1 and no reply").arg(WEXITSTATUS(job->status));
            }
            const QByteArray message = detail.isEmpty()
                ? QByteArray("Android could not remove this application.")
                : QByteArray("Android could not remove this application: ") + detail.trimmed().toUtf8();
            g_dbus_method_invocation_return_dbus_error(job->call, "org.openwave.Applications.Error", message.constData());
        }
        if (job->call) g_object_unref(job->call);
        g_message("appd: Android uninstall appId=%s result=%s", id.constData(),
                  success ? "removed" : cancelled ? "cancelled" : "failed");
    } else {
        const QByteArray id = job->appId.toUtf8();
        g_hash_table_remove(m->android->launches, id.constData());
        App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, id.constData()));
        if (app) {
            if (success) {
                app->android_hold = g_get_monotonic_time() + StateHoldUs;
                appd_state(m, app, "Running", nullptr);
                appd_emit(m, "ActivationRequested", g_variant_new("(su)", app->id, 0u));
            } else appd_state(m, app, "Stopped", "Android could not start this application.");
        }
        g_message("appd: Android launch appId=%s result=%s", id.constData(), success ? "started" : "failed");
    }
    delete job;
}

struct BurstTick { Manager *manager; int index; };

gboolean burstFire(gpointer data)
{
    auto *tick = static_cast<BurstTick *>(data);
    Manager *m = tick->manager;
    m->android->burst[tick->index] = 0;
    if (!m->quitting && available(m->android)) startList(m);
    return G_SOURCE_REMOVE;
}

void scheduleBurst(Manager *m)
{
    static const guint delays[3] = {1000, 3000, 6000};
    for (int i = 0; i < 3; ++i) {
        if (m->android->burst[i]) g_source_remove(m->android->burst[i]);
        m->android->burst[i] = g_timeout_add_full(G_PRIORITY_DEFAULT, delays[i], burstFire,
                                                  new BurstTick{m, i},
                                                  [](gpointer tick) { delete static_cast<BurstTick *>(tick); });
    }
}

void startList(Manager *m)
{
    if (m->android->list) { m->android->listAgain = true; return; }
    if (m->quitting) return;
    Job *job = new Job;
    job->operation = Operation::List;
    job->settled = m->android->settled;
    if (!spawn(m, job, {QStringLiteral("list")})) { delete job; return; }
    m->android->list = job;
}

void startClose(Manager *m, const QString &package)
{
    if (m->quitting) return;
    if (m->android->close) {
        if (!m->android->closeQueue.contains(package)) m->android->closeQueue.append(package);
        return;
    }
    Job *job = new Job;
    job->operation = Operation::Close;
    job->appId = package;
    const QStringList arguments = package.isEmpty() ? QStringList{QStringLiteral("close-all")}
                                                    : QStringList{QStringLiteral("close"), package};
    if (!spawn(m, job, arguments)) { delete job; return; }
    m->android->close = job;
}

struct CloseRequest { QString package; };

void closeAfterSession(Manager *m, const QString &state, gpointer data)
{
    auto *request = static_cast<CloseRequest *>(data);
    if (state == QLatin1String("RUNNING")) startClose(m, request->package);
    delete request;
}

void applyTasks(Manager *m, const QJsonArray &packages)
{
    QSet<QString> running;
    for (const auto &value : packages) {
        const QString package = value.toString();
        if (validPackage(package)) running.insert(package);
    }
    const gint64 now = g_get_monotonic_time();
    guint started = 0, stopped = 0;
    GHashTableIter iterator; gpointer value;
    g_hash_table_iter_init(&iterator, m->android_apps);
    while (g_hash_table_iter_next(&iterator, nullptr, &value)) {
        App *app = static_cast<App *>(value);
        if (app->android_hold > now || !g_strcmp0(app->state, "Starting")) continue;
        const bool listed = running.contains(QString::fromUtf8(app->package));
        if (listed && g_strcmp0(app->state, "Running")) { appd_state(m, app, "Running", nullptr); ++started; }
        else if (!listed && !g_strcmp0(app->state, "Running")) { appd_state(m, app, "Stopped", nullptr); ++stopped; }
    }
    if (started || stopped)
        g_message("appd: Android task list: %d running, %u newly running, %u stopped", int(running.size()), started, stopped);
}

void startWatch(Manager *m)
{
    if (m->android->watch || m->quitting) return;
    Job *job = new Job;
    job->operation = Operation::Watch;
    if (!spawn(m, job, {QStringLiteral("watch")})) { delete job; return; }
    m->android->watch = job;
}

void watchEvent(Job *job, const QByteArray &line)
{
    const auto document = QJsonDocument::fromJson(line);
    if (!document.isObject()) return;
    const QString event = document.object().value(QStringLiteral("event")).toString();
    if (event == QLatin1String("registered")) {
        g_message("appd: Android user monitor registered");
    } else if (event == QLatin1String("error")) {
        const QString message = document.object().value(QStringLiteral("message")).toString().left(300);
        QString printable; for (const QChar ch : message) if (ch.isPrint()) printable.append(ch);
        g_warning("appd: Android event watcher failed: %s", printable.toUtf8().constData());
    } else if (event == QLatin1String("note")) {
        QString printable;
        for (const QChar ch : document.object().value(QStringLiteral("message")).toString().left(200))
            if (ch.isPrint()) printable.append(ch);
        g_message("appd: Android event watcher: %s", printable.toUtf8().constData());
    } else if (event == QLatin1String("tasks")) {
        applyTasks(job->manager, document.object().value(QStringLiteral("packages")).toArray());
    } else if (event == QLatin1String("idle")) {
        Manager *m = job->manager;
        guint stopped = 0;
        GHashTableIter iterator; gpointer value;
        g_hash_table_iter_init(&iterator, m->android_apps);
        while (g_hash_table_iter_next(&iterator, nullptr, &value)) {
            App *app = static_cast<App *>(value);
            if (!g_strcmp0(app->state, "Running") && app->android_hold <= g_get_monotonic_time()) {
                appd_state(m, app, "Stopped", nullptr); ++stopped;
            }
        }
        g_message("appd: Android reported idle; %u application(s) marked stopped", stopped);
    } else if (event == QLatin1String("unlocked") || event == QLatin1String("package") || event == QLatin1String("handlers")) {
        g_message("appd: Android reported %s; refreshing the catalogue", event.toUtf8().constData());
        if (event == QLatin1String("unlocked") || event == QLatin1String("package")) job->manager->android->settled = true;
        startList(job->manager);
        if (event == QLatin1String("package")) scheduleBurst(job->manager);
    }
}

void startLaunch(Manager *m, App *app, const QString &uri = QString())
{
    Job *job = new Job;
    job->operation = Operation::Launch;
    job->appId = QString::fromUtf8(app->id);
    const QStringList arguments = uri.isEmpty() ? QStringList{QStringLiteral("launch"), QString::fromUtf8(app->package)}
                                                : QStringList{QStringLiteral("view"), QString::fromUtf8(app->package), uri};
    if (!spawn(m, job, arguments)) {
        delete job;
        appd_state(m, app, "Stopped", "The Android application service could not be started.");
        return;
    }
    g_hash_table_insert(m->android->launches, g_strdup(app->id), job);
}

using SessionCallback = void (*)(Manager *, const QString &, gpointer);
struct SessionQuery { Manager *manager; SessionCallback callback; gpointer data; };

void sessionReply(GObject *source, GAsyncResult *result, gpointer data)
{
    auto *query = static_cast<SessionQuery *>(data);
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    QString state = QStringLiteral("UNAVAILABLE");
    if (reply) {
        GVariant *session = nullptr;
        g_variant_get(reply, "(@a{ss})", &session);
        const char *value = nullptr;
        if (g_variant_lookup(session, "state", "&s", &value) && value) state = QString::fromUtf8(value);
        g_variant_unref(session); g_variant_unref(reply);
    }
    g_clear_error(&error);
    if (state != QLatin1String("RUNNING") && state != QLatin1String("FROZEN") && state != QLatin1String("STOPPED"))
        state = QStringLiteral("UNAVAILABLE");
    setSession(query->manager, state);
    query->callback(query->manager, state, query->data);
    delete query;
}

void querySession(Manager *m, SessionCallback callback, gpointer data)
{
    auto *query = new SessionQuery{m, callback, data};
    g_dbus_connection_call(m->bus, "id.waydro.Container", "/ContainerManager", "id.waydro.ContainerManager",
        "GetSession", nullptr, G_VARIANT_TYPE("(a{ss})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, nullptr,
        sessionReply, query);
}

void refreshAfterSession(Manager *m, const QString &state, gpointer)
{
    if (state == QLatin1String("RUNNING")) {
        startWatch(m);
        if (!m->android->list) startList(m);
    }
}

gboolean refreshTimer(gpointer data)
{
    Manager *m = static_cast<Manager *>(data);
    if (m->bus && !m->quitting) querySession(m, refreshAfterSession, nullptr);
    return G_SOURCE_CONTINUE;
}

struct LaunchRequest { Manager *manager; char *id; char *uri; };

void unfrozen(GObject *source, GAsyncResult *result, gpointer data)
{
    auto *request = static_cast<LaunchRequest *>(data);
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (reply) g_variant_unref(reply);
    g_clear_error(&error);
    Manager *m = request->manager;
    App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, request->id));
    if (app) startLaunch(m, app, QString::fromUtf8(request->uri ? request->uri : ""));
    g_free(request->id); g_free(request->uri); delete request;
}

void launchAfterSession(Manager *m, const QString &state, gpointer data)
{
    auto *request = static_cast<LaunchRequest *>(data);
    App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, request->id));
    if (!app) { g_free(request->id); g_free(request->uri); delete request; return; }
    if (state == QLatin1String("RUNNING")) {
        startLaunch(m, app, QString::fromUtf8(request->uri ? request->uri : ""));
        g_free(request->id); g_free(request->uri); delete request;
    } else if (state == QLatin1String("FROZEN")) {
        g_dbus_connection_call(m->bus, "id.waydro.Container", "/ContainerManager", "id.waydro.ContainerManager",
            "Unfreeze", nullptr, nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, nullptr, unfrozen, request);
    } else {
        appd_state(m, app, "Stopped", "Android is not running.");
        g_free(request->id); g_free(request->uri); delete request;
    }
}
}

extern "C" void appd_android_start(Manager *m)
{
    m->android_apps = g_hash_table_new_full(g_str_hash, g_str_equal, nullptr, freeApp);
    m->android = new AndroidState;
    m->android->session = QStringLiteral("UNAVAILABLE");
    m->android->launches = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
    guchar *remembered = nullptr;
    gsize rememberedLength = 0;
    if (appd_android_cache_load(APPD_ANDROID_ROOT, MaxOutput, &remembered, &rememberedLength)) {
        applyCatalogue(m, QByteArray(reinterpret_cast<const char *>(remembered), qsizetype(rememberedLength)), true);
        g_free(remembered);
    }
    refreshTimer(m);
    g_timeout_add_seconds(RefreshSeconds, refreshTimer, m);
}

extern "C" App *appd_lookup(Manager *m, const char *id)
{
    if (!id) return nullptr;
    if (g_str_has_prefix(id, APPD_ANDROID_PREFIX))
        return m->android_apps ? static_cast<App *>(g_hash_table_lookup(m->android_apps, id)) : nullptr;
    return static_cast<App *>(g_hash_table_lookup(m->apps, id));
}

extern "C" GVariant *appd_android_info(Manager *m, App *app) { return infoFor(m, app); }

static gboolean launchWith(Manager *m, App *app, const char *uri, GError **error)
{
    if (!available(m->android)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "Android is not running.");
        return FALSE;
    }
    if (g_hash_table_contains(m->android->launches, app->id)) {
        if (!uri) return TRUE;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "This Android application is already starting.");
        return FALSE;
    }
    appd_state(m, app, "Starting", nullptr);
    querySession(m, launchAfterSession, new LaunchRequest{m, g_strdup(app->id), uri ? g_strdup(uri) : nullptr});
    return TRUE;
}

extern "C" gboolean appd_android_launch(Manager *m, App *app, GError **error)
{
    return launchWith(m, app, nullptr, error);
}

extern "C" gboolean appd_android_launch_uri(Manager *m, App *app, const char *uri, GError **error)
{
    for (char **scheme = app->schemes; scheme && *scheme; ++scheme) {
        char *given = g_uri_parse_scheme(uri);
        const bool match = given && (!g_ascii_strcasecmp(given, *scheme)
            || (!strcmp(*scheme, "sms") && !g_ascii_strcasecmp(given, "smsto")));
        g_free(given);
        if (match) return launchWith(m, app, uri, error);
    }
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "This Android application does not declare support for that link.");
    return FALSE;
}

struct RemoveRequest { Manager *manager; char *id; GDBusMethodInvocation *call; };
struct PresentRequest { Manager *manager; char *id; };

gboolean presentRemove(gpointer data)
{
    auto *request = static_cast<PresentRequest *>(data);
    Manager *m = request->manager;
    App *app = m->quitting ? nullptr : static_cast<App *>(g_hash_table_lookup(m->android_apps, request->id));
    if (app && app->removing && g_hash_table_contains(m->android->launches, request->id)) {
        app->android_hold = g_get_monotonic_time() + RemoveHoldUs;
        appd_state(m, app, "Running", nullptr);
        appd_emit(m, "ActivationRequested", g_variant_new("(su)", app->id, 0u));
    }
    g_free(request->id);
    delete request;
    return G_SOURCE_REMOVE;
}

void startRemove(Manager *m, App *app, GDBusMethodInvocation *call)
{
    Job *job = new Job;
    job->operation = Operation::Remove;
    job->appId = QString::fromUtf8(app->id);
    job->call = call;
    if (!spawn(m, job, {QStringLiteral("remove"), QString::fromUtf8(app->package)})) {
        app->removing = FALSE;
        appd_state(m, app, "Stopped", "The Android application service could not be started.");
        g_dbus_method_invocation_return_dbus_error(call, "org.openwave.Applications.Error", "The Android application service could not be started.");
        g_object_unref(call);
        delete job;
        return;
    }
    g_hash_table_insert(m->android->launches, g_strdup(app->id), job);
    g_timeout_add(RemovePresentMs, presentRemove, new PresentRequest{m, g_strdup(app->id)});
}

void failRemove(Manager *m, RemoveRequest *request, const char *message)
{
    App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, request->id));
    if (app) { app->removing = FALSE; appd_state(m, app, "Stopped", nullptr); }
    g_dbus_method_invocation_return_dbus_error(request->call, "org.openwave.Applications.Error", message);
    g_object_unref(request->call);
    g_free(request->id); delete request;
}

void unfrozenForRemove(GObject *source, GAsyncResult *result, gpointer data)
{
    auto *request = static_cast<RemoveRequest *>(data);
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (reply) g_variant_unref(reply);
    g_clear_error(&error);
    Manager *m = request->manager;
    App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, request->id));
    if (!app) { failRemove(m, request, "Application is no longer installed"); return; }
    startRemove(m, app, request->call);
    g_free(request->id); delete request;
}

void removeAfterSession(Manager *m, const QString &state, gpointer data)
{
    auto *request = static_cast<RemoveRequest *>(data);
    App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, request->id));
    if (!app) { failRemove(m, request, "Application is no longer installed"); return; }
    if (state == QLatin1String("RUNNING")) {
        startRemove(m, app, request->call);
        g_free(request->id); delete request;
    } else if (state == QLatin1String("FROZEN")) {
        g_dbus_connection_call(m->bus, "id.waydro.Container", "/ContainerManager", "id.waydro.ContainerManager",
            "Unfreeze", nullptr, nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, nullptr, unfrozenForRemove, request);
    } else failRemove(m, request, "Android is not running.");
}

extern "C" gboolean appd_android_uninstall(Manager *m, App *app, GDBusMethodInvocation *call, GError **error)
{
    if (!available(m->android)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "Android is not running.");
        return FALSE;
    }
    if (g_hash_table_contains(m->android->launches, app->id)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "This Android application is busy. Try again in a moment.");
        return FALSE;
    }
    app->removing = TRUE;
    appd_state(m, app, "Uninstalling", nullptr);
    querySession(m, removeAfterSession, new RemoveRequest{m, g_strdup(app->id), G_DBUS_METHOD_INVOCATION(g_object_ref(call))});
    return TRUE;
}

extern "C" void appd_android_terminate(Manager *m, App *app)
{
    app->android_hold = g_get_monotonic_time() + StateHoldUs;
    appd_state(m, app, "Stopped", nullptr);
    querySession(m, closeAfterSession, new CloseRequest{QString::fromUtf8(app->package)});
}

extern "C" gboolean appd_android_child_exited(Manager *m, GPid pid, int status)
{
    if (!m->android) return FALSE;
    Job *job = nullptr;
    if (m->android->list && m->android->list->pid == pid) job = m->android->list;
    if (!job && m->android->watch && m->android->watch->pid == pid) job = m->android->watch;
    if (!job && m->android->close && m->android->close->pid == pid) job = m->android->close;
    GHashTableIter iterator; gpointer value;
    g_hash_table_iter_init(&iterator, m->android->launches);
    while (!job && g_hash_table_iter_next(&iterator, nullptr, &value))
        if (static_cast<Job *>(value)->pid == pid) job = static_cast<Job *>(value);
    if (!job) return FALSE;
    job->exited = true; job->status = status;
    finishJob(job);
    return TRUE;
}
