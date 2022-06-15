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

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <memory>

#define SCOPE(TYPE, DATA) std::unique_ptr<TYPE,decltype(&TYPE##_free)>(static_cast<TYPE*>(DATA), TYPE##_free)

static QVariant headerValue(const QJsonObject &obj, QLatin1String key) {
	return obj.value(QLatin1String("META-INF")).toObject().value(key);
}

static QJsonObject toObject(const QByteArray &data) {
	return QJsonDocument::fromJson(data).object();
}

static QByteArray readFile(const QString &file) {
	QFile f(file);
	if(f.open(QFile::ReadOnly))
		return f.readAll();
	qWarning() << "Failed to read file" << file;
	return {};
}

class Configuration::Private
{
public:
	void initCache(bool clear);
	static bool lessThanVersion( const QString &current, const QString &available );
	void setData(const QByteArray &data, const QByteArray &signature);
	bool validate(const QByteArray &data, const QByteArray &signature) const;

#ifndef NO_CACHE
	QString cache = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/";
#endif
	QByteArray data, signature;
	QJsonObject dataobject;
	QUrl rsaurl, url = QUrl(QStringLiteral(CONFIG_URL));
	EVP_PKEY *publicKey = nullptr;
	QNetworkRequest req;
	QNetworkAccessManager *net = nullptr;
#ifdef LAST_CHECK_DAYS
	QSettings s;
#endif
};

void Configuration::Private::initCache(bool clear)
{
#ifndef NO_CACHE
	auto readAll = [clear, this](const QString &fileName, const QString &copy) {
		QFile f(cache + fileName);
		if(clear && f.exists())
			f.remove();
		if(!f.exists())
		{
			QFile::copy(copy, f.fileName());
			f.setPermissions(QFile::Permissions(0x6444));
		}
		return f.open(QFile::ReadOnly) ? f.readAll() : QByteArray();
	};
	setData(readAll(url.fileName(), QStringLiteral(":/config.json")),
			readAll(rsaurl.fileName(), QStringLiteral(":/config.rsa")));
#else
	setData(readFile(QStringLiteral(":/config.json")),
			readFile(QStringLiteral(":/config.rsa")));
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

void Configuration::Private::setData(const QByteArray &_data, const QByteArray &_signature)
{
	data = _data;
	signature = _signature;
	dataobject = toObject(data);
#ifdef Q_OS_MAC
	QSettings s2(QSettings::SystemScope, nullptr);
#else
	QSettings s2(QSettings::SystemScope, qApp->organizationName(), qApp->applicationName());
#endif

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

bool Configuration::Private::validate(const QByteArray &data, const QByteArray &signature) const
{
	if(!publicKey || data.isEmpty())
		return false;

	QByteArray sig = QByteArray::fromBase64(signature);
	size_t size = 0;
	auto ctx = SCOPE(EVP_PKEY_CTX, EVP_PKEY_CTX_new(publicKey, nullptr));
	if(!ctx || EVP_PKEY_verify_recover_init(ctx.get()) < 1 ||
		EVP_PKEY_verify_recover(ctx.get(), nullptr, &size,
			(const unsigned char*)sig.constData(), size_t(sig.size())) < 1)
		return false;
	QByteArray digest(int(size), '\0');
	if(EVP_PKEY_verify_recover(ctx.get(), (unsigned char*)digest.data(), &size,
			(const unsigned char*)sig.constData(), size_t(sig.size())) < 1)
		return false;
	digest.resize(int(size));

	static const QByteArray SHA1_OID = QByteArray::fromHex("3021300906052b0e03021a05000414");
	static const QByteArray SHA224_OID = QByteArray::fromHex("302d300d06096086480165030402040500041c");
	static const QByteArray SHA256_OID = QByteArray::fromHex("3031300d060960864801650304020105000420");
	static const QByteArray SHA384_OID = QByteArray::fromHex("3041300d060960864801650304020205000430");
	static const QByteArray SHA512_OID = QByteArray::fromHex("3051300d060960864801650304020305000440");
	if(!(digest.startsWith(SHA1_OID) && digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha1))) &&
		!(digest.startsWith(SHA224_OID) && digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha224))) &&
		!(digest.startsWith(SHA256_OID) && digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha256))) &&
		!(digest.startsWith(SHA384_OID) && digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha384))) &&
		!(digest.startsWith(SHA512_OID) && digest.endsWith(QCryptographicHash::hash(data, QCryptographicHash::Sha512))))
		return false;
	QString date = headerValue(toObject(data), QLatin1String("DATE")).toString();
	return QDateTime::currentDateTimeUtc() > QDateTime::fromString(date, QStringLiteral("yyyyMMddHHmmss'Z'"));
}



