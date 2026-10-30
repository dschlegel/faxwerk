#pragma once

#include "Journal.h"

namespace faxwerk::gui {

// Writes a one-page PDF transmission report for a journal entry.
bool writeSendReport(const JournalEntry &entry, const QString &path, QString *error = nullptr);

} // namespace faxwerk::gui
