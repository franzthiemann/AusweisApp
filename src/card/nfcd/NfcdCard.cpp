/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 */

#include "NfcdCard.h"

#include "NfcdDbus.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QLoggingCategory>


using namespace governikus;


Q_DECLARE_LOGGING_CATEGORY(card_nfc)


NfcdCard::NfcdCard(const QString& pTagPath)
	: Card()
	, mTagPath(pTagPath)
	, mConnected(false)
	, mIsValid(true)
{
	qCDebug(card_nfc) << "Card created for tag" << mTagPath;
}


bool NfcdCard::isValid() const
{
	return mIsValid;
}


const QString& NfcdCard::getTagPath() const
{
	return mTagPath;
}


void NfcdCard::invalidate()
{
	mIsValid = false;
}


CardReturnCode NfcdCard::establishConnection()
{
	if (isConnected())
	{
		qCCritical(card_nfc) << "Card is already connected";
		return CardReturnCode::COMMAND_FAILED;
	}

	if (!mIsValid)
	{
		qCWarning(card_nfc) << "Tag is no longer valid";
		return CardReturnCode::COMMAND_FAILED;
	}

	mConnected = true;
	return CardReturnCode::OK;
}


CardReturnCode NfcdCard::releaseConnection()
{
	if (!isConnected())
	{
		qCCritical(card_nfc) << "Card is already disconnected";
		return CardReturnCode::COMMAND_FAILED;
	}

	mConnected = false;
	return CardReturnCode::OK;
}


bool NfcdCard::isConnected() const
{
	return mConnected;
}


ResponseApduResult NfcdCard::transmit(const CommandApdu& pCmd)
{
	if (!mIsValid)
	{
		qCWarning(card_nfc) << "Tag is no longer valid";
		return {CardReturnCode::COMMAND_FAILED};
	}

	qCDebug(card_nfc) << "Transmit command APDU:" << pCmd;

	// CommandApdu serialises itself, extended-length forms included, and
	// Transceive takes the bytes as-is. Nothing re-encodes the APDU in between,
	// so whether extended length works comes down to the HAL alone.
	auto msg = QDBusMessage::createMethodCall(nfcd::SERVICE, mTagPath,
			nfcd::IFACE_TAG, QStringLiteral("Transceive"));
	msg << QByteArray(pCmd);

	const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block,
			nfcd::TRANSCEIVE_TIMEOUT_MS);

	if (reply.type() != QDBusMessage::ReplyMessage)
	{
		qCWarning(card_nfc) << "Transceive failed:" << reply.errorName() << reply.errorMessage();
		return {CardReturnCode::COMMAND_FAILED};
	}

	const auto args = reply.arguments();
	if (args.isEmpty())
	{
		qCWarning(card_nfc) << "Transceive returned no response";
		return {CardReturnCode::COMMAND_FAILED};
	}

	const QByteArray recvBuffer = args.constFirst().toByteArray();
	if (recvBuffer.size() < 2)
	{
		// Anything shorter cannot even carry a status word.
		qCWarning(card_nfc) << "Truncated response of" << recvBuffer.size() << "byte(s)";
		return {CardReturnCode::COMMAND_FAILED};
	}

	const ResponseApdu responseApdu(recvBuffer);
	qCDebug(card_nfc) << "Transmit response APDU:" << responseApdu;
	return {CardReturnCode::OK, responseApdu};
}
