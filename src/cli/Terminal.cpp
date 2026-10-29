#include "Terminal.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace faxwerk::cli {

namespace {

std::atomic<bool> g_interrupted{false};

void onSignal(int)
{
    g_interrupted.store(true);
}

} // namespace

bool stdinIsTerminal()
{
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

QString readPassword(const QString &prompt)
{
    std::cerr << prompt.toStdString() << std::flush;
    std::string line;
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool haveMode = GetConsoleMode(input, &mode) != 0;
    if (haveMode)
        SetConsoleMode(input, mode & ~DWORD(ENABLE_ECHO_INPUT));
    std::getline(std::cin, line);
    if (haveMode)
        SetConsoleMode(input, mode);
#else
    termios old {};
    const bool haveTermios = tcgetattr(STDIN_FILENO, &old) == 0;
    if (haveTermios) {
        termios noEcho = old;
        noEcho.c_lflag &= ~tcflag_t(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &noEcho);
    }
    std::getline(std::cin, line);
    if (haveTermios)
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
#endif
    std::cerr << std::endl;
    return QString::fromStdString(line);
}

void installInterruptHandler()
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
}

bool interruptRequested()
{
    return g_interrupted.load();
}

} // namespace faxwerk::cli
