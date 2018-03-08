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
#include "Diagnostics.h"

#include "QPCSC.h"
#include "SslCertificate.h"
#include "Common.h"

#ifdef CONFIG_URL
#include "Configuration.h"
#include <QtCore/QJsonObject>
#endif

#include <QtCore/QStringList>
#include <QtCore/QTextStream>

Diagnostics::Diagnostics() : hasAppInfo( true )
{
}

Diagnostics::Diagnostics( const QString &appInfo )
	: hasAppInfo( false ), appInfoMsg( appInfo )
{
}

void Diagnostics::appInfo(QTextStream &s) const
{
	if( hasAppInfo )
	{
		qApp->diagnostics(s);
	}
	else if ( !appInfoMsg.isEmpty() )
	{
		s << appInfoMsg;
	}
}

void Diagnostics::generalInfo(QTextStream &s) const
{
	auto app = QCoreApplication::instance();
	s << "<b>" << tr("Arguments:") << "</b> " << app->arguments().join(" ") << "<br />";
	s << "<b>" << tr("Library paths:") << "</b> " << QCoreApplication::libraryPaths().join( ";" ) << "<br />";
	s << "<b>" << "URLs:" << "</b>";
#ifdef CONFIG_URL
	s << "<br />CONFIG_URL: " << CONFIG_URL;
#endif
	appInfo(s);
	s << "<br /><br />";

#ifdef CONFIG_URL
	s << "<b>" << tr("Central Configuration") << ":</b>";
	QJsonObject metainf = Configuration::instance().object().value("META-INF").toObject();
	for(QJsonObject::const_iterator i = metainf.constBegin(), end = metainf.constEnd(); i != end; ++i)
	{
		switch(i.value().type())
		{
		case QJsonValue::Double: s << "<br />" << i.key() << ": " << i.value().toInt(); break;
		default: s << "<br />" << i.key() << ": " << i.value().toString(); break;
		}
	}
	s << "<br /><br />";
#endif

	s << "<b>" << tr("Smart Card service status: ") << "</b>" << " "
		<< (QPCSC::instance().serviceRunning() ? tr("Running") : tr("Not running"));

	s << "<br /><b>" << tr("Smart Card readers") << ":</b><br />";
	for( const QString &readername: QPCSC::instance().readers() )
	{
		s << readername;
		QPCSCReader reader( readername, &QPCSC::instance() );
		if( !reader.isPresent() )
		{
#ifndef Q_OS_WIN /* Apple 10.5.7 and pcsc-lite previous to v1.5.5 do not support 0 as protocol identifier */
			reader.connect( QPCSCReader::Direct );
#else
			reader.connect( QPCSCReader::Direct, QPCSCReader::Undefined );
#endif
		}
		else
			reader.connect();

		if( readername.contains( "EZIO SHIELD", Qt::CaseInsensitive ) )
		{
			s << " - Secure PinPad";
			if( !reader.isPinPad() )
				s << " (Driver missing)";
		}
		else if( reader.isPinPad() )
			s << " - PinPad";

		QHash<QPCSCReader::Properties,int> prop = reader.properties();
		if(prop.contains(QPCSCReader::dwMaxAPDUDataSize))
			s << " max APDU size " << prop.value(QPCSCReader::dwMaxAPDUDataSize);
		s << "<br />" << "Reader state: " << reader.state().join(", ") << "<br />";
		if( !reader.isPresent() )
			continue;

		reader.reconnect( QPCSCReader::UnpowerCard );
		QString cold = reader.atr();
		reader.reconnect( QPCSCReader::ResetCard );
		QString warm = reader.atr();

		s << "ATR cold - " << cold << "<br />"
		  << "ATR warm - " << warm << "<br />";

		reader.beginTransaction();
		#define APDU QByteArray::fromHex
		const QByteArray AID34 = APDU("00A40400 0E F04573744549442076657220312E");
		const QByteArray AID35 = APDU("00A40400 0F D23300000045737445494420763335");
		const QByteArray UPDATER_AID =	APDU("00A40400 0A D2330000005550443101");
		const QByteArray MASTER_FILE =	APDU("00A4000C");// 00"); // Compatibilty for some cards
		const QByteArray ESTEIDDF =		APDU("00A4010C 02 EEEE");
		const QByteArray PERSONALDATA =	APDU("00A4020C 02 5044");
		const QByteArray READRECORD =	APDU("00B20004 00");
		auto printAID = [&s, &reader](const QString &label, const QByteArray &apdu)
		{
			QByteArray SW = reader.transfer(apdu).SW;
			s << label << ": " << SW.toHex();
			if (SW == APDU("9000")) s << " (OK)";
			if (SW == APDU("6A81")) s << " (Locked)";
			if (SW == APDU("6A82")) s << " (Not found)";
			s << "<br />";
		};
		printAID("AID34", AID34);
		printAID("AID35", AID35);
		printAID("UPDATER_AID", UPDATER_AID);
		reader.transfer(MASTER_FILE);
		reader.transfer(ESTEIDDF);
		reader.transfer(PERSONALDATA);
		QByteArray row = READRECORD;
		row[2] = 0x07; // read card id
		s << "ID - " << reader.transfer(row).data << "<br />";
		reader.endTransaction();
	}

#ifdef Q_OS_WIN
	s << "<b>" << tr("Smart Card reader drivers") << ":</b><br />" << QPCSC::instance().drivers().join( "<br />" );
#endif
}
