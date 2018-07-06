#include "baserestapi.h"

#include "baserestapi_p.h"

#include "proofcore/tasks.h"

static const int NETWORK_SSL_ERROR_OFFSET = 1500;
static const int NETWORK_ERROR_OFFSET = 1000;

static const QSet<int> ALLOWED_HTTP_STATUSES = {200, 201, 202, 203, 204, 205, 206};

using namespace Proof;

BaseRestApi::BaseRestApi(const RestClientSP &restClient, BaseRestApiPrivate &dd, QObject *parent)
    : ProofObject(dd, parent)
{
    Q_D(BaseRestApi);
    d->restClient = restClient;
}

BaseRestApi::BaseRestApi(const RestClientSP &restClient, QObject *parent)
    : BaseRestApi(restClient, *new BaseRestApiPrivate, parent)
{}

RestClientSP BaseRestApi::restClient() const
{
    Q_D_CONST(BaseRestApi);
    return d->restClient;
}

bool BaseRestApi::isLoggedOut() const
{
    if (!restClient())
        return true;

    switch (restClient()->authType()) {
    case Proof::RestAuthType::Basic:
        return restClient()->userName().isEmpty() || restClient()->password().isEmpty();
    case Proof::RestAuthType::Wsse:
        return restClient()->userName().isEmpty();
    case Proof::RestAuthType::BearerToken:
        return restClient()->token().isEmpty();
    default:
        return false;
    }
}

qlonglong BaseRestApi::clientNetworkErrorOffset()
{
    return NETWORK_ERROR_OFFSET;
}

qlonglong BaseRestApi::clientSslErrorOffset()
{
    return NETWORK_SSL_ERROR_OFFSET;
}

void BaseRestApi::abortAllRequests()
{
    Q_D(BaseRestApi);
    d->allRepliesLock.lock();
    const QVector<CancelableFuture<RestApiReply>> snapshot = algorithms::toValuesVector(d->allReplies);
    d->allRepliesLock.unlock();
    for (const auto &reply : snapshot)
        reply.cancel();
}

CancelableFuture<RestApiReply> BaseRestApi::get(const QString &method, const QUrlQuery &query)
{
    Q_D(BaseRestApi);
    return d->configureReply(d->restClient->get(method, query, vendor()));
}

CancelableFuture<RestApiReply> BaseRestApi::post(const QString &method, const QUrlQuery &query, const QByteArray &body)
{
    Q_D(BaseRestApi);
    return d->configureReply(d->restClient->post(method, query, body, vendor()));
}

CancelableFuture<RestApiReply> BaseRestApi::post(const QString &method, const QUrlQuery &query, QHttpMultiPart *multiParts)
{
    Q_D(BaseRestApi);
    return d->configureReply(d->restClient->post(method, query, multiParts));
}

CancelableFuture<RestApiReply> BaseRestApi::put(const QString &method, const QUrlQuery &query, const QByteArray &body)
{
    Q_D(BaseRestApi);
    return d->configureReply(d->restClient->put(method, query, body, vendor()));
}

CancelableFuture<RestApiReply> BaseRestApi::patch(const QString &method, const QUrlQuery &query, const QByteArray &body)
{
    Q_D(BaseRestApi);
    return d->configureReply(d->restClient->patch(method, query, body, vendor()));
}

CancelableFuture<RestApiReply> BaseRestApi::deleteResource(const QString &method, const QUrlQuery &query)
{
    Q_D(BaseRestApi);
    return d->configureReply(d->restClient->deleteResource(method, query, vendor()));
}

void BaseRestApi::processSuccessfulReply(QNetworkReply *reply, const PromiseSP<RestApiReply> &promise)
{
    int errorCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (ALLOWED_HTTP_STATUSES.contains(errorCode)) {
        //TODO: move readAll to tasks too if will be needed
        //For now reading data from restclient is done at the same thread where all other reply related checks are done
        //And processing this data is done on separate thread
        //It guarantees that we will not delete something that is still used by other thread
        //It also can be a bottleneck if a lot of requests are done in the same time with big responses
        //Moving readAll to task will mean more complex sync though
        RestApiReply data = RestApiReply::fromQNetworkReply(reply);
        tasks::run([promise, data] { promise->success(data); });
        return;
    }

    QString message;
    QStringList contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString().split(QStringLiteral(";"), QString::SkipEmptyParts);
    for (QString &str : contentType)
        str = str.trimmed();
    if (contentType.contains(QLatin1String("text/plain"))) {
        message = reply->readAll().trimmed();
    } else if (contentType.contains(QLatin1String("application/json"))) {
        QJsonParseError jsonError;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &jsonError);
        if (jsonError.error == QJsonParseError::NoError && doc.isObject())
            message = doc.object()[QStringLiteral("message")].toString();
    }
    if (message.isEmpty())
        message = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString().trimmed();

    qCDebug(proofNetworkMiscLog) << "Network error occurred"
                                 << reply->request().url().toDisplayString(QUrl::FormattingOptions(QUrl::FullyDecoded))
                                 << ": " << errorCode << message;
    int hints = Failure::UserFriendlyHint;
    if (errorCode > 0)
        hints |= Failure::DataIsHttpCodeHint;
    promise->failure(Failure(message, NETWORK_MODULE_CODE, NetworkErrorCode::ServerError, hints, errorCode));
}

