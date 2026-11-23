# Ocean Files selection actions

Hold a file or folder to enter selection mode and select that item. Row icons,
names and sizes slide right as selection circles fade in. Tap other rows to
toggle their selection; holding an already selected item keeps it selected.

- File operations live in the translucent bottom bar. The dropdown contains
  Sort and Show hidden folders.
- **Rename** appears in the bottom bar for exactly one selected item.
- **Delete** opens the shared frosted prompt. Deletion is permanent, includes
  folder contents, and starts only after choosing Delete in the confirmation.
- **Move** or **Copy** captures the selected items and exits selection mode.
  Browse to the destination, then choose **Move here** or **Copy here** in
  the bottom bar. **Cancel** discards the pending operation.

New Folder and New File are available outside selection mode. Selection enables
Delete, Move, Copy and Cancel. Deselecting every item keeps selection mode active
with only Cancel available. Cancel restores the row positions and creation
actions. Unavailable actions fade to a blurred appearance and cannot be clicked.
During destination browsing, New Folder remains available so a destination can
be created. Reduced motion skips the row and action-state animations.

Files preserves existing destination items and rejects placing a folder inside
itself. The source directory and item identities are checked before acting.
Transfers remain available while browsing and are not persisted on app exit.
Completed items are retained if a later item fails; the pending transfer then
contains only the remaining items.

Recursive operations include hidden contents and use directory handles without
following symbolic links. Copy preserves links as links. Delete removes links
inside a selected folder without deleting their targets. Mounted subtrees are
not traversed. File content and ordinary permission bits are preserved on copy;
move uses an atomic rename that refuses an existing destination.

Backend tests are in `tests/FilesBackendTest.cpp`; dropdown, confirmation and
destination workflows are tested in `tests/tst_menu.qml`.
