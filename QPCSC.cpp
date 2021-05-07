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

#include "QPCSC.h"
#include "QPCSC_p.h"

#include <QtCore/QDateTime>
#include <QtCore/QLoggingCategory>
#include <QtCore/QStringList>
#include <QtCore/QtEndian>

#include <cstring>

#ifdef Q_OS_WIN
#include <Regstr.h>
#include <Setupapi.h>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#define QT_HEX Qt::hex
#else
#define QT_HEX hex
#endif


Q_LOGGING_CATEGORY(APDU,"QPCSC.APDU")
Q_LOGGING_CATEGORY(SCard,"QPCSC.SCard")

template < typename Func, typename... Args>
LONG SCCall( const char *file, int line, const char *function, Func func, Args... args)
{
	LONG err = func(args...);
	if(SCard().isDebugEnabled())
		QMessageLogger(file, line, function, SCard().categoryName()).debug()
			<< function << QT_HEX << (unsigned long)err;
	return err;
}
#define SC(API, ...) SCCall(__FILE__, __LINE__, "SCard"#API, SCard##API, __VA_ARGS__)

QByteArray QPCSCReader::Private::attrib( DWORD id ) const
{
	if(!card)
		return {};
	DWORD size = 0;
	LONG err = SC(GetAttrib, card, id, nullptr, &size);
	if( err != SCARD_S_SUCCESS || !size )
		return {};
	QByteArray data(int(size), 0);
	err = SC(GetAttrib, card, id, LPBYTE(data.data()), &size);
	if( err != SCARD_S_SUCCESS || !size )
		return {};
	return data;
}

QHash<DRIVER_FEATURES,quint32> QPCSCReader::Private::features()
{
	if(!featuresList.isEmpty())
		return featuresList;
	DWORD size = 0;
	BYTE feature[256];
	LONG rv = SC(Control, card, DWORD(CM_IOCTL_GET_FEATURE_REQUEST), nullptr, 0u, feature, DWORD(sizeof(feature)), &size);
	if(rv != SCARD_S_SUCCESS)
		return featuresList;
	for(unsigned char *p = feature; DWORD(p-feature) < size; )
	{
		unsigned int tag = *p++, len = *p++, value = 0;
		for(unsigned int i = 0; i < len; ++i)
			value |= *p++ << 8 * i;
		featuresList[DRIVER_FEATURES(tag)] = qFromBigEndian<quint32>(value);
	}
	return featuresList;
}



QPCSC::QPCSC()
	: d( new QPCSCPrivate )
{
	const_cast<QLoggingCategory&>(SCard()).setEnabled(QtDebugMsg, qEnvironmentVariableIsSet("PCSC_DEBUG"));
	const_cast<QLoggingCategory&>(APDU()).setEnabled(QtDebugMsg, qEnvironmentVariableIsSet("APDU_DEBUG"));
	serviceRunning();
}

QPCSC::~QPCSC()
{
	if( d->context )
		SC(ReleaseContext, d->context);
	qDeleteAll(d->lock);
	delete d;
}

QStringList QPCSC::drivers() const
{
#ifdef Q_OS_WIN
	HDEVINFO h = SetupDiGetClassDevs( 0, 0, 0, DIGCF_ALLCLASSES | DIGCF_PRESENT );
	if( !h )
		return {};

	SP_DEVINFO_DATA info = { sizeof(SP_DEVINFO_DATA) };
	QStringList list;
	for( DWORD i = 0; SetupDiEnumDeviceInfo( h, i, &info ); i++ )
	{
		DWORD size = 0;
		WCHAR data[1024];

		SetupDiGetDeviceRegistryPropertyW( h, &info,
			SPDRP_CLASS, 0, LPBYTE(data), sizeof(data), &size );
		if( _wcsicmp( data, L"SmartCardReader" ) != 0 )
			continue;

		DWORD conf = 0;
		SetupDiGetDeviceRegistryPropertyW( h, &info,
			SPDRP_CONFIGFLAGS, 0, LPBYTE(&conf), sizeof(conf), &size );
		if( conf & CONFIGFLAG_DISABLED )
			continue;

		SetupDiGetDeviceRegistryPropertyW( h, &info,
			SPDRP_DEVICEDESC, 0, LPBYTE(data), sizeof(data), &size );
		QString name = QString::fromWCharArray(data);

		SetupDiGetDeviceRegistryPropertyW( h, &info,
			SPDRP_HARDWAREID, 0, LPBYTE(data), sizeof(data), &size );

		list << QStringLiteral("%1 (%2)").arg(name, QString::fromWCharArray(data));
	}
	SetupDiDestroyDeviceInfoList( h );

	return list;
#else
	return readers();
#endif
}

