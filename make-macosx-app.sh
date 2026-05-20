#!/bin/bash

PRODUCT_NAME="Lilium Arena"
VERSION="1.36"
CLIENTBIN="liliumarena"
SERVERBIN="liliumarena-server"
RENDERER_PREFIX="liliumarena-renderer-"
BASEGAME="baseq3"
MISSIONPACK="missionpack"
PROTOCOL_HANDLER="quake3"
BUILD_SERVER=1
BUILD_RENDERER_OPENGL1=1
BUILD_RENDERER_OPENGL2=1
BUILD_BASEGAME=1
BUILD_MISSIONPACK=1

# set this to a reverse URL that you own, i.e. io.github.<yourusername>.${CLIENTBIN}
BUNDLE_IDENTIFIER="moe.clover.${CLIENTBIN}"

BUNDLE_COPYRIGHT="${PRODUCT_NAME} Copyright © 1999-2005 id Software, 2005-2025 ioquake3 contributors, 2008-2026 zturtleman."

# Let's make the user give us a target to work with.
# architecture is assumed universal if not specified, and is optional.
# if arch is defined, it we will store the .app bundle in the target arch build directory
if [ $# == 0 ] || [ $# -gt 2 ]; then
	echo "Usage:   $0 target <arch>"
	echo "Example: $0 release x86"
	echo "Valid targets are:"
	echo " release"
	echo " debug"
	echo
	echo "Optional architectures are:"
	echo " x86"
	echo " x86_64"
	echo " ppc"
	echo " arm64"
	echo
	exit 1
fi

# validate target name
if [ "$1" == "release" ]; then
	TARGET_NAME="release"
elif [ "$1" == "debug" ]; then
	TARGET_NAME="debug"
else
	echo "Invalid target: $1"
	echo "Valid targets are:"
	echo " release"
	echo " debug"
	exit 1
fi

CURRENT_ARCH=""

# validate the architecture if it was specified
if [ "$2" != "" ]; then
	if [ "$2" == "x86" ]; then
		CURRENT_ARCH="x86"
	elif [ "$2" == "x86_64" ]; then
		CURRENT_ARCH="x86_64"
	elif [ "$2" == "ppc" ]; then
		CURRENT_ARCH="ppc"
	elif [ "$2" == "arm64" ]; then
		CURRENT_ARCH="arm64"
	else
		echo "Invalid architecture: $2"
		echo "Valid architectures are:"
		echo " x86"
		echo " x86_64"
		echo " ppc"
		echo " arm64"
		echo
		exit 1
	fi
fi

# symlinkArch() creates a symlink with the architecture suffix.
# meant for universal binaries, but also handles the way this script generates
# application bundles for a single architecture as well.
function symlinkArch()
{
    EXT="dylib"
    SEP="${3}"
    SRCFILE="${1}"
    DSTFILE="${2}${SEP}"
    DSTPATH="${4}"

    if [ ! -e "${DSTPATH}/${SRCFILE}.${EXT}" ]; then
        echo "**** ERROR: missing ${SRCFILE}.${EXT} from ${MACOS}"
        exit 1
    fi

    if [ ! -d "${DSTPATH}" ]; then
        echo "**** ERROR: path not found ${DSTPATH}"
        exit 1
    fi

    pushd "${DSTPATH}" > /dev/null

    IS32=`file "${SRCFILE}.${EXT}" | grep "i386"`
    IS64=`file "${SRCFILE}.${EXT}" | grep "x86_64"`
    ISPPC=`file "${SRCFILE}.${EXT}" | grep "ppc"`
    ISARM=`file "${SRCFILE}.${EXT}" | grep "arm64"`

    if [ "${IS32}" != "" ]; then
        if [ ! -L "${DSTFILE}x86.${EXT}" ]; then
            ln -s "${SRCFILE}.${EXT}" "${DSTFILE}x86.${EXT}"
        fi
    elif [ -L "${DSTFILE}x86.${EXT}" ]; then
        rm "${DSTFILE}x86.${EXT}"
    fi

    if [ "${IS64}" != "" ]; then
        if [ ! -L "${DSTFILE}x86_64.${EXT}" ]; then
            ln -s "${SRCFILE}.${EXT}" "${DSTFILE}x86_64.${EXT}"
        fi
    elif [ -L "${DSTFILE}x86_64.${EXT}" ]; then
        rm "${DSTFILE}x86_64.${EXT}"
    fi

    if [ "${ISPPC}" != "" ]; then
        if [ ! -L "${DSTFILE}ppc.${EXT}" ]; then
            ln -s "${SRCFILE}.${EXT}" "${DSTFILE}ppc.${EXT}"
        fi
    elif [ -L "${DSTFILE}ppc.${EXT}" ]; then
        rm "${DSTFILE}ppc.${EXT}"
    fi

    if [ "${ISARM}" != "" ]; then
        if [ ! -L "${DSTFILE}arm64.${EXT}" ]; then
            ln -s "${SRCFILE}.${EXT}" "${DSTFILE}arm64.${EXT}"
        fi
    elif [ -L "${DSTFILE}arm64.${EXT}" ]; then
        rm "${DSTFILE}arm64.${EXT}"
    fi

    popd > /dev/null
}

SEARCH_ARCHS="																	\
	x86																			\
	x86_64																		\
	ppc																			\
	arm64																		\
"

HAS_LIPO=`command -v lipo`
HAS_CP=`command -v cp`

# if lipo is not available, we cannot make a universal binary, print a warning
if [ ! -x "${HAS_LIPO}" ] && [ "${CURRENT_ARCH}" == "" ]; then
	CURRENT_ARCH=`uname -m`
	if [ "${CURRENT_ARCH}" == "i386" ]; then CURRENT_ARCH="x86"; fi
	echo "$0 cannot make a universal binary, falling back to architecture ${CURRENT_ARCH}"
fi

# if the optional arch parameter is used, replace SEARCH_ARCHS to only work with one
if [ "${CURRENT_ARCH}" != "" ]; then
	SEARCH_ARCHS="${CURRENT_ARCH}"
fi

# select SDL run-time dylib
if [ "${MACOSX_DEPLOYMENT_TARGET}" = "10.5" ] \
  || [ "${MACOSX_DEPLOYMENT_TARGET}" = "10.6" ] \
  || [ "${MACOSX_DEPLOYMENT_TARGET}" = "10.7" ] \
  || [ "${MACOSX_DEPLOYMENT_TARGET}" = "10.8" ]; then
  UNIVERSAL_BINARY=1
else
  UNIVERSAL_BINARY=2
fi

AVAILABLE_ARCHS=""

CLIENTBIN_FILES=""
SERVERBIN_FILES=""
OPENGL1_FILES=""
OPENGL2_FILES=""
BASE_CGAME_FILES=""
BASE_GAME_FILES=""
BASE_UI_FILES=""
MP_CGAME_FILES=""
MP_GAME_FILES=""
MP_UI_FILES=""

CGAME="cgame"
GAME="qagame"
UI="ui"

ICNSDIR="misc"
ICNS="lilium.icns"
PKGINFO="APPL????"

OBJROOT="build"
#BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-${CURRENT_ARCH}"
WRAPPER_EXTENSION="app"
WRAPPER_NAME="${PRODUCT_NAME}.${WRAPPER_EXTENSION}"
CONTENTS_FOLDER_PATH="${WRAPPER_NAME}/Contents"
UNLOCALIZED_RESOURCES_FOLDER_PATH="${CONTENTS_FOLDER_PATH}/Resources"
EXECUTABLE_FOLDER_PATH="${CONTENTS_FOLDER_PATH}/MacOS"

# loop through the architectures to build string lists for each universal binary
for ARCH in $SEARCH_ARCHS; do
	CURRENT_ARCH=${ARCH}
	BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-${CURRENT_ARCH}"
	ARCH_CLIENTBIN="${CLIENTBIN}.${CURRENT_ARCH}"
	ARCH_SERVERBIN="${SERVERBIN}.${CURRENT_ARCH}"
	ARCH_OPENGL1="${RENDERER_PREFIX}opengl1_${CURRENT_ARCH}.dylib"
	ARCH_OPENGL2="${RENDERER_PREFIX}opengl2_${CURRENT_ARCH}.dylib"
	ARCH_CGAME="${CGAME}${CURRENT_ARCH}.dylib"
	ARCH_GAME="${GAME}${CURRENT_ARCH}.dylib"
	ARCH_UI="${UI}${CURRENT_ARCH}.dylib"

	if [ ! -d ${BUILT_PRODUCTS_DIR} ]; then
		CURRENT_ARCH=""
		BUILT_PRODUCTS_DIR=""
		continue
	fi

	# executables
	if [ -e ${BUILT_PRODUCTS_DIR}/${ARCH_CLIENTBIN} ]; then
		CLIENTBIN_FILES="${BUILT_PRODUCTS_DIR}/${ARCH_CLIENTBIN} ${CLIENTBIN_FILES}"
		VALID_ARCHS="${ARCH} ${VALID_ARCHS}"
	else
		continue
	fi
	if [ ${BUILD_SERVER} -eq 1 ]; then
		if [ -e ${BUILT_PRODUCTS_DIR}/${ARCH_SERVERBIN} ]; then
			SERVERBIN_FILES="${BUILT_PRODUCTS_DIR}/${ARCH_SERVERBIN} ${SERVERBIN_FILES}"
		fi
	fi

	# renderers
	if [ ${BUILD_RENDERER_OPENGL1} -eq 1 ]; then
		if [ -e ${BUILT_PRODUCTS_DIR}/${ARCH_OPENGL1} ]; then
			OPENGL1_FILES="${BUILT_PRODUCTS_DIR}/${ARCH_OPENGL1} ${OPENGL1_FILES}"
		fi
	fi
	if [ ${BUILD_RENDERER_OPENGL2} -eq 1 ]; then
		if [ -e ${BUILT_PRODUCTS_DIR}/${ARCH_OPENGL2} ]; then
			OPENGL2_FILES="${BUILT_PRODUCTS_DIR}/${ARCH_OPENGL2} ${OPENGL2_FILES}"
		fi
	fi

	# baseq3
	if [ ${BUILD_BASEGAME} -eq 1 ]; then
		if [ -e ${BUILT_PRODUCTS_DIR}/${BASEGAME}/${ARCH_CGAME} ]; then
			BASE_CGAME_FILES="${BUILT_PRODUCTS_DIR}/${BASEGAME}/${ARCH_CGAME} ${BASE_CGAME_FILES}"
		fi
		if [ -e ${BUILT_PRODUCTS_DIR}/${BASEGAME}/${ARCH_GAME} ]; then
			BASE_GAME_FILES="${BUILT_PRODUCTS_DIR}/${BASEGAME}/${ARCH_GAME} ${BASE_GAME_FILES}"
		fi
		if [ -e ${BUILT_PRODUCTS_DIR}/${BASEGAME}/${ARCH_UI} ]; then
			BASE_UI_FILES="${BUILT_PRODUCTS_DIR}/${BASEGAME}/${ARCH_UI} ${BASE_UI_FILES}"
		fi
	fi
	# missionpack
	if [ ${BUILD_MISSIONPACK} -eq 1 ]; then
		if [ -e ${BUILT_PRODUCTS_DIR}/${MISSIONPACK}/${ARCH_CGAME} ]; then
			MP_CGAME_FILES="${BUILT_PRODUCTS_DIR}/${MISSIONPACK}/${ARCH_CGAME} ${MP_CGAME_FILES}"
		fi
		if [ -e ${BUILT_PRODUCTS_DIR}/${MISSIONPACK}/${ARCH_GAME} ]; then
			MP_GAME_FILES="${BUILT_PRODUCTS_DIR}/${MISSIONPACK}/${ARCH_GAME} ${MP_GAME_FILES}"
		fi
		if [ -e ${BUILT_PRODUCTS_DIR}/${MISSIONPACK}/${ARCH_UI} ]; then
			MP_UI_FILES="${BUILT_PRODUCTS_DIR}/${MISSIONPACK}/${ARCH_UI} ${MP_UI_FILES}"
		fi
	fi

	#echo "valid arch: ${ARCH}"
done

# final preparations and checks before attempting to make the application bundle
cd `dirname $0`

if [ ! -f Makefile ]; then
	echo "$0 must be run from the ${PRODUCT_NAME} build directory"
	exit 1
fi

if [ "${CLIENTBIN_FILES}" == "" ]; then
	echo "$0: no ${PRODUCT_NAME} binary architectures were found for target '${TARGET_NAME}'"
	exit 1
fi

# set the final application bundle output directory
if [ "${2}" == "" ]; then
	if [ -n "${MACOSX_DEPLOYMENT_TARGET_ARM64}" ]; then
		BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-universal2"
	else
		BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-universal"
	fi
	if [ ! -d ${BUILT_PRODUCTS_DIR} ]; then
		mkdir -p ${BUILT_PRODUCTS_DIR} || exit 1;
	fi
else
	BUILT_PRODUCTS_DIR="${OBJROOT}/${TARGET_NAME}-darwin-${CURRENT_ARCH}"
fi

BUNDLEBINDIR="${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}"


# here we go
echo "Creating bundle '${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}'"
echo "with architectures:"
for ARCH in ${VALID_ARCHS}; do
	echo " ${ARCH}"
done
echo ""

# make the application bundle directories
if [ ${BUILD_BASEGAME} -eq 1 ]; then
	if [ ! -d "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}/${BASEGAME}" ]; then
		mkdir -p "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}/${BASEGAME}" || exit 1;
	fi
fi
if [ ${BUILD_MISSIONPACK} -eq 1 ]; then
	if [ ! -d "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}/${MISSIONPACK}" ]; then
		mkdir -p "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}/${MISSIONPACK}" || exit 1;
	fi
fi
if [ ! -d "${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}" ]; then
	mkdir -p "${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}" || exit 1;
fi

# copy and generate some application bundle resources
if [ $UNIVERSAL_BINARY -eq 2 ]; then
	cp code/libs/macosx-ub2/*.dylib "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}"
else
	cp code/libs/macosx-ub/*.dylib "${BUILT_PRODUCTS_DIR}/${EXECUTABLE_FOLDER_PATH}"
fi
cp ${ICNSDIR}/${ICNS} "${BUILT_PRODUCTS_DIR}/${UNLOCALIZED_RESOURCES_FOLDER_PATH}/$ICNS" || exit 1;
echo -n ${PKGINFO} > "${BUILT_PRODUCTS_DIR}/${CONTENTS_FOLDER_PATH}/PkgInfo" || exit 1;

# create Info.Plist
PLIST="<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>${CLIENTBIN}</string>
    <key>CFBundleIconFile</key>
    <string>lilium</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_IDENTIFIER}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>${PRODUCT_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CGDisableCoalescedUpdates</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>${MACOSX_DEPLOYMENT_TARGET}</string>"

if [ -n "${MACOSX_DEPLOYMENT_TARGET_PPC}" ] || [ -n "${MACOSX_DEPLOYMENT_TARGET_X86}" ] || [ -n "${MACOSX_DEPLOYMENT_TARGET_X86_64}" ] || [ -n "${MACOSX_DEPLOYMENT_TARGET_ARM64}" ]; then
	PLIST="${PLIST}
    <key>LSMinimumSystemVersionByArchitecture</key>
    <dict>"

	if [ -n "${MACOSX_DEPLOYMENT_TARGET_PPC}" ]; then
	PLIST="${PLIST}
        <key>ppc</key>
        <string>${MACOSX_DEPLOYMENT_TARGET_PPC}</string>"
	fi

	if [ -n "${MACOSX_DEPLOYMENT_TARGET_X86}" ]; then
	PLIST="${PLIST}
        <key>i386</key>
        <string>${MACOSX_DEPLOYMENT_TARGET_X86}</string>"
	fi

	if [ -n "${MACOSX_DEPLOYMENT_TARGET_X86_64}" ]; then
	PLIST="${PLIST}
        <key>x86_64</key>
        <string>${MACOSX_DEPLOYMENT_TARGET_X86_64}</string>"
	fi
	
	if [ -n "${MACOSX_DEPLOYMENT_TARGET_ARM64}" ]; then
	PLIST="${PLIST}
        <key>arm64</key>
        <string>${MACOSX_DEPLOYMENT_TARGET_ARM64}</string>"
	fi

	PLIST="${PLIST}
    </dict>"
fi

	if [ -n "${PROTOCOL_HANDLER}" ]; then
	PLIST="${PLIST}
    <key>CFBundleURLTypes</key>
    <array>
      <dict>
        <key>CFBundleURLName</key>
        <string>${PRODUCT_NAME}</string>
        <key>CFBundleURLSchemes</key>
        <array>
          <string>${PROTOCOL_HANDLER}</string>
        </array>
      </dict>
    </array>"
	fi

PLIST="${PLIST}
    <key>NSHumanReadableCopyright</key>
    <string>${BUNDLE_COPYRIGHT}</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>NSHighResolutionCapable</key>
    <false/>
    <key>NSRequiresAquaSystemAppearance</key>
    <false/>
</dict>
</plist>
"
echo -e "${PLIST}" > "${BUILT_PRODUCTS_DIR}/${CONTENTS_FOLDER_PATH}/Info.plist"

# action takes care of generating universal binaries if lipo is available
# otherwise, it falls back to using a simple copy, expecting the first item in
# the second parameter list to be the desired architecture
function action()
{
	COMMAND=""

	if [ -x "${HAS_LIPO}" ]; then
		COMMAND="${HAS_LIPO} -create -o"
		$HAS_LIPO -create -o "${1}" ${2} # make sure $2 is treated as a list of files
	elif [ -x ${HAS_CP} ]; then
		COMMAND="${HAS_CP}"
		SRC="${2// */}" # in case there is a list here, use only the first item
		$HAS_CP "${SRC}" "${1}"
	else
		"$0 cannot create an application bundle."
		exit 1
	fi

	#echo "${COMMAND}" "${1}" "${2}"
}

