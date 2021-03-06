/* Copyright 2018, OpenSoft Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *     * Neither the name of OpenSoft Inc. nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "proofnetwork/amqppublisher.h"

#include "proofnetwork/abstractamqpclient_p.h"

#include "3rdparty/qamqp/src/qamqpclient.h"
#include "3rdparty/qamqp/src/qamqpexchange.h"
#include "3rdparty/qamqp/src/qamqpmessage.h"

namespace Proof {

class AmqpPublisherPrivate : public AbstractAmqpClientPrivate
{
    Q_DECLARE_PUBLIC(AmqpPublisher)

    enum class ExchangeState
    {
        Opening,
        Error,
        Reopening,
        Declared
    };

    void connected() override;

    QAmqpExchange *exchange = nullptr;
    QString exchangeName;
    std::atomic_ullong nextPublishId{1};

    ExchangeState exchangeState = ExchangeState::Error;

    bool createdExchangeIfNotExists = false;
    QAmqpExchange::ExchangeType exchangeType = QAmqpExchange::Topic;
    QAmqpExchange::ExchangeOptions exchangeOptions = {QAmqpExchange::Durable};
};

} // namespace Proof

using namespace Proof;

AmqpPublisher::AmqpPublisher(QObject *parent) : AbstractAmqpClient(*new AmqpPublisherPrivate, parent)
{}

QString AmqpPublisher::exchangeName() const
{
    Q_D_CONST(AmqpPublisher);
    return d->exchangeName;
}

void AmqpPublisher::setExchangeName(const QString &exchangeName)
{
    Q_D(AmqpPublisher);
    d->exchangeName = exchangeName;
}

qulonglong AmqpPublisher::publishMessage(const QString &message, const QString &routingKey)
{
    Q_D(AmqpPublisher);
    qulonglong result = d->nextPublishId++;
    publishMessageImpl(message, routingKey, result);
    return result;
}

QAmqpExchange::ExchangeOptions AmqpPublisher::exchangeOptions() const
{
    Q_D_CONST(AmqpPublisher);
    return d->exchangeOptions;
}

bool AmqpPublisher::createExchangeIfNotExists() const
{
    Q_D_CONST(AmqpPublisher);
    return d->createdExchangeIfNotExists;
}

void AmqpPublisher::setCreateExchangeIfNotExists(bool createExchangeIfNotExists, QAmqpExchange::ExchangeType type,
                                                 QAmqpExchange::ExchangeOptions options)
{
    Q_D(AmqpPublisher);
    d->createdExchangeIfNotExists = createExchangeIfNotExists;
    d->exchangeType = type;
    d->exchangeOptions = options;
}

void AmqpPublisher::publishMessageImpl(const QString &message, const QString &routingKey, qulonglong publishId)
{
    if (safeCall(this, &AmqpPublisher::publishMessageImpl, message, routingKey, publishId))
        return;

    Q_D(AmqpPublisher);
    if (!d->exchange) {
        emit errorOccurred(NETWORK_MODULE_CODE, NetworkErrorCode::InternalError,
                           QStringLiteral("No amqp exchange found for message publishing"), false);
    } else {
        d->exchange->publish(message, routingKey);
        emit messagePublished(publishId);
    }
}

void AmqpPublisherPrivate::connected()
{
    Q_Q(AmqpPublisher);
    auto newExchange = rabbitClient->createExchange(exchangeName);
    exchangeState = ExchangeState::Opening;

    if (newExchange != exchange) {
        exchange = newExchange;
        qCDebug(proofNetworkAmqpLog) << "Create exchange:" << exchangeName;
        QObject::connect(exchange, QOverload<QAMQP::Error>::of(&QAmqpExchange::error), q, [this, q](QAMQP::Error error) {
            if ((exchangeState == ExchangeState::Declared) && (error == QAMQP::PreconditionFailedError)
                && createdExchangeIfNotExists) {
                exchangeState = ExchangeState::Reopening;
                exchange->reset();
                exchange->reopen();
            } else {
                emit q->errorOccurred(NETWORK_MODULE_CODE, NetworkErrorCode::InternalError,
                                      QStringLiteral("Exchange Error: %1").arg(error), false);
                qCWarning(proofNetworkAmqpLog) << "RabbitMQ exchange error:" << error;
            }
        });

        QObject::connect(exchange, &QAmqpExchange::declared, q, [this]() { exchangeState = ExchangeState::Declared; });

        QObject::connect(exchange, &QAmqpExchange::opened, q, [this, q]() {
            qCDebug(proofNetworkAmqpLog) << "RabbitMQ exchange opened" << q->sender();
            if (createdExchangeIfNotExists && exchangeState == ExchangeState::Opening) {
                exchangeState = ExchangeState::Declared;
                exchange->declare(exchangeType, exchangeOptions);
            } else {
                emit q->connected();
            }
        });
    }
}
