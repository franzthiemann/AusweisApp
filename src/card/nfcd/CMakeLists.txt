#####################################################################
# The ReaderManagerPlugin for nfcd.
#
# nfcd (https://github.com/sailfishos/nfcd) bridges the Android NFC HAL
# over binder to a system-bus D-Bus API. It is the only route to raw
# ISO-DEP APDU exchange on Ubuntu Touch, which has no NFC API of its own.
#
# The plugin registers as ReaderManagerPluginType::NFC. That is not a
# workaround: the Qt NFC plugin is only built when Qt::Nfc exists (Android
# and iOS), so the two can never collide, and reusing the type means the
# whole UI, SDK protocol and workflow layer treat this exactly like any
# other NFC reader -- no enum change, no QML change, no new translations.
#
# Supported: Ubuntu Touch on Halium 9+ ports that expose an NFC HAL.
#####################################################################

ADD_PLATFORM_LIBRARY(AusweisAppCardNfcd)

target_link_libraries(AusweisAppCardNfcd ${Qt}::Core ${Qt}::DBus AusweisAppGlobal AusweisAppCard)
target_compile_definitions(AusweisAppCardNfcd PRIVATE QT_STATICPLUGIN)
