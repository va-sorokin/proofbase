#ifndef USER_H
#define USER_H

#include "proofnetwork/networkdataentity.h"
#include "proofnetwork/proofnetwork_global.h"
#include "proofnetwork/proofnetwork_types.h"
#include "proofnetwork/qmlwrappers/userqmlwrapper.h"

namespace Proof {
class UserPrivate;
class PROOF_NETWORK_EXPORT User : public NetworkDataEntity // clazy:exclude=ctor-missing-parent-argument
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(User)
public:
    QString userName() const;
    QString fullName() const;
    QString email() const;

    UserQmlWrapper *toQmlWrapper(QObject *parent = nullptr) const override;

    static UserSP create(const QString &userName);
    static UserSP defaultObject();

signals:
    void userNameChanged(const QString &arg);
    void fullNameChanged(const QString &arg);
    void emailChanged(const QString &arg);

protected:
    explicit User(const QString &userName);
    User(UserPrivate &dd);
    void updateSelf(const Proof::NetworkDataEntitySP &other) override;
};
} // namespace Proof

#endif // USER_H
