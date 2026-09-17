/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 */

#include "IfdServiceHandler.h"

#include "AppSettings.h"
#include "SmartCardDefinitions.h"
#include "Env.h"
#include "ReaderManager.h"
#include "RemoteIfdServer.h"
#include "TlsChecker.h"
#include "controller/IfdServiceController.h"
#include "context/IfdServiceContext.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>


Q_DECLARE_LOGGING_CATEGORY(ifd)

using namespace governikus;


namespace
{
const QLatin1String CMD("cmd");
const QLatin1String MSG("msg");
} // namespace


IfdServiceHandler::IfdServiceHandler()
	: QObject()
	, mServer()
	, mContext()
	, mPairingCode()
{
}


void IfdServiceHandler::send(const QJsonObject& pMessage)
{
	Q_EMIT fireMessage(QJsonDocument(pMessage).toJson(QJsonDocument::Compact));
}


void IfdServiceHandler::sendStatus()
{
	const auto& settings = Env::getSingleton<AppSettings>()->getRemoteServiceSettings();

	QJsonObject msg;
	msg[MSG] = QStringLiteral("IFD_STATUS");
	msg[QLatin1String("running")] = mServer && mServer->isRunning();
	msg[QLatin1String("connected")] = mServer && mServer->isConnected();
	msg[QLatin1String("pairing")] = !mPairingCode.isEmpty();
	msg[QLatin1String("name")] = settings.getDeviceName();

	QJsonArray paired;
	const auto& infos = settings.getRemoteInfos();
	for (const auto& info : infos)
	{
		QJsonObject entry;
		entry[QLatin1String("name")] = info.getNameEscaped();
		entry[QLatin1String("fingerprint")] = QString::fromLatin1(info.getFingerprint().toHex());
		paired << entry;
	}
	msg[QLatin1String("paired")] = paired;

	send(msg);
}


void IfdServiceHandler::sendError(const QString& pError)
{
	qCWarning(ifd) << "IFD service:" << pError;

	QJsonObject msg;
	msg[MSG] = QStringLiteral("IFD_ERROR");
	msg[QLatin1String("error")] = pError;
	send(msg);
}


bool IfdServiceHandler::process(const QByteArray& pMessage)
{
	QJsonParseError error{};
	const auto& doc = QJsonDocument::fromJson(pMessage, &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject())
	{
		// Not ours to judge -- let the standard API produce its own error.
		return false;
	}

	const auto& obj = doc.object();
	const auto& cmd = obj[CMD].toString();
	if (!cmd.startsWith(QLatin1String("IFD_")))
	{
		return false;
	}

	if (cmd == QLatin1String("IFD_START"))
	{
		handleStart(obj);
	}
	else if (cmd == QLatin1String("IFD_STOP"))
	{
		handleStop();
	}
	else if (cmd == QLatin1String("IFD_SET_NAME"))
	{
		handleSetName(obj);
	}
	else if (cmd == QLatin1String("IFD_GET_STATUS"))
	{
		sendStatus();
	}
	else if (cmd == QLatin1String("IFD_FORGET"))
	{
		handleForget(obj);
	}
	else if (cmd == QLatin1String("IFD_SET_PIN")
			|| cmd == QLatin1String("IFD_SET_CAN")
			|| cmd == QLatin1String("IFD_SET_PUK"))
	{
		handleSetSecret(cmd, obj);
	}
	else
	{
		sendError(QStringLiteral("Unknown IFD command: %1").arg(cmd));
	}

	return true;
}


