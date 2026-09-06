#pragma once
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QString>

#include <QMainWindow>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QHash>


class QFileSystemModel;
class QWidget;
class QTreeView;
class QLineEdit;
class QPushButton;
class QFileSystemModel;
class QWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    // this will store state for back and forward button
    struct TabState {
        QTreeView *fileTreeView;
        QLineEdit *pathLineEdit;

        QPushButton *backButton;
        QPushButton *forwardButton;

        QStringList history;
        int historyIndex = -1;
    };
    Ui::MainWindow *ui;
    QFileSystemModel *fileModel;

    QHash<QWidget *, TabState> tabStates;

    void createTab(const QString &path);
    void updateTabTitle(QWidget *page, const QString &path);
    void navigateTo(QWidget *page, const QString &path, bool addToHistory = true);

    void updateNavigationButtons(QWidget *page);
};

#endif // MAINWINDOW_H