#
# the meat of universal binary creation
# destination file names do not have architecture suffix.
# action will handle merging universal binaries if supported.
# symlink appropriate architecture names for universal (fat) binary support.
#

# executables
action "${BUNDLEBINDIR}/${CLIENTBIN}"				"${CLIENTBIN_FILES}"
if [ ${BUILD_SERVER} -eq 1 ]; then
	action "${BUNDLEBINDIR}/${SERVERBIN}"			"${SERVERBIN_FILES}"
fi

#
# enable this to create multi-arch libraries and symlinks. however it doesn't
# work well with the default zip behavior (duplicates files instead of keeping
# symlinks).
#
MERGE_LIBS=0

if [ ${MERGE_LIBS} -eq 0 ]; then

# renderers
if [ ${BUILD_RENDERER_OPENGL1} -eq 1 ]; then
	cp ${OPENGL1_FILES} "${BUNDLEBINDIR}/"
fi
if [ ${BUILD_RENDERER_OPENGL2} -eq 1 ]; then
	cp ${OPENGL2_FILES} "${BUNDLEBINDIR}/"
fi

# baseq3
if [ ${BUILD_BASEGAME} -eq 1 ]; then
	cp ${BASE_CGAME_FILES} "${BUNDLEBINDIR}/${BASEGAME}/"
	cp ${BASE_GAME_FILES} "${BUNDLEBINDIR}/${BASEGAME}/"
	cp ${BASE_UI_FILES} "${BUNDLEBINDIR}/${BASEGAME}/"
