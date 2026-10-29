#pragma once

#include <QString>

namespace faxwerk::cli {

// True if stdin is an interactive terminal.
bool stdinIsTerminal();

// Prompts on stderr and reads a line from stdin without echo.
QString readPassword(const QString &prompt);

// Installs a SIGINT/SIGTERM (Ctrl+C) handler that only sets a flag.
void installInterruptHandler();
bool interruptRequested();

} // namespace faxwerk::cli
