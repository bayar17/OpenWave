#include "OceanDownloadManager.h"
#include "BrowserPreferences.h"
#include <OpenWave/OpenWavePortals.h>
#include <OpenWave/ApplicationChannel.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <functional>
#undef signals
#include <wpe/webkit.h>

namespace {
constexpr auto privateFile = QFile::ReadOwner | QFile::WriteOwner;
bool safeDirectory(const QString &path) {
    QString p = QDir::cleanPath(path);
    while (p != "/") {
        if (QFileInfo(p).isSymbolicLink()) return false;
        p = QFileInfo(p).absolutePath();
    }
    return QDir().mkpath(path) && QFile::setPermissions(path, privateFile | QFile::ExeOwner);
}
bool payloadAvailable(const QString &path) {
    struct stat metadata;
    if (!stat(QFile::encodeName(path).constData(), &metadata)) return S_ISREG(metadata.st_mode);
    return errno == EACCES || errno == EPERM;
}
bool active(const QString &s) { return s == "Pending" || s == "Downloading" || s == "Paused"; }
// The native engine exposes WebKitWebView* through its Qt meta-object API.
WebKitWebView *nativeView(QObject *object) {
    WebKitWebView *result = nullptr;
    if (object) QMetaObject::invokeMethod(object, "webView", Qt::DirectConnection,
        QGenericReturnArgument("WebKitWebView*", &result));
    return result;
}
}

struct OceanDownloadManager::Session {
    OceanDownloadManager *owner;
    WebKitNetworkSession *native;
    bool privateTab;
    int views = 0;
};
struct OceanDownloadManager::Context {
    OceanDownloadManager *owner;
    QPointer<QObject> object;
    WebKitWebView *native;
    Session *session;
};
struct OceanDownloadManager::Entry {
    OceanDownloadManager *owner;
    QString key, source, name, destination, mime, status = "Pending", error, stage;
    qint64 total = -1, received = 0, started = QDateTime::currentMSecsSinceEpoch(), finished = 0;
    qint64 sampleTime = started, sampleBytes = 0;
    double speed = 0;
    bool privateTab = false, image = false, saveAs = false, safeRetry = false, finalizing = false, archive = false;
    GCancellable *archiveCancel = nullptr;
    WebKitDownload *native = nullptr;
    QPointer<QObject> view;
    Session *session = nullptr;
    OpenWavePortals *portal = nullptr;
    int descriptor = -1;
    QVariantMap snapshot() const {
        return {{"key",key},{"source_url",source},{"filename",name},{"destination",destination.isNull()?QStringLiteral(""):destination},
            {"mime_type",mime},{"total_bytes",total},{"received_bytes",received},{"status",status},
            {"started_at",started},{"completed_at",finished ? QVariant(finished) : QVariant()}, {"error",error}};
    }
};

