#####################################################################
# The module card is responsible to handle all generic card commands
# (APDU).
#
# It provides an interface that can be implemented by plugins like
# PCSC or Android-NFC called ReaderManagerPlugin.
# Also it provides a ReaderManager that can be used as a generic
# control interface. It handles access and commands to specific
# reader, cards and commands to the identity card.
#####################################################################

add_subdirectory(base)

if(DESKTOP)
	# drivers (ReaderDetector) is not optional -- card/base includes its header
	# and links it -- and it needs only libudev, so it builds fine here. Only
	# the PC/SC plugin is dropped: Ubuntu Touch has no pcscd and confinement
	# grants no USB access, so it could never find a reader.
	if(NOT UBUNTU_TOUCH)
		add_subdirectory(pcsc)
	endif()
	add_subdirectory(drivers)
endif()

if(TARGET ${Qt}::Nfc)
	add_subdirectory(nfc)
endif()

if(UBUNTU_TOUCH)
	add_subdirectory(nfcd)
endif()

if(USE_SMARTEID)
	add_subdirectory(smart)
endif()

add_subdirectory(simulator)
