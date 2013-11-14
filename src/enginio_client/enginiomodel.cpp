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

#include <Enginio/enginiomodel.h>
#include <Enginio/enginioreply.h>
#include <Enginio/private/enginioclient_p.h>
#include <Enginio/private/enginiofakereply_p.h>
#include <Enginio/private/enginiodummyreply_p.h>
#include <Enginio/private/enginiobackendconnection_p.h>
#include <Enginio/enginiomodelbase.h>
#include <Enginio/private/enginiomodelbase_p.h>

#include <QtCore/qobject.h>
#include <QtCore/qvector.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qjsonarray.h>

QT_BEGIN_NAMESPACE

const int EnginioModelBasePrivate::IncrementalModelUpdate = -2;

/*!
  \class EnginioModel
  \inmodule enginio-qt
  \ingroup enginio-client
  \target EnginioModelCpp
  \brief EnginioModel represents data from Enginio as a \l QAbstractListModel.

  EnginioModel is a \l QAbstractListModel, together with a view it allows
  to show the result of a query in a convenient way. The model executes query, update
  and create operations asynchronously on an Enginio backend collection, which allows
  not only to read data from the cloud but also modify it.

  The simplest type of query is:
  \code
  { "objectType": "objects.fruits" }
  \endcode

  Assigning such a query to the model results in downloading of all objects from the "objects.fruits"
  collection. It is possible to \l{EnginioModel::append}{append} new objects, to
  \l{EnginioModel::setProperty}{modify} or to \l{EnginioModel::remove}{remove} them.

  The query has to result in a list of objects, each object becomes an item in the model. Properties
  of the items are used as role names.
  There are a few predefined role names that will always be available (\l{EnginioModel::Roles}{Roles}).

  The operations are executed asynchronously, which means that user interface is not
  blocked while the model is initialized and updated. Every modification is divided in
  two steps; request and confirmation. For example when \l{EnginioModel::append}{append}
  is called EnginioModel returns immediately as if the operation had succeeded. In
  the background it waits for confirmation from the backend and only then the operation is
  really finished. It may happen that operation fails, for example
  because of insufficient access rights, in that case the operation will be reverted.

  There are two, ways of tracking if an item state is the same in the model and backend.
  Each item has a role that returns a boolean \l{EnginioModel::SyncedRole}{SyncedRole}, role name "_synced" which
  indicates whether the item is successfully updated on the server.
  This role can for example meant to be used for a busy indicator while a property is being updated.
  Alternatively the status of each \l{EnginioReply}{EnginioReply} returned by EnginioModel can be tracked.
  The operation is confirmed when the reply is \l{EnginioReply::isFinished}{finished} without \l{EnginioReply::isError}{error}.

  When a reply is finished it is the user's responsibility to delete it, it can be done
  by connecting the \l{EnginioReply::finished}{finished} signal to \l{QObject::deleteLater()}{deleteLater}.
  \code
  QObject::connect(reply, &EnginioReply::finished, reply, &EnginioReply::deleteLater);
  \endcode
  \note it is not safe to use the delete operator directly in \l{EnginioReply::finished}{finished}.

  \note EnginioClient emits the finished and error signals for the model, not the model itself.

  The \l{EnginioModel::query}{query} can contain one or more options:
  The "sort" option, to get presorted data:
  \code
  {
    "objectType": "objects.fruits",
    "sort": [{"sortBy":"price", "direction": "asc"}]
  }
  \endcode
  The "query" option is used for filtering:
  \code
  {
    "objectType": "objects.fruits",
    "query": {"name": {"$in": ["apple", "orange", "kiwi"]}}
  }
  \endcode
  The "limit" option to limit the amount of results:
  \code
  {
    "objectType": "objects.fruits",
    "limit": 10
  }
  \endcode
  The "offset" option to skip some results from the beginning of a result set:
  \code
  {
    "objectType": "objects.fruits",
    "offset": 10
  }
  \endcode
  The options are valid only during the initial model population and
  are not enforced in anyway when updating or otherwise modifying the model data.
  \l QSortFilterProxyModel can be used to do more advanced sorting and filtering on the client side.

  EnginioModel can not detect when a property of a result is computed by the backend.
  For example the "include" option to \l{EnginioModel::query}{query} fills in the original creator of
  and object with the full object representing the "creator".
  \code
  {
    "objectType": "objects.fruits",
    "include": {"creator": {}}
  }
  \endcode
  For the model the "creator" property is not longer a reference (as it is on the backend), but a full object.
  But while the full object is accessible, attempts to alter the object's data will fail.

  For the QML version of this class see \l {Enginio1::EnginioModel}{EnginioModel (QML)}
*/

