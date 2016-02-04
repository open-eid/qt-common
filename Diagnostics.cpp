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

void Diagnostics::generalInfo(QTextStream &s) const
{
	s << "<b>" << tr("Arguments:") << "</b> " << qApp->arguments().join(" ") << "<br />";
	s << "<b>" << tr("Library paths:") << "</b> " << QCoreApplication::libraryPaths().join( ";" ) << "<br />";
	s << "<b>" << "URLs:" << "</b>";
#ifdef BREAKPAD
	s << "<br />BREAKPAD: " << BREAKPAD;
#endif
#ifdef CONFIG_URL
	s << "<br />CONFIG_URL: " << CONFIG_URL;
#endif
	qApp->diagnostics(s);
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
		reader.transfer( "\x00\xA4\x00\x0C\x00", 5 ); // MASTER FILE
		reader.transfer( "\x00\xA4\x01\x0C\x02\xEE\xEE", 7 ); // ESTEID DATAFILE
		reader.transfer( "\x00\xA4\x02\x04\x02\x50\x44", 7 ); // PERSONAL DATAFILE
		QString id = reader.transfer( "\x00\xB2\x07\x04\x00", 5 ).data; // read card id
		if( id.trimmed().isEmpty() )
		{
			reader.transfer( "\x00\xA4\x02\x04\x02\xAA\xCE", 7 );
			QByteArray cert;
			while( cert.size() < 0x0600 )
			{
				QByteArray cmd( "\x00\xB0\x00\x00\x00", 5 );
				cmd[2] = cert.size() >> 8;
				cmd[3] = cert.size();
				QByteArray data = reader.transfer( cmd ).data;
				if( data.isEmpty() )
					break;
				cert += data;
			}
			id = SslCertificate( cert, QSsl::Der ).subjectInfo( "serialNumber" );
		}
		reader.endTransaction();
		s << "ID - " << id << "<br />";
	}

#ifdef Q_OS_WIN
	s << "<b>" << tr("Smart Card reader drivers") << ":</b><br />" << QPCSC::instance().drivers().join( "<br />" );
#endif
}
