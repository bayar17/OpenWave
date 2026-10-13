#include <OpenWave/OpenWaveXml.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static const char *fixtures;
static const char *interfaces;

static void test_contracts(void)
{
    const char *names[] = {
        "org.openwave.Connectivity1", "org.bluez.Agent1", "org.openwave.Time1",
        "org.openwave.Audio1", "org.openwave.Wallpaper1", "org.openwave.Prompt1",
        "org.openwave.portal.FileChooser", NULL
    };
    for (guint i = 0; names[i]; ++i) {
        char *basename = g_strconcat(names[i], ".xml", NULL);
        char *filename = g_build_filename(interfaces, basename, NULL);
        GError *error = NULL;
        GDBusNodeInfo *node = openwave_xml_load_dbus_file(filename, names[i], &error);
        g_assert_no_error(error);
        g_assert_nonnull(node);
        g_assert_cmpstr(node->interfaces[0]->name, ==, names[i]);
        g_dbus_node_info_unref(node);
        g_free(filename);
        g_free(basename);
    }
}

static void test_utf8(void)
{
    char *filename = g_build_filename(fixtures, "unicode.xml", NULL);
    GError *error = NULL;
    char *xml = openwave_xml_load_file(filename, &error);
    g_assert_no_error(error);
    g_assert_nonnull(xml);
    g_assert_true(g_utf8_validate(xml, -1, NULL));
    g_assert_nonnull(strstr(xml, "OpenWave &amp; İstanbul"));
    g_assert_nonnull(strstr(xml, "<first/>  <second/>"));
    g_free(xml);
    g_free(filename);
}

static void test_parse_errors(void)
{
    const char *names[] = {"malformed.xml", "external-entity.xml", NULL};
    for (guint i = 0; names[i]; ++i) {
        char *filename = g_build_filename(fixtures, names[i], NULL);
        GError *error = NULL;
        char *xml = openwave_xml_load_file(filename, &error);
        g_assert_null(xml);
        int code = i == 0 ? G_MARKUP_ERROR_PARSE : G_MARKUP_ERROR_INVALID_CONTENT;
        g_assert_error(error, G_MARKUP_ERROR, code);
        g_clear_error(&error);
        g_free(filename);
    }
}

static void test_missing(void)
{
    char *filename = g_build_filename(fixtures, "missing.xml", NULL);
    GError *error = NULL;
    g_assert_null(openwave_xml_load_file(filename, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&error);
    g_free(filename);
}

static void test_interface_identity(void)
{
    char *filename = g_build_filename(interfaces, "org.openwave.Time1.xml", NULL);
    GError *error = NULL;
    g_assert_null(openwave_xml_load_dbus_file(filename, "org.openwave.Audio1", &error));
    g_assert_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT);
    g_clear_error(&error);
    g_assert_null(openwave_xml_load_dbus_interface("../invalid", &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error(&error);
    g_free(filename);
}

static void test_size_limit(void)
{
    char *filename = NULL;
    GError *error = NULL;
    int fd = g_file_open_tmp("openwave-xml-limit-XXXXXX", &filename, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    gsize length = 2 * 1024 * 1024;
    char *contents = g_malloc0(length);
    g_assert_true(g_file_set_contents(filename, contents, length, &error));
    g_assert_no_error(error);
    g_free(contents);
    g_assert_null(openwave_xml_load_file(filename, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_unlink(filename);
    g_free(filename);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (argc != 3)
        return 2;
    fixtures = argv[1];
    interfaces = argv[2];
    g_test_add_func("/xml/contracts", test_contracts);
    g_test_add_func("/xml/utf8", test_utf8);
    g_test_add_func("/xml/parse-errors", test_parse_errors);
    g_test_add_func("/xml/missing", test_missing);
    g_test_add_func("/xml/interface-identity", test_interface_identity);
    g_test_add_func("/xml/size-limit", test_size_limit);
    return g_test_run();
}
