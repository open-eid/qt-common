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

#include "Configuration.h"

#include "Common.h"
#include "QPCSC.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QSettings>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtWidgets/QMessageBox>

#include <openssl/err.h>
#include <openssl/pem.h>

class Configuration::Private
{
public:
	void initCache(bool clear);
	static bool lessThanVersion( const QString &current, const QString &available );
	void setData(const QByteArray &_data)
	{
		data = _data;
		dataobject = QJsonDocument::fromJson(data).object();
		QSettings s2(QSettings::SystemScope, nullptr);

		for(const QString &key: s2.childKeys())
		{
			if(dataobject.contains(key))
			{
				QVariant value = s2.value(key);
				switch(value.type())
				{
				case QVariant::String:
					dataobject[key] = QJsonValue(value.toString()); break;
				case QVariant::StringList:
					dataobject[key] = QJsonValue(QJsonArray::fromStringList(value.toStringList())); break;
				default: break;
				}
			}
		}
	}
	bool validate(const QByteArray &data, const QByteArray &signature) const;

#ifndef NO_CACHE
	QString cache = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/";
#endif
	QByteArray data, signature, tmpsignature;
	QJsonObject dataobject;
	QUrl rsaurl, url = QUrl(QStringLiteral(CONFIG_URL));
	RSA *rsa = nullptr;
	QNetworkRequest req;
	QNetworkAccessManager *net = nullptr;
	QList<QNetworkReply*> requestcache;
#ifdef LAST_CHECK_DAYS
	QSettings s;
#endif
};

void Configuration::Private::initCache(bool clear)
{
#ifndef NO_CACHE
	// Signature
	QFile f(cache + rsaurl.fileName());
	if(clear && f.exists())
		f.remove();
	if(!f.exists())
	{
		QFile::copy(QStringLiteral(":/config.rsa"), f.fileName());
		f.setPermissions(QFile::Permissions(0x6444));
	}
	if(f.open(QFile::ReadOnly))
		signature = QByteArray::fromBase64(f.readAll());
	f.close();

	// Config
	f.setFileName(cache + url.fileName());
	if(clear && f.exists())
		f.remove();
	if(!f.exists())
	{
		QFile::copy(QStringLiteral(":/config.json"), f.fileName());
		f.setPermissions(QFile::Permissions(0x6444));
	}
	if(f.open(QFile::ReadOnly))
		setData(f.readAll());
	f.close();
#else
	// Signature
	QFile f(QStringLiteral(":/config.rsa"));
	if(f.open(QFile::ReadOnly))
		signature = QByteArray::fromBase64(f.readAll());
	f.close();

	// Config
	f.setFileName(QStringLiteral(":/config.json"));
	if(f.open(QFile::ReadOnly))
		setData(f.readAll());
	f.close();
#endif
}

bool Configuration::Private::lessThanVersion( const QString &current, const QString &available )
{
	QStringList curList = current.split('.');
	QStringList avaList = available.split('.');
	for( int i = 0; i < std::max<int>(curList.size(), avaList.size()); ++i )
	{
		bool curconv = false, avaconv = false;
		unsigned int cur = curList.value(i).toUInt( &curconv );
		unsigned int ava = avaList.value(i).toUInt( &avaconv );
		if( curconv && avaconv )
		{
			if( cur != ava )
				return cur < ava;
		}
		else
		{
			int status = QString::localeAwareCompare( curList.value(i), avaList.value(i) );
			if( status != 0 )
				return status < 0;
		}
	}
	return false;
}

