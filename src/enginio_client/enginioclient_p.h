/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtEnginio module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef ENGINIOCLIENT_P_H
#define ENGINIOCLIENT_P_H

#include "chunkdevice_p.h"
#include "enginioclient.h"
#include "enginioreply.h"
#include "enginiofakereply_p.h"
#include "enginioidentity.h"
#include "enginioobjectadaptor_p.h"
#include "enginiostring_p.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>
#include <QtCore/qjsondocument.h>
#include <QtNetwork/qnetworkrequest.h>
#include <QtNetwork/qnetworkreply.h>
#include <QtNetwork/qhttpmultipart.h>
#include <QtCore/qurlquery.h>
#include <QtCore/qfile.h>
#include <QtCore/qmimedatabase.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qbuffer.h>
#include <QtCore/qlinkedlist.h>
#include <QtCore/quuid.h>

QT_BEGIN_NAMESPACE

#define CHECK_AND_SET_URL_PATH_IMPL(Url, Object, Operation, Flags) \
    QString dataPropertyName; \
    {\
        QString _path; \
        QByteArray _errorMsg; \
        GetPathReturnValue _ret = getPath(Object, Operation, &_path, &_errorMsg, Flags); \
    if (!_ret.successful()) \
            return new EnginioFakeReply(this, _errorMsg); \
        dataPropertyName = _ret; \
        Url.setPath(_path); \
    }

#define CHECK_AND_SET_PATH(Url, Object, Operation) \
    CHECK_AND_SET_URL_PATH_IMPL(Url, Object, Operation, EnginioClientPrivate::Default)

#define CHECK_AND_SET_PATH_WITH_ID(Url, Object, Operation) \
    CHECK_AND_SET_URL_PATH_IMPL(Url, Object, Operation, EnginioClientPrivate::IncludeIdInPath)

static QByteArray constructErrorMessage(QByteArray msg)
{
    static QByteArray msgBegin = QByteArrayLiteral("{\"errors\": [{\"message\": \"");
    static QByteArray msgEnd = QByteArrayLiteral("\",\"reason\": \"BadRequest\"}]}");
    return msgBegin + msg + msgEnd;
}

class ENGINIOCLIENT_EXPORT EnginioClientPrivate
{
    enum PathOptions { Default, IncludeIdInPath = 1};

    QNetworkRequest prepareRequest(const QUrl &url);

    struct ENGINIOCLIENT_EXPORT GetPathReturnValue : public QPair<bool, QString>
    {
        GetPathReturnValue(bool value)
            : QPair<bool, QString>(value, QString())
        {}
        GetPathReturnValue(bool value, const QString &propertyName)
            : QPair<bool, QString>(value, propertyName)
        {}
        bool successful() const { return first; }
        operator QString() const { return second; }
    };

