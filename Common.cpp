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

#include "Common.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLockFile>
#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <Regstr.h>
#include <Setupapi.h>

using namespace Qt::StringLiterals;
#elifdef Q_OS_MAC
#include <PCSC/wintypes.h>
#include <PCSC/winscard.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
#include <wintypes.h>
#include <winscard.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

QString Common::applicationOs()
{
#ifdef Q_OS_MAC
	const auto version = QOperatingSystemVersion::current();
	return QLatin1String("%1 %2.%3.%4 (%5/%6)")
		.arg(version.name())
		.arg(version.majorVersion())
		.arg(version.minorVersion())
		.arg(version.microVersion())
		.arg(QSysInfo::buildCpuArchitecture())
		.arg(QSysInfo::currentCpuArchitecture());
#elifdef Q_OS_WIN
	QString product = QSysInfo::productType();
	product[0] = product[0].toUpper();
	QString version = QSysInfo::productVersion();
	version.replace(QLatin1String("server"), QLatin1String("Server "));
	QSettings s(R"(HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Session Manager\Environment)"_L1, QSettings::Registry64Format);
	return QStringLiteral("%1 %2 %3 (%4/%5)")
		.arg(product)
		.arg(version)
		.arg(QOperatingSystemVersion::current().microVersion())
		.arg(QSysInfo::buildCpuArchitecture())
		.arg(s.value("PROCESSOR_ARCHITECTURE"_L1, QSysInfo::currentCpuArchitecture()).toString());
#else
	return QStringLiteral("%1 (%2/%3)").arg(
		QSysInfo::prettyProductName(),
		QSysInfo::buildCpuArchitecture(),
		QSysInfo::currentCpuArchitecture());
#endif
}

QStringList Common::drivers()
{
	QStringList list;
#ifdef Q_OS_WIN
	static const GUID guid {0x50dd5230L, 0xba8a, 0x11d1, 0xbf, 0x5d, 0x00, 0x00, 0xf8, 0x05, 0xf5, 0x30}; // SmartCardReader
	HDEVINFO h = SetupDiGetClassDevs(&guid, nullptr, 0, DIGCF_PRESENT);
	if(!h)
		return list;

	SP_DEVINFO_DATA info { sizeof(SP_DEVINFO_DATA) };
	DWORD size = 0;
	WCHAR data[1024];
	for(DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &info); ++i)
	{
		DWORD conf = 0;
		SetupDiGetDeviceRegistryPropertyW(h, &info,
			SPDRP_CONFIGFLAGS, nullptr, LPBYTE(&conf), sizeof(conf), &size);
		if(conf & CONFIGFLAG_DISABLED)
			continue;

		SetupDiGetDeviceRegistryPropertyW(h, &info,
			SPDRP_DEVICEDESC, nullptr, LPBYTE(data), sizeof(data), &size);
		QString name = QString::fromWCharArray(data);

		SetupDiGetDeviceRegistryPropertyW(h, &info,
			SPDRP_HARDWAREID, nullptr, LPBYTE(data), sizeof(data), &size);

		list.append(QStringLiteral("%1 (%2)").arg(name, QString::fromWCharArray(data)));
	}
	SetupDiDestroyDeviceInfoList(h);
#else
	SCARDCONTEXT context{};
	SCardEstablishContext(DWORD(SCARD_SCOPE_USER), nullptr, nullptr, &context);
	if(!context)
		return list;
	DWORD size{};
	if(SCardListReaders(context, nullptr, nullptr, &size) != SCARD_S_SUCCESS || !size)
		return list;
	QByteArray data(int(size), 0);
	if(SCardListReaders(context, nullptr, data.data(), &size) != SCARD_S_SUCCESS)
		data.clear();
	SCardReleaseContext(context);
	for(const char *name = data.data(); *name; name += std::char_traits<char>::length(name) + 1)
		list.append(QString::fromLatin1(name));
#endif
	return list;
}

