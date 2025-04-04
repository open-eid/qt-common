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
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QVersionNumber>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <array>

template<auto D>
struct free_deleter
{
	template<typename T>
	constexpr void operator()(T *t) const noexcept { D(t); }
};

static QVariant headerValue(const QJsonObject &obj, QLatin1String key) {
	return obj.value(QLatin1String("META-INF")).toObject().value(key);
}

static QJsonObject toObject(const QByteArray &data) {
	return QJsonDocument::fromJson(data).object();
}

static QByteArray readFile(const QString &file) {
	if(QFile f(file); f.open(QFile::ReadOnly))
		return f.readAll();
	qWarning() << "Failed to read file" << file;
	return {};
}

class Configuration::Private
{
public:
	void initCache(bool clear);
	void setData(const QByteArray &data, const QByteArray &signature);
	bool validate(const QByteArray &data, const QByteArray &signature) const;

#ifndef NO_CACHE
	QString cache = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/";
#endif
	QByteArray data, signature;
	QJsonObject dataobject;
	QUrl eccurl, url = QUrl(QStringLiteral(CONFIG_URL));
	std::unique_ptr<EVP_PKEY,free_deleter<EVP_PKEY_free>> publicKey;
	QNetworkRequest req;
	QNetworkAccessManager *net = nullptr;
#ifdef LAST_CHECK_DAYS
	QSettings s;
#endif
};

void Configuration::Private::initCache(bool clear)
{
#ifndef NO_CACHE
	auto readAll = [clear, this](const QUrl &url, const QString &copy) -> QByteArray {
		QFile f(cache + url.fileName());
		if(clear && f.exists())
			f.remove();
		if(!f.exists())
		{
			QFile::copy(copy, f.fileName());
			f.setPermissions(QFile::Permissions(0x6444));
		}
		return f.open(QFile::ReadOnly) ? f.readAll() : QByteArray();
	};
	setData(readAll(url, QStringLiteral(":/config.json")),
			readAll(eccurl, QStringLiteral(":/config.ecc")));
#else
	setData(readFile(QStringLiteral(":/config.json")),
			readFile(QStringLiteral(":/config.ecc")));
#endif
}

void Configuration::Private::setData(const QByteArray &_data, const QByteArray &_signature)
{
	data = _data;
	signature = _signature;
	dataobject = toObject(data);
	QSettings system(QSettings::SystemScope);
	for(const QString &key: system.childKeys())
	{
		if(!dataobject.contains(key))
			continue;
		QVariant value = system.value(key);
		switch(value.typeId())
		{
		case QMetaType::QString:
			dataobject[key] = QJsonValue(value.toString()); break;
		case QMetaType::QStringList:
			dataobject[key] = QJsonValue(QJsonArray::fromStringList(value.toStringList())); break;
		default: break;
		}
	}
}

bool Configuration::Private::validate(const QByteArray &data, const QByteArray &signature) const
{
	if(!publicKey || data.isEmpty())
		return false;

	QByteArray sig = QByteArray::fromBase64(signature);
	const EVP_MD *md = [&]{
		switch(EVP_PKEY_bits(publicKey.get()))
		{
		case SHA256_DIGEST_LENGTH * 8: return EVP_sha256();
		case SHA384_DIGEST_LENGTH * 8: return EVP_sha384();
		default: return EVP_sha512();
		}
	}();
	if(std::unique_ptr<EVP_MD_CTX,free_deleter<EVP_MD_CTX_free>> ctx(EVP_MD_CTX_new());
		!ctx ||
		EVP_DigestVerifyInit(ctx.get(), nullptr, md, nullptr, publicKey.get()) < 1 ||
		EVP_DigestVerify(ctx.get(), (const unsigned char*)sig.constData(), size_t(sig.size()),
			(const unsigned char*)data.constData(), size_t(data.size())) < 1)
		return false;

	QString date = headerValue(toObject(data), QLatin1String("DATE")).toString();
	return QDateTime::currentDateTimeUtc() > QDateTime::fromString(date, QStringLiteral("yyyyMMddHHmmss'Z'"));
}