    template<class T>
    static GetPathReturnValue getPath(const T &object, int operation, QString *path, QByteArray *errorMsg, PathOptions flags = Default)
    {
        enum {Failed = false};
        QByteArray &msg = *errorMsg;

        QString &result = *path;
        result.reserve(96);
        result.append(QStringLiteral("/v1/"));

        switch (operation) {
        case ObjectOperation: {
            QString objectType = object[EnginioString::objectType].toString();
            if (objectType.isEmpty()) {
                msg = constructErrorMessage(EnginioString::Requested_object_operation_requires_non_empty_objectType_value);
                return GetPathReturnValue(Failed);
            }

            result.append(objectType.replace('.', '/'));
            break;
        }
        case ObjectAclOperation:
        {
            QString objectType = object[EnginioString::objectType].toString();
            if (objectType.isEmpty()) {
                msg = constructErrorMessage(EnginioString::Requested_object_acl_operation_requires_non_empty_objectType_value);
                return GetPathReturnValue(Failed);
            }

            result.append(objectType.replace('.', '/'));
            QString id = object[EnginioString::id].toString();
            if (id.isEmpty()) {
                msg = constructErrorMessage(EnginioString::Requested_object_acl_operation_requires_non_empty_id_value);
                return GetPathReturnValue(Failed);
            }
            result.append('/');
            result.append(id);
            result.append('/');
            result.append(EnginioString::access);
            return GetPathReturnValue(true, EnginioString::access);
        }
        case AuthenticationOperation:
            result.append(EnginioString::authIdentity);
            break;
        case FileOperation: {
            result.append(EnginioString::files);
            // if we have a fileID, it becomes "view", otherwise it is up/download
            QString fileId = object[EnginioString::id].toString();
            if (!fileId.isEmpty()) {
                result.append('/');
                result.append(fileId);
            }
            break;
        }
        case FileGetDownloadUrlOperation: {
            result.append(EnginioString::files);
            QString fileId = object[EnginioString::id].toString();
            if (fileId.isEmpty()) {
                msg = constructErrorMessage(EnginioString::Download_operation_requires_non_empty_fileId_value);
                return GetPathReturnValue(Failed);
            }
            result.append(QLatin1Char('/') + fileId + QStringLiteral("/download_url"));
            break;
        }
        case FileChunkUploadOperation: {
            const QString fileId = object[EnginioString::id].toString();
            Q_ASSERT(!fileId.isEmpty());
            result.append(EnginioString::files + QLatin1Char('/') + fileId + QStringLiteral("/chunk"));
            break;
        }
        case SearchOperation:
            result.append(EnginioString::search);
            break;
        case SessionOperation:
            result.append(EnginioString::session);
            break;
        case UserOperation:
            result.append(EnginioString::users);
            break;
        case UsergroupOperation:
            result.append(EnginioString::usergroups);
            break;
        case UsergroupMemberOperation:
        {
            QString id = object[EnginioString::id].toString();
            if (id.isEmpty()) {
                msg = constructErrorMessage(EnginioString::Requested_usergroup_member_operation_requires_non_empty_id_value);
                return GetPathReturnValue(Failed);
            }
            result.append(EnginioString::usergroups);
            result.append('/');
            result.append(id);
            result.append('/');
            result.append(EnginioString::members);
            return GetPathReturnValue(true, EnginioString::member);
        }
        }

        if (flags & IncludeIdInPath) {
            QString id = object[EnginioString::id].toString();
            if (id.isEmpty()) {
                msg = constructErrorMessage(EnginioString::Requested_operation_requires_non_empty_id_value);
                return GetPathReturnValue(Failed);
            }
            result.append('/');
            result.append(id);
        }

        return GetPathReturnValue(true, QString());
    }

    class ReplyFinishedFunctor
    {
        EnginioClientPrivate *d;

    public:
        ReplyFinishedFunctor(EnginioClientPrivate *enginio)
            : d(enginio)
        {
            Q_ASSERT(d);
        }

        void operator ()(QNetworkReply *nreply)
        {
            d->replyFinished(nreply);
        }
    };

    class CallPrepareSessionToken
    {
        EnginioClientPrivate *_enginio;
        EnginioIdentity *_identity;

    public:
        CallPrepareSessionToken(EnginioClientPrivate *enginio, EnginioIdentity *identity)
            : _enginio(enginio)
            , _identity(identity)
        {}
        void operator ()()
        {
            if (!_enginio->_backendId.isEmpty() && !_enginio->_backendSecret.isEmpty()) {
                // TODO should we disconnect backendId and backendSecret change singals?
                _identity->prepareSessionToken(_enginio);
            }
        }
    };

    class IdentityInstanceDestroyed
    {
        EnginioClientPrivate *_enginio;

    public:
        IdentityInstanceDestroyed(EnginioClientPrivate *enginio)
            : _enginio(enginio)
        {}
        void operator ()()
        {
            _enginio->setIdentity(0);
        }
    };

protected:
    class AuthenticationStateTrackerFunctor
    {
        EnginioClientPrivate *_enginio;
        EnginioClientBase::AuthenticationState _state;
    public:
        AuthenticationStateTrackerFunctor(EnginioClientPrivate *enginio, EnginioClientBase::AuthenticationState state = EnginioClient::NotAuthenticated)
            : _enginio(enginio)
            , _state(state)
        {}

