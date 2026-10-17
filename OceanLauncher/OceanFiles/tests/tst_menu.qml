import QtQuick
import QtQuick.Window
import QtTest
import ".." as Files

Item {
    ListModel {
        id: backend
        property int activations: 0
        property int creations: 0
        property int replacements: 0
        property bool existing: false
        property int navigations: 0
        property string rootPath: "/Data/Users/root"
        property string currentPath: rootPath
        property string searchQuery: ""
        property bool selectionMode: false
        property var selectedRows: []
        readonly property int selectedCount: selectedRows.length
        readonly property string selectedName: selectedCount === 1 ? get(selectedRows[0]).name : ""
        readonly property bool selectedFolder: selectedCount === 1 && get(selectedRows[0]).folder
        property int renames: 0
        property int deletions: 0
        property string deletionDirectory: ""
        property string deletionToken: ""
        property string transferMode: ""
        property int transferCount: 0
        property int transfers: 0
        property string transferDestination: ""
        property bool renameFails: false
        property string renamedFrom: ""
        property string renamedTo: ""
        property bool showHidden: false
        property string error: ""
        property bool previewVisible: false
        property bool previewAudio: false
        property string previewName: ""
        property string previewDetail: ""
        property int sortMode: 0
        property string breadcrumb: "Data"
        ListElement { name: "First folder"; folder: true; detail: ""; selected: false; audio: false }
        ListElement { name: "Second folder"; folder: true; detail: ""; selected: false; audio: false }
        ListElement { name: "Third folder"; folder: true; detail: ""; selected: false; audio: false }
        ListElement { name: "Fourth folder"; folder: true; detail: ""; selected: false; audio: false }
        function activate(index) {
            activations++
            if (selectionMode) {
                const rows = selectedRows.slice()
                const position = rows.indexOf(index)
                if (position < 0) rows.push(index)
                else rows.splice(position, 1)
                selectedRows = rows
                setProperty(index, "selected", position < 0)
            }
        }
        function entryPath(index) { return rootPath + "/folder-" + index }
        function goToDirectory(path) {
            currentPath = path
            breadcrumb = "Data > " + path.split("/").pop()
            navigations++
            return true
        }
        function up() {
            if (currentPath === rootPath) return false
            currentPath = rootPath
            breadcrumb = "Data"
            navigations++
            return true
        }
        function setSearchQuery(query) { searchQuery = query }
        function createFile(name) { creations++; return true }
        function fileExists(name) { return existing }
        function replaceFile(name, directory) {
            if (currentPath !== directory) return false
            replacements++; return true
        }
        function createFolder(name) { creations++; return true }
        function setSelectionMode(value) {
            selectionMode = value
            if (!value) {
                selectedRows = []
                for (let row = 0; row < count; ++row) setProperty(row, "selected", false)
            }
        }
        function renameItem(oldName, newName, directory) {
            if (currentPath !== directory || renameFails) { error = "Name already exists"; return false }
            renames++; renamedFrom = oldName; renamedTo = newName
            setSelectionMode(false)
            return true
        }
        function prepareDeletion() { deletionDirectory = currentPath; deletionToken = "delete-1"; return deletionToken }
        function cancelDeletion() { deletionToken = "" }
        function deleteSelection(token, directory) {
            if (token !== deletionToken || directory !== deletionDirectory || directory !== currentPath) return false
            deletions++; cancelDeletion(); setSelectionMode(false); return true
        }
        function prepareTransfer(mode) {
            if (selectedCount < 1) return false
            transferMode = mode; transferCount = selectedCount; setSelectionMode(false); return true
        }
        function transferSelection(directory) {
            if (!transferMode || directory !== currentPath) return false
            transfers++; transferDestination = directory; cancelTransfer(); return true
        }
        function cancelTransfer() { transferMode = ""; transferCount = 0 }
        function setShowHidden(value) { showHidden = value }
        function setSortMode(value) { sortMode = value }
        function closePreview() { previewVisible = false }
    }

    QtObject {
        id: prompts
        property bool busy: false
        property int requests: 0
        property string title: ""
        property string message: ""
        property string type: ""
        property var options: ({})
        signal finished(string requestId, string result, string text)
        signal errorOccurred(string message)
        function confirmation(title, message, options) {
            this.title = title; this.message = message; this.options = options; type = "confirmation"
            busy = true; requests++
        }
        function textInput(title, message, options) {
            this.title = title; this.message = message; this.options = options; type = "text_input"
            busy = true; requests++
        }
        function cancel() { if (busy) complete("cancelled") }
        function complete(result, text) { busy = false; finished("prompt-1", result, text || "") }
    }
    Files.Main {
        id: files
        filesBackend: backend
        promptClient: prompts
    }

    TestCase {
        name: "FilesMenu"
        when: windowShown
        function menuAction(key) { return findChild(findChild(files, "filesMenu"), "filesMenuAction_" + key) }
        function operation(key) { return findChild(findChild(files, "filesOperationsBar"), "filesOperation_" + key) }
        function clickOperation(key) { wait(40); mouseClick(operation(key)) }
        function row(index) { return findChild(findChild(files, "filesList"), "filesRow_" + index) }

        function init() {
            files.showNormal()
            files.menuOpen = false
            if (files.naming) files.back()
            files.namingAction = ""
            prompts.busy = false
            files.sorting = false
            files.reduceMotion = false
            backend.previewVisible = false
            backend.activations = 0
            backend.creations = 0
            backend.replacements = 0
            backend.renames = 0
            backend.deletions = 0
            backend.transfers = 0
            backend.cancelDeletion()
            backend.cancelTransfer()
            backend.renameFails = false
            backend.error = ""
            backend.existing = false
            prompts.requests = 0
            files.promptError = ""
            backend.navigations = 0
            backend.currentPath = backend.rootPath
            backend.breadcrumb = "Data"
            backend.searchQuery = ""
            backend.setSelectionMode(false)
            backend.showHidden = false
            backend.sortMode = 0
            wait(320)
        }

        function test_replacement_needs_positive_confirmation() {
            backend.existing = true
            files.beginNaming("create_file")
            prompts.complete("accepted", "original.txt")
            compare(prompts.requests, 2)
            compare(prompts.type, "confirmation")
            compare(backend.creations, 0)
            compare(backend.replacements, 0)
            prompts.complete("cancelled")
            compare(backend.replacements, 0)
            files.beginNaming("create_file")
            prompts.complete("accepted", "original.txt")
            prompts.errorOccurred("GDBus.Error: simulated disconnect")
            compare(files.promptError, "The prompt could not be completed. No files were changed.")
            prompts.complete("service_lost")
            compare(backend.replacements, 0)
            files.beginNaming("create_file")
            prompts.complete("accepted", "original.txt")
            backend.currentPath = backend.rootPath + "/Documents"
            prompts.complete("accepted")
            compare(backend.replacements, 0)
            backend.currentPath = backend.rootPath
            files.beginNaming("create_file")
            prompts.complete("accepted", "original.txt")
            prompts.complete("accepted")
            compare(backend.replacements, 1)
            verify(!files.naming)
        }

        function test_corner_and_open_animation() {
            let menu = findChild(files, "filesMenu")
            compare(menu.topRightRadius, 0)
            compare(menu.radius, 42)
            files.menuOpen = true
            wait(80)
            verify(menu.reveal > 0 && menu.reveal < 1)
            let corner = menu.mapToItem(files.contentItem, menu.width, 0)
            compare(Math.round(corner.x), 649)
            compare(Math.round(corner.y), 157)
            tryCompare(menu, "reveal", 1)
        }

        function test_menu_uses_source_sans_pro_extralight() {
            files.menuOpen = true
            wait(50)
            let label = findChild(findChild(files, "filesMenu"), "filesMenuLabel")
            verify(label !== null)
            verify(label.font.family.indexOf("Source Sans Pro") >= 0)
            compare(label.font.weight, Font.ExtraLight)
        }

        function test_menu_action_does_not_open_file_underneath() {
            files.menuOpen = true
            wait(320)
            mouseClick(menuAction("sort"))
            compare(files.sorting, true)
            compare(backend.activations, 0)
        }

        function test_menu_blocks_files_and_outside_tap_dismisses() {
            files.menuOpen = true
            wait(320)
            mouseClick(files.contentItem, 110, 250)
            compare(files.menuOpen, false)
            compare(backend.activations, 0)

            wait(320)
            files.menuOpen = true
            wait(320)
            mouseClick(menuAction("sort"))
            compare(files.sorting, true)
            compare(backend.activations, 0)
        }

        function test_hidden_folders_toggle_does_not_activate_file() {
            const menu = findChild(files, "filesMenu")
            compare(menu.hiddenFoldersAction, "Show hidden folders")
            files.menuOpen = true
            wait(320)
            const menuWidth = menu.width
            mouseClick(menuAction("hidden"))
            compare(backend.showHidden, true)
            compare(files.menuOpen, false)
            compare(menu.hiddenFoldersAction, "Hide hidden folders")
            compare(menu.width, menuWidth)
            compare(backend.activations, 0)
            wait(320)
            files.menuOpen = true
            wait(320)
            mouseClick(menuAction("hidden"))
            compare(backend.showHidden, false)
            compare(backend.activations, 0)
        }

        function test_pending_prompt_shields_app_input() {
            files.beginNaming("create_file")
            mouseClick(files.contentItem, 300, 540)
            compare(files.naming, true)
            compare(backend.activations, 0)
            mouseClick(files.contentItem, 110, 245)
            compare(files.naming, true)
            compare(backend.activations, 0)
            files.back()
            verify(!files.naming)
            compare(backend.creations, 0)
        }

        function test_delete_requires_confirmation_and_preserves_cancel() {
            files.beginDeletion()
            compare(prompts.requests, 0)
            backend.setSelectionMode(true)
            files.activateRow(0, true)
            clickOperation("delete")
            compare(prompts.title, "Delete folder?")
            compare(prompts.options.acceptLabel, "Delete")
            verify(prompts.message.indexOf("First folder") >= 0)
            verify(prompts.message.indexOf("all its contents") >= 0)
            compare(backend.deletions, 0)
            prompts.complete("cancelled")
            compare(backend.deletions, 0)
            compare(backend.selectedCount, 1)
            files.beginDeletion()
            prompts.complete("service_lost")
            compare(backend.deletions, 0)
            files.beginDeletion()
            backend.currentPath = backend.rootPath + "/Documents"
            prompts.complete("accepted")
            compare(backend.deletions, 0)
            backend.currentPath = backend.rootPath
            files.beginDeletion()
            prompts.complete("accepted")
            compare(backend.deletions, 1)
            verify(!files.naming)
        }
        function test_delete_multiple_and_back_cancel() {
            backend.setSelectionMode(true)
            files.activateRow(0, true); files.activateRow(1, true)
            files.beginDeletion()
            compare(prompts.title, "Delete selected items?")
            verify(prompts.message.indexOf("2 selected items") >= 0)
            files.back()
            compare(backend.deletions, 0)
            compare(backend.deletionToken, "")
            verify(!files.naming)
        }
        function test_copy_and_move_choose_destination() {
            for (const mode of ["copy", "move"]) {
                backend.setSelectionMode(true)
                files.activateRow(0, true); files.activateRow(1, true)
                clickOperation(mode)
                compare(backend.transferMode, mode)
                compare(backend.transferCount, 2)
                verify(!backend.selectionMode)
                compare(backend.transfers, mode === "copy" ? 0 : 1)
                files.reduceMotion = true
                files.navigateTo(backend.rootPath + "/Documents", false)
                clickOperation(mode)
                compare(backend.transferDestination, backend.rootPath + "/Documents")
                compare(backend.transferMode, "")
            }
            compare(backend.transfers, 2)
            backend.setSelectionMode(true); files.activateRow(0, true)
            files.beginTransfer("move")
            clickOperation("cancel")
            compare(backend.transferMode, "")
            compare(backend.transfers, 2)
        }

        function test_sort_option_does_not_activate_file() {
            files.sorting = true
            mouseClick(files.contentItem, 180, 650)
            compare(backend.sortMode, 1)
            compare(files.sorting, false)
            compare(backend.activations, 0)
        }

        function test_creation_uses_system_text_prompt() {
            files.beginNaming("create_file")
            compare(prompts.type, "text_input")
            compare(prompts.title, "New file")
            compare(prompts.message, "")
            compare(prompts.options.placeholder, "type file name")
            compare(prompts.options.acceptLabel, "Create")
            verify(new RegExp(prompts.options.validationPattern).test("notes.txt"))
            verify(!new RegExp("^(?:" + prompts.options.validationPattern + ")$").test("../notes.txt"))
            prompts.complete("cancelled")
            compare(backend.creations, 0)
            files.beginNaming("create_folder")
            compare(prompts.title, "New folder")
            compare(prompts.options.placeholder, "type folder name")
            prompts.complete("accepted", "Designs")
            compare(backend.creations, 1)
            compare(files.naming, false)
        }

        function test_rename_requires_one_selection_and_preserves_cancel() {
            files.beginNaming("rename")
            compare(prompts.requests, 0)
            verify(!files.operationAvailable("rename"))
            backend.setSelectionMode(true)
            files.activateRow(0, true)
            verify(files.operationAvailable("rename"))
            compare(backend.navigations, 0)
            clickOperation("rename")
            compare(prompts.title, "Rename folder")
            compare(prompts.options.initialText, "First folder")
            compare(prompts.options.placeholder, "type new folder name")
            compare(prompts.options.acceptLabel, "Rename")
            prompts.complete("cancelled")
            compare(backend.renames, 0)
            files.beginNaming("rename")
            prompts.complete("accepted", "New name")
            compare(backend.renames, 1)
            compare(backend.renamedFrom, "First folder")
            compare(backend.renamedTo, "New name")
            verify(!backend.selectionMode)
            backend.setSelectionMode(true)
            files.activateRow(0, true)
            files.activateRow(1, true)
            verify(!files.operationAvailable("rename"))
            files.beginNaming("rename")
            compare(prompts.requests, 2)
        }

        function test_operations_leave_dropdown_and_creation_uses_bar() {
            compare(menuAction("create_file"), null)
            compare(menuAction("create_folder"), null)
            compare(menuAction("delete"), null)
            compare(menuAction("move"), null)
            compare(menuAction("copy"), null)
            compare(menuAction("rename"), null)
            verify(menuAction("sort") !== null)
            verify(menuAction("hidden") !== null)
            verify(operation("create_file").available)
            verify(operation("create_folder").available)
            verify(!operation("cancel").available)
            clickOperation("delete")
            compare(prompts.requests, 0)
            clickOperation("create_folder")
            compare(prompts.title, "New folder")
            prompts.complete("cancelled")
            clickOperation("create_file")
            compare(prompts.title, "New file")
            prompts.complete("cancelled")
            compare(backend.activations, 0)
        }
        function test_hold_selects_without_opening_and_animates_rows() {
            const first = row(0)
            const contents = findChild(first, "filesRowContents")
            const circle = findChild(first, "filesSelectionCircle")
            compare(contents.x, 0)
            compare(circle.opacity, 0)
            mousePress(first, 120, 45)
            wait(550)
            compare(backend.selectionMode, true)
            compare(backend.selectedCount, 1)
            compare(backend.navigations, 0)
            mouseRelease(first, 120, 45)
            tryCompare(contents, "x", 38)
            compare(backend.selectedCount, 1)
            compare(backend.navigations, 0)
            verify(operation("rename").visible)
            clickOperation("cancel")
            verify(!backend.selectionMode)
            wait(65)
            verify(contents.x > 0 && contents.x < 38)
            tryCompare(contents, "x", 0)
            compare(circle.opacity, 0)
            verify(!operation("rename").visible)
        }
        function test_selection_availability_deselection_and_cancel() {
            files.holdSelectRow(0, false)
            verify(!operation("create_file").available)
            verify(!operation("create_folder").available)
            for (const key of ["delete", "move", "copy", "cancel"]) verify(operation(key).available)
            clickOperation("create_file"); clickOperation("create_folder")
            compare(prompts.requests, 0)
            files.activateRow(0, true)
            compare(backend.selectedCount, 0)
            verify(backend.selectionMode)
            for (const key of ["delete", "move", "copy", "create_file", "create_folder"]) verify(!operation(key).available)
            verify(operation("cancel").available)
            tryCompare(operation("delete"), "inactive", 1)
            clickOperation("delete")
            compare(prompts.requests, 0)
            clickOperation("cancel")
            verify(operation("create_file").available)
            verify(operation("create_folder").available)
            for (const key of ["delete", "move", "copy", "cancel"]) verify(!operation(key).available)
        }
        function test_circle_and_contents_share_open_animation() {
            const first = row(0)
            const contents = findChild(first, "filesRowContents")
            const circle = findChild(first, "filesSelectionCircle")
            files.holdSelectRow(0, false)
            compare(contents.x, 0)
            compare(circle.opacity, 0)
            wait(65)
            verify(contents.x > 0 && contents.x < 38)
            verify(circle.opacity > 0 && circle.opacity < 1)
            fuzzyCompare(contents.x, 38 * circle.opacity, 0.01)
            tryCompare(contents, "x", 38)
        }
        function test_holding_selected_row_keeps_selection_and_reduced_motion() {
            files.reduceMotion = true
            files.holdSelectRow(0, false)
            compare(files.selectionReveal, 1)
            files.holdSelectRow(0, true)
            compare(backend.selectedCount, 1)
            compare(backend.navigations, 0)
            files.holdSelectRow(1, false)
            compare(backend.selectedCount, 2)
            verify(!operation("rename").visible)
            files.cancelOperations()
            compare(files.selectionReveal, 0)
            compare(backend.selectedCount, 0)
        }

        function test_failed_rename_retries_and_stale_directory_is_rejected() {
            backend.setSelectionMode(true)
            files.activateRow(0, true)
            backend.renameFails = true
            files.beginNaming("rename")
            prompts.complete("accepted", "Existing name")
            compare(backend.renames, 0)
            verify(files.naming)
            compare(prompts.options.initialText, "Existing name")
            compare(prompts.message, "Name already exists")
            backend.renameFails = false
            prompts.complete("accepted", "Available name")
            compare(backend.renames, 1)
            compare(files.promptError, "")
            backend.setSelectionMode(true)
            files.activateRow(0, true)
            files.beginNaming("rename")
            backend.currentPath = backend.rootPath + "/Documents"
            prompts.complete("accepted", "Different name")
            compare(backend.renames, 1)
            verify(!files.naming)
            compare(files.promptError, "The folder changed. Please try again.")
        }

        function test_preview_prompt_blocks_file_taps() {
            backend.previewVisible = true
            mouseClick(files.contentItem, 300, 540)
            compare(backend.previewVisible, true)
            compare(backend.activations, 0)
            mouseClick(files.contentItem, 110, 245)
            compare(backend.previewVisible, false)
            compare(backend.activations, 0)
        }

        function test_folder_navigation_crossfades_and_back_returns() {
            files.activateRow(0, true)
            compare(files.navigating, true)
            files.activateRow(1, true)
            let oldView = findChild(files, "filesPreviousFolderContent")
            verify(oldView !== null)
            tryCompare(oldView, "visible", true)
            tryCompare(backend, "currentPath", backend.rootPath + "/folder-0")
            verify(oldView.opacity > 0)
            compare(backend.activations, 0)
            tryCompare(files, "navigating", false)
            compare(backend.navigations, 1)
            compare(backend.breadcrumb, "Data > folder-0")

            files.back()
            compare(files.navigating, true)
            tryCompare(backend, "currentPath", backend.rootPath)
            tryCompare(files, "navigating", false)
            compare(backend.navigations, 2)
        }

        function test_system_back_key_returns_to_parent_folder() {
            files.activateRow(0, true)
            tryCompare(files, "navigating", false)
            compare(backend.currentPath, backend.rootPath + "/folder-0")
            files.requestActivate()
            wait(30)
            keyClick(Qt.Key_Back)
            tryCompare(backend, "currentPath", backend.rootPath)
            tryCompare(files, "navigating", false)
            compare(backend.navigations, 2)
        }

        function test_root_back_minimizes_and_preserves_state() {
            backend.searchQuery = "keep this search"
            files.requestActivate()
            wait(30)
            try {
                keyClick(Qt.Key_Back)
                tryCompare(files, "visibility", Window.Minimized)
                compare(backend.currentPath, backend.rootPath)
                compare(backend.searchQuery, "keep this search")
            } finally {
                files.showNormal()
            }
        }

        function test_file_activation_and_selection_do_not_navigate() {
            files.activateRow(0, false)
            compare(backend.activations, 1)
            compare(files.navigating, false)
            backend.selectionMode = true
            files.activateRow(0, true)
            compare(backend.activations, 2)
            compare(backend.navigations, 0)
        }

        function test_reduced_motion_navigates_without_animation() {
            files.reduceMotion = true
            files.activateRow(0, true)
            compare(backend.currentPath, backend.rootPath + "/folder-0")
            compare(files.navigating, false)
            files.back()
            compare(backend.currentPath, backend.rootPath)
            compare(files.navigating, false)
        }
    }
}
