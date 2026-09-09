// SPDX-FileCopyrightText: Estonian Information System Authority
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtCore/QObject>
#include <QtCore/QStringList>

#include <functional>
#include <memory>

class QLockFile;

class Common
{
public:
	static QString applicationOs();
	static QStringList drivers();
	static std::unique_ptr<QLockFile> acquireInstanceLock(const QStringList &args);
	static bool startLocalServer(QObject *parent, std::function<void (const QStringList&)> f);
};
