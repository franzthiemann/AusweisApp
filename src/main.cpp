/**
 * Copyright (c) 2014-2025 Governikus GmbH & Co. KG, Germany
 */

#include "Bootstrap.h"

#include "init/config.h"

#include <QtPlugin>


Q_IMPORT_PLUGIN(SimulatorReaderManagerPlugin)

#if !defined(Q_OS_WINRT) && !defined(INTEGRATED_SDK)
Q_IMPORT_PLUGIN(RemoteIfdReaderManagerPlugin)
#endif

#if defined(UBUNTU_TOUCH)
// Ubuntu Touch reaches the NFC hardware through nfcd over D-Bus. A static
// plugin is only pulled out of its archive by Q_IMPORT_PLUGIN, so without this
// the plugin links but never registers and no reader appears.
Q_IMPORT_PLUGIN(NfcdReaderManagerPlugin)
#endif

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(Q_OS_WINRT)
	#if !defined(UBUNTU_TOUCH)
// No pcscd on Ubuntu Touch, and the plugin is not built there.
Q_IMPORT_PLUGIN(PcscReaderManagerPlugin)
	#endif

	#if !defined(INTEGRATED_SDK) || defined(CONTAINER_SDK)
Q_IMPORT_PLUGIN(UiPluginWebService)
Q_IMPORT_PLUGIN(UiPluginProxy)
	#endif

	#if defined(USE_SMARTEID) && (defined(CONTAINER_SDK) || !defined(QT_NO_DEBUG))
Q_IMPORT_PLUGIN(SmartReaderManagerPlugin)
	#endif

#endif


#if defined(Q_OS_ANDROID)
Q_IMPORT_PLUGIN(NfcReaderManagerPlugin)

	#ifndef INTEGRATED_SDK
Q_IMPORT_PLUGIN(UiPluginScheme)

		#ifdef USE_SMARTEID
Q_IMPORT_PLUGIN(UiPluginLocalIfd)
Q_IMPORT_PLUGIN(SmartReaderManagerPlugin)
		#endif

	#else
Q_IMPORT_PLUGIN(LocalIfdReaderManagerPlugin)
	#endif

#endif


#if defined(Q_OS_IOS)
Q_IMPORT_PLUGIN(NfcReaderManagerPlugin)

	#ifndef INTEGRATED_SDK
Q_IMPORT_PLUGIN(UiPluginScheme)

		#ifdef USE_SMARTEID
Q_IMPORT_PLUGIN(SmartReaderManagerPlugin)
		#endif

Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QSvgPlugin)
	#endif

#endif


#if defined(Q_OS_ANDROID) || !defined(QT_NO_DEBUG)
Q_IMPORT_PLUGIN(UiPluginAidl)
#endif

#ifndef INTEGRATED_SDK
Q_IMPORT_PLUGIN(UiPluginQml)
#endif

#if defined(INTEGRATED_SDK) && !defined(Q_OS_ANDROID) && !defined(CONTAINER_SDK)
Q_IMPORT_PLUGIN(UiPluginFunctional)
#endif

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && (!defined(INTEGRATED_SDK) || defined(CONTAINER_SDK))
Q_IMPORT_PLUGIN(UiPluginAutomatic)
Q_IMPORT_PLUGIN(UiPluginWebSocket)
#endif

#if !defined(Q_OS_IOS) || defined(INTEGRATED_SDK)
Q_IMPORT_PLUGIN(UiPluginJson)
#endif

Q_DECL_EXPORT int main(int argc, char** argv)
{
	return governikus::initApp(argc, argv);
}