bool Configuration::Private::validate(const QByteArray &data, const QByteArray &signature) const
{
	if(!rsa || data.isEmpty())
		return false;

	QByteArray digest(RSA_size(rsa), 0);
	int size = RSA_public_decrypt(signature.size(), (const unsigned char*)signature.constData(),
		(unsigned char*)digest.data(), rsa, RSA_PKCS1_PADDING);
	digest.resize(std::max(size, 0));

	static const QByteArray SHA1_OID = QByteArray::fromHex("3021300906052b0e03021a05000414");
	static const QByteArray SHA224_OID = QByteArray::fromHex("302d300d06096086480165030402040500041c");
	static const QByteArray SHA256_OID = QByteArray::fromHex("3031300d060960864801650304020105000420");
	static const QByteArray SHA384_OID = QByteArray::fromHex("3041300d060960864801650304020205000430");
	static const QByteArray SHA512_OID = QByteArray::fromHex("3051300d060960864801650304020305000440");
	if(digest.startsWith(SHA1_OID))
	{
		if(!digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha1)))
			return false;
	}
	else if(digest.startsWith(SHA224_OID))
	{
		if(!digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha224)))
			return false;
	}
	else if(digest.startsWith(SHA256_OID))
	{
		if(!digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha256)))
			return false;
	}
	else if(digest.startsWith(SHA384_OID))
	{
		if(!digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha384)))
			return false;
	}
	else if(digest.startsWith(SHA512_OID))
	{
		if(!digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha512)))
			return false;
	}
	else
		return false;

	QJsonObject obj = QJsonDocument::fromJson(data).object().value(QStringLiteral("META-INF")).toObject();
	return QDateTime::currentDateTimeUtc() > QDateTime::fromString(obj.value(QStringLiteral("DATE")).toString(), QStringLiteral("yyyyMMddHHmmss'Z'"));
}



Configuration::Configuration(QObject *parent)
	: QObject(parent)
	, d(new Private)
{
	Q_INIT_RESOURCE(config);

#ifndef NO_CACHE
	if(!QDir().exists(d->cache))
		QDir().mkpath(d->cache);
#endif
	d->rsaurl = QStringLiteral("%1%2.rsa").arg(
		d->url.adjusted(QUrl::RemoveFilename).toString(),
		QFileInfo(d->url.fileName()).baseName());
	d->req.setRawHeader("User-Agent", QStringLiteral("%1/%2 (%3) Lang: %4 Devices: %5")
		.arg(qApp->applicationName(), qApp->applicationVersion(),
			Common::applicationOs(), Common::language(), QPCSC::instance().drivers().join('/')).toUtf8());
	d->net = new QNetworkAccessManager(this);
	connect(d->net, &QNetworkAccessManager::sslErrors, this,
			[](QNetworkReply *reply, const QList<QSslError> &errors){
		reply->ignoreSslErrors(errors);
	});
	connect(d->net, &QNetworkAccessManager::finished, this, [=](QNetworkReply *reply){
		d->requestcache.removeAll(reply);
		switch(reply->error())
		{
		case QNetworkReply::NoError:
			if(reply->url() == d->rsaurl)
			{
				d->tmpsignature = QByteArray::fromBase64(reply->readAll());
				if(d->validate(d->data, d->tmpsignature))
				{
#ifdef LAST_CHECK_DAYS
					d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
#endif
					Q_EMIT finished(false, QString());
					break;
				}
				qDebug() << "Remote signature does not match, downloading new configuration";
				sendRequest(d->url);
			}
			else if(reply->url() == d->url)
			{
				QByteArray data = reply->readAll();
				if(!d->validate(data, d->tmpsignature))
				{
					qWarning() << "Remote configuration is invalid";
					Q_EMIT finished(false, tr("The configuration file located on the server cannot be validated."));
					break;
				}

				QJsonObject obj = QJsonDocument::fromJson(data).object().value(QStringLiteral("META-INF")).toObject();
				QJsonObject old = object().value(QStringLiteral("META-INF")).toObject();
				if(old.value(QStringLiteral("SERIAL")).toInt() > obj.value(QStringLiteral("SERIAL")).toInt())
				{
					qWarning() << "Remote serial is smaller than current";
					Q_EMIT finished(false, tr("Your computer's configuration file is later than the server has."));
					break;
				}

				qDebug() << "Writing new configuration";
				d->setData(data);
				d->signature = d->tmpsignature.toBase64();
#ifndef NO_CACHE
				QFile f(d->cache + d->url.fileName());
				if(f.exists())
					f.remove();
				if(f.open(QFile::WriteOnly))
					f.write(d->data);
				f.close();

				f.setFileName(d->cache + d->rsaurl.fileName());
				if(f.exists())
					f.remove();
				if(f.open(QFile::WriteOnly))
					f.write(d->signature);
				f.close();
#endif
#ifdef LAST_CHECK_DAYS
				d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
#endif
				Q_EMIT finished(true, QString());
			}
			break;
		default:
			Q_EMIT finished(false, reply->errorString());
			break;
		}
		reply->deleteLater();
	});

	QFile f(QStringLiteral(":/config.pub"));
	if(!f.open(QFile::ReadOnly))
	{
		qWarning() << "Failed to read public key";
		return;
	}

	QByteArray key = f.readAll();
	BIO *bio = BIO_new_mem_buf((void*)key.constData(), key.size());
	if(!bio)
	{
		qWarning() << "Failed to parse public key";
		return;
	}

	d->rsa = PEM_read_bio_RSAPublicKey(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);
	if(!d->rsa)
	{
		qWarning() << "Failed to parse public key";
		return;
	}

	d->initCache(false);
	if(!d->validate(d->data, d->signature))
	{
		qWarning() << "Config siganture is invalid, clearing cache";
		d->initCache(true);
	}
	else
	{
		int serial = object().value(QStringLiteral("META-INF")).toObject().value(QStringLiteral("SERIAL")).toInt();
		qDebug() << "Chache configuration serial:" << serial;
		QFile embedConf(QStringLiteral(":/config.json"));
		if(embedConf.open(QFile::ReadOnly))
		{
			QJsonObject obj = QJsonDocument::fromJson(embedConf.readAll()).object();
			int bundledSerial = obj.value(QStringLiteral("META-INF")).toObject().value(QStringLiteral("SERIAL")).toInt();
			qDebug() << "Bundled configuration serial:" << bundledSerial;
			if(serial < bundledSerial)
			{
				qWarning() << "Bundled configuration is recent than cache, resetting cache";
				d->initCache(true);
			}
		}
	}

	Q_EMIT finished(true, QString());

#ifdef LAST_CHECK_DAYS
	// Clean computer
	if(d->s.value(QStringLiteral("LastCheck")).isNull()) {
		d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
		update();
	}

	QDate lastCheck = QDate::fromString(d->s.value(QStringLiteral("LastCheck")).toString(), QStringLiteral("yyyyMMdd"));
	
	// Scheduled update
	if (lastCheck < QDate::currentDate().addDays(-LAST_CHECK_DAYS))
		update();

	// DigiDoc4 updated
	if (Private::lessThanVersion(QSettings().value(QStringLiteral("LastVersion")).toString(), qApp->applicationVersion()))
		update();
#endif
}

