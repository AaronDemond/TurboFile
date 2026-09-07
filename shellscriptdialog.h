#pragma once
#ifndef SHELLSCRIPTDIALOG_H
#define SHELLSCRIPTDIALOG_H

#include <QDialog>
#include <QProcess>

class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// Presents a modeless Bash editor and runs the entered script inside one
// filesystem directory without blocking or disabling the explorer window.
class ShellScriptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShellScriptDialog(
        const QString &workingDirectory,
        QWidget *parent = nullptr
    );

    // Treat Escape and the dialog's Close button like a window close so a
    // running child process cannot be abandoned accidentally.
    void reject() override;

signals:
    // A script may mutate any descendant of its working directory. The file
    // model listens for completion and invalidates affected cached totals.
    void scriptFinished();

    // Saving creates or updates a normal filesystem item, so the explorer can
    // invalidate the exact path without coupling this editor to its model.
    void scriptSaved(const QString &filePath);

protected:
    // Ask before terminating a running script when the window manager's close
    // button is used.
    void closeEvent(QCloseEvent *event) override;

private:
    // Validate the editor and directory, then start Bash asynchronously.
    void runScript();

    // Let the user choose a destination and atomically save the current script
    // without inserting confirmation messages into the editing or run flow.
    void saveScript();

    // Request graceful termination first; QProcess escalates only if the
    // process does not stop within the short timeout.
    void stopScript();

    // Append one process stream while retaining its stdout/stderr color.
    void appendOutput(
        const QString &text,
        bool isError
    );

    // Restore editor controls after every normal or abnormal process exit.
    void setRunning(bool running);

    // Report the final exit state after all remaining process output is read.
    void handleFinished(
        int exitCode,
        QProcess::ExitStatus exitStatus
    );

    // Share the running-process close confirmation between reject() and the
    // window-system close event.
    bool confirmStopBeforeClose();

    QString workingDirectory;
    QString savedFilePath;
    QPlainTextEdit *scriptEditor = nullptr;
    QPlainTextEdit *outputView = nullptr;
    QLabel *saveStatusLabel = nullptr;
    QPushButton *runButton = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *closeButton = nullptr;
    QProcess *process = nullptr;
    bool failedToStart = false;
    bool stopRequested = false;
    bool closeWhenFinished = false;
};

#endif // SHELLSCRIPTDIALOG_H