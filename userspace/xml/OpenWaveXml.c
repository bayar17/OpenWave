#include "OpenWaveXml.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

#ifndef OPENWAVE_DBUS_INTERFACE_DIR
#define OPENWAVE_DBUS_INTERFACE_DIR "/usr/share/dbus-1/interfaces"
#endif

#define MAX_XML_BYTES (1024 * 1024)

char *openwave_xml_load_file(const char *filename, GError **error)
{
    if (!filename || !*filename) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "An XML filename is required");
        return NULL;
    }
    GFile *file = g_file_new_for_path(filename);
    GFileInputStream *stream = g_file_read(file, NULL, error);
    g_object_unref(file);
    if (!stream)
        return NULL;
    char *contents = g_malloc(MAX_XML_BYTES + 1);
    gsize length = 0;
    gboolean read = g_input_stream_read_all(G_INPUT_STREAM(stream), contents,
        MAX_XML_BYTES + 1, &length, NULL, error);
    g_object_unref(stream);
    if (!read) {
        g_free(contents);
        return NULL;
    }
    if (length > MAX_XML_BYTES) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "XML file '%s' exceeds the 1 MiB limit", filename);
        g_free(contents);
        return NULL;
    }
    xmlParserCtxtPtr context = xmlNewParserCtxt();
    if (!context) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Cannot create an XML parser");
        g_free(contents);
        return NULL;
    }
    xmlDocPtr document = xmlCtxtReadMemory(context, contents, (int) length,
        filename, NULL, XML_PARSE_NONET |
        XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    g_free(contents);
    if (!document) {
        const xmlError *parse_error = xmlCtxtGetLastError(context);
        char *message = g_strdup(parse_error && parse_error->message
            ? parse_error->message : "Invalid XML document");
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                    "Cannot parse XML file '%s': %s", filename, g_strstrip(message));
        g_free(message);
        xmlFreeParserCtxt(context);
        return NULL;
    }
    xmlFreeParserCtxt(context);
    if (document->intSubset || document->extSubset) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "DTD declarations are not supported in XML file '%s'", filename);
        xmlFreeDoc(document);
        return NULL;
    }
    xmlChar *serialized = NULL;
    int size = 0;
    xmlDocDumpMemoryEnc(document, &serialized, &size, "UTF-8");
    xmlFreeDoc(document);
    if (!serialized) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Cannot serialize XML file '%s'", filename);
        return NULL;
    }
    char *result = g_strndup((const char *) serialized, size);
    xmlFree(serialized);
    return result;
}

GDBusNodeInfo *openwave_xml_load_dbus_file(const char *filename,
                                         const char *interface_name,
                                         GError **error)
{
    if (!interface_name || !g_dbus_is_interface_name(interface_name)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "A valid D-Bus interface name is required");
        return NULL;
    }
    char *contents = openwave_xml_load_file(filename, error);
    if (!contents)
        return NULL;
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(contents, error);
    g_free(contents);
    if (!node)
        return NULL;
    if (!node->interfaces || !node->interfaces[0] || node->interfaces[1] ||
        g_strcmp0(node->interfaces[0]->name, interface_name) != 0) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "XML file '%s' must declare only interface '%s'",
                    filename, interface_name);
        g_dbus_node_info_unref(node);
        return NULL;
    }
    return node;
}

GDBusNodeInfo *openwave_xml_load_dbus_interface(const char *interface_name,
                                              GError **error)
{
    if (!interface_name || !g_dbus_is_interface_name(interface_name)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "A valid D-Bus interface name is required");
        return NULL;
    }
    char *basename = g_strconcat(interface_name, ".xml", NULL);
    char *filename = g_build_filename(OPENWAVE_DBUS_INTERFACE_DIR, basename, NULL);
    g_free(basename);
    GDBusNodeInfo *node = openwave_xml_load_dbus_file(filename, interface_name, error);
    g_free(filename);
    return node;
}
