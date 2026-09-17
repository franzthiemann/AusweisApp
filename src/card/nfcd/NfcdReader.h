/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 */

#pragma once

#include "NfcdCard.h"
#include "Reader.h"

#include <QDBusObjectPath>
#include <QList>
#include <QScopedPointer>
#include <QStringList>
#include <QTimer>
#include <QString>


namespace governikus
{

/*!
 * \brief The single logical reader backed by an nfcd adapter.
 *
 * nfcd reports tags as D-Bus object paths under the adapter. Arrival and
 * removal both come from Adapter.TagsChanged, so there is exactly one event
 * source and no polling.
 */
class NfcdReader
	: public ConnectableReader
{
	Q_OBJECT

	private:
		const QString mAdapterPath;
		QScopedPointer<NfcdCard, QScopedPointerDeleteLater> mCard;
		uint mModeRequestId;
		QTimer mPollTimer;
		QStringList mKnownTags;
		int mUnreadableAttempts = 0;

		/*! How often to re-read a card that is detected but not identifiable
		    before reporting it as unknown. */
		static constexpr int MAX_UNREADABLE_ATTEMPTS = 3;

		static constexpr int POLL_INTERVAL_MS = 700;

		void handleTagArrived(const QString& pTagPath);
		void handleTagLost();
		void releaseTag(const QString& pTagPath) const;
		[[nodiscard]] static int configuredMaxApduLength();

	private Q_SLOTS:
		void onTagsChanged(const QList<QDBusObjectPath>& pTags);
		void pollTags();

	public:
		explicit NfcdReader(const QString& pAdapterPath);
		~NfcdReader() override;

		[[nodiscard]] Card* getCard() const override;

		void connectReader() override;
		void disconnectReader(const QString& pError = QString()) override;
};

} // namespace governikus
