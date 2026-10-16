#include <OpenWave/ApplicationsClient.h>
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() < 2) { std::fprintf(stderr, "Usage: openwave-launch APPLICATION_ID [PATH ...]\n"); return 2; }
    ApplicationsClient client;
    client.launch(arguments.at(1), arguments.mid(2), [&](const QVariantList &, const QString &error) {
        if (!error.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(error));
        app.exit(error.isEmpty() ? 0 : 1);
    });
    QTimer::singleShot(15000, &app, [&] { app.exit(1); });
    return app.exec();
}