static QString instanceName()
{
	QString id = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
	id = id.toLower();
#endif
	const QByteArray hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
	QString name = QStringLiteral("%1-%2").arg(QCoreApplication::applicationName(), QString::fromLatin1(hash));
#ifdef Q_OS_WIN
	DWORD sessionId = 0;
	ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
	name += QLatin1Char('-') + QString::number(sessionId, 16);
#else
	name += QLatin1Char('-') + QString::number(uint(::getuid()), 16);
#endif
	return name;
}

static QString tempDir()
{
#ifdef Q_OS_WIN
	return QDir::tempPath();
#else
	QString path = QDir::tempPath() + QLatin1Char('/') + QString::number(uint(::getuid()));
	QFileInfo info(path);
	if(info.exists())
		return !info.isSymLink() && info.isDir() && info.ownerId() == uint(::getuid()) ? path : QDir::tempPath();
	return QDir().mkdir(path) && QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner) ?
		path : QDir::tempPath();
#endif
}

static QString runtimePath(const QString &fileName)
{
	QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	if(runtimeDir.isEmpty())
		runtimeDir = tempDir();
	return runtimeDir + QLatin1Char('/') + fileName;
}

static QString serverName()
{
#ifdef Q_OS_WIN
	return instanceName();
#else
	return runtimePath(instanceName());
#endif
}

std::unique_ptr<QLockFile> Common::acquireInstanceLock(const QStringList &args)
{
	auto lockFile = std::make_unique<QLockFile>(runtimePath(instanceName() + QStringLiteral("-lockfile")));
	if(lockFile->tryLock(500))
		return lockFile;

	QLocalSocket socket;
	int timeout = 5000;
	for(int i = 0; i < 2; i++) {
		socket.connectToServer(serverName(), QIODevice::ReadWrite);
		if(socket.waitForConnected(timeout/2))
			break;
		if(i)
		{
			qWarning() << "Failed to connect to running instance:" << socket.errorString();
			return {};
		}
		QThread::msleep(250);
	}
	QDataStream ds(&socket);
	ds << args;
	if(!socket.waitForBytesWritten(timeout) || !socket.waitForReadyRead(timeout) || socket.read(3) != "ack")
		qWarning() << "Running instance did not acknowledge activation message";
	return {};
}

bool Common::startLocalServer(QObject *parent, std::function<void (const QStringList&)> f)
{
	auto *server = new QLocalServer(parent);
	server->setSocketOptions(QLocalServer::UserAccessOption);
	QObject::connect(server, &QLocalServer::newConnection, parent, [server, parent, f = std::move(f)] {
		while(QLocalSocket *socket = server->nextPendingConnection())
		{
			constexpr qint64 maxFrameSize = 64 * 1024;
			QObject::connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);

			auto *timeout = new QTimer(socket);
			timeout->setSingleShot(true);
			QObject::connect(timeout, &QTimer::timeout, socket, &QLocalSocket::abort);
			timeout->start(5000);

			QObject::connect(socket, &QLocalSocket::readyRead, parent, [socket, f] {
				if(socket->bytesAvailable() > maxFrameSize)
				{
					socket->abort();
					return;
				}
				QDataStream ds(socket);
				ds.startTransaction();
				QStringList args;
				ds >> args;
				if(!ds.commitTransaction())
					return;
				socket->write("ack", 3);
				socket->waitForBytesWritten(1000);
				socket->waitForDisconnected(1000);
				socket->deleteLater();
				f(args);
			});
		}
	});
	const QString name = serverName();
	if(server->listen(name))
		return true;
#ifndef Q_OS_WIN
	if(server->serverError() == QAbstractSocket::AddressInUseError)
	{
		QFileInfo info(name);
		if(info.isSymLink() || !info.exists() || info.ownerId() != uint(::getuid()))
			return false;
		QFile::remove(name);
		return server->listen(name);
	}
#endif
	return false;
}
