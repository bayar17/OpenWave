#pragma once
#include <QMap>
#include <QString>

struct PermissionDefinition { QString id, label, description; bool permanent = false; };
class PermissionRegistry {
public:
    PermissionRegistry();
    bool add(PermissionDefinition definition);
    PermissionDefinition definition(const QString &id) const;
private:
    QMap<QString, PermissionDefinition> m_definitions;
};