        void operator()() const
        {
            _enginio->setAuthenticationState(_state);
        }
    };

public:
    enum Operation {
        // Do not forget to keep in sync with EnginioClientBase::Operation!
        ObjectOperation = EnginioClientBase::ObjectOperation,
        ObjectAclOperation = EnginioClientBase::ObjectAclOperation,
        UserOperation = EnginioClientBase::UserOperation,
        UsergroupOperation = EnginioClientBase::UsergroupOperation,
        UsergroupMemberOperation = EnginioClientBase::UsergroupMembersOperation,
        FileOperation = EnginioClientBase::FileOperation,

        // private
        AuthenticationOperation,
        SessionOperation,
        SearchOperation,
        FileChunkUploadOperation,
        FileGetDownloadUrlOperation
    };

    Q_ENUMS(Operation)

    EnginioClientPrivate(EnginioClientBase *client = 0);
    virtual ~EnginioClientPrivate();
    static EnginioClientPrivate* get(EnginioClientBase *client) { return client->d_func(); }
    static const EnginioClientPrivate* get(const EnginioClientBase *client) { return client->d_func(); }
    static EnginioClient* get(EnginioClientPrivate *client) { return static_cast<EnginioClient*>(client->q_ptr); }
    static const EnginioClient* get(const EnginioClientPrivate *client) { return static_cast<EnginioClient*>(client->q_ptr); }


    EnginioClientBase *q_ptr;
    QByteArray _backendId;
    QByteArray _backendSecret;
    EnginioIdentity *_identity;

    QLinkedList<QMetaObject::Connection> _connections;
    QVarLengthArray<QMetaObject::Connection, 4> _identityConnections;
    QUrl _serviceUrl;
    QNetworkAccessManager *_networkManager;
    QMetaObject::Connection _networkManagerConnection;
    QNetworkRequest _request;
    QMap<QNetworkReply*, EnginioReplyBase*> _replyReplyMap;
    QMap<QNetworkReply*, QByteArray> _requestData;

    // device and last position
    QMap<QNetworkReply*, QPair<QIODevice*, qint64> > _chunkedUploads;
    qint64 _uploadChunkSize;
    QJsonObject _identityToken;
    EnginioClientBase::AuthenticationState _authenticationState;

    QSet<EnginioReplyBase*> _delayedReplies; // Used only for testing

    virtual void init();

    void replyFinished(QNetworkReply *nreply);
    bool finishDelayedReplies();

    void setAuthenticationState(const EnginioClientBase::AuthenticationState state)
    {
        if (_authenticationState == state)
            return;
        _authenticationState = state;
        emit q_ptr->authenticationStateChanged(state);
    }

    EnginioClientBase::AuthenticationState authenticationState() const Q_REQUIRED_RESULT
    {
        return _authenticationState;
    }

    QJsonObject identityToken() const Q_REQUIRED_RESULT
    {
        return _identityToken;
    }

    void setIdentityToken(EnginioReplyBase *reply)
    {
        QByteArray sessionToken;
        if (reply) {
            _identityToken = reply->data();
            sessionToken = _identityToken[EnginioString::sessionToken].toString().toLatin1();
        }

        _request.setRawHeader(EnginioString::Enginio_Backend_Session, sessionToken);
        if (sessionToken.isEmpty())
            emitSessionTerminated();
        else
            emitSessionAuthenticated(reply);
    }

    void registerReply(QNetworkReply *nreply, EnginioReplyBase *ereply)
    {
        nreply->setParent(ereply);
        _replyReplyMap[nreply] = ereply;
    }

    EnginioIdentity *identity() const Q_REQUIRED_RESULT
    {
        return _identity;
    }

