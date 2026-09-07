#include "shellscriptdialog.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

namespace
{

// Applies lightweight Bash-oriented coloring to each editor line. This is a
// highlighter rather than a shell parser: Bash still remains the authority on
// whether the complete script is syntactically valid when Run is clicked.
class BashSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    explicit BashSyntaxHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        // Control-flow words define the structure of shell programs, so they
        // receive the strongest color and bold weight.
        QTextCharFormat keywordFormat;
        keywordFormat.setForeground(QColor("#c678dd"));
        keywordFormat.setFontWeight(QFont::Bold);
        addRule(
            R"(\b(if|then|elif|else|fi|for|while|until|do|done|case|esac|function|select|in|time)\b)",
            keywordFormat
        );

        // Common built-ins are highlighted separately from flow keywords so
        // commands such as cd, read, and export are easy to identify.
        QTextCharFormat builtinFormat;
        builtinFormat.setForeground(QColor("#e5c07b"));
        addRule(
            R"(\b(alias|cd|declare|echo|eval|exec|exit|export|false|local|printf|read|readonly|return|set|shift|source|test|trap|true|type|unalias|unset)\b)",
            builtinFormat
        );

        // Shell variable references include named, braced, positional, and
        // special parameters such as $?, $$, and $@.
        QTextCharFormat variableFormat;
        variableFormat.setForeground(QColor("#61afef"));
        addRule(
            R"(\$\{[^}]*\}|\$[A-Za-z_][A-Za-z0-9_]*|\$[0-9@#?$!*-])",
            variableFormat
        );

        QTextCharFormat numberFormat;
        numberFormat.setForeground(QColor("#d19a66"));
        addRule(R"(\b[0-9]+\b)", numberFormat);

        // Single and double quoted strings use separate expressions because
        // double quoted strings may contain escaped quote characters.
        QTextCharFormat stringFormat;
        stringFormat.setForeground(QColor("#98c379"));
        addRule(R"('[^']*')", stringFormat);
        addRule(R"("([^"\\]|\\.)*")", stringFormat);

        QTextCharFormat operatorFormat;
        operatorFormat.setForeground(QColor("#56b6c2"));
        addRule(R"(&&|\|\||;;|[|&;<>])", operatorFormat);

        // Comments are applied last so text following an unquoted # is shown
        // as commentary rather than receiving keyword or variable colors.
        QTextCharFormat commentFormat;
        commentFormat.setForeground(QColor("#7f848e"));
        commentFormat.setFontItalic(true);
        addRule(R"((^|\s)#[^\n]*$)", commentFormat);

        // A shebang is technically a comment but carries execution metadata,
        // so give the first-line interpreter declaration its own color.
        QTextCharFormat shebangFormat;
        shebangFormat.setForeground(QColor("#abb2bf"));
        shebangFormat.setFontWeight(QFont::Bold);
        addRule(R"(^#!.*$)", shebangFormat);
    }

protected:
    void highlightBlock(const QString &text) override
    {
        // Each regular expression may occur multiple times in one line. The
        // match iterator supplies exact ranges for QSyntaxHighlighter.
        for (const HighlightRule &rule : rules)
        {
            QRegularExpressionMatchIterator matches =
                rule.pattern.globalMatch(text);

            while (matches.hasNext())
            {
                QRegularExpressionMatch match = matches.next();
                setFormat(
                    match.capturedStart(),
                    match.capturedLength(),
                    rule.format
                );
            }
        }
    }

private:
    struct HighlightRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    // Store compiled expressions once so repainting the editor does not need
    // to reconstruct regular expressions for every visible line.
    void addRule(
        const QString &pattern,
        const QTextCharFormat &format
    )
    {
        rules.append(
            {
                QRegularExpression(pattern),
                format
            }
        );
    }

    QList<HighlightRule> rules;
};

} // namespace

