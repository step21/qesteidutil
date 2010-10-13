/*
 * QEstEidCommon
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

#include "Common.h"

#include "SslCertificate.h"
#include "TokenData.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QUrl>

#ifdef Q_OS_WIN32
#include <QDir>
#include <QLibrary>
#include <QTemporaryFile>

#include <windows.h>
#include <mapi.h>
#endif

#ifdef Q_OS_MAC
#include <Carbon/Carbon.h>
#endif

Common::Common( QObject *parent )
:	QObject( parent )
{
	QDesktopServices::setUrlHandler( "browse", this, "browse" );
	QDesktopServices::setUrlHandler( "mailto", this, "mailTo" );
}

bool Common::canWrite( const QString &filename )
{
#ifdef Q_OS_WIN32
	return QTemporaryFile( QFileInfo( filename ).absolutePath().append( "/XXXXXX" ) ).open();
#else
	QFileInfo file( filename );
	return file.isFile() ? file.isWritable() : QFileInfo( file.absolutePath() ).isWritable();
#endif
}

void Common::browse( const QUrl &url )
{
	QUrl u = url;
	u.setScheme( "file" );
#if defined(Q_OS_WIN32)
	if( QProcess::startDetached( "explorer", QStringList() << "/select," <<
		QDir::toNativeSeparators( u.toLocalFile() ) ) )
		return;
#elif defined(Q_OS_MAC)
	QProcess p;
	p.start( "/usr/bin/osascript", QStringList() << "-" << u.toLocalFile() );
	p.waitForStarted();
	QTextStream s( &p );
	s << "on run argv" << endl
		<< "set vfile to POSIX file (item 1 of argv)" << endl
		<< "tell application \"Finder\"" << endl
		<< "select vfile" << endl
		<< "activate" << endl
		<< "end tell" << endl
		<< "end run" << endl;
	p.closeWriteChannel();
	p.waitForFinished();
	if( p.exitCode() == 0 )
		return;
#endif
	QDesktopServices::openUrl( QUrl::fromLocalFile( QFileInfo( u.toLocalFile() ).absolutePath() ) );
}

QString Common::fileSize( quint64 bytes )
{
	const quint64 kb = 1024;
	const quint64 mb = 1024 * kb;
	const quint64 gb = 1024 * mb;
	const quint64 tb = 1024 * gb;
	if( bytes >= tb )
		return QString( "%1 TB" ).arg( qreal(bytes) / tb, 0, 'f', 3 );
	if( bytes >= gb )
		return QString( "%1 GB" ).arg( qreal(bytes) / gb, 0, 'f', 2 );
	if( bytes >= mb )
		return QString( "%1 MB" ).arg( qreal(bytes) / mb, 0, 'f', 1 );
	if( bytes >= kb )
		return QString( "%1 KB" ).arg( bytes / kb );
	return QString( "%1 B" ).arg( bytes );
}

void Common::mailTo( const QUrl &url )
{
#if defined(Q_OS_WIN32)
	QString file = url.queryItemValue( "attachment" );
	QByteArray filePath = QDir::toNativeSeparators( file ).toLatin1();
	QByteArray fileName = QFileInfo( file ).fileName().toLatin1();
	QByteArray subject = url.queryItemValue( "subject" ).toLatin1();

	MapiFileDesc doc[1];
	doc[0].ulReserved = 0;
	doc[0].flFlags = 0;
	doc[0].nPosition = -1;
	doc[0].lpszPathName = const_cast<char*>(filePath.constData());
	doc[0].lpszFileName = const_cast<char*>(fileName.constData());
	doc[0].lpFileType = NULL;

	// Create message
	MapiMessage message;
	message.ulReserved = 0;
	message.lpszSubject = const_cast<char*>(subject.constData());
	message.lpszNoteText = "";
	message.lpszMessageType = NULL;
	message.lpszDateReceived = NULL;
	message.lpszConversationID = NULL;
	message.flFlags = 0;
	message.lpOriginator = NULL;
	message.nRecipCount = 0;
	message.lpRecips = NULL;
	message.nFileCount = 1;
	message.lpFiles = (lpMapiFileDesc)&doc;

	QLibrary lib("mapi32");
	typedef ULONG (PASCAL *SendMail)(ULONG,ULONG,MapiMessage*,FLAGS,ULONG);
	SendMail mapi = (SendMail)lib.resolve("MAPISendMail");
	if( mapi )
	{
		mapi( NULL, 0, &message, MAPI_LOGON_UI|MAPI_DIALOG, 0 );
		return;
	}
#elif defined(Q_OS_MAC)
	CFURLRef emailUrl = CFURLCreateWithString( kCFAllocatorDefault, CFSTR("mailto:"), 0 );
	CFURLRef appUrl = 0;
	CFStringRef appPath = 0;
	if( LSGetApplicationForURL( emailUrl, kLSRolesAll, NULL, &appUrl ) == noErr )
	{
		appPath = CFURLCopyFileSystemPath( appUrl, kCFURLPOSIXPathStyle );
		CFRelease( appUrl );
	}
	CFRelease( emailUrl );

	if( appPath )
	{
		QProcess p;
		p.start( "/usr/bin/osascript", QStringList() << "-" << url.queryItemValue("attachment") << url.queryItemValue("subject") );
		p.waitForStarted();
		QTextStream s( &p );
		if( CFStringCompare( appPath, CFSTR("/Applications/Mail.app"), 0 ) == kCFCompareEqualTo )
		{
			s << "on run argv" << endl
				<< "set vattachment to (item 1 of argv)" << endl
				<< "set vsubject to (item 2 of argv)" << endl
				<< "tell application \"Mail\"" << endl
				<< "set composeMessage to make new outgoing message at beginning with properties {visible:true}" << endl
				<< "tell composeMessage" << endl
				<< "set subject to vsubject" << endl
				<< "set content to \" \"" << endl
				<< "tell content" << endl
				<< "make new attachment with properties {file name: vattachment} at after the last word of the last paragraph" << endl
				<< "end tell" << endl
				<< "end tell" << endl
				<< "activate" << endl
				<< "end tell" << endl
				<< "end run" << endl;
		}
		else if( CFStringFind( appPath, CFSTR("Entourage"), 0 ).location != kCFNotFound )
		{
			s << "on run argv" << endl
				<< "set vattachment to (item 1 of argv)" << endl
				<< "set vsubject to (item 2 of argv)" << endl
				<< "tell application \"Microsoft Entourage\"" << endl
				<< "set vmessage to make new outgoing message with properties" << endl
				<< "{subject:vsubject, attachments:vattachment}" << endl
				<< "open vmessage" << endl
				<< "activate" << endl
				<< "end tell" << endl
				<< "end run" << endl;
		}
#if 0
		else if(CFStringCompare(appPath, CFSTR("/Applications/Thunderbird.app"), 0) == kCFCompareEqualTo)
		{
			// TODO: Handle Thunderbird here? Impossible?
		}
#endif
		CFRelease( appPath );
		p.closeWriteChannel();
		p.waitForFinished();
		if( p.exitCode() == 0 )
			return;
	}
#elif defined(Q_OS_LINUX)
	QByteArray thunderbird;
	QProcess p;
	QStringList env = QProcess::systemEnvironment();
	if( env.indexOf( QRegExp("KDE_FULL_SESSION.*") ) != -1 )
	{
		p.start( "kreadconfig", QStringList()
			<< "--file" << "emaildefaults"
			<< "--group" << "PROFILE_Default"
			<< "--key" << "EmailClient" );
		p.waitForFinished();
		QByteArray data = p.readAllStandardOutput().trimmed();
		if( data.contains("thunderbird") )
			thunderbird = data;
	}
	else if( env.indexOf( QRegExp("GNOME_DESKTOP_SESSION_ID.*") ) != -1 )
	{
		p.start( "gconftool-2", QStringList()
			<< "--get" << "/desktop/gnome/url-handlers/mailto/command" );
		p.waitForFinished();
		QByteArray data = p.readAllStandardOutput();
		if( data.contains("thunderbird") )
			thunderbird = data.split(' ').value(0);
	}
	/*
	else
	{
		p.start( "xprop", QStringList() << "-root" << "_DT_SAVE_MODE" );
		p.waitForFinished();
		if( p.readAllStandardOutput().contains("xfce4") )
		{}
	}*/

	if( !thunderbird.isEmpty() )
	{
		if( p.startDetached( thunderbird, QStringList() << "-compose"
			<< QString( "subject='%1',attachment='%2'" )
				.arg( url.queryItemValue( "subject" ) )
				.arg( QUrl::fromLocalFile( url.queryItemValue( "attachment" ) ).toString() ) ) );
			return;
	}
	else
	{
		if( p.startDetached( "xdg-email", QStringList()
				<< "--subject" << url.queryItemValue( "subject" )
				<< "--attach" << url.queryItemValue( "attachment" ) ) )
			return;
	}
