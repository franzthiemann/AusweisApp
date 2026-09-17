/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 */

#include "NfcdReader.h"

#include "NfcdDbus.h"
#include "SmartCardDefinitions.h"

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QLoggingCategory>
#include <QStringList>


using namespace governikus;


Q_DECLARE_LOGGING_CATEGORY(card_nfc)


namespace
{

QDBusMessage callTag(const QString& pTagPath, const QString& pMethod)
{
	auto msg = QDBusMessage::createMethodCall(nfcd::SERVICE, pTagPath,
			nfcd::IFACE_TAG, pMethod);
	return QDBusConnection::systemBus().call(msg);
}


} // namespace


NfcdReader::NfcdReader(const QString& pAdapterPath)
	: ConnectableReader(ReaderManagerPluginType::NFC, QStringLiteral("NFC"))
	, mAdapterPath(pAdapterPath)
	, mCard()
	, mModeRequestId(0)
{
	setInfoBasicReader(true);

	// nfcd has no "max transceive length" property, so unlike Android there is
	// nothing to read back and the ReaderInfo default (500, i.e. "sufficient")
	// stands. That default is right wherever the M0 spike passes, and the spike
	// deliberately does NOT report a number to plug in here: what it measures is
	// the size of EF.CardAccess, a property of the card, not a transport limit.
	// The override exists only for a device found to be genuinely capped, where
	// it makes insufficientApduLength() -- and the warning the UI shows -- true.
	if (const int configured = configuredMaxApduLength(); configured > 0)
	{
		setInfoMaxApduLength(configured);
		if (getReaderInfo().insufficientApduLength())
		{
			qCWarning(card_nfc) << "ExtendedLengthApduSupport missing. MaxApduLength:" << configured;
		}
	}

	// "ao" only demarshals into QList<QDBusObjectPath> once that type is known to
	// the D-Bus type system; without it the signal arrives and is dropped.
	qDBusRegisterMetaType<QList<QDBusObjectPath>>();

	const bool subscribed = QDBusConnection::systemBus().connect(nfcd::SERVICE, mAdapterPath,
			nfcd::IFACE_ADAPTER, QStringLiteral("TagsChanged"), this,
			SLOT(onTagsChanged(QList<QDBusObjectPath>)));
	if (!subscribed)
	{
		qCWarning(card_nfc) << "Cannot subscribe to TagsChanged on" << mAdapterPath
							<< "-- falling back to polling";
	}

	// Poll as well as subscribe. nfcd demonstrably emits TagsChanged, but a
	// dropped subscription is invisible at runtime -- the reader simply never
	// notices a card, which is indistinguishable from bad antenna placement.
	// Polling only runs between connectReader() and disconnectReader(), i.e.
	// exactly while a card is being waited for, so it costs nothing at idle.
	connect(&mPollTimer, &QTimer::timeout, this, &NfcdReader::pollTags);
	mPollTimer.setInterval(POLL_INTERVAL_MS);
}


void NfcdReader::pollTags()
{
	auto msg = QDBusMessage::createMethodCall(nfcd::SERVICE, mAdapterPath,
			nfcd::IFACE_ADAPTER, QStringLiteral("GetTags"));

	const QDBusMessage reply = QDBusConnection::systemBus().call(msg);
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
	{
		return;
	}

	onTagsChanged(qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().constFirst()));
}


NfcdReader::~NfcdReader()
{
	disconnectReader();
}


int NfcdReader::configuredMaxApduLength()
{
	bool ok = false;
	const int length = qEnvironmentVariableIntValue("EIDTOUCH_MAX_APDU_LENGTH", &ok);
	return ok ? length : -1;
}


Card* NfcdReader::getCard() const
{
	if (mCard && mCard->isValid())
	{
		return mCard.data();
	}

	return nullptr;
}


void NfcdReader::connectReader()
{
	if (mModeRequestId != 0)
	{
		return;
	}

	// Ask nfcd to poll for tags. The request is refcounted and keyed by id, so
	// releasing ours in disconnectReader() leaves any other client's polling
	// untouched.
	auto msg = QDBusMessage::createMethodCall(nfcd::SERVICE, nfcd::PATH_DAEMON,
			nfcd::IFACE_DAEMON, QStringLiteral("RequestMode"));
	msg << nfcd::MODE_READER_WRITER << 0u;

	const QDBusMessage reply = QDBusConnection::systemBus().call(msg);
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
	{
		qCWarning(card_nfc) << "Cannot enable reader mode:" << reply.errorMessage();
		Q_EMIT fireReaderDisconnected();
		return;
	}

	mModeRequestId = reply.arguments().constFirst().toUInt();
	qCDebug(card_nfc) << "Reader mode enabled, request id" << mModeRequestId;

	mPollTimer.start();
	pollTags();   // a card may already be on the antenna
}


void NfcdReader::disconnectReader(const QString& pError)
{
	Q_UNUSED(pError)

	mPollTimer.stop();
	mKnownTags.clear();
	mUnreadableAttempts = 0;
	handleTagLost();

	if (mModeRequestId == 0)
	{
		return;
	}

	auto msg = QDBusMessage::createMethodCall(nfcd::SERVICE, nfcd::PATH_DAEMON,
			nfcd::IFACE_DAEMON, QStringLiteral("ReleaseMode"));
	msg << mModeRequestId;
	QDBusConnection::systemBus().call(msg, QDBus::NoBlock);
	mModeRequestId = 0;
}