QPCSC& QPCSC::instance()
{
	static QPCSC pcsc;
	return pcsc;
}

QStringList QPCSC::readers() const
{
	if( !serviceRunning() )
		return {};

	DWORD size = 0;
	LONG err = SC(ListReaders, d->context, nullptr, nullptr, &size);
	if( err != SCARD_S_SUCCESS || !size )
		return {};

	QByteArray data(int(size), 0);
	err = SC(ListReaders, d->context, nullptr, data.data(), &size);
	if( err != SCARD_S_SUCCESS )
		return {};

	QStringList readers = QString::fromLocal8Bit(data, int(size)).split(QChar(0));
	readers.removeAll(QString());
	return readers;
}

bool QPCSC::serviceRunning() const
{
	if(d->context && SC(IsValidContext, d->context) == SCARD_S_SUCCESS)
		return true;
	SC(EstablishContext, DWORD(SCARD_SCOPE_USER), nullptr, nullptr, &d->context);
	return d->context;
}



QPCSCReader::QPCSCReader( const QString &reader, QPCSC *parent )
	: d(new Private)
{
	if(!parent->d->lock.contains(reader))
		parent->d->lock[reader] = new QMutex();
	parent->d->lock[reader]->lock();
	d->d = parent->d;
	d->reader = reader.toUtf8();
	d->state.szReader = d->reader.constData();
	updateState();
}

QPCSCReader::~QPCSCReader()
{
	disconnect();
	d->d->lock[d->reader]->unlock();
	delete d;
}

QByteArray QPCSCReader::atr() const
{
	return QByteArray::fromRawData((const char*)d->state.rgbAtr, int(d->state.cbAtr)).toHex().toUpper();
}

bool QPCSCReader::beginTransaction()
{
	return d->isTransacted = SC(BeginTransaction, d->card) == SCARD_S_SUCCESS;
}

bool QPCSCReader::connect(Connect connect, Mode mode)
{
	return connectEx(connect, mode) == SCARD_S_SUCCESS;
}

quint32 QPCSCReader::connectEx(Connect connect, Mode mode)
{
	LONG err = SC(Connect, d->d->context, d->state.szReader, connect, mode, &d->card, &d->io.dwProtocol);
	updateState();
	return quint32(err);
}

void QPCSCReader::disconnect( Reset reset )
{
	if(d->isTransacted)
		endTransaction();
	if( d->card )
		SC(Disconnect, d->card, reset);
	d->io.dwProtocol = SCARD_PROTOCOL_UNDEFINED;
	d->card = 0;
	d->featuresList.clear();
	updateState();
}

bool QPCSCReader::endTransaction( Reset reset )
{
	bool result = SC(EndTransaction, d->card, reset) == SCARD_S_SUCCESS;
	if(result)
		d->isTransacted = false;
	return result;
}

QString QPCSCReader::friendlyName() const
{
	return QString::fromLocal8Bit( d->attrib( SCARD_ATTR_DEVICE_FRIENDLY_NAME_A ) );
}

bool QPCSCReader::isConnected() const
{
	return d->card;
}

bool QPCSCReader::isPinPad() const
{
	if(d->reader.contains("HID Global OMNIKEY 3x21 Smart Card Reader") ||
		d->reader.contains("HID Global OMNIKEY 6121 Smart Card Reader"))
		return false;
	if(qEnvironmentVariableIsSet("SMARTCARDPP_NOPINPAD"))
		return false;
	QHash<DRIVER_FEATURES,quint32> features = d->features();
	return features.contains(FEATURE_VERIFY_PIN_DIRECT) || features.contains(FEATURE_VERIFY_PIN_START);
}

bool QPCSCReader::isPresent() const
{
	return d->state.dwEventState & SCARD_STATE_PRESENT;
}

QString QPCSCReader::name() const
{
	return QString::fromLocal8Bit( d->reader );
}