fi

# missionpack
if [ ${BUILD_MISSIONPACK} -eq 1 ]; then
	cp ${MP_CGAME_FILES} "${BUNDLEBINDIR}/${MISSIONPACK}/"
	cp ${MP_GAME_FILES} "${BUNDLEBINDIR}/${MISSIONPACK}/"
	cp ${MP_UI_FILES} "${BUNDLEBINDIR}/${MISSIONPACK}/"
fi

else

# renderers
if [ ${BUILD_RENDERER_OPENGL1} -eq 1 ]; then
	action "${BUNDLEBINDIR}/${RENDERER_PREFIX}opengl1.dylib"	"${OPENGL1_FILES}"
	symlinkArch "${RENDERER_PREFIX}opengl1" "${RENDERER_PREFIX}opengl1" "_" "${BUNDLEBINDIR}"
fi
if [ ${BUILD_RENDERER_OPENGL2} -eq 1 ]; then
	action "${BUNDLEBINDIR}/${RENDERER_PREFIX}opengl2.dylib"	"${OPENGL2_FILES}"
	symlinkArch "${RENDERER_PREFIX}opengl2" "${RENDERER_PREFIX}opengl2" "_" "${BUNDLEBINDIR}"
fi

# baseq3
if [ ${BUILD_BASEGAME} -eq 1 ]; then
	action "${BUNDLEBINDIR}/${BASEGAME}/${CGAME}.dylib"		"${BASE_CGAME_FILES}"
	action "${BUNDLEBINDIR}/${BASEGAME}/${GAME}.dylib"		"${BASE_GAME_FILES}"
	action "${BUNDLEBINDIR}/${BASEGAME}/${UI}.dylib"		"${BASE_UI_FILES}"
	symlinkArch "${CGAME}"	"${CGAME}"	""	"${BUNDLEBINDIR}/${BASEGAME}"
	symlinkArch "${GAME}"	"${GAME}"	""	"${BUNDLEBINDIR}/${BASEGAME}"
	symlinkArch "${UI}"		"${UI}"		""	"${BUNDLEBINDIR}/${BASEGAME}"
fi

# missionpack
if [ ${BUILD_MISSIONPACK} -eq 1 ]; then
	action "${BUNDLEBINDIR}/${MISSIONPACK}/${CGAME}.dylib"	"${MP_CGAME_FILES}"
	action "${BUNDLEBINDIR}/${MISSIONPACK}/${GAME}.dylib"	"${MP_GAME_FILES}"
	action "${BUNDLEBINDIR}/${MISSIONPACK}/${UI}.dylib"		"${MP_UI_FILES}"
	symlinkArch "${CGAME}"	"${CGAME}"	""	"${BUNDLEBINDIR}/${MISSIONPACK}"
	symlinkArch "${GAME}"	"${GAME}"	""	"${BUNDLEBINDIR}/${MISSIONPACK}"
	symlinkArch "${UI}"		"${UI}"		""	"${BUNDLEBINDIR}/${MISSIONPACK}"
fi

fi # MERGE_LIBS