Configuration::Configuration(QObject *parent)
	: QObject(parent)
	, d(new Private)
{
	Q_INIT_RESOURCE(config);

#ifndef NO_CACHE
	QDir().mkpath(d->cache);
#endif
	d->rsaurl = QStringLiteral("%1%2.rsa").arg(
		d->url.adjusted(QUrl::RemoveFilename).toString(),
		QFileInfo(d->url.fileName()).baseName());
	d->req.setRawHeader("User-Agent", QStringLiteral("%1/%2 (%3) Lang: %4 Devices: %5")
		.arg(qApp->applicationName(), qApp->applicationVersion(),
			Common::applicationOs(), Common::language(), QPCSC::instance().drivers().join('/')).toUtf8());
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
	d->req.setTransferTimeout();
#endif
	d->net = new QNetworkAccessManager(this);
	connect(d->net, &QNetworkAccessManager::sslErrors, this,
			[](QNetworkReply *reply, const QList<QSslError> &errors){
		reply->ignoreSslErrors(errors);
	});
	connect(d->net, &QNetworkAccessManager::finished, this, [=](QNetworkReply *reply){
		switch(reply->error())
		{
		case QNetworkReply::NoError:
			if(reply->url() == d->rsaurl)
			{
				QByteArray signature = reply->readAll();
				if(d->validate(d->data, signature))
				{
#ifdef LAST_CHECK_DAYS
					d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
#endif
					Q_EMIT finished(false, {});
					break;
				}
				qDebug() << "Remote signature does not match, downloading new configuration";
				sendRequest(d->url)->setProperty("signature", signature);
			}
			else if(reply->url() == d->url)
			{
				QByteArray data = reply->readAll();
				QByteArray signature = reply->property("signature").toByteArray();
				if(!d->validate(data, signature))
				{
					qWarning() << "Remote configuration is invalid";
					Q_EMIT finished(false, tr("The configuration file located on the server cannot be validated."));
					break;
				}

				int newSerial = headerValue(toObject(data), QLatin1String("SERIAL")).toInt();
				int oldSerial = headerValue(object(), QLatin1String("SERIAL")).toInt();
				if(oldSerial > newSerial)
				{
					qWarning() << "Remote serial is smaller than current";
					Q_EMIT finished(false, tr("Your computer's configuration file is later than the server has."));
					break;
				}

				qDebug() << "Writing new configuration";
				d->setData(data, signature);
#ifndef NO_CACHE
				auto writeAll = [this](const QString &fileName, const QByteArray &data) {
					QFile f(d->cache + fileName);
					if(f.open(QFile::WriteOnly|QFile::Truncate))
						f.write(data);
				};
				writeAll(d->url.fileName(), d->data);
				writeAll(d->rsaurl.fileName(), d->signature);
#endif
#ifdef LAST_CHECK_DAYS
				d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
#endif
				Q_EMIT finished(true, {});
			}
			break;
		default:
			Q_EMIT finished(false, reply->errorString());
			break;
		}
		reply->deleteLater();
	});

	QByteArray key = readFile(QStringLiteral(":/config.pub"));
	BIO *bio = BIO_new_mem_buf(key.constData(), key.size());
	if(!bio)
	{
		qWarning() << "Failed to parse public key";
		return;
	}

#if OPENSSL_VERSION < 0x30000000L
	RSA *rsa = PEM_read_bio_RSAPublicKey(bio, nullptr, nullptr, nullptr);
	d->publicKey = EVP_PKEY_new();
	EVP_PKEY_set1_RSA(d->publicKey, rsa);
	RSA_free(rsa);
#else
	d->publicKey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
#endif
	BIO_free(bio);
	if(!d->publicKey)
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
		int serial = headerValue(object(), QLatin1String("SERIAL")).toInt();
		qDebug() << "Chache configuration serial:" << serial;
		int bundledSerial = headerValue(toObject(readFile(QStringLiteral(":/config.json"))), QLatin1String("SERIAL")).toInt();
		qDebug() << "Bundled configuration serial:" << bundledSerial;
		if(serial < bundledSerial)
		{
			qWarning() << "Bundled configuration is recent than cache, resetting cache";
			d->initCache(true);
		}
	}

	Q_EMIT finished(true, {});

#ifdef LAST_CHECK_DAYS
	QDate lastCheck = QDate::fromString(d->s.value(QStringLiteral("LastCheck")).toString(), QStringLiteral("yyyyMMdd"));
	// Clean computer
	if(lastCheck.isNull()) {
		d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
		update();
	}
	// Scheduled update
	else if(lastCheck < QDate::currentDate().addDays(-LAST_CHECK_DAYS))
		update();
	// DigiDoc4 updated
	else if(Private::lessThanVersion(QSettings().value(QStringLiteral("LastVersion")).toString(), qApp->applicationVersion()))
		update();
#endif
}

Configuration::~Configuration()
{
	if(d->publicKey)
		EVP_PKEY_free(d->publicKey);
	delete d;
}

void Configuration::checkVersion(const QString &name)
{
	if(Private::lessThanVersion(qApp->applicationVersion(), object()[name+"-SUPPORTED"].toString()))
		Q_EMIT updateReminder(true, tr("Update is available"), tr("Your ID-software has expired. To download the latest software version, go to the "
				"<a href=\"https://www.id.ee/en/article/install-id-software/\">id.ee</a> website. "
				"macOS users can download the latest ID-software version from the "
				"<a href=\"https://itunes.apple.com/ee/developer/ria/id556524921?mt=12\">Mac App Store</a>."));

	connect(this, &Configuration::finished, this, [=](bool changed, const QString &){
		if(changed && Private::lessThanVersion(qApp->applicationVersion(), object()[name+"-LATEST"].toString()))
			Q_EMIT updateReminder(false, tr("Update is available"),
				tr("An ID-software update has been found. To download the update, go to the "
					"<a href=\"https://www.id.ee/en/article/install-id-software/\">id.ee</a> website. "
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

QNetworkReply* Configuration::sendRequest(const QUrl &url)
{
	d->req.setUrl(url);
	QNetworkReply *reply = d->net->get(d->req);
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
	if(!reply->isRunning())
		return reply;
	QTimer *timer = new QTimer(this);
	timer->setSingleShot(true);
	connect(reply, &QNetworkReply::finished, timer, [=]{
		timer->stop();
		timer->deleteLater();
	});
	connect(timer, &QTimer::timeout, this, [=]{
		timer->deleteLater();
		reply->abort();
		qDebug() << "Request timed out";
		Q_EMIT finished(false, tr("Request timed out"));
	});
	timer->start(30*1000);
#endif
	return reply;
}

void Configuration::update(bool force)
{
	d->initCache(force);
	sendRequest(d->rsaurl);
	QSettings().setValue(QStringLiteral("LastVersion"), qApp->applicationVersion());
}