QHash<QPCSCReader::Properties, int> QPCSCReader::properties() const
{
	QHash<Properties,int> properties;
	if( DWORD ioctl = d->features().value(FEATURE_GET_TLV_PROPERTIES) )
	{
		DWORD size = 0;
		BYTE recv[256];
		if(SC(Control, d->card, ioctl, nullptr, 0u, recv, DWORD(sizeof(recv)), &size) != SCARD_S_SUCCESS)
			return properties;
		for(unsigned char *p = recv; DWORD(p-recv) < size; )
		{
			int tag = *p++, len = *p++, value = 0;
			for(int i = 0; i < len; ++i)
				value |= *p++ << 8 * i;
			properties[Properties(tag)] = value;
		}
	}
	return properties;
}

int QPCSCReader::protocol() const
{
	return int(d->io.dwProtocol);
}

bool QPCSCReader::reconnect( Reset reset, Mode mode )
{
	if( !d->card )
		return false;
	LONG err = SC(Reconnect, d->card, DWORD(SCARD_SHARE_SHARED), mode, reset, &d->io.dwProtocol);
	updateState();
	return err == SCARD_S_SUCCESS;
}

QStringList QPCSCReader::state() const
{
	QStringList result;
#define STATE(X) if( d->state.dwEventState & SCARD_STATE_##X ) result << QStringLiteral(#X)
	STATE(IGNORE);
	STATE(CHANGED);
	STATE(UNKNOWN);
	STATE(UNAVAILABLE);
	STATE(EMPTY);
	STATE(PRESENT);
	STATE(ATRMATCH);
	STATE(EXCLUSIVE);
	STATE(INUSE);
	STATE(MUTE);
	return result;
}

QPCSCReader::Result QPCSCReader::transfer( const char *cmd, int size ) const
{
	return transfer( QByteArray::fromRawData( cmd, size ) );
}

QPCSCReader::Result QPCSCReader::transfer( const QByteArray &apdu ) const
{
	QByteArray data( 1024, 0 );
	DWORD size = DWORD(data.size());

	qCDebug(APDU).nospace() << 'T' << d->io.dwProtocol - 1 << "> " << apdu.toHex().constData();
	LONG ret = SC(Transmit, d->card, &d->io,
		LPCBYTE(apdu.constData()), DWORD(apdu.size()), nullptr, LPBYTE(data.data()), &size);
	if( ret != SCARD_S_SUCCESS )
		return { {}, {}, quint32(ret) };

	Result result = { data.mid(int(size - 2), 2), data.left(int(size - 2)), quint32(ret) };
	qCDebug(APDU).nospace() << 'T' << d->io.dwProtocol - 1 << "< " << result.SW.toHex().constData();
	if(!result.data.isEmpty()) qCDebug(APDU).nospace() << data.left(int(size)).toHex().constData();

	switch(result.SW.at(0))
	{
	case 0x61: // Read more
	{
		QByteArray cmd( "\x00\xC0\x00\x00\x00", 5 );
		cmd[4] = data.at(int(size - 1));
		Result result2 = transfer( cmd );
		result2.data.prepend(result.data);
		return result2;
	}
	case 0x6C: // Excpected lenght
	{
		QByteArray cmd = apdu;
		cmd[4] = result.SW.at(1);
		return transfer(cmd);
	}
	default: return result;
	}
}