void IfdServiceHandler::handleStart(const QJsonObject& pCommand)
{
	if (mServer && mServer->isRunning())
	{
		// Already up. Honour a pairing change, since that is the usual reason
		// to ask twice, and report rather than silently ignoring.
		mServer->setPairing(pCommand[QLatin1String("pairing")].toBool(false));
		sendStatus();
		return;
	}

	const auto& server = QSharedPointer<IfdServer>(new RemoteIfdServer());
	connect(server.data(), &IfdServer::firePskChanged, this, &IfdServiceHandler::onPskChanged);
	connect(server.data(), &IfdServer::fireConnectedChanged, this, &IfdServiceHandler::onConnectedChanged);
	connect(server.data(), &IfdServer::fireIsRunningChanged, this, &IfdServiceHandler::onIsRunningChanged);
	connect(server.data(), &IfdServer::firePairingCompleted, this, &IfdServiceHandler::onPairingCompleted);
	connect(server.data(), &IfdServer::fireSocketError, this, &IfdServiceHandler::onSocketError);
	mServer = server;

	// The workflow's StateStartIfdService is what actually calls start(), using
	// the device name from settings; pairing has to be armed after that.
	Q_EMIT fireWorkflowRequested(
			WorkflowRequest::create<IfdServiceController, IfdServiceContext>(server));

	if (pCommand[QLatin1String("pairing")].toBool(false))
	{
		mServer->setPairing(true);
	}
}


void IfdServiceHandler::handleStop()
{
	if (mContext)
	{
		// Cancelling the workflow is the supported way down: its
		// StateStopIfdService clears pairing and stops the server.
		mContext->killWorkflow();
		return;
	}

	if (mServer)
	{
		mServer->setPairing(false);
		mServer->stop();
		mServer.clear();
	}
	sendStatus();
}


void IfdServiceHandler::handleSetName(const QJsonObject& pCommand)
{
	const auto& name = pCommand[QLatin1String("name")].toString().trimmed();
	if (name.isEmpty())
	{
		sendError(QStringLiteral("IFD_SET_NAME requires a non-empty name"));
		return;
	}

	// setDeviceName persists through AbstractSettings itself; there is no
	// instance save() to call.
	Env::getSingleton<AppSettings>()->getRemoteServiceSettings().setDeviceName(name);
	sendStatus();
}


void IfdServiceHandler::handleForget(const QJsonObject& pCommand)
{
	const auto& hex = pCommand[QLatin1String("fingerprint")].toString();
	if (hex.isEmpty())
	{
		sendError(QStringLiteral("IFD_FORGET requires a fingerprint"));
		return;
	}

	const auto& fingerprint = QByteArray::fromHex(hex.toLatin1());
	auto& settings = Env::getSingleton<AppSettings>()->getRemoteServiceSettings();
	if (settings.getRemoteInfo(fingerprint).getFingerprint().isEmpty())
	{
		sendError(QStringLiteral("No paired device with that fingerprint"));
		return;
	}

	// Dropping the trust alone would leave an already-established connection
	// running until the next restart, so "forgotten" would not be true yet. If
	// the forgotten device is the one currently connected, end the session too.
	const bool forgettingCurrentPeer = mServer && mServer->isConnected()
			&& RemoteServiceSettings::generateFingerprint(
					TlsChecker::getRootCertificate({mServer->getCurrentCertificate()})) == fingerprint;

	settings.removeTrustedCertificate(fingerprint);

	if (forgettingCurrentPeer && mContext)
	{
		qCDebug(ifd) << "Forgot the connected device; ending its session";
		mContext->killWorkflow();
	}

	sendStatus();
}


void IfdServiceHandler::onPskChanged(const QByteArray& pPsk)
{
	mPairingCode = pPsk;

	if (!pPsk.isEmpty())
	{
		QJsonObject msg;
		msg[MSG] = QStringLiteral("IFD_PAIRING");
		msg[QLatin1String("code")] = QString::fromLatin1(pPsk);
		send(msg);
	}
	sendStatus();
}


void IfdServiceHandler::onConnectedChanged(bool pConnected)
{
	qCDebug(ifd) << "IFD service peer connected:" << pConnected;
	sendStatus();
}


void IfdServiceHandler::onIsRunningChanged()
{
	sendStatus();
}


void IfdServiceHandler::onPairingCompleted(const QSslCertificate& pCertificate)
{
	Q_UNUSED(pCertificate)

	mPairingCode.clear();

	QJsonObject msg;
	msg[MSG] = QStringLiteral("IFD_PAIRED");
	send(msg);
	sendStatus();
}