class DownloadStore : public QObject {
public:
    DownloadStore(OceanDownloadManager *owner, QString path) : m_owner(owner), m_path(std::move(path)) {}
    void post(std::function<void(OceanDownloadManager *)> callback) {
        QMetaObject::invokeMethod(m_owner, [owner=QPointer(m_owner),callback=std::move(callback)] {
            if (owner) callback(owner);
        }, Qt::QueuedConnection);
    }
    void failed(const QString &detail) { post([detail](auto *o) { o->errorMessage(tr("Download history: %1").arg(detail)); }); }
    void initialize() {
        const QString directory = m_path + "/Data/Downloads";
        if (!safeDirectory(directory)) { failed(tr("Private storage is unavailable.")); return; }
        const QString file = directory + "/Downloads.sqlite";
        const int fd = ::open(QFile::encodeName(file).constData(), O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW, 0600);
        if (fd >= 0) ::close(fd);
        for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            const QFileInfo info(file + suffix);
            struct stat metadata;
            if (!::lstat(QFile::encodeName(info.filePath()).constData(), &metadata)
                && (!S_ISREG(metadata.st_mode) || metadata.st_nlink != 1 || metadata.st_uid != ::geteuid()
                    || !QFile::setPermissions(info.filePath(), privateFile))) {
                failed(tr("Unsafe SQLite storage.")); return;
            }
        }
        m_connection = "ocean-downloads-" + QUuid::createUuid().toString(QUuid::Id128);
        m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
        m_db.setDatabaseName(file);
        if (!m_db.open()) { failed(m_db.lastError().text()); return; }
        QSqlQuery q(m_db);
        for (const QString &sql : {
            QStringLiteral("PRAGMA busy_timeout=5000"), QStringLiteral("PRAGMA journal_mode=WAL"),
            QStringLiteral("PRAGMA foreign_keys=ON"), QStringLiteral("PRAGMA secure_delete=ON"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS downloads(id INTEGER PRIMARY KEY AUTOINCREMENT,source_url TEXT NOT NULL,filename TEXT NOT NULL,destination TEXT NOT NULL,mime_type TEXT,total_bytes INTEGER DEFAULT -1,received_bytes INTEGER DEFAULT 0,status TEXT NOT NULL,started_at INTEGER NOT NULL,completed_at INTEGER,error TEXT DEFAULT '')"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_downloads_date ON downloads(started_at DESC)")}) {
            if (!q.exec(sql)) { failed(q.lastError().text()); return; }
        }
        bool hasError = false;
        if (!q.exec("PRAGMA table_info(downloads)")) { failed(q.lastError().text()); return; }
        while (q.next()) hasError |= q.value(1).toString() == "error";
        if (!hasError && !q.exec("ALTER TABLE downloads ADD COLUMN error TEXT DEFAULT ''")) { failed(q.lastError().text()); return; }
        if (!q.exec("UPDATE downloads SET status='Failed',error='Interrupted before completion' WHERE status IN ('Pending','Downloading','Paused')")) {
            failed(q.lastError().text()); return;
        }
        QList<QVariantMap> records;
        if (!q.exec("SELECT * FROM downloads ORDER BY started_at DESC,id DESC")) { failed(q.lastError().text()); return; }
        while (q.next()) {
            QVariantMap record;
            const QString key = "db:" + q.value(0).toString();
            m_ids.insert(key, q.value(0).toLongLong()); record["key"] = key;
            const QStringList fields={"source_url","filename","destination","mime_type","total_bytes","received_bytes","status","started_at","completed_at","error"};
            for (int i=0;i<fields.size();++i) record[fields[i]]=q.value(i+1);
            records.append(record);
        }
        m_usable = true;
        post([records](auto *o) {
            o->beginResetModel();
            for (const auto &r : records) {
                auto e=std::make_shared<OceanDownloadManager::Entry>(); e->owner=o;
                e->key=r.value("key").toString(); e->source=r.value("source_url").toString();
                e->name=r.value("filename").toString(); e->destination=r.value("destination").toString();
                e->mime=r.value("mime_type").toString(); e->total=r.value("total_bytes").toLongLong();
                e->received=r.value("received_bytes").toLongLong(); e->status=r.value("status").toString();
                e->started=r.value("started_at").toLongLong(); e->finished=r.value("completed_at").toLongLong();
                e->error=r.value("error").toString(); o->m_items.append(e);
            }
            o->m_ready=true; o->endResetModel(); emit o->stateChanged();
        });
    }
    void save(const QVariantMap &r) {
        if (!m_usable) return;
        const QString key=r.value("key").toString();
        if (!m_db.transaction()) { failed(m_db.lastError().text()); return; }
        QSqlQuery q(m_db);
        const QString fields="source_url=?,filename=?,destination=?,mime_type=?,total_bytes=?,received_bytes=?,status=?,started_at=?,completed_at=?,error=?";
        const bool exists=m_ids.contains(key);
        q.prepare(exists ? "UPDATE downloads SET "+fields+" WHERE id=?"
                         : "INSERT INTO downloads(source_url,filename,destination,mime_type,total_bytes,received_bytes,status,started_at,completed_at,error) VALUES(?,?,?,?,?,?,?,?,?,?)");
        for (const QString &f : {"source_url","filename","destination","mime_type","total_bytes","received_bytes","status","started_at","completed_at","error"}) q.addBindValue(r.value(f));
        if (exists) q.addBindValue(m_ids.value(key));
        if (!q.exec() || !m_db.commit()) { failed(q.lastError().text()); m_db.rollback(); return; }
        if (!exists) m_ids.insert(key,q.lastInsertId().toLongLong());
    }
    void remove(const QString &key) {
        if (!m_usable || !m_ids.contains(key)) return;
        QSqlQuery q(m_db); q.prepare("DELETE FROM downloads WHERE id=?"); q.addBindValue(m_ids.value(key));
        if (!q.exec()) failed(q.lastError().text()); else m_ids.remove(key);
    }
    void close() { m_db.close(); m_db={}; if (!m_connection.isEmpty()) QSqlDatabase::removeDatabase(m_connection); }