EnginioModelBasePrivate::~EnginioModelBasePrivate()
{
    foreach (const QMetaObject::Connection &connection, _clientConnections)
        QObject::disconnect(connection);

    foreach (const QMetaObject::Connection &connection, _repliesConnections)
        QObject::disconnect(connection);
}

void EnginioModelBasePrivate::receivedNotification(QJsonObject data)
{
    const QJsonObject origin = data[EnginioString::origin].toObject();
    const QString requestId = origin[EnginioString::apiRequestId].toString();
    if (_attachedData.markRequestIdAsHandled(requestId))
        return; // request was handled

    QJsonObject object = data[EnginioString::data].toObject();
    QString event = data[EnginioString::event].toString();
    if (event == EnginioString::update) {
        receivedUpdateNotification(object);
    } else if (event == EnginioString::_delete) {
        receivedRemoveNotification(object);
    } else  if (event == EnginioString::create) {
        const int rowHint = _attachedData.rowFromRequestId(requestId);
        if (rowHint != NoHintRow)
            receivedUpdateNotification(object, QString(), rowHint);
        else
            receivedCreateNotification(object);
    }
}

void EnginioModelBasePrivate::receivedRemoveNotification(const QJsonObject &object, int rowHint)
{
    int row = rowHint;
    if (rowHint == NoHintRow) {
        QString id = object[EnginioString::id].toString();
        if (Q_UNLIKELY(!_attachedData.contains(id))) {
            // removing not existing object
            return;
        }
        row = _attachedData.rowFromObjectId(id);
    }
    if (Q_UNLIKELY(row == DeletedRow))
        return;

    q->beginRemoveRows(QModelIndex(), row, row);
    _data.removeAt(row);
    // we need to updates rows in _attachedData
    _attachedData.updateAllDataAfterRowRemoval(row);
    q->endRemoveRows();
}

void EnginioModelBasePrivate::receivedUpdateNotification(const QJsonObject &object, const QString &idHint, int row)
{
    // update an existing object
    if (row == NoHintRow) {
        QString id = idHint.isEmpty() ? object[EnginioString::id].toString() : idHint;
        Q_ASSERT(_attachedData.contains(id));
        row = _attachedData.rowFromObjectId(id);
    }
    if (Q_UNLIKELY(row == DeletedRow))
        return;
    Q_ASSERT(row >= 0);

    QJsonObject current = _data[row].toObject();
    QDateTime currentUpdateAt = QDateTime::fromString(current[EnginioString::updatedAt].toString(), Qt::ISODate);
    QDateTime newUpdateAt = QDateTime::fromString(object[EnginioString::updatedAt].toString(), Qt::ISODate);
    if (newUpdateAt < currentUpdateAt) {
        // we already have a newer version
        return;
    }
    if (_data[row].toObject()[EnginioString::id].toString().isEmpty()) {
        // Create and update may go through the same code path because
        // the model already have a dummy item. No id means that it
        // is a dummy item.
        const QString newId = object[EnginioString::id].toString();
        AttachedData newData(row, newId);
        _attachedData.insert(newData);
    }
    if (_data.count() == 1) {
        q->beginResetModel();
        _data.replace(row, object);
        syncRoles();
        q->endResetModel();
    } else {
        _data.replace(row, object);
        emit q->dataChanged(q->index(row), q->index(row));
    }
}

