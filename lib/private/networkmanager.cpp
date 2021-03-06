#include "networkmanager.hpp"
#include "networktimeout.hpp"
#include "cookiejar.hpp"
#include "context.hpp"

namespace ph {

NetworkManager::NetworkManager(QObject *parent): QNetworkAccessManager(parent) {
    setCookieJar(CookieJar::instance());
    m_authAttempts = 0;
    m_maxAuthAttempts = 3;
    m_requestTimeout = 0;
#ifdef PHANTOMPY_QT4
    connect(this, SIGNAL(authenticationRequired(QNetworkReply *, QAuthenticator *)),
            this, SLOT(provideAuthentication(QNetworkReply *, QAuthenticator *)));
#else
    connect(this, &NetworkManager::authenticationRequired,
            this, &NetworkManager::provideAuthentication);
#endif
}

NetworkManager::~NetworkManager() {}

void NetworkManager::setCookieJar(QNetworkCookieJar *cookieJar) {
    QNetworkAccessManager::setCookieJar(cookieJar);
    cookieJar->setParent(Context::instance());
}

void NetworkManager::setAuthData(const QString &username, const QString &password) {
    m_username = username;
    m_password = password;
}

void NetworkManager::setRequestTimeout(int requestTimeout) {
    m_requestTimeout = requestTimeout;
}

void NetworkManager::provideAuthentication(QNetworkReply *reply, QAuthenticator *authenticator) {
    if (m_authAttempts++ < m_maxAuthAttempts) {
        authenticator->setUser(m_username);
        authenticator->setPassword(m_password);
    } else {
        m_authAttempts = 0;
        reply->close();
    }
}

QNetworkReply* NetworkManager::createRequest(Operation op, const QNetworkRequest &rq, QIODevice *data) {
    QNetworkReply *reply = QNetworkAccessManager::createRequest(op, rq, data);

    if (m_requestTimeout > 0) {
        NetworkTimeoutTimer *timeoutTimer = new NetworkTimeoutTimer();
        timeoutTimer->reply = reply;
        timeoutTimer->setInterval(m_requestTimeout);
        timeoutTimer->setSingleShot(true);

#ifdef PHANTOMPY_QT4
        connect(timeoutTimer, SIGNAL(timeout()),
                this, SLOT(handleTimeout()));
#else
        connect(timeoutTimer, &NetworkTimeoutTimer::timeout,
                this, &NetworkManager::handleTimeout);
#endif
    }

#ifdef PHANTOMPY_QT4
    connect(reply, SIGNAL(readyRead()), this, SLOT(handleReadyReply()));
#else
    connect(reply, &QNetworkReply::readyRead, this, &NetworkManager::handleReadyReply);
#endif
    return reply;
}

void NetworkManager::handleReadyReply() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());

    if (!reply) return;

    QByteArray replyData(reply->peek(reply->size()));

    QVariantList headers;
    foreach (QByteArray headerName, reply->rawHeaderList()) {
        QVariantMap header;
        header["name"] = QString::fromUtf8(headerName);
        header["value"] = QString::fromUtf8(reply->rawHeader(headerName));

        headers.append(header);
    }

    QVariantMap data;
    data["url"] = reply->url().toEncoded().data();
    data["status"] = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    data["statusText"] = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
    data["contentType"] = reply->header(QNetworkRequest::ContentTypeHeader);
    data["bodySize"] = reply->size();
    data["redirectURL"] = reply->header(QNetworkRequest::LocationHeader);
    data["headers"] = headers;
    data["time"] = QDateTime::currentDateTime();
    data["data"] = replyData;
    data["size"] = replyData.size();

    emit replyReceived(data);
}

void NetworkManager::handleTimeout() {
    NetworkTimeoutTimer *timer = qobject_cast<NetworkTimeoutTimer*>(sender());
    timer->reply->abort();
    delete timer;
}

}