#endif
	QDesktopServices::openUrl( url );
}

void Common::showHelp( const QString &msg )
{
	QUrl u( "http://support.sk.ee/" );
	u.addQueryItem( "searchquery", msg );
	u.addQueryItem( "searchtype", "all" );
	u.addQueryItem( "_m", "core" );
	u.addQueryItem( "_a", "searchclient" );
	QDesktopServices::openUrl( u );
}

bool Common::startDetached( const QString &program )
{ return startDetached( program, QStringList() ); }

bool Common::startDetached( const QString &program, const QStringList &arguments )
{
#ifdef Q_OS_MAC
	return QProcess::startDetached( "/usr/bin/open", QStringList() << "-a" << program << arguments );
#else
	return QProcess::startDetached( program, arguments );
#endif
}

QString Common::tokenInfo( CertType type, const TokenData &data )
{
	QString content;
	QTextStream s( &content );
	SslCertificate c( data.cert() );

	s << "<table width=\"100%\"><tr><td>";
	if( c.isTempel() )
	{
		s << tr("Company") << ": <font color=\"black\">"
			<< c.toString( "CN" ) << "</font><br />";
		s << tr("Register code") << ": <font color=\"black\">"
			<< c.subjectInfo( "serialNumber" ) << "</font><br />";
	}
	else
	{
		s << tr("Name") << ": <font color=\"black\">"
			<< c.toString( "GN SN" ) << "</font><br />";
		s << tr("Personal code") << ": <font color=\"black\">"
			<< c.subjectInfo( "serialNumber" ) << "</font><br />";
	}
	s << tr("Card in reader") << ": <font color=\"black\">" << data.card() << "</font><br />";

	bool willExpire = c.expiryDate().toLocalTime() <= QDateTime::currentDateTime().addDays( 105 );
	s << (type == AuthCert ? tr("Auth certificate is") : tr("Sign certificate is") ) << " ";
	if( c.isValid() )
	{
		s << "<font color=\"green\">" << tr("valid") << "</font>";
		if( willExpire )
			s << "<br /><font color=\"red\">" << tr("Your certificates will expire soon") << "</font>";
	}
	else
		s << "<font color=\"red\">" << tr("expired") << "</font>";
	if( data.flags() & TokenData::PinLocked )
		s << "<br /><font color=\"red\">" << tr("PIN is locked") << "</font>";

	s << "</td><td align=\"center\" width=\"75\">";
	if( !c.isValid() || willExpire || data.flags() & TokenData::PinLocked )
	{
		s << "<a href=\"openUtility\"><img src=\":/images/warning.png\"><br />"
			"<font color=\"red\">" << tr("Open utility") << "</font></a>";
	}
	else if( c.isTempel() )
		s << "<img src=\":/images/ico_stamp_blue_75.png\">";
	else
		s << "<img src=\":/images/ico_person_blue_75.png\">";
	s << "</td></tr></table>";

	return content;
}
