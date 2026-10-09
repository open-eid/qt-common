// SPDX-FileCopyrightText: Estonian Information System Authority
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>

class QNetworkReply;

class Configuration final : public QObject
{
	Q_OBJECT
public:
	explicit Configuration(QObject *parent = nullptr);
	~Configuration() final;
	QJsonObject object() const;
	QJsonObject rawObject() const;
	void update(bool force = false);

Q_SIGNALS:
	void finished(bool changed, const QString &error);

private:
	Q_DISABLE_COPY(Configuration)

	class Private;
	std::unique_ptr<Private> d;
};
