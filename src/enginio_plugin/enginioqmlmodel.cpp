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

#include "enginioqmlmodel.h"
#include <Enginio/private/enginiomodelbase_p.h>
#include <QtCore/qjsondocument.h>
#include "enginioqmlclient_p.h"
#include "enginioqmlreply.h"
#include "enginioqmlobjectadaptor_p.h"


QT_BEGIN_NAMESPACE

/*!
  \qmltype EnginioModel
  \instantiates EnginioQmlModel
  \inqmlmodule Enginio 1
  \ingroup engino-qml

  \brief Makes accessing collections of objects easy
  \snippet simple.qml import

  The query property of the model takes a JSON object.

  To get a model of each object of type "objects.city" use:
  \snippet models.qml model

  It is then possible to use a regular Qt Quick ListView
  to display the list of cities that the backend contains.
  \snippet models.qml view
  Note that the properties of the objects can be directly accessed.
  In this example, we have the type "objects.city" in the backend
  with two properties: "name" and "population".

  The model supports several function to modify the data, for example
  \l append(), \l remove(), \l setProperty()

  The QML version of EnginioModel supports the same functionality as the C++ version.
  \l {EnginioModelCpp}{EnginioModel C++}
*/

/*!
  \qmlproperty QJsonObject Enginio1::EnginioModel::query
  The query used to populate the model with data.
*/

/*!
  \qmlproperty Enginio Enginio1::EnginioModel::enginio
  The instance of \l Enginio used for this model.
*/

/*!
  \qmlproperty EnginioClientBase::Operation Enginio1::EnginioModelBase::operation
  The operation used for the \l query.
*/

/*!
  \qmlmethod EnginioReply Enginio1::EnginioModel::append(QJsonObject value)
  \brief Add a new object to the model and database.

  To add a "city" object to the model by appending it:
  \snippet models.qml append
  The append will be reflected by the model immediately and will be propagated
  to the server asynchronously.

  The returned \l Enginio1::EnginioReply can be used to react to a finished object creation.
*/

/*!
  \qmlmethod EnginioReply Enginio1::EnginioModel::setProperty(int row, string propertyName, QVariant value)
  \brief Change a property of an object in the model

  The property \a propertyName of the object at \a row will be set to \a value.
  The model will locally reflect the change immediately and propagage the change to the
  server in the background.
*/

/*!
  \qmlmethod EnginioReply Enginio1::EnginioModel::remove(int row)
  \brief removes the object at \a row
*/

namespace  {

struct Types
{
    typedef EnginioQmlReply Reply;
    typedef EnginioQmlModel Public;
    typedef EnginioQmlClient Client;
    typedef EnginioQmlClientPrivate ClientPrivate;
    typedef QJSValue Data;
};

} // namespace

class EnginioQmlModelPrivate : public EnginioModelPrivateT<EnginioQmlModelPrivate, Types>
{
public:
    typedef EnginioModelPrivateT<EnginioQmlModelPrivate, Types> Base;

    QJSValue convert(const QJsonObject &object) const
    {
        // TODO that is sooo bad, that I can not look at this.
        // TODO trace all calls and try to remove them
        EnginioQmlClientPrivate *enginio = static_cast<EnginioQmlClientPrivate*>(_enginio);
        QJsonDocument doc(object);
        QByteArray buffer = doc.toJson(QJsonDocument::Compact);
        return enginio->fromJson(buffer);
    }

    QJsonObject convert(const QJSValue &object) const
    {
        // TODO that is sooo bad, that I can not look at this.
        // TODO trace all calls and try to remove them
        EnginioQmlClientPrivate *enginio = static_cast<EnginioQmlClientPrivate*>(_enginio);
        QByteArray buffer = enginio->toJson(object);
        return QJsonDocument::fromJson(buffer).object();
    }

    EnginioQmlModelPrivate(EnginioModelBase *pub)
        : Base(pub)
    {}

    virtual QJsonObject replyData(const EnginioReplyBase *reply) const Q_DECL_OVERRIDE
    {
        QJSValue data = static_cast<const EnginioQmlReply*>(reply)->data();
        return convert(data);
    }

