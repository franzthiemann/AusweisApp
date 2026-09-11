/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 */

#include "NfcdReaderManagerPlugin.h"

#include "NfcdDbus.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QLoggingCategory>
#include <QVariant>


using namespace governikus;


Q_DECLARE_LOGGING_CATEGORY(card_nfc)


NfcdReaderManagerPlugin::NfcdReaderManagerPlugin()
	: ReaderManagerPlugin(ReaderManagerPluginType::NFC)
	, mServiceWatcher()
	, mReader()
{
}


QString NfcdReaderManagerPlugin::findAdapter()
{
	auto msg = QDBusMessage::createMethodCall(nfcd::SERVICE, nfcd::PATH_DAEMON,
			nfcd::IFACE_DAEMON, QStringLiteral("GetAdapters"));

	const QDBusMessage reply = QDBusConnection::systemBus().call(msg);
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
	{
		qCDebug(card_nfc) << "Cannot query nfcd adapters:" << reply.errorMessage();
		return QString();
	}

	const auto adapters = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().constFirst());
	if (adapters.isEmpty())
	{
		// nfcd is running but the binder plugin did not bind an NFC HAL --
		// normal on ports whose device simply has no NFC hardware.
		qCDebug(card_nfc) << "nfcd reports no adapter";
		return QString();
	}

	return adapters.constFirst().path();
}


bool NfcdReaderManagerPlugin::isAdapterPowered(const QString& pAdapterPath)
{
	auto msg = QDBusMessage::createMethodCall(nfcd::SERVICE, pAdapterPath,
			nfcd::IFACE_ADAPTER, QStringLiteral("GetPowered"));

	const QDBusMessage reply = QDBusConnection::systemBus().call(msg);
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
	{
		return false;
	}

	return reply.arguments().constFirst().toBool();
}


void NfcdReaderManagerPlugin::init()
{
	ReaderManagerPlugin::init();

	if (mServiceWatcher)
	{
		return;
	}

	mServiceWatcher.reset(new QDBusServiceWatcher(nfcd::SERVICE,
			QDBusConnection::systemBus(),
			QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration));
	connect(mServiceWatcher.data(), &QDBusServiceWatcher::serviceRegistered,
			this, &NfcdReaderManagerPlugin::onServiceRegistered);
	connect(mServiceWatcher.data(), &QDBusServiceWatcher::serviceUnregistered,
			this, &NfcdReaderManagerPlugin::onServiceUnregistered);

	createReader();
}


void NfcdReaderManagerPlugin::shutdown()
{
	destroyReader();
	mServiceWatcher.reset();
}


void NfcdReaderManagerPlugin::createReader()
{
	if (mReader)
	{
		return;
	}

	const QString adapterPath = findAdapter();
	if (adapterPath.isEmpty())
	{
		setPluginAvailable(false);
		return;
	}

	setPluginAvailable(true);

	if (!isAdapterPowered(adapterPath))
	{
		// NFC is switched off. The adapter still exists, so keep the plugin
		// available and let the user turn it on.
		qCDebug(card_nfc) << "Adapter" << adapterPath << "is not powered";
		setPluginEnabled(false);
		return;
	}

	mReader.reset(new NfcdReader(adapterPath));
	connect(mReader.data(), &NfcdReader::fireCardInserted, this, &NfcdReaderManagerPlugin::fireCardInserted);
	connect(mReader.data(), &NfcdReader::fireCardRemoved, this, &NfcdReaderManagerPlugin::fireCardRemoved);
	connect(mReader.data(), &NfcdReader::fireCardInfoChanged, this, &NfcdReaderManagerPlugin::fireCardInfoChanged);
	connect(mReader.data(), &NfcdReader::fireReaderPropertiesUpdated, this, &NfcdReaderManagerPlugin::fireReaderPropertiesUpdated);
	connect(mReader.data(), &NfcdReader::fireReaderDisconnected, this, &NfcdReaderManagerPlugin::onReaderDisconnected);

	qCDebug(card_nfc) << "Add reader" << mReader->getName() << "on" << adapterPath;
	setPluginEnabled(true);
	Q_EMIT fireReaderAdded(mReader->getReaderInfo());
}


void NfcdReaderManagerPlugin::destroyReader()
{
	if (!mReader)
	{
		return;
	}

	const ReaderInfo info = mReader->getReaderInfo();
	mReader.reset();
	setPluginEnabled(false);
	Q_EMIT fireReaderRemoved(info);
}


void NfcdReaderManagerPlugin::onServiceRegistered()
{
	qCDebug(card_nfc) << "nfcd appeared on the system bus";
	createReader();
}


void NfcdReaderManagerPlugin::onServiceUnregistered()
{
	qCDebug(card_nfc) << "nfcd disappeared from the system bus";
	destroyReader();
	setPluginAvailable(false);
}


void NfcdReaderManagerPlugin::onReaderDisconnected()
{
	ReaderManagerPlugin::stopScan();
}


QList<Reader*> NfcdReaderManagerPlugin::getReaders() const
{
	if (mReader)
	{
		return QList<Reader*>({mReader.data()});
	}

	return QList<Reader*>();
}


void NfcdReaderManagerPlugin::startScan(bool pAutoConnect)
{
	// nfcd may have been installed, or NFC switched on, after we last looked.
	createReader();

	if (mReader)
	{
		mReader->connectReader();
		ReaderManagerPlugin::startScan(pAutoConnect);
	}
}


void NfcdReaderManagerPlugin::stopScan(const QString& pError)
{
	if (mReader)
	{
		mReader->disconnectReader(pError);
		ReaderManagerPlugin::stopScan(pError);
	}
}
