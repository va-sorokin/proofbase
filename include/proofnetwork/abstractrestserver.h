#ifndef ABSTRACTRESTSERVER_H
#define ABSTRACTRESTSERVER_H

#include "proofnetwork/proofnetwork_global.h"

#include <QTcpServer>
#include <QScopedPointer>

namespace Proof {

class AbstractRestServerPrivate;

class PROOF_NETWORK_EXPORT AbstractRestServer : public QTcpServer
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(AbstractRestServer)
public:
    explicit AbstractRestServer(QObject *parent = 0);
    AbstractRestServer(const QString &userName, const QString &password,
                                const QString &pathPrefix = QString(), int port = 80, QObject *parent = 0);
    ~AbstractRestServer();

    QString userName() const;
    QString password() const;
    QString pathPrefix() const;
    int port() const;

    void setUserName(const QString &userName);
    void setPassword(const QString &password);
    void setPathPrefix(const QString &pathPrefix);
    void setPort(int port);
    void setNumberOfConnectionThreads(int count = -1);

    Q_INVOKABLE void startListen();
    Q_INVOKABLE void stopListen();

signals:
    void userNameChanged(const QString &arg);
    void passwordChanged(const QString &arg);
    void pathPrefixChanged(const QString &arg);
    void portChanged(int arg);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

    void sendAnswer(QTcpSocket *socket, const QByteArray &body, const QString &contentType, int returnCode = 200, const QString &reason = QString());
    void sendNotFound(QTcpSocket *socket, const QString &reason = "Not Found");
    void sendNotAuthorized(QTcpSocket *socket, const QString &reason = "Unauthorized");
    void sendInternalError(QTcpSocket *socket);
    bool checkBasicAuth(const QString &encryptedAuth) const;
    QString parseAuth(QTcpSocket *socket, const QString &header);


    AbstractRestServer(AbstractRestServerPrivate &dd, const QString &userName, const QString &password, const QString &pathPrefix, int port, QObject *parent = 0);
    QScopedPointer<AbstractRestServerPrivate> d_ptr;
};

}
#endif // ABSTRACTRESTSERVER_H