    void setIdentity(EnginioIdentity *identity)
    {
        foreach (const QMetaObject::Connection &identityConnection, _identityConnections)
            QObject::disconnect(identityConnection);
        _identityConnections.clear();

        if (!(_identity = identity)) {
            // invalidate old identity token
            setIdentityToken(0);
            return;
        }
        CallPrepareSessionToken callPrepareSessionToken(this, identity);
        if (_backendId.isEmpty() || _backendSecret.isEmpty()) {
            if (_backendId.isEmpty())
                _identityConnections.append(QObject::connect(q_ptr, &EnginioClientBase::backendIdChanged, callPrepareSessionToken));
            if (_backendSecret.isEmpty())
                _identityConnections.append(QObject::connect(q_ptr, &EnginioClientBase::backendSecretChanged, callPrepareSessionToken));
        } else
            identity->prepareSessionToken(this);
        _identityConnections.append(QObject::connect(identity, &EnginioIdentity::dataChanged, callPrepareSessionToken));
        _identityConnections.append(QObject::connect(identity, &EnginioIdentity::destroyed, IdentityInstanceDestroyed(this)));
        emit q_ptr->identityChanged(identity);
    }

    QNetworkReply *identify(const QJsonObject &object)
    {
        QUrl url(_serviceUrl);
        CHECK_AND_SET_PATH(url, object, AuthenticationOperation);

        QNetworkRequest req = prepareRequest(url);
        QByteArray data(QJsonDocument(object).toJson(QJsonDocument::Compact));
        QNetworkReply *reply = networkManager()->post(req, data);

        setAuthenticationState(EnginioClientBase::Authenticating);

        if (gEnableEnginioDebugInfo)
            _requestData.insert(reply, data);

        return reply;
    }

    QNetworkReply *customRequest(const QUrl &url, const QByteArray &httpOperation, const QJsonObject &data)
    {
        Q_ASSERT(!url.isEmpty());
        Q_ASSERT(!httpOperation.isEmpty());

        QNetworkRequest req = prepareRequest(url);

        if (data[EnginioString::headers].isObject()) {
            QJsonObject headers = data[EnginioString::headers].toObject();

            QJsonObject::const_iterator end = headers.constEnd();
            for (QJsonObject::const_iterator i = headers.constBegin(); i != end; i++) {
                QByteArray headerName = i.key().toUtf8();
                QByteArray headerValue = i.value().toString().toUtf8();
                req.setRawHeader(headerName, headerValue);
            }
        }

        QBuffer *buffer = 0;
        QByteArray payload;

        if (data[EnginioString::payload].isObject()) {
            ObjectAdaptor<QJsonObject> o(data[EnginioString::payload].toObject());
            payload = o.toJson();
            buffer = new QBuffer();
            buffer->setData(payload);
            buffer->open(QIODevice::ReadOnly);
        }

        QNetworkReply *reply = networkManager()->sendCustomRequest(req, httpOperation, buffer);

        if (gEnableEnginioDebugInfo && !payload.isEmpty())
            _requestData.insert(reply, payload);

        if (buffer)
            buffer->setParent(reply);

        return reply;
    }

    template<class T>
    QNetworkReply *update(const ObjectAdaptor<T> &object, const EnginioClientBase::Operation operation)
    {
        QUrl url(_serviceUrl);
        CHECK_AND_SET_PATH_WITH_ID(url, object, operation);

        QNetworkRequest req = prepareRequest(url);

        QByteArray data = dataPropertyName.isEmpty() ? object.toJson() : object[dataPropertyName].toJson();

        QNetworkReply *reply = networkManager()->put(req, data);

        if (gEnableEnginioDebugInfo)
            _requestData.insert(reply, data);

        return reply;
    }

