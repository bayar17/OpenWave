#include "PermissionRegistry.h"
#include "PolicyStore.h"

PermissionRegistry::PermissionRegistry()
{
    add({"files.read", "Open selected files", "Allow opening files you select in the system file chooser.", true});
    add({"files.write", "Save files", "Allow creating new files in Downloads or folders you choose. Existing files will not be replaced.", true});
    add({"files.directory", "Choose folders", "Allow choosing folders and listing their names. Saving files requires separate authorization.", true});
    add({"camera", "Camera", "Use a camera through the system camera service."});
    add({"microphone", "Microphone", "Record audio through the system capture service."});
    add({"location", "Location", "Access location through the system location service."});
    add({"photos", "Photos and media library", "Access media through the system media service."});
    add({"notifications", "Notifications", "Send notifications through the system notification service."});
}
bool PermissionRegistry::add(PermissionDefinition entry)
{
    if (!PolicyStore::validPermission(entry.id) || entry.label.trimmed().isEmpty() || entry.label.size() > 128
        || entry.description.size() > 512 || m_definitions.contains(entry.id)) return false;
    m_definitions.insert(entry.id, std::move(entry)); return true;
}
PermissionDefinition PermissionRegistry::definition(const QString &id) const
{ return m_definitions.value(id, {id, id, "", false}); }
