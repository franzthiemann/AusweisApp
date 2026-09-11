/**
 * Copyright (c) 2026 Franz Thiemann
 *
 * Modification of AusweisApp (Governikus GmbH & Co. KG), licensed under EUPL v1.2.
 *
 * D-Bus names of nfcd (https://github.com/sailfishos/nfcd), as packaged by
 * UBports. nfcd owns its name on the SYSTEM bus.
 */

#pragma once

#include <QLatin1String>

namespace governikus
{
namespace nfcd
{

inline const auto SERVICE = QLatin1String("org.sailfishos.nfc.daemon");
inline const auto PATH_DAEMON = QLatin1String("/");

inline const auto IFACE_DAEMON = QLatin1String("org.sailfishos.nfc.Daemon");
inline const auto IFACE_ADAPTER = QLatin1String("org.sailfishos.nfc.Adapter");
inline const auto IFACE_TAG = QLatin1String("org.sailfishos.nfc.Tag");

/*
 * Daemon mode bitmask (nfc_types.h NFC_MODE_*). We only ever request
 * reader/writer polling; card emulation and peer-to-peer stay untouched so we
 * do not disturb other users of the radio.
 */
constexpr uint MODE_READER_WRITER = 0x02;

/* Tag protocol bitmask (nfc_types.h NFC_PROTOCOL_*). */
constexpr uint PROTOCOL_T4A_TAG = 0x08;
constexpr uint PROTOCOL_T4B_TAG = 0x10;

/*
 * Tag.Transceive was added in Tag interface version 4. Below that there is no
 * way to move raw APDUs, so the plugin refuses the tag rather than silently
 * degrading.
 */
constexpr int MIN_TAG_INTERFACE_VERSION = 4;

/*
 * A single APDU exchange with the ID card. PACE and Chip Authentication involve
 * on-card crypto, so this is generous compared to an ordinary tag read.
 */
constexpr int TRANSCEIVE_TIMEOUT_MS = 5000;

} // namespace nfcd
} // namespace governikus
