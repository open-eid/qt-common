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

#include "AboutDialog.h"
#include "ui_AboutDialog.h"

#include "Common.h"
#include "Diagnostics.h"
#ifdef CONFIG_URL
#include "Configuration.h"
#endif
#include "Settings.h"

#include <QtCore/QFile>
#include <QtCore/QThreadPool>
#include <QtCore/QStandardPaths>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>

AboutDialog::AboutDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::AboutDialog)
{
	ui->setupUi( this );
	setAttribute( Qt::WA_DeleteOnClose, true );

	QString package;
#ifndef Q_OS_MAC
	QStringList packages = Common::packages({
		"Eesti ID-kaardi tarkvara", "Estonian ID-card software", "estonianidcard", "eID software"});
	if( !packages.isEmpty() )
		package = "<br />" + tr("Base version:") + " " + packages.first();
#endif
	ui->version->setText( tr("%1 version %2, released %3%4")
		.arg( qApp->applicationName(), qApp->applicationVersion(), BUILD_DATE, package ) );

#ifdef CONFIG_URL
	QPushButton *update = ui->buttonBox->addButton(tr("Check for updates"), QDialogButtonBox::ActionRole);
	connect(&Configuration::instance(), &Configuration::finished, this, [=](bool /*update*/, const QString &error){
		if(error.isEmpty())
			return;
		QMessageBox b(QMessageBox::Warning, tr("Checking updates has failed."),
			tr("Checking updates has failed.") + "<br />" + tr("Please try again."),
			QMessageBox::Ok, this);
		b.setTextFormat(Qt::RichText);
		b.setDetailedText(error);
		b.exec();
	});
	connect(update, &QPushButton::clicked, []{
		Configuration::instance().update(true);
	});
#endif

	ui->diagnosticsTab->setEnabled( false );
	QApplication::setOverrideCursor( Qt::WaitCursor );
	Diagnostics *worker = new Diagnostics();
	connect(worker, &Diagnostics::update, ui->diagnostics, &QTextBrowser::insertHtml, Qt::QueuedConnection);
	connect(worker, &Diagnostics::destroyed, this, [=]{
		ui->diagnosticsTab->setEnabled(true);
		QApplication::restoreOverrideCursor();
	});
	QThreadPool::globalInstance()->start( worker );
	QPushButton *save = ui->buttonBox1->button(QDialogButtonBox::Save);
	if(save && Settings(QSettings::SystemScope).value("disableSave", false).toBool())
	{
		ui->buttonBox1->removeButton(save);
		save->deleteLater();
	}
}

AboutDialog::~AboutDialog()
{
	delete ui;
}

void AboutDialog::openTab( int index )
{
	ui->tabWidget->setCurrentIndex( index );
	open();
}

void AboutDialog::saveDiagnostics()
{
	QString filename = QFileDialog::getSaveFileName( this, tr("Save as"), QString( "%1/%2_diagnostics.txt")
		.arg( QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), qApp->applicationName() ),
		tr("Text files (*.txt)") );
	if( filename.isEmpty() )
		return;
	QFile f( filename );
	if( !f.open( QIODevice::WriteOnly ) || !f.write( ui->diagnostics->toPlainText().toUtf8() ) )
		QMessageBox::warning( this, tr("Error occurred"), tr("Failed write to file!") );
}
