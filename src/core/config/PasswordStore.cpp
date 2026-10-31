#include "config/PasswordStore.h"

#include "ProductInfo.h"

#ifdef FAXWERK_HAVE_KEYCHAIN
#include <QEventLoop>

#include <qt6keychain/keychain.h>
#endif

namespace faxwerk {

QString PasswordStore::sipKey(const QString &locationId)
{
    return locationId + QStringLiteral("/sip");
}

QString PasswordStore::phonebookKey(const QString &locationId)
{
    return locationId + QStringLiteral("/phonebook");
}

#ifdef FAXWERK_HAVE_KEYCHAIN

namespace {

// Starts the job and waits for it.
PasswordStore::Result run(QKeychain::Job &job, QString *error)
{
    bool done = false;
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, [&done, &loop] {
        done = true;
        loop.quit();
    });
    job.setAutoDelete(false);
    job.start();
    if (!done)
        loop.exec(QEventLoop::ExcludeUserInputEvents);

    if (error)
        *error = job.error() == QKeychain::NoError ? QString() : job.errorString();
    switch (job.error()) {
    case QKeychain::NoError: return PasswordStore::Result::Ok;
    case QKeychain::EntryNotFound: return PasswordStore::Result::NotFound;
    case QKeychain::NoBackendAvailable:
    case QKeychain::NotImplemented: return PasswordStore::Result::Unavailable;
    default: return PasswordStore::Result::Failed;
    }
}

class KeychainPasswordStore : public PasswordStore
{
public:
    Result read(const QString &key, QString *password, QString *error) override
    {
        QKeychain::ReadPasswordJob job(service());
        job.setKey(key);
        const Result result = run(job, error);
        if (password)
            *password = result == Result::Ok ? job.textData() : QString();
        return result;
    }

    Result write(const QString &key, const QString &password, QString *error) override
    {
        QKeychain::WritePasswordJob job(service());
        job.setKey(key);
        job.setTextData(password);
        return run(job, error);
    }

    Result remove(const QString &key, QString *error) override
    {
        QKeychain::DeletePasswordJob job(service());
        job.setKey(key);
        return run(job, error);
    }

private:
    static QString service() { return QStringLiteral(FAXWERK_PRODUCT_NAME); }
};

} // namespace

std::unique_ptr<PasswordStore> PasswordStore::createSystem()
{
    return std::make_unique<KeychainPasswordStore>();
}

#else

std::unique_ptr<PasswordStore> PasswordStore::createSystem()
{
    return nullptr;
}

#endif

} // namespace faxwerk