Configuration::Configuration(QObject *parent)
	: QObject(parent)
	, d(new Private)
{
#ifndef NO_CACHE
	QDir().mkpath(d->cache);
#endif
	d->eccurl = QStringLiteral("%1%2.ecc").arg(
		d->url.adjusted(QUrl::RemoveFilename).toString(),
		QFileInfo(d->url.fileName()).baseName());
	d->req.setRawHeader("User-Agent", QStringLiteral("%1/%2 (%3) Lang: %4 Devices: %5")
		.arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion(),
			Common::applicationOs(), QLocale().uiLanguages().first(), Common::drivers().join('/')).toUtf8());
	d->req.setTransferTimeout();
	d->net = new QNetworkAccessManager(this);
	connect(d->net, &QNetworkAccessManager::sslErrors, this,
			[](QNetworkReply *reply, const QList<QSslError> &errors){
		reply->ignoreSslErrors(errors);
	});
	connect(d->net, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply){
		QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> replyScoped(reply);
		if(reply->error() != QNetworkReply::NoError)
		{
			Q_EMIT finished(false, reply->errorString());
			return;
		}
		if(reply->url() == d->eccurl)
		{
			QByteArray signature = reply->readAll();
			if(d->validate(d->data, signature))
			{
#ifdef LAST_CHECK_DAYS
				d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
#endif
				Q_EMIT finished(false, {});
				return;
			}
			qDebug() << "Remote signature does not match, downloading new configuration";
			d->req.setUrl(d->url);
			d->net->get(d->req)->setProperty("signature", signature);
		}
		else if(reply->url() == d->url)
		{
			QByteArray data = reply->readAll();
			QByteArray signature = reply->property("signature").toByteArray();
			if(!d->validate(data, signature))
			{
				qWarning() << "Remote configuration is invalid";
				Q_EMIT finished(false, tr("The configuration file located on the server cannot be validated."));
				return;
			}

			int newSerial = headerValue(toObject(data), QLatin1String("SERIAL")).toInt();
			int oldSerial = headerValue(object(), QLatin1String("SERIAL")).toInt();
			if(oldSerial > newSerial)
			{
				qWarning() << "Remote serial is smaller than current";
				Q_EMIT finished(false, tr("Your computer's configuration file is later than the server has."));
				return;
			}

			qDebug() << "Writing new configuration";
			d->setData(data, signature);
#ifndef NO_CACHE
			auto writeAll = [this](const QString &fileName, const QByteArray &data) {
				if(QFile f(d->cache + fileName); f.open(QFile::WriteOnly|QFile::Truncate))
					f.write(data);
			};
			writeAll(d->url.fileName(), d->data);
			writeAll(d->eccurl.fileName(), d->signature);
#endif
#ifdef LAST_CHECK_DAYS
			d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
#endif
			Q_EMIT finished(true, {});
		}
	});

	QByteArray key = readFile(QStringLiteral(":/config.ecpub"));
	BIO *bio = BIO_new_mem_buf(key.constData(), int(key.size()));
	if(!bio)
	{
		qWarning() << "Failed to parse public key";
		return;
	}

	d->publicKey.reset(PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr));
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

#ifdef LAST_CHECK_DAYS
	QDate lastCheck = QDate::fromString(d->s.value(QStringLiteral("LastCheck")).toString(), QStringLiteral("yyyyMMdd"));
	// Clean computer
	if(lastCheck.isNull()) {
		d->s.setValue(QStringLiteral("LastCheck"), QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
		update();
	}
	// Scheduled update or DigiDoc4 updated
	else if(lastCheck < QDate::currentDate().addDays(-LAST_CHECK_DAYS) ||
		QVersionNumber::fromString(QSettings().value(QStringLiteral("LastVersion")).toString()) <
		QVersionNumber::fromString(QCoreApplication::applicationVersion()))
		update();
#endif
}

Configuration::~Configuration() = default;

QJsonObject Configuration::object() const
{
	return d->dataobject;
}

void Configuration::update(bool force)
{
	d->initCache(force);
	d->req.setUrl(d->eccurl);
	d->net->get(d->req);
	QSettings().setValue(QStringLiteral("LastVersion"), QCoreApplication::applicationVersion());
}