    template<class T>
    QNetworkReply *remove(const ObjectAdaptor<T> &object, const EnginioClientBase::Operation operation)
    {
        QUrl url(_serviceUrl);
        CHECK_AND_SET_PATH_WITH_ID(url, object, operation);

        QNetworkRequest req = prepareRequest(url);

        QNetworkReply *reply = 0;
        QByteArray data;
#if 1 // QT_VERSION < QT_VERSION_CHECK(5, 4, 0) ?
        if (operation != EnginioClientBase::ObjectAclOperation)
            reply = networkManager()->deleteResource(req);
        else {
            data = dataPropertyName.isEmpty() ? object.toJson() : object[dataPropertyName].toJson();
            QBuffer *buffer = new QBuffer();
            buffer->setData(data);
            buffer->open(QIODevice::ReadOnly);
            reply = networkManager()->sendCustomRequest(req, EnginioString::Delete, buffer);
            buffer->setParent(reply);
        }
#else
        // TODO enable me https://codereview.qt-project.org/#change,56920
        data = dataPropertyName.isEmpty() ? object.toJson() : object[dataPropertyName].toJson();
        reply = networkManager()->deleteResource(req, data);
#endif

        Q_ASSERT(reply);

        if (gEnableEnginioDebugInfo && !data.isEmpty())
            _requestData.insert(reply, data);

        return reply;
    }

    template<class T>
    QNetworkReply *create(const ObjectAdaptor<T> &object, const EnginioClientBase::Operation operation)
    {
        QUrl url(_serviceUrl);

        CHECK_AND_SET_PATH(url, object, operation);

        QNetworkRequest req = prepareRequest(url);

        QByteArray data = dataPropertyName.isEmpty() ? object.toJson() : object[dataPropertyName].toJson();

        QNetworkReply *reply = networkManager()->post(req, data);

        if (gEnableEnginioDebugInfo)
            _requestData.insert(reply, data);

        return reply;
    }

    template<class T>
    QNetworkReply *query(const ObjectAdaptor<T> &object, const Operation operation)
    {
        QUrl url(_serviceUrl);
        CHECK_AND_SET_PATH(url, object, operation);

        // TODO add all params here
        QUrlQuery urlQuery;
        if (int limit = object[EnginioString::limit].toInt()) {
            urlQuery.addQueryItem(EnginioString::limit, QString::number(limit));
        }
        if (int offset = object[EnginioString::offset].toInt()) {
            urlQuery.addQueryItem(EnginioString::offset, QString::number(offset));
        }
        if (object.contains(EnginioString::count)) { // TODO docs are saying about integer but it is not interpreted.
            urlQuery.addQueryItem(EnginioString::count, QString(0, Qt::Uninitialized));
        }
        ValueAdaptor<T> include = object[EnginioString::include];
        if (include.isComposedType()) {
            urlQuery.addQueryItem(EnginioString::include,
                QString::fromUtf8(include.toJson()));
        }
        ValueAdaptor<T> sort = object[EnginioString::sort];
        if (sort.isComposedType()) {
            urlQuery.addQueryItem(EnginioString::sort,
                QString::fromUtf8(sort.toJson()));
        }
        if (operation == SearchOperation) {
            ValueAdaptor<T> search = object[EnginioString::search];
            ArrayAdaptor<T> objectTypes = object[EnginioString::objectTypes].toArray();
            if (Q_UNLIKELY(objectTypes.isEmpty()))
                return new EnginioFakeReply(this, constructErrorMessage(EnginioString::Fulltext_Search_objectTypes_parameter_is_missing_or_it_is_not_an_array));
            if (search.isComposedType()) {
                for (typename ArrayAdaptor<T>::const_iterator i = objectTypes.constBegin(); i != objectTypes.constEnd(); ++i) {
                    urlQuery.addQueryItem(QStringLiteral("objectTypes[]"), (*i).toString());
                }
                urlQuery.addQueryItem(EnginioString::search,
                    QString::fromUtf8(search.toJson()));
            } else {
                return new EnginioFakeReply(this, constructErrorMessage(EnginioString::Fulltext_Search_search_parameter_missing));
            }
        } else
        if (object[EnginioString::query].isComposedType()) { // TODO docs are inconsistent on that
            urlQuery.addQueryItem(QStringLiteral("q"),
                QString::fromUtf8(object[EnginioString::query].toJson()));
        }
        url.setQuery(urlQuery);

        QNetworkRequest req = prepareRequest(url);

        return networkManager()->get(req);
    }

