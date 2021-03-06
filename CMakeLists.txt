cmake_minimum_required(VERSION 3.0)
if(POLICY CMP0043)
	cmake_policy(SET CMP0043 NEW)
endif()
if(POLICY CMP0063)
	cmake_policy(SET CMP0063 NEW)
endif()
project(qesteidutil VERSION 3.12.10)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
include( VersionInfo )
set_app_name( PROGNAME qesteidutil )

find_package( Qt5 COMPONENTS Core Widgets Network LinguistTools REQUIRED )
if(UNIX AND NOT APPLE)
	find_package(Threads)
endif()

add_subdirectory( common )

configure_file( src/translations/tr.qrc tr.qrc COPYONLY )
qt5_add_translation( SOURCES src/translations/en.ts src/translations/et.ts src/translations/ru.ts )
qt5_add_resources( SOURCES ${CMAKE_BINARY_DIR}/tr.qrc src/qesteidutil.qrc )
qt5_wrap_ui( SOURCES src/MainWindow.ui src/Updater.ui )

if( WIN32 )
	qt5_wrap_ui( SOURCES src/SettingsDialog.ui )
	list( APPEND SOURCES src/SettingsDialog.cpp src/CertStore.cpp )
	list( APPEND ADDITIONAL_LIBRARIES Crypt32 )
endif()

include_directories(${OPENSSL_INCLUDE_DIR})

add_executable( ${PROGNAME} WIN32 MACOSX_BUNDLE
	src/qesteidutil.rc
	src/main.cpp
	src/MainWindow.cpp
	src/QSmartCard.cpp
	src/sslConnect.cpp
	src/XmlReader.cpp
	src/Updater.cpp
	${SOURCES}
	${RESOURCE_FILES}
)
add_manifest( ${PROGNAME} )
target_link_libraries(${PROGNAME} ${ADDITIONAL_LIBRARIES} qdigidoccommon ${CMAKE_THREAD_LIBS_INIT})

if(APPLE)
	add_custom_target( macdeployqt DEPENDS ${PROGNAME}
		COMMAND ${_qt5Core_install_prefix}/bin/macdeployqt ${CMAKE_CURRENT_BINARY_DIR}/${PROGNAME}.app
		COMMAND find $<TARGET_FILE_DIR:${PROGNAME}>/../PlugIns/imageformats -type f -a ! -name libqjpeg.dylib -delete
		COMMAND rm -rf $<TARGET_FILE_DIR:${PROGNAME}>/../PlugIns/bearer
	)
	add_custom_target( codesign DEPENDS ${PROGNAME}
		COMMAND codesign -f -s \"$$SIGNCERT\"
			$<TARGET_FILE_DIR:${PROGNAME}>/../Frameworks/*
			$<TARGET_FILE_DIR:${PROGNAME}>/../PlugIns/*/*
		COMMAND codesign -f -s \"$$SIGNCERT\" $<TARGET_FILE_DIR:${PROGNAME}>/../..
			--entitlements ${CMAKE_SOURCE_DIR}/${PROGNAME}.entitlements
	)
	add_custom_target( productbuild DEPENDS ${PROGNAME}
		COMMAND productbuild --component ${CMAKE_CURRENT_BINARY_DIR}/${PROGNAME}.app /Applications
			--sign \"$$INSTCERT\" ${PROGNAME}-store_${VERSION}$ENV{VER_SUFFIX}.pkg
	)
	add_custom_target( zip DEPENDS ${PROGNAME}
		COMMAND zip --symlinks -r -D ${PROGNAME}_${VERSION}$ENV{VER_SUFFIX}.zip ${PROGNAME}.app
	)
	add_custom_target( zipdebug DEPENDS ${PROGNAME}
		COMMAND dsymutil -o ${PROGNAME}.dSYM $<TARGET_FILE:${PROGNAME}>
		COMMAND zip -r ${PROGNAME}-dbg_${VERSION}$ENV{VER_SUFFIX}.zip ${PROGNAME}.dSYM
	)