    virtual QJsonValue queryData(const QString &name) Q_DECL_OVERRIDE
    {
        QJSValue value = _query.property(name);
        QJsonValue result;
        if (value.isObject())
            return convert(value);
        if (value.isString())
            return QJsonValue(value.toString());
        if (value.isBool())
            return QJsonValue(value.toBool());
        if (value.isNumber())
            return QJsonValue(value.toNumber());
        if (value.isUndefined())
            return QJsonValue(QJsonValue::Undefined);
        if (value.isNull())
            return QJsonValue(QJsonValue::Null);
        Q_ASSERT(false);
        return QJsonValue();
    }

    virtual QJsonObject queryAsJson() const Q_DECL_OVERRIDE
    {
        return convert(_query);
    }
};

EnginioQmlModel::EnginioQmlModel(QObject *parent)
    : EnginioModelBase(*new EnginioQmlModelPrivate(this), parent)
{
    Q_D(EnginioQmlModel);
    d->init();
    QObject::connect(this, &EnginioModelBase::rowsRemoved, this, &EnginioQmlModel::rowCountChanged);
    QObject::connect(this, &EnginioModelBase::rowsInserted, this, &EnginioQmlModel::rowCountChanged);
    QObject::connect(this, &EnginioModelBase::modelReset, this, &EnginioQmlModel::rowCountChanged);
}

EnginioQmlModel::~EnginioQmlModel()
{
}

EnginioQmlReply *EnginioQmlModel::append(const QJSValue &value)
{
    Q_D(EnginioQmlModel);

    if (Q_UNLIKELY(!d->enginio())) {
        qWarning("EnginioQmlModel::append(): Enginio client is not set");
        return 0;
    }

    return d->append(d->convert(value));
}

EnginioQmlReply *EnginioQmlModel::remove(int row)
{
    Q_D(EnginioQmlModel);
    if (Q_UNLIKELY(!d->enginio())) {
        qWarning("EnginioQmlModel::remove(): Enginio client is not set");
        return 0;
    }

    if (unsigned(row) >= unsigned(d->rowCount())) {
        EnginioQmlClientPrivate *client = EnginioQmlClientPrivate::get(d->enginio());
        QNetworkReply *nreply = new EnginioFakeReply(client, EnginioQmlClientPrivate::constructErrorMessage(EnginioString::EnginioQmlModel_remove_row_is_out_of_range));
        EnginioQmlReply *ereply = new EnginioQmlReply(client, nreply);
        return ereply;
    }

    return d->remove(row);
}

EnginioQmlReply *EnginioQmlModel::setProperty(int row, const QString &role, const QVariant &value)
{
    Q_D(EnginioQmlModel);
    if (Q_UNLIKELY(!d->enginio())) {
        qWarning("EnginioQmlModel::setProperty(): Enginio client is not set");
        return 0;
    }

    if (unsigned(row) >= unsigned(d->rowCount())) {
        EnginioQmlClientPrivate *client = EnginioQmlClientPrivate::get(d->enginio());
        QNetworkReply *nreply = new EnginioFakeReply(client, EnginioQmlClientPrivate::constructErrorMessage(EnginioString::EnginioQmlModel_setProperty_row_is_out_of_range));
        EnginioQmlReply *ereply = new EnginioQmlReply(client, nreply);
        return ereply;
    }

    return d->setValue(row, role, value);
}

EnginioQmlClient *EnginioQmlModel::enginio() const
{
    Q_D(const EnginioQmlModel);
    return d->enginio();
}

void EnginioQmlModel::setEnginio(const EnginioQmlClient *enginio)
{
    Q_D(EnginioQmlModel);
    if (enginio == d->enginio())
        return;

    d->setEnginio(enginio);
}

QJSValue EnginioQmlModel::query()
{
    Q_D(EnginioQmlModel);
    return d->query();
}

void EnginioQmlModel::setQuery(const QJSValue &query)
{
    Q_D(EnginioQmlModel);
    if (d->query().equals(query))
        return;
    return d->setQuery(query);
}

QT_END_NAMESPACE

