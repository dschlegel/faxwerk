#pragma once

#include <QByteArray>
#include <QString>

namespace faxwerk {

// spandsp draws the page header with a built-in bitmap font laid out like
// code page 437 (the IBM PC character set). Text passed to
// t30_set_tx_page_header_info() therefore has to be CP437, not Latin-1:
// in Latin-1, "ä" is 0xE4, which the font draws as "Σ".
//
// Characters CP437 cannot represent become '?'. Control characters become spaces.
QByteArray toFaxHeaderCharset(const QString &text);

} // namespace faxwerk
