import QtQuick
import QtTest

import "../.." as Settings

Item {
    width: 400; height: 800
    QtObject {
        id: backend
        property var defaultCategories: []
        property string categoriesError: ""
        property int refreshed: 0
        property var chosen: ({})
        function refreshCategories() { refreshed++ }
        function refreshFileTypes() {}
        function setCategoryDefault(category, appId) { chosen = {category: category, appId: appId} }
    }
    QtObject {
        id: prompts
        property bool busy: false
        property var options: ({})
        property string title: ""
        signal finished(string requestId, string result, string text)
        function selection(heading, message, choices) { title = heading; options = choices; busy = true }
        function cancel() { busy = false; finished("1", "cancelled", "") }
        function accept(value) { busy = false; finished("1", "accepted", value) }
    }
    Settings.DefaultAppsPage { id: page; width: 400; height: 800; backend: backend; prompts: prompts }
    SignalSpy { id: fileTypes; target: page; signalName: "fileTypesRequested" }
    SignalSpy { id: back; target: page; signalName: "backRequested" }
    TestCase {
        name: "DefaultApps"; when: windowShown
        function init() {
            backend.defaultCategories = [
                {id: "pdf", name: "PDF viewer", handlers: [{id: "org.test.viewer", name: "Viewer"}, {id: "org.test.other", name: "Other"}], defaultId: "org.test.viewer", defaultName: "Viewer", state: "set"},
                {id: "photos", name: "Photos and images", handlers: [{id: "org.test.other", name: "Other"}], defaultId: "", defaultName: "", state: "unset"},
                {id: "video", name: "Videos", handlers: [{id: "org.android.player", name: "Player"}], defaultId: "", defaultName: "", state: "unavailable"}
            ]
            backend.chosen = ({}); prompts.busy = false; prompts.options = ({}); fileTypes.clear(); back.clear()
        }
        function test_categories_show_their_current_choice() {
            wait(50)
            const pdf = findChild(page, "settingsDefaultCategory_pdf")
            const photos = findChild(page, "settingsDefaultCategory_photos")
            const video = findChild(page, "settingsDefaultCategory_video")
            verify(pdf && photos && video)
            compare(pdf.detail, "Viewer")
            compare(photos.detail, "Not set")
            compare(video.detail, "Chosen app is not available right now")
        }
        function test_choosing_a_handler_saves_exactly_that_category() {
            wait(50)
            page.choose(backend.defaultCategories[0])
            verify(prompts.busy)
            compare(prompts.options.initialText, "org.test.viewer")
            compare(Object.keys(prompts.options.choices).length, 3)
            prompts.accept("org.test.other")
            compare(backend.chosen.category, "pdf")
            compare(backend.chosen.appId, "org.test.other")
        }
        function test_no_default_clears_the_category() {
            wait(50)
            page.choose(backend.defaultCategories[0])
            prompts.accept("none")
            compare(backend.chosen.category, "pdf")
            compare(backend.chosen.appId, "")
        }
        function test_cancelling_changes_nothing() {
            wait(50)
            page.choose(backend.defaultCategories[0])
            prompts.cancel()
            compare(Object.keys(backend.chosen).length, 0)
        }
        function test_file_types_and_back_requests() {
            wait(50)
            mouseClick(findChild(page, "settingsDefaultFileTypes"))
            compare(fileTypes.count, 1)
            mouseClick(findChild(page, "settingsDefaultAppsBack"))
            compare(back.count, 1)
        }
        function test_a_category_without_handlers_is_not_offered() {
            wait(50)
            page.choose({id: "email", name: "Email", handlers: [], defaultId: "", defaultName: "", state: "unset"})
            verify(!prompts.busy)
        }
    }
}