ShellScriptDialog::ShellScriptDialog(
    const QString &workingDirectory,
    QWidget *parent
)
    : QDialog(parent)
    , workingDirectory(QDir::cleanPath(workingDirectory))
    , process(new QProcess(this))
{
    setWindowTitle("Run Shell Script Here");
    resize(860, 680);
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    // The path label confirms the exact process working directory before the
    // user runs commands that may create, modify, or delete files.
    auto *directoryLabel =
        new QLabel(
            QString("Working directory: %1")
                .arg(this->workingDirectory),
            this
        );
    directoryLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse
    );
    layout->addWidget(directoryLabel);

    scriptEditor = new QPlainTextEdit(this);
    scriptEditor->setPlaceholderText("#!/usr/bin/env bash\n");
    scriptEditor->setFont(
        QFontDatabase::systemFont(
            QFontDatabase::FixedFont
        )
    );
    scriptEditor->setTabStopDistance(
        scriptEditor->fontMetrics()
            .horizontalAdvance(' ') * 4
    );
    layout->addWidget(scriptEditor, 3);

    // The highlighter is parented by the editor's QTextDocument and therefore
    // follows the document lifetime without a separate member pointer.
    new BashSyntaxHighlighter(scriptEditor->document());

    auto *outputLabel = new QLabel("Output", this);
    layout->addWidget(outputLabel);

    outputView = new QPlainTextEdit(this);
    outputView->setReadOnly(true);
    outputView->setFont(
        QFontDatabase::systemFont(
            QFontDatabase::FixedFont
        )
    );
    outputView->setMaximumBlockCount(10000);
    layout->addWidget(outputView, 2);

    // Standard dialog buttons retain native keyboard and platform behavior.
    // Stop begins disabled because no child process exists initially.
    auto *buttonBox = new QDialogButtonBox(this);
    runButton = buttonBox->addButton(
        "Run",
        QDialogButtonBox::AcceptRole
    );
    stopButton = buttonBox->addButton(
        "Stop",
        QDialogButtonBox::DestructiveRole
    );
    closeButton = buttonBox->addButton(
        QDialogButtonBox::Close
    );
    stopButton->setEnabled(false);
    layout->addWidget(buttonBox);

    connect(
        runButton,
        &QPushButton::clicked,
        this,
        &ShellScriptDialog::runScript
    );
    connect(
        stopButton,
        &QPushButton::clicked,
        this,
        &ShellScriptDialog::stopScript
    );
    connect(
        closeButton,
        &QPushButton::clicked,
        this,
        &ShellScriptDialog::reject
    );

    // Keep stdout and stderr separate so errors can be colored and retained
    // even when the process emits both streams concurrently.
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(
        process,
        &QProcess::readyReadStandardOutput,
        this,
        [this]()
        {
            appendOutput(
                QString::fromLocal8Bit(
                    process->readAllStandardOutput()
                ),
                false
            );
        }
    );

    connect(
        process,
        &QProcess::readyReadStandardError,
        this,
        [this]()
        {
            appendOutput(
                QString::fromLocal8Bit(
                    process->readAllStandardError()
                ),
                true
            );
        }
    );

    connect(
        process,
        &QProcess::finished,
        this,
        &ShellScriptDialog::handleFinished
    );

    connect(
        process,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error)
        {
            if (error != QProcess::FailedToStart)
            {
                return;
            }

            failedToStart = true;
            setRunning(false);
            appendOutput(
                process->errorString() + '\n',
                true
            );
            QMessageBox::critical(
                this,
                "Script Failed",
                process->errorString()
            );
        }
    );
}

void ShellScriptDialog::reject()
{
    if (!confirmStopBeforeClose())
    {
        return;
    }

    QDialog::reject();
}

void ShellScriptDialog::closeEvent(QCloseEvent *event)
{
    if (!confirmStopBeforeClose())
    {
        event->ignore();
        return;
    }

    event->accept();
}

