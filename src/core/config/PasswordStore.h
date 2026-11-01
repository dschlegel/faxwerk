#pragma once

#include <QString>

#include <memory>

namespace faxwerk {

// Where the desktop app and the command line tool keep passwords, so that
// they don't end up readable in the INI file.
//
// The system implementation uses QtKeychain: the Windows Credential Manager,
// the macOS Keychain, or the Secret Service (GNOME Keyring, KeePassXC) or
// KWallet on Linux. The calls block until the keychain answers; they run a
// local event loop meanwhile, without user input, so a QCoreApplication must
// exist.
class PasswordStore
{
public:
    enum class Result {
        Ok,
        NotFound,    // no entry under this key (read, remove)
        Unavailable, // no keychain on this system
        Failed,      // the keychain refused (locked, access denied, ...)
    };

    virtual ~PasswordStore() = default;

    virtual Result read(const QString &key, QString *password, QString *error = nullptr) = 0;
    virtual Result write(const QString &key, const QString &password, QString *error = nullptr) = 0;
    virtual Result remove(const QString &key, QString *error = nullptr) = 0;

    // The keychain of the operating system, or nullptr when Faxwerk was built
    // without QtKeychain.
    static std::unique_ptr<PasswordStore> createSystem();

    // Keys of the passwords of a location (Location::id in the desktop app).
    static QString sipKey(const QString &locationId);
    static QString phonebookKey(const QString &locationId);
};

} // namespace faxwerk
