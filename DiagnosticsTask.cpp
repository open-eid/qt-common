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
#include "DiagnosticsTask.h"

#include <cstdio>
#include <QFile>
#include <QTextDocument>
#include <QTextStream>


DiagnosticsTask::DiagnosticsTask( QObject *parent, QString outFile ) : QObject(parent), outFile(outFile)
{
}

void DiagnosticsTask::run()
{
	Diagnostics *worker = new Diagnostics( false );
	QObject::connect( worker, &Diagnostics::update, this, &DiagnosticsTask::insertHtml );
	worker->run();
	complete();
	logDiagnostics();

	emit finished();
}

void DiagnosticsTask::logDiagnostics()
{
	QFile file( outFile );
	if( outFile.isEmpty() )
	{
		file.open( stdout, QIODevice::WriteOnly );
	}
	else
	{
		file.open( QIODevice::WriteOnly );
	}

	QTextStream out( &file );
	out << getDiagnostics();
	out.flush();
}

QString DiagnosticsTask::getDiagnostics() const
{
	return data;
}

void DiagnosticsTask::insertHtml( const QString &text )
{
	html << text;
}

void DiagnosticsTask::complete()
{
	QTextDocument doc;
	doc.setHtml( html.join("") );

	data = doc.toPlainText();
}
