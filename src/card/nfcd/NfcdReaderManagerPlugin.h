/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 */

#pragma once

#include "NfcdReader.h"
#include "ReaderManagerPlugin.h"

#include <QDBusServiceWatcher>
#include <QScopedPointer>
#include <QString>


namespace governikus
{

/*!
 * \brief ReaderManagerPlugin backed by nfcd on Ubuntu Touch.
 *
 * nfcd is not installed by default and may be restarted independently of us, so
 * the plugin follows its name on the system bus rather than assuming it is
 * there, and rebuilds the reader whenever it comes back.
 */
class NfcdReaderManagerPlugin
	: public ReaderManagerPlugin
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID "governikus.ReaderManagerPlugin" FILE "metadata.json")
	Q_INTERFACES(governikus::ReaderManagerPlugin)

	private:
		QScopedPointer<QDBusServiceWatcher> mServiceWatcher;
		QScopedPointer<NfcdReader> mReader;

		void createReader();
		void destroyReader();
		[[nodiscard]] static QString findAdapter();
		[[nodiscard]] static bool isAdapterPowered(const QString& pAdapterPath);

	private Q_SLOTS:
		void onServiceRegistered();
		void onServiceUnregistered();
		void onReaderDisconnected();

	public:
		NfcdReaderManagerPlugin();
		~NfcdReaderManagerPlugin() override = default;

		[[nodiscard]] QList<Reader*> getReaders() const override;

		void init() override;
		void shutdown() override;

		void startScan(bool pAutoConnect) override;
		void stopScan(const QString& pError = QString()) override;
};

} // namespace governikus
