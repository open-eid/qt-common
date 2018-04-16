#pragma once

#include <QObject>

class ConfigurationPrivate;
class Configuration : public QObject
{
	Q_OBJECT
public:
	void checkVersion(const QString &name);
	static Configuration& instance();
	QJsonObject object() const;
	void update();

Q_SIGNALS:
	void finished(bool changed, const QString &error);

private:
	explicit Configuration(QObject *parent = 0);
	~Configuration();
	void sendRequest(const QUrl &url);

	Q_DISABLE_COPY(Configuration)

	ConfigurationPrivate *d;
};