QPCSCReader::Result QPCSCReader::transferCTL(const QByteArray &apdu, bool verify,
	quint16 lang, quint8 minlen, quint8 newPINOffset, bool requestCurrentPIN) const
{
	bool display = false;
	QHash<DRIVER_FEATURES,quint32> features = d->features();
	if( DWORD ioctl = features.value(FEATURE_IFD_PIN_PROPERTIES) )
	{
		DWORD size = 0;
		BYTE recv[256];
		LONG rv = SC(Control, d->card, ioctl, nullptr, 0u, recv, DWORD(sizeof(recv)), &size);
		if( rv == SCARD_S_SUCCESS )
		{
			PIN_PROPERTIES_STRUCTURE *caps = (PIN_PROPERTIES_STRUCTURE *)recv;
			display = caps->wLcdLayout > 0;
		}
	}

	quint8 PINFrameOffset = 0, PINLengthOffset = 0;
	#define SET() \
		data->bTimerOut = 30; \
		data->bTimerOut2 = 30; \
		data->bmFormatString = FormatASCII|AlignLeft|quint8(PINFrameOffset << 4)|PINFrameOffsetUnitBits; \
		data->bmPINBlockString = PINLengthNone << 5|PINFrameSizeAuto; \
		data->bmPINLengthFormat = PINLengthOffsetUnitBits|PINLengthOffset; \
		data->wPINMaxExtraDigit = quint16(minlen << 8) | 12; \
		data->bEntryValidationCondition = ValidOnKeyPressed; \
		data->wLangId = lang; \
		data->bTeoPrologue[0] = 0x00; \
		data->bTeoPrologue[1] = 0x00; \
		data->bTeoPrologue[2] = 0x00; \
		data->ulDataLength = quint32(apdu.size())

	QByteArray cmd( 255, 0 );
	if( verify )
	{
		PIN_VERIFY_STRUCTURE *data = (PIN_VERIFY_STRUCTURE*)cmd.data();
		SET();
		data->bNumberMessage = display ? CCIDDefaultInvitationMessage : NoInvitationMessage;
		data->bMsgIndex = NoInvitationMessage;
		cmd.resize( sizeof(PIN_VERIFY_STRUCTURE) - 1 );
	}
	else
	{
		PIN_MODIFY_STRUCTURE *data = (PIN_MODIFY_STRUCTURE*)cmd.data();
		SET();
		data->bNumberMessage = display ? ThreeInvitationMessage : NoInvitationMessage;
		data->bInsertionOffsetOld = 0x00;
		data->bInsertionOffsetNew = newPINOffset;
		data->bConfirmPIN = ConfirmNewPin;
		if(requestCurrentPIN)
		{
			data->bConfirmPIN |= RequestCurrentPin;
			data->bMsgIndex1 = NoInvitationMessage;
			data->bMsgIndex2 = OneInvitationMessage;
			data->bMsgIndex3 = TwoInvitationMessage;
		}
		else
		{
			data->bMsgIndex1 = OneInvitationMessage;
			data->bMsgIndex2 = TwoInvitationMessage;
			data->bMsgIndex3 = ThreeInvitationMessage;
		}
		cmd.resize( sizeof(PIN_MODIFY_STRUCTURE) - 1 );
	}
	cmd += apdu;

	DWORD ioctl = features.value( verify ? FEATURE_VERIFY_PIN_START : FEATURE_MODIFY_PIN_START );
	if( !ioctl )
		ioctl = features.value( verify ? FEATURE_VERIFY_PIN_DIRECT : FEATURE_MODIFY_PIN_DIRECT );

	qCDebug(APDU).nospace() << 'T' << d->io.dwProtocol - 1 << "> " << apdu.toHex().constData();
	qCDebug(APDU).nospace() << "CTL" << "> " << cmd.toHex().constData();
	QByteArray data( 255 + 3, 0 );
	DWORD size = DWORD(data.size());
	LONG err = SC(Control, d->card, ioctl, cmd.constData(), DWORD(cmd.size()), LPVOID(data.data()), DWORD(data.size()), &size);

	if( DWORD finish = features.value( verify ? FEATURE_VERIFY_PIN_FINISH : FEATURE_MODIFY_PIN_FINISH ) )
	{
		size = DWORD(data.size());
		err = SC(Control, d->card, finish, nullptr, 0u, LPVOID(data.data()), DWORD(data.size()), &size);
	}

	Result result = { data.mid(int(size - 2), 2), data.left(int(size - 2)), quint32(err) };
	qCDebug(APDU).nospace() << 'T' << d->io.dwProtocol - 1 << "< " << result.SW.toHex().constData();
	if(!result.data.isEmpty()) qCDebug(APDU).nospace() << data.left(int(size)).toHex().constData();
	return result;
}

bool QPCSCReader::updateState( quint32 msec )
{
	if(!d->d->context)
		return false;
	d->state.dwCurrentState = d->state.dwEventState; //(currentReaderCount << 16)
	DWORD err = SC(GetStatusChange, d->d->context, msec, &d->state, 1u); //INFINITE
	switch(err) {
	case SCARD_S_SUCCESS: return true;
	case SCARD_E_TIMEOUT: return msec == 0;
	default: return false;
	}
}
