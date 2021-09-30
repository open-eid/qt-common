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

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QSettings>
#include <QtGui/QIcon>
#include <QtGui/QTextDocument>
#include <QtNetwork/QNetworkProxyFactory>
#include <QtWidgets/QLabel>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#ifndef COMMON_STATIC
Common::Common( int &argc, char **argv, const QString &app, const QString &icon )
	: BaseApplication( argc, argv )
{
	setApplicationName( app );
	setApplicationVersion(QStringLiteral("%1.%2.%3.%4")
		.arg( MAJOR_VER ).arg( MINOR_VER ).arg( RELEASE_VER ).arg( BUILD_VER ) );
	setOrganizationDomain(QStringLiteral("ria.ee"));
	setOrganizationName(QStringLiteral("RIA"));
	setWindowIcon( QIcon( icon ) );
	if(QFile::exists(QStringLiteral("%1/%2.log").arg(QDir::tempPath(), app)))
		qInstallMessageHandler(msgHandler);

	Q_INIT_RESOURCE(common_tr);
#if defined(Q_OS_WIN)
	AllowSetForegroundWindow( ASFW_ANY );
	setLibraryPaths({ applicationDirPath() });
#elif defined(Q_OS_MAC)
	qputenv("OPENSSL_CONF", applicationDirPath().toUtf8() + "../Resources/openssl.cnf");
	setLibraryPaths({ applicationDirPath() + "/../PlugIns" });
#endif
	setStyleSheet(QStringLiteral(
		"QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }\n"));

	QNetworkProxyFactory::setUseSystemConfiguration(true);
}
#endif

QString Common::applicationOs()
{
#if defined(Q_OS_MAC)
	const auto version = QOperatingSystemVersion::current();
	return QStringLiteral("%1 %2.%3.%4 (%5/%6)")
		.arg(version.name())
		.arg(version.majorVersion())
		.arg(version.minorVersion())
		.arg(version.microVersion())
		.arg(QSysInfo::buildCpuArchitecture())
		.arg(QSysInfo::currentCpuArchitecture());
#elif defined(Q_OS_WIN)
	QString product = QSysInfo::productType();
	product[0] = product[0].toUpper();
	QString version = QSysInfo::productVersion();
	version.replace(QStringLiteral("server"), QStringLiteral("Server "));
	return QStringLiteral("%1 %2 %3 (%4/%5)")
		.arg(product)
		.arg(version)
		.arg(QOperatingSystemVersion::current().microVersion())
		.arg(QSysInfo::buildCpuArchitecture())
		.arg(QSysInfo::currentCpuArchitecture());
#else
	return QStringLiteral("%1 (%2/%3)").arg(
		QSysInfo::prettyProductName(),
		QSysInfo::buildCpuArchitecture(),
		QSysInfo::currentCpuArchitecture());
#endif
}

void Common::msgHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
	QFile f(QStringLiteral("%1/%2.log").arg(QDir::tempPath(), applicationName()));
	if(!f.open( QFile::Append ))
		return;
	f.write(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss ")).toUtf8());
	switch(type)
	{
	case QtDebugMsg: f.write("D"); break;
	case QtWarningMsg: f.write("W"); break;
	case QtCriticalMsg: f.write("C"); break;
	case QtFatalMsg: f.write("F"); break;
	default: f.write("I"); break;
	}
	f.write(QStringLiteral(" %1 ").arg(ctx.category).toUtf8());
	if(ctx.line > 0)
	{
		f.write(QStringLiteral("%1:%2 \"%3\" ")
			.arg(QFileInfo(ctx.file).fileName())
			.arg(ctx.line)
			.arg(ctx.function).toUtf8());
	}
	f.write(msg.toUtf8());
	f.write("\n");
}

QString Common::language()
{
	QString deflang;
	switch( QLocale().language() )
	{
	case QLocale::Russian: deflang = QStringLiteral("ru"); break;
	case QLocale::Estonian: deflang = QStringLiteral("et"); break;
	case QLocale::English:
	default: deflang = QStringLiteral("en"); break;
	}
	return QSettings().value(QStringLiteral("Language"), deflang).toString();
}
