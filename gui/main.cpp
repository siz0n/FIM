#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("file_integrity_monitor"));
    QCoreApplication::setApplicationName(QStringLiteral("file_integrity_monitor"));
    QApplication::setQuitOnLastWindowClosed(false);

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(37, 37, 38));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(30, 30, 30));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(140, 140, 140));
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(140, 140, 140));
    darkPalette.setColor(QPalette::Highlight, QColor(0, 122, 204));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);

    app.setPalette(darkPalette);
    app.setStyleSheet(QStringLiteral(
        "QToolTip {"
        "background-color: rgb(45,45,48);"
        "color: white;"
        "border: 1px solid rgb(70,70,70);"
        "padding: 6px;"
        "}"
    ));

    MainWindow window;
    window.show();
    return app.exec();
}
