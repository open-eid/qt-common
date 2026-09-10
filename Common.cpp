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
#include <QtCore/QLocale>
#include <QtCore/QMutex>
#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <Regstr.h>
#include <Setupapi.h>

using namespace Qt::StringLiterals;
#elif defined(Q_OS_MAC)
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
	GUID guid {0x50dd5230L, 0xba8a, 0x11d1, 0xbf, 0x5d, 0x00, 0x00, 0xf8, 0x05, 0xf5, 0x30}; // SmartCardReader
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
	list = QString::fromLatin1(data).split('\0');
	list.removeAll({});
#endif
	return list;
}

namespace
{
	// Written when the user switches language, read when composing the header, which may
	// happen on a worker thread (e.g. certificate fetches during validation).
	QMutex languageMutex;
	QString language;
}

void Common::setLanguage(const QString &lang)
{
	QMutexLocker locker(&languageMutex);
	language = lang;
}

QByteArray Common::userAgent(bool devices)
{
	// RIA User-Agent Header Specification, schema 1:
	// AppName/AppVersion (schema=1; key=value; key=value)
	// libdigidocpp prefixes this with "LIB libdigidocpp/<version> (<arch>) APP ".
	static const QRegularExpression disallowed(QStringLiteral("[;\\x00-\\x1F\\x7F]"));
	auto value = [](QString value) {
		value = value.remove(disallowed).simplified();
		// The metadata block is an RFC 9110 comment. Quote characters that have
		// structural meaning there so arbitrary system-provided values cannot
		// terminate the comment or leave it syntactically incomplete.
		value.replace(u'\\', QStringLiteral("\\\\"));
		value.replace(u'(', QStringLiteral("\\("));
		value.replace(u')', QStringLiteral("\\)"));
		return value;
	};
	QStringList metadata {
		QStringLiteral("schema=1"),
		QStringLiteral("os=%1").arg(value(applicationOs())),
		QStringLiteral("lang=%1").arg(value([] {
			QMutexLocker locker(&languageMutex);
			return language.isEmpty() ? QLocale().bcp47Name() : language;
		}())),
		QStringLiteral("devicetype=desktop"),
	};
	if(devices)
	{
		QStringList labels;
		for(QString label: drivers())
		{
			// Comma is the devices sub-delimiter and cannot be represented inside
			// an individual label in schema 1.
			label.replace(u',', u' ');
			if(label = value(label); !label.isEmpty())
				labels.append(label);
		}
		if(!labels.isEmpty())
			metadata.append(QStringLiteral("devices=%1").arg(labels.join(QLatin1String(", "))));
	}
	return QStringLiteral("%1/%2 (%3)").arg(QCoreApplication::applicationName(),
		QCoreApplication::applicationVersion(), metadata.join(QLatin1String("; "))).toUtf8();
}