void BaseRestApi::processErroredReply(QNetworkReply *reply, const PromiseSP<RestApiReply> &promise)
{
    int errorCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    bool errorCodeIsHttp = errorCode > 0;
    if (!errorCodeIsHttp)
        errorCode = NETWORK_ERROR_OFFSET + static_cast<int>(reply->error());
    QString errorString = reply->errorString();
    long proofErrorCode = NetworkErrorCode::ServerError;
    int hints = errorCodeIsHttp ? Failure::DataIsHttpCodeHint : Failure::NoHint;
    qCDebug(proofNetworkMiscLog) << "Error occurred for"
                                 << reply->request().url().toDisplayString(QUrl::FormattingOptions(QUrl::FullyDecoded))
                                 << ": " << errorCode << errorString;
    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
        errorString = QStringLiteral("Host %1 not found. Try again later").arg(reply->url().host());
        proofErrorCode = NetworkErrorCode::ServiceUnavailable;
        hints |= Failure::UserFriendlyHint;
        break;
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        errorString = QStringLiteral("Host %1 is unavailable. Try again later").arg(reply->url().host());
        proofErrorCode = NetworkErrorCode::ServiceUnavailable;
        hints |= Failure::UserFriendlyHint;
        break;
    default:
        break;
    }

    promise->failure(Failure(errorString, NETWORK_MODULE_CODE, proofErrorCode, hints, errorCode));
}

QVector<QString> BaseRestApi::serverErrorAttributes() const
{
    return QVector<QString>{};
}

QString BaseRestApi::vendor() const
{
    return QLatin1String();
}

CancelableFuture<RestApiReply> BaseRestApiPrivate::configureReply(CancelableFuture<QNetworkReply *> replyFuture)
{
    Q_Q(BaseRestApi);
    auto promise = PromiseSP<RestApiReply>::create();
    promise->future()->onFailure([replyFuture](const Failure &) { replyFuture.cancel(); });

    replyFuture->onSuccess([this, q, promise](QNetworkReply *reply) {
        if (promise->filled()) {
            reply->abort();
            reply->deleteLater();
            return;
        }

        promise->future()->onFailure([reply](const Failure &) {
            if (reply->isRunning())
                reply->abort();
        });

        QObject::connect(reply, &QNetworkReply::finished, q, [this, q, promise, reply]() {
            if (promise->filled() || replyShouldBeHandledByError(reply))
                return;
            q->processSuccessfulReply(reply, promise);
            reply->deleteLater();
        });

        QObject::connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error), q,
                         [this, q, promise, reply](QNetworkReply::NetworkError) {
                             if (promise->filled() || !replyShouldBeHandledByError(reply))
                                 return;
                             q->processErroredReply(reply, promise);
                             reply->deleteLater();
                         });

        QObject::connect(reply, &QNetworkReply::sslErrors, q, [promise, reply](const QList<QSslError> &errors) {
            for (const QSslError &error : errors) {
                if (error.error() != QSslError::SslError::NoError) {
                    int errorCode = NETWORK_SSL_ERROR_OFFSET + static_cast<int>(error.error());
                    qCWarning(proofNetworkMiscLog)
                        << "SSL error occurred"
                        << reply->request().url().toDisplayString(QUrl::FormattingOptions(QUrl::FullyDecoded)) << ": "
                        << errorCode << error.errorString();
                    if (promise->filled())
                        continue;
                    promise->failure(Failure(error.errorString(), NETWORK_MODULE_CODE, NetworkErrorCode::SslError,
                                             Failure::UserFriendlyHint, errorCode));
                }
            }
            reply->deleteLater();
        });
    });

    auto result = CancelableFuture<RestApiReply>(promise);
    rememberReply(result);
    return result;
}

bool BaseRestApiPrivate::replyShouldBeHandledByError(QNetworkReply *reply) const
{
    if (reply->error() == QNetworkReply::NetworkError::NoError)
        return false;
    int errorHundreds = reply->error() / 100;
    return (errorHundreds != 2 && errorHundreds != 4);
}

void BaseRestApiPrivate::rememberReply(const CancelableFuture<RestApiReply> &reply)
{
    allRepliesLock.lock();
    qint64 replyId = QTime::currentTime().msecsSinceStartOfDay();
    while (allReplies.contains(replyId))
        replyId = qrand();
    allReplies[replyId] = reply;
    allRepliesLock.unlock();
    auto cleaner = [this, replyId]() {
        allRepliesLock.lock();
        allReplies.remove(replyId);
        allRepliesLock.unlock();
    };
    reply->onSuccess([cleaner](const RestApiReply &) { cleaner(); })->onFailure([cleaner](const Failure &) { cleaner(); });
}

RestApiReply::RestApiReply(const QByteArray &data, const QHash<QByteArray, QByteArray> &headers,
                           const QByteArray &httpReason, int httpStatus)
    : data(data), headers(headers), httpReason(httpReason), httpStatus(httpStatus)
{}

RestApiReply RestApiReply::fromQNetworkReply(QNetworkReply *qReply)
{
    // Passing by value or explicit non-ref return type are required here,
    // because rawHeaderPairs returns const&, but not the copy
    auto headers = algorithms::map(qReply->rawHeaderPairs(), [](QNetworkReply::RawHeaderPair header) { return header; },
                                   QHash<QByteArray, QByteArray>());
    return RestApiReply(qReply->readAll(), headers,
                        qReply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toByteArray(),
                        qReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
}
