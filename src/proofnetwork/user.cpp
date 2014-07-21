#include "user.h"
#include "user_p.h"

using namespace Proof;

User::User(const QString &userName)
    : User(userName, *new UserPrivate)
{
}

User::User(const QString &userName, Proof::UserPrivate &dd, QObject *parent)
    : NetworkDataEntity(dd, parent)
{
    Q_D(User);
    d->setUserName(userName);
}

QString User::userName() const
{
    Q_D(const User);
    return d->userName;
}

QString User::fullName() const
{
    Q_D(const User);
    return d->fullName;
}

QString User::email() const
{
    Q_D(const User);
    return d->email;
}

void User::updateFrom(const NetworkDataEntitySP &other)
{
    Q_D(User);
    UserSP castedOther = qSharedPointerCast<User>(other);
    d->setUserName(castedOther->userName());
    d->setFullName(castedOther->fullName());
    d->setEmail(castedOther->email());

    NetworkDataEntity::updateFrom(other);
}

UserQmlWrapper *User::toQmlWrapper(QObject *parent) const
{
    Q_D(const User);
    UserSP castedSelf = qSharedPointerCast<User>(d->weakSelf);
    Q_ASSERT(castedSelf);
    return new UserQmlWrapper(castedSelf, parent);
}

UserSP User::create(const QString &userName)
{
    UserSP result(new User(userName));
    result->d_func()->weakSelf = result.toWeakRef();
    return result;
}

void UserPrivate::setUserName(const QString &arg)
{
    Q_Q(User);
    if (userName != arg) {
        userName = arg;
        emit q->userNameChanged(arg);
    }
}

void UserPrivate::setFullName(const QString &arg)
{
    Q_Q(User);
    if (fullName != arg) {
        fullName = arg;
        emit q->fullNameChanged(arg);
    }
}

void UserPrivate::setEmail(const QString &arg)
{
    Q_Q(User);
    if (email != arg) {
        email = arg;
        emit q->emailChanged(arg);
    }
}