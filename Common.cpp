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
#include <QtCore/QProcess>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QSettings>
#include <QtGui/QIcon>
#include <QtGui/QTextDocument>
#include <QtNetwork/QNetworkProxyFactory>
#include <QtWidgets/QLabel>

#include <cstdlib>

#if defined(Q_OS_WIN)
#include <QtCore/QLibrary>
#include <qt_windows.h>
#elif defined(Q_OS_MAC)
#include <QtCore/QXmlStreamReader>
#include <sys/utsname.h>
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
	setLibraryPaths({ applicationDirPath() });
#elif defined(Q_OS_MAC)
	qputenv("OPENSSL_CONF", applicationDirPath().toUtf8() + "../Resources/openssl.cnf");
	setLibraryPaths({ applicationDirPath() + "/../PlugIns" });
#endif
	setStyleSheet(QStringLiteral(
		"QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }\n"));

	QNetworkProxyFactory::setUseSystemConfiguration(true);

#if defined(Q_OS_WIN)
	AllowSetForegroundWindow( ASFW_ANY );
#endif
}
#endif

QString Common::applicationOs()
{
#if defined(Q_OS_LINUX)
	QProcess p;
	p.start("lsb_release", { "-s", "-d" });
	p.waitForFinished();
	return QString::fromLocal8Bit( p.readAll().trimmed() );
#elif defined(Q_OS_MAC)
	struct utsname unameData;
	uname( &unameData );
	QFile f(QStringLiteral("/System/Library/CoreServices/SystemVersion.plist"));
	if( f.open( QFile::ReadOnly ) )
	{
		QXmlStreamReader xml( &f );
		while( xml.readNext() != QXmlStreamReader::Invalid )
		{
			if( !xml.isStartElement() || xml.name() != "key" || xml.readElementText() != QStringLiteral("ProductVersion"))
				continue;
			xml.readNextStartElement();
			return QStringLiteral("Mac OS %1 (%2/%3)")
				.arg( xml.readElementText() ).arg( QSysInfo::WordSize ).arg( unameData.machine );
		}
	}
#elif defined(Q_OS_WIN)
	OSVERSIONINFOEX osvi = { sizeof( OSVERSIONINFOEX ) };
	if( GetVersionEx( (OSVERSIONINFO *)&osvi ) )
	{
		bool workstation = osvi.wProductType == VER_NT_WORKSTATION;
		SYSTEM_INFO si;
		typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);
		if( PGNSI pGNSI = PGNSI( QLibrary( "kernel32" ).resolve( "GetNativeSystemInfo" ) ) )
			pGNSI( &si );
		else
			GetSystemInfo( &si );
		QString os;
		switch( (osvi.dwMajorVersion << 8) + osvi.dwMinorVersion )
		{
		case 0x0500: os = workstation ? "2000 Professional" : "2000 Server"; break;
		case 0x0501: os = osvi.wSuiteMask & VER_SUITE_PERSONAL ? "XP Home" : "XP Professional"; break;
		case 0x0502:
			if( GetSystemMetrics( SM_SERVERR2 ) )
				os = "Server 2003 R2";
			else if( workstation && si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 )
				os = "XP Professional";
			else
				os = "Server 2003";
			break;
		case 0x0600: os = workstation ? "Vista" : "Server 2008"; break;
		case 0x0601: os = workstation ? "7" : "Server 2008 R2"; break;
		case 0x0602: os = workstation ? "8" : "Server 2012"; break;
		case 0x0603: os = workstation ? "8.1" : "Server 2012 R2"; break;
		case 0x0A00: os = (workstation ? "10" : "Server 10") + QString(" %1").arg(osvi.dwBuildNumber); break;
		default: break;
		}
		QString extversion( (const QChar*)osvi.szCSDVersion );
		return QString( "Windows %1 %2(%3 bit)" ).arg( os )
			.arg( extversion.isEmpty() ? "" : extversion + " " )
			.arg( si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "64" : "32" );
	}
	else
	{
		switch( QSysInfo::WindowsVersion )
		{
		case QSysInfo::WV_2000: return "Windows 2000";
		case QSysInfo::WV_XP: return "Windows XP";
		case QSysInfo::WV_2003: return "Windows 2003";
		case QSysInfo::WV_VISTA: return "Windows Vista";
		case QSysInfo::WV_WINDOWS7: return "Windows 7";
		case QSysInfo::WV_WINDOWS8: return "Windows 8";
		case QSysInfo::WV_WINDOWS8_1: return "Windows 8.1";
		case QSysInfo::WV_WINDOWS10: return "Windows 10";
		default: break;
		}
	}
#endif

	return tr("Unknown OS");
}

QUrl Common::helpUrl()
{
	QString lang = language();
	QUrl u(QStringLiteral("http://www.id.ee/index.php?id=10583"));
	if(lang == QStringLiteral("en")) u = QStringLiteral("http://www.id.ee/index.php?id=30466");
	if(lang == QStringLiteral("ru")) u = QStringLiteral("http://www.id.ee/index.php?id=30515");
	return u;
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

void Common::setAccessibleName( QLabel *widget )
{
	QTextDocument doc;
	doc.setHtml( widget->text() );
	widget->setAccessibleName( doc.toPlainText() );
}

QString Common::language()
{
	QString deflang;
	switch( QLocale().language() )
	{
	case QLocale::Russian: deflang = "ru"; break;
	case QLocale::Estonian: deflang = "et"; break;
	case QLocale::English:
	default: deflang = "en"; break;
	}
	return QSettings().value( "Language", deflang ).toString();
}
