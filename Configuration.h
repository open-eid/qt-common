/*
 * QEstEidCommon
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

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
	void update(bool force = false);

Q_SIGNALS:
	void finished(bool changed, const QString &error);

private:
	Q_DISABLE_COPY(Configuration)

	class Private;
	std::unique_ptr<Private> d;
};