void IfdServiceHandler::onStateChanged(const QString& pNewState)
{
	if (!mContext)
	{
		return;
	}

	// The whole point of "smartphone as card reader" is that the secret is typed
	// on the phone, never on the PC: pinPadMode defaults to true and the card is
	// in the user's hand here. So this one state must NOT be waved through --
	// StateEnterPacePasswordIfd::run() continues immediately, and approving it
	// before a secret has been set sends PACE an empty password. Ask the UI, and
	// approve only once it answers.
	if (pNewState == QLatin1String("StateEnterPacePasswordIfd"))
	{
		QJsonObject msg;
		msg[MSG] = QStringLiteral("IFD_ENTER_SECRET");
		msg[QLatin1String("secret")] = passwordKind();
		send(msg);
		return;
	}

	// Everything else is driven by the remote PC, not by a local user.
	mContext->setStateApproved();
}


QString IfdServiceHandler::passwordKind() const
{
	if (!mContext)
	{
		return QStringLiteral("pin");
	}

	switch (mContext->getEstablishPaceChannelType())
	{
		case PacePasswordId::PACE_CAN:
			return QStringLiteral("can");

		case PacePasswordId::PACE_PUK:
			return QStringLiteral("puk");

		default:
			return QStringLiteral("pin");
	}
}


void IfdServiceHandler::handleSetSecret(const QString& pCmd, const QJsonObject& pCommand)
{
	if (!mContext)
	{
		sendError(QStringLiteral("%1 with no active IFD workflow").arg(pCmd));
		return;
	}

	const auto& value = pCommand[QLatin1String("value")].toString();
	if (value.isEmpty())
	{
		sendError(QStringLiteral("%1 requires a value").arg(pCmd));
		return;
	}

	if (pCmd == QLatin1String("IFD_SET_CAN"))
	{
		mContext->setCan(value);
	}
	else if (pCmd == QLatin1String("IFD_SET_PUK"))
	{
		mContext->setPuk(value);
	}
	else
	{
		mContext->setPin(value);
	}

	// Now, and only now, let the state proceed.
	mContext->setStateApproved();
}


void IfdServiceHandler::onSocketError(QAbstractSocket::SocketError pError)
{
	sendError(QStringLiteral("Socket error: %1").arg(static_cast<int>(pError)));
}


void IfdServiceHandler::onWorkflowStarted(const QSharedPointer<WorkflowContext>& pContext)
{
	if (!pContext.objectCast<IfdServiceContext>())
	{
		return;
	}

	mContext = pContext;

	// AppController aborts any workflow no UI claimed ("Workflow was not claimed
	// by any UI... aborting"), so this is not optional bookkeeping -- without it
	// StateStartIfdService is entered and immediately cancelled.
	mContext->claim(this);

	// Every state waits for the claiming UI to approve it. Without this the
	// workflow enters StateStartIfdService and simply sits there -- the server
	// is never started and nothing says why.
	connect(mContext.data(), &WorkflowContext::fireStateChanged,
			this, &IfdServiceHandler::onStateChanged);

	// Without NFC in the workflow's reader set, StateSelectReader's ReaderFilter
	// discards the nfcd reader and the remote PC is offered nothing to read.
	mContext->setReaderPluginTypes({ReaderManagerPluginType::NFC});

	// Deliberately NOT approving here. setStateApproved only emits when the
	// value CHANGES, and only onExit resets it to false -- so approving before
	// the first state has connected its handler fires the signal into nothing
	// and that state then waits forever. Let onStateChanged do it.
	sendStatus();
}


void IfdServiceHandler::onWorkflowFinished(const QSharedPointer<WorkflowContext>& pContext)
{
	if (mContext != pContext)
	{
		return;
	}

	mContext.clear();
	mServer.clear();
	mPairingCode.clear();
	sendStatus();
}


void IfdServiceHandler::reset()
{
	if (mContext)
	{
		mContext->killWorkflow();
		mContext.clear();
	}
	mServer.clear();
	mPairingCode.clear();
}
