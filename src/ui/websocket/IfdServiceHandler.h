/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 */

#pragma once

#include "IfdServer.h"
#include "context/WorkflowContext.h"
#include "WorkflowRequest.h"

#include <QByteArray>
#include <QJsonObject>
#include <QSharedPointer>


namespace governikus
{

/*!
 * \brief "Smartphone as card reader": lets a remote AusweisApp on a PC use this
 *        device's NFC reader.
 *
 * Upstream drives this from RemoteServiceModel, which lives in the QML UI that
 * an INTEGRATED_SDK build never compiles -- so the whole server exists in the
 * binary with nothing able to start it. This is that missing caller.
 *
 * It lives beside the WebSocket UI rather than in the JSON API because
 * AusweisAppUiWebsocket already links AusweisAppIfdRemote and
 * AusweisAppWorkflowsIfd, so nothing new is pulled into the build, and because
 * the JSON API is a published protocol that is better left untouched: these
 * commands are a private extension of this fork, and keeping them in one file
 * keeps the rebase surface to one file.
 */
class IfdServiceHandler
	: public QObject
{
	Q_OBJECT

	private:
		QSharedPointer<IfdServer> mServer;
		QSharedPointer<WorkflowContext> mContext;
		QByteArray mPairingCode;

		void send(const QJsonObject& pMessage);
		void sendStatus();
		void sendError(const QString& pError);

		void handleStart(const QJsonObject& pCommand);
		void handleStop();
		void handleSetName(const QJsonObject& pCommand);
		void handleSetSecret(const QString& pCmd, const QJsonObject& pCommand);
		void handleForget(const QJsonObject& pCommand);
		[[nodiscard]] QString passwordKind() const;

	private Q_SLOTS:
		void onStateChanged(const QString& pNewState);
		void onPskChanged(const QByteArray& pPsk);
		void onConnectedChanged(bool pConnected);
		void onIsRunningChanged();
		void onPairingCompleted(const QSslCertificate& pCertificate);
		void onSocketError(QAbstractSocket::SocketError pError);

	public:
		IfdServiceHandler();
		~IfdServiceHandler() override = default;

		/*!
		 * \brief Consume an IFD_* command.
		 * \return true when the message was an IFD command and must not be
		 *         forwarded to the standard JSON API.
		 */
		bool process(const QByteArray& pMessage);

		/*! \brief Called for every workflow; ignores anything but our own. */
		void onWorkflowStarted(const QSharedPointer<WorkflowContext>& pContext);
		void onWorkflowFinished(const QSharedPointer<WorkflowContext>& pContext);

		/*! \brief Drop the server when the SDK client goes away. */
		void reset();

	Q_SIGNALS:
		void fireMessage(const QByteArray& pMessage);
		void fireWorkflowRequested(const QSharedPointer<WorkflowRequest>& pRequest);
};

} // namespace governikus
