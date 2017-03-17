#ifndef PROOF_NETWORKSERVICES_PROOFSERVICERESTAPI_H
#define PROOF_NETWORKSERVICES_PROOFSERVICERESTAPI_H

#include "proofnetwork/abstractrestapi.h"
#include "proofnetwork/proofnetwork_global.h"

namespace Proof {
namespace NetworkServices {

enum class VersionedEntityType {
    Station,
    Service,
    Framework,
    Unknown
};

class ProofServiceRestApiPrivate;
class PROOF_NETWORK_EXPORT ProofServiceRestApi : public AbstractRestApi
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(ProofServiceRestApi)
signals:
    void versionFetched(Proof::NetworkServices::VersionedEntityType type, const QString &name, const QString &version);

protected:
    ProofServiceRestApi(const RestClientSP &restClient, ProofServiceRestApiPrivate &dd, QObject *parent = nullptr);
};

} // namespace NetworkServices
} // namespace Proof

Q_DECLARE_METATYPE(Proof::NetworkServices::VersionedEntityType)
#endif // PROOF_NETWORKSERVICES_PROOFSERVICERESTAPI_H
