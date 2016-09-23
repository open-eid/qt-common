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
#include "CliApplication.h"
#include "DiagnosticsTask.h"

#include <iostream>
#include <QtCore/QObject>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>


CliApplication::CliApplication( int &argc, char **argv ) : argc(argc), argv(argv)
{
}

CliApplication::CliApplication( int &argc, char **argv, QString outFile ) : CliApplication(argc, argv) 
{
	this->outFile = outFile;
}

bool CliApplication::isDiagnosticRun()
{
	for( size_t i = 1; i < argc; ++i )
	{
		auto parameter = QString( argv[i] );
		if( parameter.startsWith("/diag") )
		{
			outFile = parameter.remove("/diag").remove(QRegExp("^[:]*"));
			return true;
		}
	}

	return false;
}

int CliApplication::run() const
{
	QCoreApplication qtApp( argc, argv );
	DiagnosticsTask *task = new DiagnosticsTask( &qtApp, outFile );
	QObject::connect( task, SIGNAL(finished()), &qtApp, SLOT(quit()));

	QTimer::singleShot( 0, task, SLOT(run()) );
	return qtApp.exec();
}
