#ifndef URLQUERYBUILDER_P_H
#define URLQUERYBUILDER_P_H

#include "proofnetwork/proofnetwork_global.h"

#include <QHash>
#include <QString>

namespace Proof {

class UrlQueryBuilder;
class PROOF_NETWORK_EXPORT UrlQueryBuilderPrivate
{
    Q_DECLARE_PUBLIC(UrlQueryBuilder)
public:
    virtual ~UrlQueryBuilderPrivate() {}

    UrlQueryBuilder *q_ptr;

    QHash<QString, QString> params;
};
}

#endif // URLQUERYBUILDER_P_H