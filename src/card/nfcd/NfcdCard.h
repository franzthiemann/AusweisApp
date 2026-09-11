/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 */

#pragma once

#include "Card.h"

#include <QDBusObjectPath>
#include <QString>


namespace governikus
{

/*!
 * \brief A card reached through nfcd's org.sailfishos.nfc.Tag interface.
 *
 * transmit() is a straight byte-level pass-through: CommandApdu already
 * serialises itself -- including the extended-length forms (cases 2e/3e/4e)
 * that the eID flow needs -- and nfcd's Transceive takes raw bytes, so no
 * re-encoding happens anywhere in between.
 */
class NfcdCard
	: public Card
{
	Q_OBJECT

	private:
		const QString mTagPath;
		bool mConnected;
		bool mIsValid;

	public:
		explicit NfcdCard(const QString& pTagPath);
		~NfcdCard() override = default;

		[[nodiscard]] bool isValid() const;
		[[nodiscard]] const QString& getTagPath() const;
		void invalidate();

		CardReturnCode establishConnection() override;
		CardReturnCode releaseConnection() override;
		[[nodiscard]] bool isConnected() const override;

		ResponseApduResult transmit(const CommandApdu& pCmd) override;
};

} // namespace governikus