void EnginioModelBasePrivate::fullQueryReset(const QJsonArray &data)
{
    foreach (const QMetaObject::Connection &connection, _repliesConnections)
        QObject::disconnect(connection);
    _repliesConnections.clear();
    q->beginResetModel();
    _data = data;
    _attachedData.initFromArray(_data);
    syncRoles();
    _canFetchMore = _canFetchMore && _data.count() && (queryData(EnginioString::limit).toDouble() <= _data.count());
    q->endResetModel();
}

void EnginioModelBasePrivate::receivedCreateNotification(const QJsonObject &object)
{
    // create a new object
    QString id = object[EnginioString::id].toString();
    Q_ASSERT(!_attachedData.contains(id));
    AttachedData data;
    data.row = _data.count();
    data.id = id;
    q->beginInsertRows(QModelIndex(), _data.count(), _data.count());
    _attachedData.insert(data);
    _data.append(object);
    q->endInsertRows();
}

void EnginioModelBasePrivate::syncRoles()
{
    QJsonObject firstObject(_data.first().toObject());

    if (!_roles.count()) {
        _roles.reserve(firstObject.count());
        _roles[EnginioModel::SyncedRole] = EnginioString::_synced; // TODO Use a proper name, can we make it an attached property in qml? Does it make sense to try?
        _roles[EnginioModel::CreatedAtRole] = EnginioString::createdAt;
        _roles[EnginioModel::UpdatedAtRole] = EnginioString::updatedAt;
        _roles[EnginioModel::IdRole] = EnginioString::id;
        _roles[EnginioModel::ObjectTypeRole] = EnginioString::objectType;
        _rolesCounter = EnginioModel::LastRole;
    }

    // check if someone does not use custom roles
    QHash<int, QByteArray> predefinedRoles = q->roleNames();
    foreach (int i, predefinedRoles.keys()) {
        if (i < EnginioModel::LastRole && i >= EnginioModel::SyncedRole && predefinedRoles[i] != _roles[i].toUtf8()) {
            qWarning("Can not use custom role index lower then EnginioModel::LastRole, but '%i' was used for '%s'", i, predefinedRoles[i].constData());
            continue;
        }
        _roles[i] = QString::fromUtf8(predefinedRoles[i].constData());
    }

    // estimate additional dynamic roles:
    QSet<QString> definedRoles = _roles.values().toSet();
    QSet<int> definedRolesIndexes = predefinedRoles.keys().toSet();
    for (QJsonObject::const_iterator i = firstObject.constBegin(); i != firstObject.constEnd(); ++i) {
        const QString key = i.key();
        if (definedRoles.contains(key)) {
            // we skip predefined keys so we can keep constant id for them
            if (Q_UNLIKELY(key == EnginioString::_synced))
                qWarning("EnginioModel can not be used with objects having \"_synced\" property. The property will be overriden.");
        } else {
            while (definedRolesIndexes.contains(_rolesCounter))
                ++_rolesCounter;
            _roles[_rolesCounter++] = i.key();
        }
    }
}

namespace  {

struct Types {
    typedef EnginioReply Reply;
    typedef EnginioModel Public;
    typedef EnginioClient Client;
    typedef EnginioClientBasePrivate ClientPrivate;
    typedef QJsonObject Data;
};

} // namespace

class EnginioModelPrivate: public EnginioModelPrivateT<EnginioModelPrivate, Types>
{
public:
    typedef EnginioModelPrivateT<EnginioModelPrivate, Types> Base;

    EnginioModelPrivate(EnginioModelBase *pub)
        : Base(pub)
    {}

    virtual QJsonObject replyData(const EnginioReplyBase *reply) const Q_DECL_OVERRIDE
    {
        return static_cast<const EnginioReply*>(reply)->data();
    }

    virtual QJsonValue queryData(const QString &name) Q_DECL_OVERRIDE
    {
        return _query[name];
    }

