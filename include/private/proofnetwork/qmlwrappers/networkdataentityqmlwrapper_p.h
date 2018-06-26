#ifndef NETWORKDATAENTITYQMLWRAPPER_P_H
#define NETWORKDATAENTITYQMLWRAPPER_P_H

#include "proofcore/proofobject_p.h"

#include "proofnetwork/networkdataentity.h"
#include "proofnetwork/proofnetwork_global.h"
#include "proofnetwork/qmlwrappers/networkdataentityqmlwrapper.h"

#include <QSharedPointer>
#include <QtGlobal>

namespace Proof {
class PROOF_NETWORK_EXPORT NetworkDataEntityQmlWrapperPrivate : public ProofObjectPrivate
{
    Q_DECLARE_PUBLIC(NetworkDataEntityQmlWrapper)
public:
    NetworkDataEntityQmlWrapperPrivate() : ProofObjectPrivate() {}
    QSharedPointer<NetworkDataEntity> dataEntity;
    QObject *entityConnectContext = nullptr;
};

} // namespace Proof

#endif // NETWORKDATAENTITYQMLWRAPPER_P_H