private:
    OceanDownloadManager *m_owner;
    QString m_path,m_connection;
    QSqlDatabase m_db;
    QHash<QString,qint64> m_ids;
    bool m_usable=false;
};

OceanDownloadManager::OceanDownloadManager(QString path, QString user, BrowserPreferences *preferences, QObject *parent)
    : QAbstractListModel(parent),m_path(std::move(path)),m_user(std::move(user)),m_preferences(preferences),m_store(new DownloadStore(this,m_path))
{
    m_store->moveToThread(&m_storeThread);
    connect(&m_storeThread,&QThread::finished,m_store,&QObject::deleteLater);
    m_storeThread.start(); QMetaObject::invokeMethod(m_store,[s=m_store]{s->initialize();},Qt::QueuedConnection);
    m_io.setMaxThreadCount(2);
    m_progress.setInterval(1000);
    connect(&m_progress,&QTimer::timeout,this,[this] {
        const qint64 now=QDateTime::currentMSecsSinceEpoch();
        for (const auto &e:m_items) if (e->native && e->status=="Downloading") {
            e->received=webkit_download_get_received_data_length(e->native);
            e->speed=qMax(0.0,1000.0*(e->received-e->sampleBytes)/qMax<qint64>(1,now-e->sampleTime));
            e->sampleBytes=e->received; e->sampleTime=now; changed(e);
        }
    });
}
OceanDownloadManager::~OceanDownloadManager() {
    m_closing=true; m_progress.stop();
    for (const auto &e:m_items) if (e->archiveCancel && !e->finalizing) fail(e,tr("Browser closed before completion."));
    m_io.waitForDone(); QCoreApplication::sendPostedEvents(this,QEvent::MetaCall);
    for (const auto &e:m_items) if (active(e->status)) fail(e,tr("Browser closed before completion."));
    const auto views=m_views.keys(); for (auto *v:views) releaseView(v);
    for (const auto &e:m_items) cleanup(e);
    QMetaObject::invokeMethod(m_store,[s=m_store]{s->close();},Qt::BlockingQueuedConnection);
    m_storeThread.quit(); m_storeThread.wait();
}
QString OceanDownloadManager::sanitizeFilename(QString name) {
    name.replace('\\','/'); name=name.section('/',-1).trimmed();
    name.remove(QRegularExpression("[\\x00-\\x1f\\x7f<>:\"|?*]"));
    while (name.startsWith('.')) name.remove(0,1);
    while (name.endsWith('.') || name.endsWith(' ')) name.chop(1);
    while (name.toUtf8().size()>200) name.chop(1);
    return name.isEmpty() ? QStringLiteral("download") : name;
}
OceanDownloadManager::Item OceanDownloadManager::find(const QString &id) const {
    for (const auto &e:m_items) if (e->key==id) return e;
    return {};
}
int OceanDownloadManager::rowCount(const QModelIndex &parent) const { return parent.isValid()?0:m_items.size(); }
QHash<int,QByteArray> OceanDownloadManager::roleNames() const {
    return {{DownloadId,"downloadId"},{FileName,"fileName"},{Destination,"destination"},{MimeType,"mimeType"},
        {TotalBytes,"totalBytes"},{ReceivedBytes,"receivedBytes"},{Progress,"progress"},{Status,"status"},
        {Speed,"speed"},{DateText,"dateText"},{ErrorDetail,"errorDetail"},{CanCancel,"canCancel"},
        {CanPause,"canPause"},{CanResume,"canResume"},{CanRetry,"canRetry"},{FileExists,"fileExists"},{PrivateDownload,"privateDownload"},{ThumbnailSource,"thumbnailSource"}};
}
QVariant OceanDownloadManager::data(const QModelIndex &index,int role) const {
    if (!index.isValid() || index.row()<0 || index.row()>=m_items.size()) return {};
    const auto &e=m_items[index.row()];
    switch (role) {
    case DownloadId:return e->key; case FileName:return e->name; case Destination:return e->destination;
    case MimeType:return e->mime; case TotalBytes:return e->total; case ReceivedBytes:return e->received;
    case Progress:return e->total>0?qMin(1.0,double(e->received)/e->total):0.0;
    case Status:return e->status; case Speed:return e->speed;
    case DateText:return QLocale().toString(QDateTime::fromMSecsSinceEpoch(e->started),QLocale::ShortFormat);
    case ErrorDetail:return e->error; case CanCancel:return active(e->status)&&!e->finalizing;
    case CanPause: case CanResume:return false;
    case CanRetry:return e->status=="Failed" && e->safeRetry && e->view && m_views.contains(e->view);
    case FileExists:return e->status=="Completed" && payloadAvailable(e->destination);
    case PrivateDownload:return e->privateTab;
    case ThumbnailSource:return QFileInfo(e->destination).isReadable() ? QUrl::fromLocalFile(e->destination) : QUrl();
    }
    return {};
}
void OceanDownloadManager::errorMessage(const QString &s) { m_error=s; emit stateChanged(); }
void OceanDownloadManager::changed(const Item &e,bool persist) {
    const int row=m_items.indexOf(e); if (row>=0) emit dataChanged(index(row),index(row));
    if (persist && !e->privateTab) {
        const auto record=e->snapshot(); QMetaObject::invokeMethod(m_store,[s=m_store,record]{s->save(record);},Qt::QueuedConnection);
    }
    bool busy=false; for (const auto &i:m_items) busy|=bool(i->native)&&active(i->status);
    if (busy && !m_progress.isActive()) m_progress.start(); else if (!busy) m_progress.stop();
}
void OceanDownloadManager::attachView(QObject *object,bool privateTab) {
    auto *view=nativeView(object); if (!view || m_views.contains(object)) return;
    auto *nativeSession=webkit_web_view_get_network_session(view);
    if (privateTab && !webkit_network_session_is_ephemeral(nativeSession)) {
        errorMessage(tr("The download session is not isolated.")); return;
    }
    Session *session=m_sessions.value(nativeSession);
    if (!session) {
        session=new Session{this,WEBKIT_NETWORK_SESSION(g_object_ref(nativeSession)),privateTab};
        m_sessions.insert(nativeSession,session);
        auto started=[](WebKitNetworkSession *,WebKitDownload *download,gpointer data) {
            auto *s=static_cast<Session *>(data); s->owner->track(download,s);
        };
        g_signal_connect(nativeSession,"download-started",G_CALLBACK(+started),session);
    }
    ++session->views;
    auto *context=new Context{this,object,WEBKIT_WEB_VIEW(g_object_ref(view)),session};
    m_views.insert(object,context);
    connect(object,&QObject::destroyed,this,[this,object]{releaseView(object);});
    auto menu=[](WebKitWebView *,WebKitContextMenu *,WebKitHitTestResult *hit,gpointer data)->gboolean {
        auto *c=static_cast<Context *>(data); c->owner->m_activeView=c->object;
        emit c->owner->contextMenuRequested(QString::fromUtf8(webkit_hit_test_result_get_image_uri(hit)),
                                           QString::fromUtf8(webkit_hit_test_result_get_link_uri(hit)));
        return TRUE;
    };
    g_signal_connect(view,"context-menu",G_CALLBACK(+menu),context);
}
void OceanDownloadManager::releaseView(QObject *object) {
    auto *c=m_views.take(object); if (!c) return;
    g_signal_handlers_disconnect_by_data(c->native,c); g_object_unref(c->native);
    auto *s=c->session; delete c;
    for (const auto &e:m_items) if (!e->view || e->view==object) { e->view=nullptr; changed(e,false); }
    if (--s->views) return;
    const auto records=m_items;
    for (const auto &e:records) if (e->session==s) {
        e->session=nullptr;
        if (s->privateTab) { if (active(e->status)&&!e->finalizing) cancel(e->key); if (!active(e->status)) removeRecord(e->key); }
    }
    g_signal_handlers_disconnect_by_data(s->native,s); m_sessions.remove(s->native); g_object_unref(s->native); delete s;
}
OceanDownloadManager::Item OceanDownloadManager::track(void *handle,Session *s) {
    auto *download=WEBKIT_DOWNLOAD(handle);
    for (const auto &e:m_items) if (e->native==download) return e;
    auto e=std::make_shared<Entry>(); e->owner=this; e->key=QUuid::createUuid().toString(QUuid::Id128);
    e->native=WEBKIT_DOWNLOAD(g_object_ref(download)); e->session=s; e->privateTab=s->privateTab;
    e->saveAs=m_preferences->askOnDownload();
    auto *request=webkit_download_get_request(download);
    e->source=QString::fromUtf8(webkit_uri_request_get_uri(request));
    e->safeRetry=QString::fromUtf8(webkit_uri_request_get_http_method(request))=="GET";
    e->name=sanitizeFilename(QUrl(e->source).fileName());
    auto *origin=webkit_download_get_web_view(download);
    for (auto *c:m_views) if (c->native==origin) { e->view=c->object; break; }
    beginInsertRows({},0,0); m_items.prepend(e); endInsertRows(); changed(e);
    auto decide=[](WebKitDownload *,const gchar *name,gpointer data)->gboolean {
        auto *e=static_cast<Entry *>(data); e->owner->decide(e->owner->find(e->key),QString::fromUtf8(name)); return TRUE;
    };
    auto failed=[](WebKitDownload *,GError *error,gpointer data) {
        auto *e=static_cast<Entry *>(data);
        if (active(e->status)) e->owner->fail(e->owner->find(e->key),QString::fromUtf8(error->message),
            g_error_matches(error,WEBKIT_DOWNLOAD_ERROR,WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER));
    };
    auto finished=[](WebKitDownload *,gpointer data) {
        auto *e=static_cast<Entry *>(data); e->owner->finishTransfer(e->owner->find(e->key));
    };
    g_signal_connect(download,"decide-destination",G_CALLBACK(+decide),e.get());
    g_signal_connect(download,"failed",G_CALLBACK(+failed),e.get());
    g_signal_connect(download,"finished",G_CALLBACK(+finished),e.get());
    return e;
}
void OceanDownloadManager::decide(const Item &e,const QString &suggested) {
    if (!e || !active(e->status)) return;
    auto *response=e->native?webkit_download_get_response(e->native):nullptr;
    if (response) {
        if (webkit_uri_response_get_status_code(response)>=400) { fail(e,tr("Server returned HTTP %1.").arg(webkit_uri_response_get_status_code(response))); return; }
        e->mime=QString::fromUtf8(webkit_uri_response_get_mime_type(response));
        const auto length=webkit_uri_response_get_content_length(response); e->total=length?static_cast<qint64>(length):-1;
    }
    e->name=sanitizeFilename(suggested);
    if (e->image && QFileInfo(e->name).suffix().isEmpty()) {
        const auto suffix=QMimeDatabase().mimeTypeForName(e->mime).preferredSuffix(); if (!suffix.isEmpty()) e->name+="."+suffix;
    }
    const QString folder = m_preferences->downloadFolder();
    e->portal=new OpenWavePortals(this);
    connect(e->portal,&OpenWavePortals::completed,this,[this,e](const QVariantList &grants) {
        if (!active(e->status)) return;
        auto *grant=grants.size()==1?qobject_cast<PortalGrant *>(grants.first().value<QObject *>()):nullptr;
        if (!grant || !grant->writable() || grant->directory()) { fail(e,tr("No writable save grant was returned.")); return; }
        e->descriptor=fcntl(grant->descriptor(),F_DUPFD_CLOEXEC,3);
        if (e->descriptor<0) { fail(e,tr("Could not retain the save grant.")); return; }
        e->name=sanitizeFilename(grant->name());
        char path[4096]; const QByteArray link=QStringLiteral("/proc/self/fd/%1").arg(e->descriptor).toLocal8Bit();
        const auto n=readlink(link.constData(),path,sizeof(path)-1);
        if (n>0) e->destination=QString::fromLocal8Bit(path,n);
        // This path is display metadata only. All writes use the granted fd.
        beginTransfer(e);
    });
    connect(e->portal,&OpenWavePortals::cancelled,this,[this,e]{if(active(e->status))fail(e,tr("Save cancelled."),true);});
    connect(e->portal,&OpenWavePortals::errorOccurred,this,[this,e](const QString &message){fail(e,message);});
    changed(e);
    if (e->saveAs) e->portal->saveFile({{"mutedTitle",tr("Downloading")},{"primaryTitle",tr("File")},
        {"initialDirectory",folder},{"suggestedFilename",e->name}});
    else e->portal->createFile(m_preferences->downloadDirectoryToken(), e->name);
}
void OceanDownloadManager::beginTransfer(const Item &e) {
    const QString directory=m_path+"/Cache/transfers";
    if (!safeDirectory(directory)) { fail(e,tr("The destination directory is unavailable.")); return; }
    const QString staging=directory+"/.ocean-download-"+e->key;
    if (QFileInfo::exists(staging) || !safeDirectory(staging)) { fail(e,tr("Could not create private transfer storage.")); return; }
    e->stage=staging+"/transfer.part";
    e->status="Downloading"; changed(e);
    if (e->archive) {
        auto *context=m_views.value(e->view);
        if (!context) { fail(e,tr("The page is no longer available.")); return; }
        struct ArchiveJob { QPointer<OceanDownloadManager> owner; Item entry; };
        e->archiveCancel=g_cancellable_new();
        auto callback=[](GObject *view,GAsyncResult *result,gpointer data) {
            std::unique_ptr<ArchiveJob> job(static_cast<ArchiveJob *>(data));
            GError *error=nullptr;
            auto *stream=webkit_web_view_save_finish(WEBKIT_WEB_VIEW(view),result,&error);
            if (!job->owner) { if(stream)g_object_unref(stream); g_clear_error(&error); return; }
            if (!active(job->entry->status) || !job->entry->archiveCancel || job->owner->m_closing) {
                if(stream)g_object_unref(stream); g_clear_error(&error); return;
            }
            if (!stream) {
                job->owner->fail(job->entry,tr("Could not archive this page: %1").arg(error?QString::fromUtf8(error->message):QString()));
                g_clear_error(&error); return;
            }
            auto e=job->entry; auto owner=job->owner;
            const QString stage=e->stage;
            auto *cancel=G_CANCELLABLE(g_object_ref(e->archiveCancel));
            owner->m_io.start([owner,e,stage,stream,cancel] {
                QString failure;
                QFile output(stage);
                if (!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)) failure=tr("Could not create page archive storage.");
                output.setPermissions(privateFile);
                char buffer[65536]; qint64 total=0;
                while (failure.isEmpty()) {
                    GError *error=nullptr;
                    const auto count=g_input_stream_read(stream,buffer,sizeof(buffer),cancel,&error);
                    if (count<0) failure=tr("Page archive export failed.");
                    g_clear_error(&error); if (count<=0) break;
                    if (output.write(buffer,count)!=count) { failure=tr("Could not write the page archive."); break; }
                    total+=count;
                }
                output.close(); g_object_unref(stream); g_object_unref(cancel);
                if (owner) QMetaObject::invokeMethod(owner,[owner,e,failure,total] {
                    if (!owner || !active(e->status) || owner->m_closing) return;
                    if (!failure.isEmpty()) owner->fail(e,failure);
                    else { e->received=e->total=total; owner->finishTransfer(e); }
                },Qt::QueuedConnection);
            });
        };
        webkit_web_view_save(context->native,WEBKIT_SAVE_MODE_MHTML,e->archiveCancel,+callback,new ArchiveJob{this,e});
        return;
    }
    webkit_download_set_allow_overwrite(e->native,FALSE);
    // The installed WPE API takes an absolute filesystem path, not a file URI.
    const QByteArray destination=QFile::encodeName(e->stage);
    webkit_download_set_destination(e->native,destination.constData());
}
void OceanDownloadManager::fail(const Item &e,const QString &message,bool cancelled) {
    if (!e || !active(e->status)) return;
    e->status=cancelled?"Cancelled":"Failed"; e->error=message; e->speed=0; changed(e);
    if (e->native) webkit_download_cancel(e->native);
    if (e->archiveCancel) g_cancellable_cancel(e->archiveCancel);
    if (e->portal && e->portal->busy()) e->portal->cancelRequest();
    if (!e->native && !e->finalizing) cleanup(e);
}
void OceanDownloadManager::finishTransfer(const Item &e) {
    if (!e) return;
    if (!active(e->status)) { cleanup(e); if (e->privateTab && !e->session) removeRecord(e->key); return; }
    if (e->native) e->received=webkit_download_get_received_data_length(e->native);
    if (e->native) { g_signal_handlers_disconnect_by_data(e->native,e.get()); g_object_unref(e->native); e->native=nullptr; }
    if (e->total > 0 && e->received < e->total) {
        fail(e,tr("The transfer ended before the complete file was received."));
        cleanup(e); return;
    }
    e->finalizing=true; changed(e);
    // Keep the destination chosen at transfer start, even if preferences change.
    const QString stage=e->stage;
    const int fd=e->descriptor; e->descriptor=-1;
    m_io.start([owner=QPointer(this),e,stage,fd] {
        QString failure,destination=e->destination;
        if (fd>=0) {
            QFile input(stage);
            if (!input.open(QIODevice::ReadOnly) || lseek(fd,0,SEEK_SET)<0 || ftruncate(fd,0)<0) failure=tr("Could not write the save grant.");
            while (failure.isEmpty() && !input.atEnd()) {
                const QByteArray chunk=input.read(1024*1024);
                if (chunk.isEmpty() && input.error()!=QFile::NoError) { failure=tr("Could not read the transfer."); break; }
                qint64 offset=0;
                while (offset<chunk.size()) {
                    const auto written=::write(fd,chunk.constData()+offset,chunk.size()-offset);
                    if (written<0 && errno==EINTR) continue;
                    if (written<=0) { failure=tr("Could not write the save grant (disk space or permission failure)."); break; }
                    offset+=written;
                }
            }
            if (failure.isEmpty() && fsync(fd)) failure=tr("Could not flush the saved file.");
            if (!failure.isEmpty()) ftruncate(fd,0);
            ::close(fd);
        } else failure=tr("The download has no writable file grant.");
        if (owner) QMetaObject::invokeMethod(owner,[owner,e,failure,destination] {
            if (!owner) return;
            e->finalizing=false;
            if (!failure.isEmpty()) owner->fail(e,failure);
            else { e->status="Completed"; e->destination=destination; e->name=QFileInfo(destination).fileName();
                e->finished=QDateTime::currentMSecsSinceEpoch(); e->speed=0; owner->changed(e); emit owner->completed(e->name); }
            owner->cleanup(e);
            if (e->privateTab && !e->session) owner->removeRecord(e->key);
        },Qt::QueuedConnection);
    });
}
void OceanDownloadManager::cleanup(const Item &e) {
    if (e->native) { g_signal_handlers_disconnect_by_data(e->native,e.get()); g_object_unref(e->native); e->native=nullptr; }
    if (e->descriptor>=0) { ::close(e->descriptor); e->descriptor=-1; }
    if (e->archiveCancel) { g_object_unref(e->archiveCancel); e->archiveCancel=nullptr; }
    if (e->portal) { e->portal->deleteLater(); e->portal=nullptr; }
    if (!e->stage.isEmpty()) { QFile::remove(e->stage); QDir().rmdir(QFileInfo(e->stage).absolutePath()); e->stage.clear(); }
}
void OceanDownloadManager::cancel(const QString &id) { fail(find(id),tr("Cancelled by you."),true); }
void OceanDownloadManager::retry(const QString &id) {
    const auto e=find(id); if (!e || !e->safeRetry || !e->view || e->status!="Failed") return;
    m_activeView=e->view; saveResource(e->source,e->image,e->saveAs);
}
void OceanDownloadManager::removeRecord(const QString &id) {
    const auto e=find(id); if (!e || active(e->status) || e->native || e->finalizing) return;
    const int row=m_items.indexOf(e); beginRemoveRows({},row,row); m_items.removeAt(row); endRemoveRows();
    if (!e->privateTab) QMetaObject::invokeMethod(m_store,[s=m_store,id]{s->remove(id);},Qt::QueuedConnection);
}
void OceanDownloadManager::clearCompleted() { const auto rows=m_items; for (const auto &e:rows) if (e->status=="Completed") removeRecord(e->key); }
void OceanDownloadManager::openFile(const QString &id) {
    const auto e=find(id); if (!e || e->status!="Completed") return;
    if (!payloadAvailable(e->destination)) { errorMessage(tr("The downloaded file is missing.")); changed(e,false); return; }
    OpenWaveApplications::launch(QStringLiteral("com.openwave.files"), {e->destination}, this,
        [this](const QString &error) { if (!error.isEmpty()) errorMessage(error); });
}
void OceanDownloadManager::showInFiles(const QString &id) {
    const auto e=find(id); if (!e || e->status!="Completed") return;
    if (!payloadAvailable(e->destination)) { errorMessage(tr("The downloaded file is missing.")); changed(e,false); return; }
    OpenWaveApplications::launch(QStringLiteral("com.openwave.files"), {QFileInfo(e->destination).absolutePath()}, this,
        [this](const QString &error) { if (!error.isEmpty()) errorMessage(error); });
}
void OceanDownloadManager::saveResource(const QString &url,bool image,bool saveAs) {
    auto *context=m_views.value(m_activeView); if (!context) return;
    auto *download=webkit_web_view_download_uri(context->native,QUrl(url).toEncoded().constData());
    if (!download) { errorMessage(tr("The engine could not start this download.")); return; }
    auto e=track(download,context->session); e->view=context->object; e->image=image; e->saveAs=saveAs || m_preferences->askOnDownload(); changed(e);
    g_object_unref(download);
}
void OceanDownloadManager::savePageAs() {
    auto *c=m_views.value(m_activeView); if (!c) return;
    const QString url=QString::fromUtf8(webkit_web_view_get_uri(c->native));
    if (!url.startsWith("http:") && !url.startsWith("https:")) { errorMessage(tr("Only web pages can be archived.")); return; }
    auto e=std::make_shared<Entry>(); e->owner=this; e->key=QUuid::createUuid().toString(QUuid::Id128);
    e->source=url; e->view=c->object; e->session=c->session; e->privateTab=c->session->privateTab;
    e->archive=e->saveAs=true; e->mime="multipart/related";
    e->name=sanitizeFilename(QString::fromUtf8(webkit_web_view_get_title(c->native)))+".mhtml";
    beginInsertRows({},0,0); m_items.prepend(e); endInsertRows(); changed(e); decide(e,e->name);
}
