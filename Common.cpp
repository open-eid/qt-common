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
#include <QtCore/QDir>
#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
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
#else
#include <wintypes.h>
#include <winscard.h>
#include <arpa/inet.h>
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

static QString serverName()
{
#ifdef Q_OS_WIN
	return QCoreApplication::applicationName();
#else
	QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	if(runtimeDir.isEmpty())
		runtimeDir = QDir::tempPath();
	return runtimeDir + QLatin1Char('/') + QCoreApplication::applicationName();
#endif
}

bool Common::sendLocalMessage(const QStringList &args)
{
	QLocalSocket socket;
	int timeout = 5000;
	for(int i = 0; i < 2; i++) {
		socket.connectToServer(serverName(), QLocalSocket::WriteOnly);
		if(socket.waitForConnected(timeout/2))
			break;
		if(i)
			return false;
		QThread::msleep(250);
	}
	QDataStream ds(&socket);
	ds << args;
	return socket.waitForBytesWritten(timeout);
}

bool Common::startLocalServer(QObject *parent, std::function<void (const QStringList&)> f)
{
	auto *server = new QLocalServer(parent);
	QObject::connect(server, &QLocalServer::newConnection, parent, [server, parent, f = std::move(f)] {
		while(QLocalSocket *socket = server->nextPendingConnection())
		{
			QObject::connect(socket, &QLocalSocket::readyRead, parent, [socket, f = std::move(f)] {
				QDataStream ds(socket);
				QStringList args;
				ds >> args;
				socket->waitForDisconnected(1000);
				socket->deleteLater();
				f(args);
			});
		}
	});
	return server->listen(serverName());
}
