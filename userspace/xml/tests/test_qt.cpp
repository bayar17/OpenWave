#include <QObject>
#include <OpenWave/OpenWaveXml.h>

class XmlConsumer : public QObject
{
signals:
    void parsed();
};

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    GError *error = nullptr;
    char *xml = openwave_xml_load_file(argv[1], &error);
    if (!xml) {
        g_clear_error(&error);
        return 1;
    }
    g_free(xml);
    return 0;
}