    template<class T>
    QNetworkReply *downloadFile(const ObjectAdaptor<T> &object)
    {
        QUrl url(_serviceUrl);
        CHECK_AND_SET_PATH(url, object, FileGetDownloadUrlOperation);
        if (object.contains(EnginioString::variant)) {
            QString variant = object[EnginioString::variant].toString();
            QUrlQuery query;
            query.addQueryItem(EnginioString::variant, variant);
            url.setQuery(query);
        }

        QNetworkRequest req = prepareRequest(url);

        QNetworkReply *reply = networkManager()->get(req);
        return reply;
    }

    template<class T>
    QNetworkReply *uploadFile(const ObjectAdaptor<T> &object, const QUrl &fileUrl)
    {
        if (!fileUrl.scheme().isEmpty() && !fileUrl.isLocalFile())
            qWarning() << "Enginio: Upload must be local file.";
        QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.path();

        QFile *file = new QFile(path);
        if (!file->exists()) {
            QByteArray msg = QByteArray("Cannot upload a not existing file ('") + path.toUtf8() + QByteArray("')");
            msg = constructErrorMessage(msg);
            delete file;
            return new EnginioFakeReply(this, msg);
        }

        if (!file->open(QFile::ReadOnly)) {
            QByteArray msg = QByteArray("File ('") + path.toUtf8() + QByteArray("') could not be opened for reading");
            msg = constructErrorMessage(msg);
            delete file;
            return new EnginioFakeReply(this, msg);
        }
        QMimeDatabase mimeDb;
        QString mimeType = mimeDb.mimeTypeForFile(path).name();
        return upload(object, file, mimeType);
    }

    template<class T>
    QNetworkReply *upload(const ObjectAdaptor<T> &object, QIODevice *device, const QString &mimeType)
    {
        QNetworkReply *reply = 0;
        if (!device->isSequential() && device->size() < _uploadChunkSize)
            reply = uploadAsHttpMultiPart(object, device, mimeType);
        else
            reply = uploadChunked(object, device);

        if (gEnableEnginioDebugInfo) {
            QByteArray data = object.toJson();
            _requestData.insert(reply, data);
        }

        return reply;
    }

    QNetworkAccessManager *networkManager() const Q_REQUIRED_RESULT
    {
        return _networkManager;
    }

    void assignNetworkManager();
    static QNetworkAccessManager *prepareNetworkManagerInThread() Q_REQUIRED_RESULT;

    bool isSignalConnected(const QMetaMethod &signal) const Q_REQUIRED_RESULT
    {
        return q_ptr->isSignalConnected(signal);
    }

    class UploadProgressFunctor
    {
    public:
        UploadProgressFunctor(EnginioClientPrivate *client, QNetworkReply *reply)
            : _client(client), _reply(reply)
        {
            Q_ASSERT(_client);
            Q_ASSERT(_reply);
        }

        void operator ()(qint64 progress, qint64 total)
        {
            if (!progress || !total) // TODO sometimes we get garbage as progress, it seems like a bug of Qt or Enginio web engine
                return;
            EnginioReplyBase *ereply = _client->_replyReplyMap.value(_reply);
            if (_client->_chunkedUploads.contains(_reply)) {
                QPair<QIODevice*, qint64> chunkData = _client->_chunkedUploads.value(_reply);
                total = chunkData.first->size();
                progress += chunkData.second;
                if (progress > total)  // TODO assert?!
                    return;
            }
            emit ereply->progress(progress, total);
        }
    private:
        EnginioClientPrivate *_client;
        QNetworkReply *_reply;
    };

    virtual void emitSessionTerminated() const;
    virtual void emitSessionAuthenticated(EnginioReplyBase *reply);
    virtual void emitSessionAuthenticationError(EnginioReplyBase *reply);
    virtual void emitFinished(EnginioReplyBase *reply);
    virtual void emitError(EnginioReplyBase *reply);
    virtual EnginioReplyBase *createReply(QNetworkReply *nreply);

private:

    template<class T>
    QNetworkReply *uploadAsHttpMultiPart(const ObjectAdaptor<T> &object, QIODevice *device, const QString &mimeType)
    {
        QUrl serviceUrl = _serviceUrl;
        CHECK_AND_SET_PATH(serviceUrl, QJsonObject(), FileOperation);

        QNetworkRequest req = prepareRequest(serviceUrl);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray());

