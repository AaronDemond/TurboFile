#pragma once
#ifndef SHELLSCRIPTDIALOG_H
#define SHELLSCRIPTDIALOG_H

#include <QDialog>
#include <QProcess>

class QCloseEvent;
class QPlainTextEdit;
class QPushButton;

// Presents a Bash editor and runs the entered script inside one filesystem
// directory without blocking TurboFile's UI thread.
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

protected:
    // Ask before terminating a running script when the window manager's close
    // button is used.
    void closeEvent(QCloseEvent *event) override;

private:
    // Validate the editor and directory, then start Bash asynchronously.
    void runScript();

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
    QPlainTextEdit *scriptEditor = nullptr;
    QPlainTextEdit *outputView = nullptr;
    QPushButton *runButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *closeButton = nullptr;
    QProcess *process = nullptr;
    bool failedToStart = false;
};

#endif // SHELLSCRIPTDIALOG_H