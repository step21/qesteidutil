/*
 * QEstEidUtil
 *
 * Copyright (C) 2009,2010 Jargo Kõster <jargo@innovaatik.ee>
 * Copyright (C) 2009,2010 Raul Metsma <raul@innovaatik.ee>
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

#include <QApplication>
#include <QDesktopServices>
#include <QDomDocument>
#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QTemporaryFile>

#include "CertUpdate.h"
#include "jsextender.h"
#include "mainwindow.h"
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
#include "SettingsDialog.h"
#endif
#ifdef Q_OS_WIN
#include "CertStore.h"
#endif

#include <common/AboutWidget.h>
#include <common/Settings.h>

#ifdef Q_OS_MAC
#include <Authorization.h>
#endif

JsExtender::JsExtender( MainWindow *main )
:	QObject( main )
,	m_mainWindow( main )
,	m_loading( 0 )
,	sslError( SSLConnect::NoError )
{
	QString deflang;
	switch( QLocale().language() )
	{
	case QLocale::English: deflang = "en"; break;
	case QLocale::Russian: deflang = "ru"; break;
	case QLocale::Estonian:
	default: deflang = "et"; break;
	}
	m_locale = Settings().value( "Main/Language", deflang ).toString();
	if ( m_locale.isEmpty() )
		m_locale = QLocale::system().name().left( 2 );
	setLanguage( m_locale );

	connect( m_mainWindow->page()->mainFrame(), SIGNAL(javaScriptWindowObjectCleared()),
            this, SLOT(javaScriptWindowObjectCleared()));
}

JsExtender::~JsExtender()
{
	if ( m_loading )
		m_loading->deleteLater();
	if ( QFile::exists( m_tempFile + "_small.jpg" ) )
		QFile::remove( m_tempFile + "_small.jpg" );
	if ( QFile::exists( m_tempFile + "_big.jpg" ) )
		QFile::remove( m_tempFile + "_big.jpg" );
}

bool JsExtender::cleanTokenCache() const
{
#if defined(Q_OS_MAC)
	AuthorizationRef ref;
	AuthorizationCreate( 0, kAuthorizationEmptyEnvironment, kAuthorizationFlagDefaults, &ref );
	char *args[] = { (char*)"-c", (char*)"/bin/rm -rf /var/db/TokenCache/tokens/com.apple.tokend.opensc*", 0 };
	FILE *pipe = 0;
	OSStatus status = AuthorizationExecuteWithPrivileges( ref, "/bin/sh",
		kAuthorizationFlagDefaults, args, &pipe );

	if( pipe )
		fclose( pipe );

	AuthorizationFree( ref, kAuthorizationFlagDestroyRights );
	return status == errAuthorizationSuccess;
#elif defined(Q_OS_WIN)
	CertStore s;
	s.remove( m_mainWindow->eidCard()->m_authCert->cert() );
	s.remove( m_mainWindow->eidCard()->m_signCert->cert() );
	return s.add( m_mainWindow->eidCard()->m_authCert->cert(), m_mainWindow->eidCard()->getId() ) ||
		s.add( m_mainWindow->eidCard()->m_signCert->cert(), m_mainWindow->eidCard()->getId() );
#else
	return true;
#endif
}

void JsExtender::setLanguage( const QString &lang )
{
	m_locale = lang;
	if ( m_locale == "C" )
		m_locale = "en";
	Settings().setValue( "Main/Language", m_locale );
	m_mainWindow->retranslate( m_locale );
}

void JsExtender::registerObject( const QString &name, QObject *object )
{
    m_mainWindow->page()->mainFrame()->addToJavaScriptWindowObject( name, object );

    m_registeredObjects[name] = object;
}

void JsExtender::javaScriptWindowObjectCleared()
{
    for (QMap<QString, QObject *>::Iterator it = m_registeredObjects.begin(); it != m_registeredObjects.end(); ++it)
        m_mainWindow->page()->mainFrame()->addToJavaScriptWindowObject( it.key(), it.value() );

    m_mainWindow->page()->mainFrame()->addToJavaScriptWindowObject( "extender", this );
}

QVariant JsExtender::jsCall( const QString &function, int argument )
{
    return m_mainWindow->page()->mainFrame()->evaluateJavaScript(
		QString( "%1(%2)" ).arg( function ).arg( argument ) );
}

QVariant JsExtender::jsCall( const QString &function, const QString &argument )
{
    return m_mainWindow->page()->mainFrame()->evaluateJavaScript(
		QString( "%1(\"%2\")" ).arg( function ).arg( argument ) );
}

QVariant JsExtender::jsCall( const QString &function, const QString &argument, const QString &argument2 )
{
    return  m_mainWindow->page()->mainFrame()->evaluateJavaScript(
		QString( "%1(\"%2\",\"%3\")" ).arg( function ).arg( argument ).arg( argument2 ) );
}

void JsExtender::openUrl( const QString &url )
{ QDesktopServices::openUrl( QUrl( url ) ); }

QByteArray JsExtender::getUrl( SSLConnect::RequestType type, const QString &def )
{
	QByteArray buffer;

	sslError = SSLConnect::NoError;
	sslErrorString.clear();

	SSLConnect sslConnect;
	if ( !sslConnect.setCard( m_mainWindow->cardManager()->activeCardId() ) )
	{
		sslError = sslConnect.error();
		sslErrorString = sslConnect.errorString();
		return buffer;
	}

	buffer = sslConnect.getUrl( type, def );

	sslError = sslConnect.error();
	sslErrorString = sslConnect.errorString();

	m_mainWindow->eidCard()->reconnect();
	return buffer;
}

void JsExtender::activateEmail( const QString &email )
{
	QByteArray buffer = getUrl( SSLConnect::ActivateEmails, email );
	if ( !buffer.size() || sslError != SSLConnect::NoError )
	{
		if ( sslError != SSLConnect::NoError )
			jsCall( "handleError", sslErrorString );
		else
			jsCall( "setEmails", "forwardFailed", "" );
		return;
	}
	xml.clear();
	xml.addData( buffer );
	QString result = "forwardFailed";
	while ( !xml.atEnd() )
	{
		xml.readNext();
		if ( xml.isStartElement() &&  xml.name() == "fault_code" )
		{
			result = xml.readElementText();
			break;
		}
	}
	jsCall( "setEmailActivate", result );
}

void JsExtender::loadEmails()
{
	QByteArray buffer = getUrl( SSLConnect::EmailInfo, "" );
	if ( !buffer.size() || sslError != SSLConnect::NoError )
	{
		if ( sslError != SSLConnect::NoError )
		{
			jsCall( "handleError", sslErrorString );
			jsCall( "setEmails", "", "" );
		} else
			jsCall( "setEmails", "loadFailed", "" );
		return;
	}
	xml.clear();
	xml.addData( buffer );
	QString result = "loadFailed", emails;
	while ( !xml.atEnd() )
	{
		xml.readNext();
		if ( xml.isStartElement() )
		{
			if ( xml.name() == "fault_code" )
			{
				result = xml.readElementText();
				continue;
			}
			if ( xml.name() == "ametlik_aadress" )
				emails += readEmailAddresses();
		}
	}
	if ( emails.length() > 4 )
		emails = emails.right( emails.length() - 4 );
	jsCall( "setEmails", result, emails );
}

QString JsExtender::readEmailAddresses()
{
	Q_ASSERT( xml.isStartElement() && xml.name() == "ametlik_aadress" );

	QString emails;

	while ( !xml.atEnd() )
	{
		xml.readNext();
		if ( xml.isStartElement() )
		{
			if ( xml.name() == "epost" )
				emails += "<BR>" + xml.readElementText();
			else if ( xml.name() == "suunamine" )
				emails += readForwards();
		}
	}
	return emails;
}

QString JsExtender::readForwards()
{
	Q_ASSERT( xml.isStartElement() && xml.name() == "suunamine" );

	bool emailActive = false, forwardActive = false;
	QString email;

	while ( !xml.atEnd() )
	{
		xml.readNext();
		if ( xml.isEndElement() )
			break;
		if ( xml.isStartElement() )
		{
			if ( xml.name() == "epost" )
				email = xml.readElementText();
			else if ( xml.name() == "aktiivne" && xml.readElementText() == "true" )
				emailActive = true;
			else if ( xml.name() == "aktiiveeritud" && xml.readElementText() == "true" )
				forwardActive = true;
		}
	}
	return (emailActive && forwardActive ) ? tr( " - %1 (active)" ).arg( email ) : tr( " - %1 (not active)" ).arg( email );
}

void JsExtender::loadPicture()
{
	QByteArray buffer = getUrl( SSLConnect::PictureInfo, "" );
	if ( !buffer.size() || sslError != SSLConnect::NoError )
	{
		if ( sslError != SSLConnect::NoError )
		{
			jsCall( "handleError", sslErrorString );
			jsCall( "setPicture", "", "" );
		} else
			jsCall( "setPicture", "", "loadPicFailed" );
		return;
	}

	QPixmap pix;
	if ( pix.loadFromData( buffer ) )
	{
		QPixmap smallPix = pix.scaled( 90, 120, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
		QTemporaryFile file( QString( "%1%2XXXXXX" )
			.arg( QDir::tempPath() ).arg( QDir::separator() ) );
		file.setAutoRemove( false );
		if ( file.open() )
		{
			m_tempFile = file.fileName();
			if ( smallPix.save( m_tempFile + "_small.jpg" ) && pix.save( m_tempFile + "_big.jpg" ) )
			{
				jsCall( "setPicture", QUrl::fromLocalFile(m_tempFile).toString(), "" );
				return;
			}
		}
		jsCall( "setPicture", "", QString( "loadPicFailed3|%1" ).arg( file.errorString() ) );
	} else { //probably got xml error string
		QString result = "loadPicFailed2";
		xml.clear();
		xml.addData( buffer );		
		while ( !xml.atEnd() )
		{
			xml.readNext();
			if ( xml.isStartElement() && xml.name() == "fault_code" )
			{
				result = xml.readElementText();
				break;
			}
		}
		jsCall( "setPicture", "", result );
	}
}

void JsExtender::savePicture()
{
	if ( !QFile::exists( m_tempFile ) )
	{
		jsCall( "handleError", "savePicFailed" );
		return;
	}
	QString pFile = QDesktopServices::storageLocation( QDesktopServices::PicturesLocation );
	if ( m_mainWindow->eidCard() )
		pFile += QString( "%1%2.jpg" ).arg( QDir::separator() ).arg( m_mainWindow->eidCard()->getId() );
	QString file = QFileDialog::getSaveFileName( m_mainWindow, tr( "Save picture" ), pFile, tr( "JPEG (*.jpg *.jpeg);;PNG (*.png);;TIFF (*.tif *.tiff);;24-bit Bitmap (*.bmp)" ) );
	if( file.isEmpty() )
		return;
	QString ext = QFileInfo( file ).suffix();
	if( ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "tiff" && ext != "bmp" )
		file.append( ".jpg" );
	QPixmap pix;
	if ( !pix.load( m_tempFile + "_big.jpg" ) )
	{
		jsCall( "handleError", "savePicFailed" );
		return;
	}
	pix.save( file );
}

void JsExtender::getMidStatus()
{
	QString result = "mobileFailed";

	QString data = "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:SOAP-ENC=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
						"<SOAP-ENV:Body>"
						"<m:GetMIDTokens xmlns:m=\"urn:GetMIDTokens\" SOAP-ENV:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"/>"
						"</SOAP-ENV:Body>"
						"</SOAP-ENV:Envelope>";
	QString header = "POST /MIDInfoWS/ HTTP/1.1\r\n"
					 "Host: " + QString(SK) + "\r\n"
					 "Content-Type: text/xml\r\n"
					 "Content-Length: " + QString::number( data.size() ) + "\r\n"
					 "SOAPAction: \"\"\r\n"
					 "Connection: close\r\n\r\n";
	QByteArray buffer = getUrl( SSLConnect::MobileInfo, header + data );
	if ( !buffer.size() || sslError != SSLConnect::NoError )
	{
		if ( sslError != SSLConnect::NoError )
			jsCall( "handleError", sslErrorString );
		else
			jsCall( "setMobile", "", result );
		return;
	}

	QDomDocument doc;
	if ( !doc.setContent( buffer ) )
	{
		jsCall( "handleError", result );
		return;
	}
	QDomElement e = doc.documentElement();
	if ( !e.elementsByTagName( "ResponseStatus" ).size() )
	{
		jsCall( "handleError", result );
		return;
	}
	MobileResult mRes = (MobileResult)e.elementsByTagName( "ResponseStatus" ).item(0).toElement().text().toInt();
	QString mResString, mNotice;
	switch( mRes )
	{
		case NoCert: mNotice = "mobileNoCert"; break;
		case NotActive: mNotice = "mobileNotActive"; break;
		case NoIDCert: mResString = "noIDCert"; break;
		case InternalError: mResString = "mobileInternalError"; break;
		case InterfaceNotReady: mResString = "mobileInterfaceNotReady"; break;
		case OK:
		default: break;
	}
	if ( !mResString.isEmpty() )
	{
		jsCall( "handleError", mResString );
		return;
	}
	if ( !mNotice.isEmpty() )
	{
		jsCall( "handleNotice", mNotice );
		return;
	}
	mResString = QString( "%1;%2;%3;%4;%5" )
					.arg( e.elementsByTagName( "MSISDN" ).item(0).toElement().text() )
					.arg( e.elementsByTagName( "Operator" ).item(0).toElement().text() )
					.arg( e.elementsByTagName( "Status" ).item(0).toElement().text() )
					.arg( e.elementsByTagName( "URL" ).item(0).toElement().text() )
					.arg( e.elementsByTagName( "MIDCertsValidTo" ).item(0).toElement().text() );
	jsCall( "setMobile", mResString );
}

void JsExtender::httpRequestFinished( int, bool error )
{
	if ( error)
		qDebug() << "Download failed: " << m_http.errorString();

	QByteArray result = m_http.readAll();
}

void JsExtender::showAbout()
{ (new AboutWidget( m_mainWindow ))->show(); }

void JsExtender::showSettings()
{
#if defined(Q_OS_WIN32) || defined(Q_OS_MACX)
	(new SettingsDialog( m_mainWindow ) )->show();
#endif
}

void JsExtender::showLoading( const QString &str )
{
	bool wide = (str.size() > 20);
	if ( !m_loading )
	{
		m_loading = new QLabel( m_mainWindow );
		m_loading->setStyleSheet( "background-color: rgba(255,255,255,200); border: 1px solid #cddbeb; border-radius: 5px;"
									"color: #509b00; font-weight: bold; font-family: Arial; font-size: 18px;" );
		m_loading->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );
	}
	m_loading->setFixedSize( wide ? 300 : 250, 100 );
	m_loading->move( wide ? 155 : 180, 305 );
	m_loading->setText( str );
	m_loading->show();
	QCoreApplication::processEvents();
}

void JsExtender::closeLoading()
{
	if ( m_loading )
		m_loading->close();
}

void JsExtender::showMessage( const QString &type, const QString &message, const QString &title )
{
	if ( type == "warning" )
		QMessageBox::warning( m_mainWindow, title.isEmpty() ? m_mainWindow->windowTitle() : title, message, QMessageBox::Ok );
	else if ( type == "error" )
		QMessageBox::critical( m_mainWindow, title.isEmpty() ? m_mainWindow->windowTitle() : title, message, QMessageBox::Ok );
	else
		QMessageBox::information( m_mainWindow, title.isEmpty() ? m_mainWindow->windowTitle() : title, message, QMessageBox::Ok );
}

bool JsExtender::updateCertAllowed()
{
	QMessageBox::StandardButton b = QMessageBox::warning( m_mainWindow, tr("Certificate update"),
		tr("For updating certificates please close all programs which are interacting with smartcard "
		   "(qdigidocclient, qdigidoccrypto, Firefox, Safari, Internet Explorer...)<br />"
		   "After updating certificates it will no longer be possible to decrypt documents that were encrypted with the old certificate.<br />"
		   "Do you want to continue?"),
		QMessageBox::Yes|QMessageBox::No, QMessageBox::No );
	if( b == QMessageBox::No )
		return false;
	try {
		CertUpdate c( m_mainWindow->cardManager()->activeReaderNum(), this );
		return c.checkUpdateAllowed();
	} catch ( std::runtime_error &e ) {
		QMessageBox::warning( m_mainWindow, tr( "Certificate update" ), tr("Certificate update failed:<br />%1").arg( QString::fromUtf8( e.what() ) ), QMessageBox::Ok );
	}
	return false;
}

bool JsExtender::updateCert()
{
	try {
		CertUpdate c( m_mainWindow->cardManager()->activeReaderNum(), this );
		if ( c.checkUpdateAllowed() && m_mainWindow->eidCard()->m_authCert )
		{
			c.startUpdate();
			return true;
		}
	} catch ( std::runtime_error &e ) {
		QMessageBox::warning( m_mainWindow, tr( "Certificate update" ), tr("Certificate update failed: %1").arg( QString::fromUtf8( e.what() ) ), QMessageBox::Ok );
	}
	return false;
}