elseif(WIN32)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(PLATFORM x64)
		list(APPEND CANDLE_EXTRA "-dlibs_path=C:/Openssl-Win64/bin")
	else()
		set(PLATFORM x86)
		list(APPEND CANDLE_EXTRA "-dlibs_path=C:/Openssl-Win32/bin")
	endif()
	set(MSI_FILE "ID-card_utility-${VERSION}$ENV{VER_SUFFIX}.${PLATFORM}")
	list(APPEND CANDLE_CMD "$ENV{WIX}bin\\candle.exe" -nologo -arch ${PLATFORM}
		-dMSI_VERSION=${VERSION} -dPlatform=${PLATFORM} ${CANDLE_EXTRA}
		-dutility_path=$<TARGET_FILE:${PROGNAME}> ${CMAKE_SOURCE_DIR}/qesteidutil.wxs -dqtconf=${CMAKE_SOURCE_DIR}/qt.conf
		${CMAKE_SOURCE_DIR}/cmake/modules/WelcomeDlg2.wxs ${CMAKE_SOURCE_DIR}/cmake/modules/WixUI_Minimal2.wxs)
	list(APPEND LIGHT_CMD "$ENV{WIX}bin\\light.exe" -nologo qesteidutil.wixobj
		WelcomeDlg2.wixobj WixUI_Minimal2.wixobj -ext WixUIExtension
		-dWixUIDialogBmp=${CMAKE_SOURCE_DIR}/cmake/modules/dlgbmp.bmp
		-dWixUIBannerBmp=${CMAKE_SOURCE_DIR}/cmake/modules/banner.bmp)
	add_custom_target(msi DEPENDS ${PROGNAME}
		COMMAND ${CANDLE_CMD}
		COMMAND ${LIGHT_CMD} -loc ${CMAKE_SOURCE_DIR}/qesteidutil.en-US.wxl -cultures:en-US -o "${MSI_FILE}.en-US.msi"
		COMMAND ${LIGHT_CMD} -loc ${CMAKE_SOURCE_DIR}/qesteidutil.et-EE.wxl -cultures:et-EE -o "${MSI_FILE}.et-EE.msi"
		#Build MSI with QT
		COMMAND ${CANDLE_CMD} -dqt_path=${_qt5Core_install_prefix}
		COMMAND ${LIGHT_CMD} -loc ${CMAKE_SOURCE_DIR}/qesteidutil.en-US.wxl -cultures:en-US -o "${MSI_FILE}.en-US.qt.msi"
		COMMAND ${LIGHT_CMD} -loc ${CMAKE_SOURCE_DIR}/qesteidutil.et-EE.wxl -cultures:et-EE -o "${MSI_FILE}.et-EE.qt.msi"
	)
	if(SIGNCERT)
		list(APPEND SIGNCMD signtool.exe sign /a /v /s MY /n "${SIGNCERT}" /fd SHA256
			/du http://installer.id.ee /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp /td SHA256)
		add_custom_command(TARGET ${PROGNAME} POST_BUILD
			COMMAND ${SIGNCMD} $<TARGET_FILE:${PROGNAME}>
		)
		add_custom_command(TARGET msi POST_BUILD
			COMMAND ${SIGNCMD} "${MSI_FILE}.en-US.msi" "${MSI_FILE}.et-EE.msi" "${MSI_FILE}.en-US.qt.msi" "${MSI_FILE}.et-EE.qt.msi"
		)
	endif()
else()
	include( GNUInstallDirs )
	configure_file( qesteidutil.1.cmake qesteidutil.1 )
	install( TARGETS ${PROGNAME} DESTINATION ${CMAKE_INSTALL_BINDIR} )
	install( FILES ${CMAKE_CURRENT_BINARY_DIR}/qesteidutil.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 )
	install( FILES qesteidutil.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications )
	foreach( RES 16x16 32x32 48x48 128x128 )
		install(
			FILES src/images/id_icon_${RES}.png
			DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${RES}/apps/
			RENAME qesteidutil.png
		)
	endforeach(RES)
endif()
