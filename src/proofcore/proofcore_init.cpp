#include "proofcore_global.h"
#include "loghandler.h"

Q_LOGGING_CATEGORY(proofLog, "proof")
Q_LOGGING_CATEGORY(proofCoreLog, "proof.core")

__attribute__((constructor))
static void libraryInit()
{
    Proof::LogHandler::setup();
}