void ShellScriptDialog::runScript()
{
    if (process->state() != QProcess::NotRunning)
    {
        return;
    }

    QString script = scriptEditor->toPlainText();

    if (script.trimmed().isEmpty())
    {
        QMessageBox::warning(
            this,
            "Empty Script",
            "Enter a Bash script before running it."
        );
        return;
    }

    if (!QDir(workingDirectory).exists())
    {
        QMessageBox::critical(
            this,
            "Script Failed",
            "The working directory no longer exists."
        );
        return;
    }

    QString bashExecutable =
        QStandardPaths::findExecutable("bash");

    if (bashExecutable.isEmpty())
    {
        QMessageBox::critical(
            this,
            "Script Failed",
            "Bash is not installed or could not be found."
        );
        return;
    }

    outputView->clear();
    failedToStart = false;
    setRunning(true);

    // Passing the complete editor text as one -c argument avoids temporary
    // executable files and shell-escaping problems. QProcess supplies the
    // working directory directly, and closing stdin prevents scripts that
    // read input from waiting forever in this non-interactive runner.
    process->setWorkingDirectory(workingDirectory);
    process->start(
        bashExecutable,
        {
            "-c",
            script
        }
    );
    process->closeWriteChannel();
}

void ShellScriptDialog::stopScript()
{
    if (process->state() == QProcess::NotRunning)
    {
        return;
    }

    appendOutput("\n[Stopping script...]\n", true);
    process->terminate();

    // terminate() gives Bash and its traps a brief opportunity to clean up.
    // kill() guarantees the dialog does not remain locked indefinitely.
    if (!process->waitForFinished(1500))
    {
        process->kill();
        process->waitForFinished(1500);
    }
}

void ShellScriptDialog::appendOutput(
    const QString &text,
    bool isError
)
{
    if (text.isEmpty())
    {
        return;
    }

    QTextCursor cursor = outputView->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat format;
    format.setForeground(
        isError
            ? QColor("#e06c75")
            : outputView->palette().text().color()
    );

    cursor.insertText(text, format);
    outputView->setTextCursor(cursor);
    outputView->ensureCursorVisible();
}

void ShellScriptDialog::setRunning(bool running)
{
    scriptEditor->setReadOnly(running);
    runButton->setEnabled(!running);
    stopButton->setEnabled(running);
    closeButton->setEnabled(!running);
}

void ShellScriptDialog::handleFinished(
    int exitCode,
    QProcess::ExitStatus exitStatus
)
{
    // Signals normally drain both streams first, but explicitly read any
    // final buffered bytes before deciding what feedback to show.
    appendOutput(
        QString::fromLocal8Bit(
            process->readAllStandardOutput()
        ),
        false
    );
    appendOutput(
        QString::fromLocal8Bit(
            process->readAllStandardError()
        ),
        true
    );

    setRunning(false);

    // Emit for success, nonzero exits, and crashes because any script that
    // actually started may have changed files before it finished.
    emit scriptFinished();

    // FailedToStart is reported by errorOccurred and may not represent a real
    // Bash exit. Avoid showing a second dialog for the same startup error.
    if (failedToStart)
    {
        return;
    }

    if (
        exitStatus == QProcess::NormalExit &&
        exitCode == 0
    )
    {
        QMessageBox::information(
            this,
            "Script Complete",
            "The shell script completed successfully."
        );
        return;
    }

    QString failureMessage =
        exitStatus == QProcess::CrashExit
            ? "The shell script was stopped or crashed."
            : QString(
                "The shell script exited with code %1.\n\n"
                "See the output panel for details."
              ).arg(exitCode);

    QMessageBox::critical(
        this,
        "Script Failed",
        failureMessage
    );
}

bool ShellScriptDialog::confirmStopBeforeClose()
{
    if (process->state() == QProcess::NotRunning)
    {
        return true;
    }

    QMessageBox::StandardButton answer =
        QMessageBox::question(
            this,
            "Script Running",
            "Stop the running script and close this window?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );

    if (answer != QMessageBox::Yes)
    {
        return false;
    }

    stopScript();
    return true;
}