        QHttpMultiPart *multiPart = createHttpMultiPart(object, device, mimeType);
        QNetworkReply *reply = networkManager()->post(req, multiPart);
        multiPart->setParent(reply);
        device->setParent(multiPart);
        _connections.append(QObject::connect(reply, &QNetworkReply::uploadProgress, UploadProgressFunctor(this, reply)));
        return reply;
    }


    /* Create a multi part upload:
     * That means the JSON metadata and the actual file get sent in one http-post.
     * The associatedObject has to be a valid object type on the server.
     * If it does not contain an id, it needs to be manually associated later or will get garbage collected eventually.
     */
    template<class T>
    QHttpMultiPart *createHttpMultiPart(const ObjectAdaptor<T> &object, QIODevice *data, const QString &mimeType)
    {
        // check file/chunk size
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        data->setParent(multiPart);

        QHttpPart objectPart;
        objectPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                             QStringLiteral("form-data; name=\"object\""));

        objectPart.setBody(object.toJson());
        multiPart->append(objectPart);

        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(object[EnginioString::file].toObject()[EnginioString::fileName].toString()));
        filePart.setBodyDevice(data);
        multiPart->append(filePart);
        return multiPart;
    }

    template<class T>
    QNetworkReply *uploadChunked(const ObjectAdaptor<T> &object, QIODevice *device)
    {
        QUrl serviceUrl = _serviceUrl;
        CHECK_AND_SET_PATH(serviceUrl, QJsonObject(), FileOperation);

        QNetworkRequest req = prepareRequest(serviceUrl);

        QNetworkReply *reply = networkManager()->post(req, object.toJson());
        _chunkedUploads.insert(reply, qMakePair(device, static_cast<qint64>(0)));
        _connections.append(QObject::connect(reply, &QNetworkReply::uploadProgress, UploadProgressFunctor(this, reply)));
        return reply;
    }

    void uploadChunk(EnginioReplyBase *ereply, QIODevice *device, qint64 startPos)
    {
        QUrl serviceUrl = _serviceUrl;
        {
            QString path;
            QByteArray errorMsg;
            if (!getPath(ereply->data(), FileChunkUploadOperation, &path, &errorMsg).successful())
                Q_UNREACHABLE(); // sequential upload can not have an invalid path!
            serviceUrl.setPath(path);
        }

        QNetworkRequest req = prepareRequest(serviceUrl);
        req.setHeader(QNetworkRequest::ContentTypeHeader, EnginioString::Application_octet_stream);

        // Content-Range: bytes {chunkStart}-{chunkEnd}/{totalFileSize}
        qint64 size = device->size();
        qint64 endPos = qMin(startPos + _uploadChunkSize, size);
        req.setRawHeader(EnginioString::Content_Range,
                         QByteArray::number(startPos) + EnginioString::Minus
                         + QByteArray::number(endPos) + EnginioString::Div
                         + QByteArray::number(size));

        // qDebug() << "Uploading chunk from " << startPos << " to " << endPos << " of " << size;

        Q_ASSERT(device->isOpen());

        ChunkDevice *chunkDevice = new ChunkDevice(device, startPos, _uploadChunkSize);
        chunkDevice->open(QIODevice::ReadOnly);

        QNetworkReply *reply = networkManager()->put(req, chunkDevice);
        chunkDevice->setParent(reply);
        _chunkedUploads.insert(reply, qMakePair(device, endPos));
        ereply->setNetworkReply(reply);
        _connections.append(QObject::connect(reply, &QNetworkReply::uploadProgress, UploadProgressFunctor(this, reply)));
    }
};

#undef CHECK_AND_SET_URL_PATH_IMPL
#undef CHECK_AND_SET_PATH_WITH_ID
#undef CHECK_AND_SET_PATH

QT_END_NAMESPACE

#endif // ENGINIOCLIENT_P_H