    virtual QJsonObject queryAsJson() const Q_DECL_OVERRIDE
    {
        return _query;
    }
};

/*!
    Constructs a new model with \a parent as QObject parent.
*/
EnginioModel::EnginioModel(QObject *parent)
    : EnginioModelBase(*new EnginioModelPrivate(this), parent)
{
    Q_D(EnginioModel);
    d->init();
}

/*!
    Destroys the model.
*/
EnginioModel::~EnginioModel()
{}

/*!
    \internal
    Constructs a new model with \a parent as QObject parent.
*/
EnginioModelBase::EnginioModelBase(EnginioModelBasePrivate &dd, QObject *parent)
    : QAbstractListModel(dd, parent)
{}

/*!
    Destroys the model.
*/
EnginioModelBase::~EnginioModelBase()
{}

/*!
  \enum EnginioModelBase::Roles

  EnginioModel defines roles which represent data used by every object
  stored in the Enginio backend

  \value CreatedAtRole \c When an item was created
  \value UpdatedAtRole \c When an item was updated last time
  \value IdRole \c What is the id of an item
  \value ObjectTypeRole \c What is item type
  \value SyncedRole \c Mark if an item is in sync with the backend
  \omitvalue InvalidRole
  \omitvalue LastRole

  Additionally EnginioModel supports dynamic roles which are mapped
  directly from recieved data. EnginioModel is mapping first item properties
  to role names.

  \note Some objects may not contain value for a static role, it may happen
  for example when an item is not in sync with the backend.

  \sa EnginioModelBase::roleNames()
*/

/*!
  \property EnginioModel::enginio
  \brief The EnginioClient used by the model.

  \sa EnginioClient
*/
EnginioClient *EnginioModel::enginio() const
{
    Q_D(const EnginioModel);
    return d->enginio();
}

void EnginioModel::setEnginio(const EnginioClient *enginio)
{
    Q_D(EnginioModel);
    if (enginio == d->enginio())
        return;
    d->setEnginio(enginio);
}

/*!
  \property EnginioModel::query
  \brief The query which returns the data for the model.

  Sorting preserved until insertion/deletion

  \sa EnginioClient::query()
*/
QJsonObject EnginioModel::query()
{
    Q_D(EnginioModel);
    return d->query();
}

void EnginioModel::setQuery(const QJsonObject &query)
{
    Q_D(EnginioModel);
    if (d->query() == query)
        return;
    return d->setQuery(query);
}

/*!
  \property EnginioModelBase::operation
  \brief The operation type of the query
  \sa EnginioClientBase::Operation, query()
  \return returns the Operation
*/
EnginioClientBase::Operation EnginioModelBase::operation() const
{
    Q_D(const EnginioModelBase);
    return d->operation();
}

void EnginioModelBase::setOperation(EnginioClientBase::Operation operation)
{
    Q_D(EnginioModelBase);
    if (operation == d->operation())
        return;
    d->setOperation(operation);
}

/*!
  Append \a value to this model local cache and send a create request
  to enginio backend.
  \return reply from backend
  \sa EnginioClient::create()
*/
EnginioReply *EnginioModel::append(const QJsonObject &value)
{
    Q_D(EnginioModel);
    if (Q_UNLIKELY(!d->enginio())) {
        qWarning("EnginioModel::append(): Enginio client is not set");
        return 0;
    }

    return d->append(value);
}

/*!
  Remove a value from \a row in this model local cache and send
  a remove request to enginio backend.
  \return reply from backend
  \sa EnginioClient::remove()
*/
EnginioReply *EnginioModel::remove(int row)
{
    Q_D(EnginioModel);
    if (Q_UNLIKELY(!d->enginio())) {
        qWarning("EnginioModel::remove(): Enginio client is not set");
        return 0;
    }

    if (unsigned(row) >= unsigned(d->rowCount())) {
        EnginioClientBasePrivate *client = EnginioClientBasePrivate::get(d->enginio());
        QNetworkReply *nreply = new EnginioFakeReply(client, EnginioClientBasePrivate::constructErrorMessage(EnginioString::EnginioModel_remove_row_is_out_of_range));
        EnginioReply *ereply = new EnginioReply(client, nreply);
        return ereply;
    }

    return d->remove(row);
}