void NfcdReader::onTagsChanged(const QList<QDBusObjectPath>& pTags)
{
	QStringList paths;
	paths.reserve(pTags.size());
	for (const auto& tag : pTags)
	{
		paths << tag.path();
	}
	// Arrival and removal come from both the TagsChanged signal and the poll, so
	// act (and log) only on an actual change -- otherwise the poll narrates the
	// same unchanged tag several times a second and buries everything else.
	if (paths == mKnownTags)
	{
		return;
	}
	if (!paths.isEmpty() && !mKnownTags.isEmpty() && paths != mKnownTags)
	{
		// A different tag is a different presentation of the card.
		mUnreadableAttempts = 0;
	}
	mKnownTags = paths;
	qCDebug(card_nfc) << "Tags:" << paths;

	if (mCard && !paths.contains(mCard->getTagPath()))
	{
		handleTagLost();
	}

	if (!mCard && !paths.isEmpty())
	{
		handleTagArrived(paths.constFirst());
	}
}


void NfcdReader::handleTagArrived(const QString& pTagPath)
{
	const QDBusMessage versionReply = callTag(pTagPath, QStringLiteral("GetInterfaceVersion"));
	if (versionReply.type() != QDBusMessage::ReplyMessage || versionReply.arguments().isEmpty())
	{
		qCWarning(card_nfc) << "Cannot query tag interface version:" << versionReply.errorMessage();
		return;
	}

	if (const int version = versionReply.arguments().constFirst().toInt();
			version < nfcd::MIN_TAG_INTERFACE_VERSION)
	{
		// Without Transceive there is no way to move APDUs at all, so refuse the
		// tag rather than present a reader that cannot read.
		qCWarning(card_nfc) << "nfcd Tag interface version" << version
							<< "is too old; Transceive needs" << nfcd::MIN_TAG_INTERFACE_VERSION;
		return;
	}

	const QDBusMessage allReply = callTag(pTagPath, QStringLiteral("GetAll"));
	if (allReply.type() != QDBusMessage::ReplyMessage || allReply.arguments().size() < 6)
	{
		qCWarning(card_nfc) << "Cannot query tag:" << allReply.errorMessage();
		return;
	}

	// GetAll: version, present, technology, protocol, type, interfaces, ndef
	const auto args = allReply.arguments();
	const uint protocol = args.at(3).toUInt();

	if (!(protocol & (nfcd::PROTOCOL_T4A_TAG | nfcd::PROTOCOL_T4B_TAG)))
	{
		qCDebug(card_nfc) << "Tag is not ISO-DEP, protocol" << Qt::hex << protocol;
		return;
	}

	// Acquire(true) waits for any other client to let go, so a competing NDEF
	// reader cannot make us drop the card mid-authentication.
	auto acquire = QDBusMessage::createMethodCall(nfcd::SERVICE, pTagPath,
			nfcd::IFACE_TAG, QStringLiteral("Acquire"));
	acquire << true;
	if (const QDBusMessage reply = QDBusConnection::systemBus().call(acquire);
			reply.type() != QDBusMessage::ReplyMessage)
	{
		qCWarning(card_nfc) << "Cannot acquire tag:" << reply.errorMessage();
		return;
	}

	mCard.reset(new NfcdCard(pTagPath));

	fetchCardInfo();
	if (!getCard())
	{
		removeCardInfo();
		releaseTag(pTagPath);
		mCard.reset();
		return;
	}

	// A card can be detected as ISO-DEP and still fail to be read: the reads
	// that identify it (EF.DIR, EF.CardAccess, the retry counter) are several
	// APDU round trips, and a card only just within range drops one of them.
	// CardInfoFactory reports that as CardType::UNKNOWN, and reporting it
	// onwards presents the workflow with a card it cannot use and the user with
	// nothing to do about it.
	//
	// Retry a couple of times instead -- the poll comes back every 700ms and the
	// card is in the user's hand, so a marginal read usually succeeds on the
	// next attempt. Only give up and report UNKNOWN once the tag has had a fair
	// chance, so that a genuinely unsupported card still says so rather than
	// retrying in silence forever.
	if (getReaderInfo().getCardInfo().getCardType() == CardType::UNKNOWN
			&& mUnreadableAttempts < MAX_UNREADABLE_ATTEMPTS)
	{
		mUnreadableAttempts++;
		qCDebug(card_nfc) << "Card not readable yet on" << pTagPath
						  << "- attempt" << mUnreadableAttempts << "of" << MAX_UNREADABLE_ATTEMPTS;
		removeCardInfo();
		releaseTag(pTagPath);
		mCard.reset();
		// Forget the tag so the next poll treats it as newly arrived.
		mKnownTags.clear();
		return;
	}
	mUnreadableAttempts = 0;

	setCardInfoTagType(protocol & nfcd::PROTOCOL_T4A_TAG
			? CardInfo::TagType::NFC_4A
			: CardInfo::TagType::NFC_4B);

	qCInfo(card_nfc) << "Card inserted:" << getReaderInfo().getCardInfo();
	Q_EMIT fireCardInserted(getReaderInfo());
}


void NfcdReader::handleTagLost()
{
	if (!mCard)
	{
		return;
	}

	const QString tagPath = mCard->getTagPath();
	mCard->invalidate();
	mCard.reset();
	releaseTag(tagPath);

	if (getReaderInfo().getCardInfo().getCardType() != CardType::NONE)
	{
		removeCardInfo();
		qCInfo(card_nfc) << "Card removed";
		Q_EMIT fireCardRemoved(getReaderInfo());
	}
}


void NfcdReader::releaseTag(const QString& pTagPath) const
{
	auto msg = QDBusMessage::createMethodCall(nfcd::SERVICE, pTagPath,
			nfcd::IFACE_TAG, QStringLiteral("Release"));
	QDBusConnection::systemBus().call(msg, QDBus::NoBlock);
}
