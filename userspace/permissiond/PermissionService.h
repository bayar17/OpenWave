#pragma once
#include "PolicyStore.h"
#include "PermissionRegistry.h"
#include "LifecycleBridge.h"
#include <memory>
#include <vector>
#undef signals
#include <gio/gio.h>

struct PermissionIdentity {
    QString app, name, session, instance, userRoot, digest;
    guint user = 0, uid = 0, pid = 0, leader = 0;
    guint64 start = 0;
    bool trusted = false, administrator = false;
};
class PermissionService {
public:
    explicit PermissionService(QString directory);
    ~PermissionService();
    bool start(GError **error);
    static void dispatch(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *, GDBusMethodInvocation *, void *);
private:
    struct Waiter;
    struct Pending;
    struct Lease;
    bool identity(const char *sender, PermissionIdentity *identity);
    bool broker(const char *sender);
    void method(const char *sender, const char *method, GVariant *, GDBusMethodInvocation *);
    void request(const QString &sender, const PermissionIdentity &, const QString &permission, bool brokered, GDBusMethodInvocation *);
    void respond(Waiter &, const QString &state);
    void complete(const std::shared_ptr<Pending> &, const QString &result, const QString &choice);
    void poll(const std::shared_ptr<Pending> &);
    void changed(const PermissionIdentity &, const QString &permission, const QString &state);
    bool finishRevocation(PermissionPolicy policy, QString *error);
    void invalidate(const QString &app, guint user, const QString &instance = {});
    void lifecycle(const QString &event, const QString &app, const QString &instance, bool replaced);
    GVariant *records(const PermissionIdentity &, bool administrative);
    static void nameChanged(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *, void *);
    static gboolean tick(void *);
    PolicyStore m_store;
    PermissionRegistry m_registry;
    std::unique_ptr<LifecycleBridge> m_lifecycle;
    QString m_policyRoot;
    GDBusConnection *m_bus = nullptr, *m_promptBus = nullptr;
    QString m_appOwner;
    guint m_sessionUser = 0;
    guint m_registration = 0, m_owner = 0, m_subscription = 0, m_timer = 0;
    std::vector<std::shared_ptr<Pending>> m_pending;
    QMap<QString, std::shared_ptr<Lease>> m_leases;
    QMap<QString, PermissionIdentity> m_watchers;
};
