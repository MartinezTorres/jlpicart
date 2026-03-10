// receipts.cc — Install receipt logging.

#include "content/receipts.h"
#include <cstring>

void append_install_receipt(AppendLog& log, const InstallReceiptData& rec)
{
    // Ignore the return value: spec §11.1 states receipts MUST NOT be
    // required to boot, so a log-full or IO failure is silently swallowed.
    log.append(ALOG_TYPE_INSTALL,
               reinterpret_cast<const uint8_t*>(&rec),
               static_cast<uint16_t>(sizeof(rec)));
}