/*!
  Update a value on \a row of this model's local cache
  and send an update request to the Enginio backend.

  The \a role is the property of the object that will be updated to be the new \a value.

  \return reply from backend
  \sa EnginioClient::update()
*/
EnginioReply *EnginioModel::setProperty(int row, const QString &role, const QVariant &value)
{
    Q_D(EnginioModel);
    if (Q_UNLIKELY(!d->enginio())) {
        qWarning("EnginioModel::setProperty(): Enginio client is not set");
        return 0;
    }

    if (unsigned(row) >= unsigned(d->rowCount())) {
        EnginioClientBasePrivate *client = EnginioClientBasePrivate::get(d->enginio());
        QNetworkReply *nreply = new EnginioFakeReply(client, EnginioClientBasePrivate::constructErrorMessage(EnginioString::EnginioModel_setProperty_row_is_out_of_range));
        EnginioReply *ereply = new EnginioReply(client, nreply);
        return ereply;
    }

    return d->setValue(row, role, value);
}

/*!
    \overload
    \internal
*/
Qt::ItemFlags EnginioModelBase::flags(const QModelIndex &index) const
{
    return QAbstractListModel::flags(index) | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

/*!
    \overload
    Use this function to access the model data at \a index.
    With the \l roleNames() function the mapping of JSON property names to data roles used as \a role is available.
    The data returned will be JSON (for example a string for simple objects, or a JSON Object).
*/
QVariant EnginioModelBase::data(const QModelIndex &index, int role) const
{
    Q_D(const EnginioModelBase);
    if (!index.isValid() || index.row() < 0 || index.row() >= d->rowCount())
        return QVariant();

    return d->data(index.row(), role);
}

/*!
    \overload
    \internal
*/
int EnginioModelBase::rowCount(const QModelIndex &parent) const
{
    Q_D(const EnginioModelBase);
    Q_UNUSED(parent);
    return d->rowCount();
}

/*!
    \overload
    \internal
*/
bool EnginioModelBase::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Q_D(EnginioModelBase);
    if (unsigned(index.row()) >= unsigned(d->rowCount()))
        return false;

    EnginioReplyBase *reply = d->setData(index.row(), value, role);
    QObject::connect(reply, &EnginioReplyBase::dataChanged, reply, &EnginioReply::deleteLater);
    return true;
}

/*!
    \overload
    Returns the mapping of the model's roles to names. Use this function to map
    the object property names to the role integers.

    EnginioModel uses heuristics to assign the properties of the objects in the \l query()
    to roles (greater than \l Qt::UserRole). Sometimes if the objects do not share
    the same structure, if for example a property is missing, it may happen that
    a role is missing. In such cases we recommend to overload this method to
    enforce existence of all required roles.

    \note when reimplementating this function, you need to call the base class implementation first and
    take the result into account as shown in the {todos-cpp}{Todos Example}
    \note custom role indexes have to be greater then or equal to \l EnginioModel::LastRole
*/
QHash<int, QByteArray> EnginioModelBase::roleNames() const
{
    Q_D(const EnginioModelBase);
    return d->roleNames();
}

/*!
    \internal
    Allows to disable notifications for autotests.
*/
void EnginioModelBase::disableNotifications()
{
    Q_D(EnginioModelBase);
    d->disableNotifications();
}

/*!
    \overload
    \internal
*/
void EnginioModelBase::fetchMore(const QModelIndex &parent)
{
    Q_D(EnginioModelBase);
    d->fetchMore(parent.row());
}

/*!
    \overload
    \internal
*/
bool EnginioModelBase::canFetchMore(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    Q_D(const EnginioModelBase);
    return d->canFetchMore();
}

QT_END_NAMESPACE
