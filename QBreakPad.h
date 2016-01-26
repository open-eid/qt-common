/*
 * QEstEidBreakpad
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

#pragma once

#include <QtWidgets/QWizard>

class QNetworkReply;
class QPlainTextEdit;
class QProgressBar;
class QSslError;
class QTextStream;
namespace google_breakpad { class CallStack; class ExceptionHandler; }

class QBreakPad: public QObject
{
	Q_OBJECT
public:
	explicit QBreakPad(QObject *parent = 0);
	~QBreakPad();

private slots:
	void sendReports();

private:
	QString path() const;

	google_breakpad::ExceptionHandler *d;
};

class QBreakPadDialog: public QWizard
{
	Q_OBJECT
public:
	explicit QBreakPadDialog( const QString &name, const QString &path = QString() );
	~QBreakPadDialog();

private slots:
	void toggleComments();
	void toggleStack();

private:
	void handleError(QNetworkReply *reply, const QList<QSslError> &errors);
	QString parseStack() const;
	void printStack( const google_breakpad::CallStack *stack, QTextStream &s ) const;
	void updateProgress( qint64 value, qint64 range );
	bool validateCurrentPage();

	QPlainTextEdit *edit, *stack;
	QString id, file;
	QProgressBar *progress;
};
