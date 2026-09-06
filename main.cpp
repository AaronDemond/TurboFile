#include "mainwindow.h"

#include <QApplication>

// Application entry point. Qt passes command-line arguments to QApplication so it
// can initialize the platform integration and manage the event loop.
int main(int argc, char *argv[])
{
    // QApplication must exist before creating any widgets.
    QApplication a(argc, argv);

    // Construct and display the application's main file-browser window.
    MainWindow w;
    w.show();

    // Run Qt's event loop until the user closes the application.
    return a.exec();
}