Configuration::~Configuration()
{
	if(d->rsa)
		RSA_free(d->rsa);
	delete d;
}

void Configuration::checkVersion(const QString &name)
{
	if(Private::lessThanVersion(qApp->applicationVersion(), object()[name+"-SUPPORTED"].toString()))
		Q_EMIT updateReminder(true, tr("Update is available"), tr("Your ID-software has expired. To download the latest software version, go to the "
				"<a href=\"http://installer.id.ee/?lang=eng\">id.ee</a> website. "
				"macOS users can download the latest ID-software version from the "
				"<a href=\"https://itunes.apple.com/ee/developer/ria/id556524921?mt=12\">Mac App Store</a>."));

	connect(this, &Configuration::finished, [=](bool changed, const QString &){
		if(changed && Private::lessThanVersion(qApp->applicationVersion(), object()[name+"-LATEST"].toString()))
			Q_EMIT updateReminder(false, tr("Update is available"),
				tr("An ID-software update has been found. To download the update, go to the "
					"<a href=\"http://installer.id.ee/?lang=eng\">id.ee</a> website. "
					"macOS users can download the update from the "
					"<a href=\"https://itunes.apple.com/ee/developer/ria/id556524921?mt=12\">Mac App Store</a>."));
	});
}

Configuration& Configuration::instance()
{
	static Configuration conf;
	return conf;
}

QJsonObject Configuration::object() const
{
	return d->dataobject;
}

void Configuration::sendRequest(const QUrl &url)
{
	d->req.setUrl(url);
	QNetworkReply *reply = d->net->get(d->req);
	d->requestcache << reply;
	QTimer *timer = new QTimer(this);
	timer->setSingleShot(true);
	connect(timer, &QTimer::timeout, this, [=]{
		timer->deleteLater();
		if(!d->requestcache.contains(reply))
			return;
		d->requestcache.removeAll(reply);
		reply->deleteLater();
		qDebug() << "Request timed out";
		Q_EMIT finished(false, tr("Request timed out"));
	});
	timer->start(30*1000);
}

void Configuration::update(bool force)
{
	d->initCache(force);
	sendRequest(d->rsaurl);
	QSettings().setValue(QStringLiteral("LastVersion"), qApp->applicationVersion());
}
