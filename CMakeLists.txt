cmake_minimum_required(VERSION 2.8.12)
PROJECT(enc-vfw)

################################################################################
# Version
################################################################################
SET(enc-vfw_VERSION_MAJOR 0)
SET(enc-vfw_VERSION_MINOR 2)
SET(enc-vfw_VERSION_PATCH 0)
SET(enc-vfw_VERSION_BUILD 0)
#configure_file(
#	"${PROJECT_SOURCE_DIR}/#Resources/package.in.bat"
#	"${PROJECT_SOURCE_DIR}/#Resources/package.bat"
#)
#configure_file(
#	"${PROJECT_SOURCE_DIR}/#Resources/Installer.in.iss"
#	"${PROJECT_SOURCE_DIR}/#Resources/Installer.iss"
#)
configure_file(
	"${PROJECT_SOURCE_DIR}/Include/Version.h.in"
	"${PROJECT_BINARY_DIR}/Include/Version.h"
)

################################################################################
# Code
################################################################################
SET(enc-vfw_HEADERS
	"${PROJECT_BINARY_DIR}/Include/Version.h"
	"Include/plugin.h"
	"Include/enc-vfw.h"
)
SET(enc-vfw_SOURCES
	"Source/plugin.cpp"
	"Source/enc-vfw.cpp"
)
SET(enc-vfw_LIBRARIES
	version
	winmm
	Vfw32.lib
)

################################################################################
# Standalone and OBS Studio Build Data
################################################################################
if(BUILD_VFW_ENCODER)
	# OBS Studio Specific
	
	# Directories
	INCLUDE_DIRECTORIES(
		"${CMAKE_SOURCE_DIR}"
		"${PROJECT_BINARY_DIR}"
		"${PROJECT_BINARY_DIR}/Include"
		"${PROJECT_BINARY_DIR}/Source"
		"${PROJECT_SOURCE_DIR}"
		"${PROJECT_SOURCE_DIR}/Include"
		"${PROJECT_SOURCE_DIR}/Source"
	)
	SET(LIBOBS_LIBRARIES libobs)
else()
	# Standlone Specific

	# Variables
	SET(PATH_OBSStudio "" CACHE PATH "OBS Studio Source Code Directory")
	
	if(PATH_OBSStudio STREQUAL "")
		message(STATUS "PATH_OBSStudio not set!")
		return()
	endif()

	if(NOT EXISTS "${PATH_OBSStudio}/libobs/obs-module.h")
		message(STATUS "PATH_OBSStudio invalid!")
		return()
	endif()
	
	# Find OBS Libraries
	SET(obsPath "${PATH_OBSStudio}")
	INCLUDE("${PATH_OBSStudio}/cmake/external/Findlibobs.cmake")
	
	# Compiling
	INCLUDE_DIRECTORIES(
		"${CMAKE_SOURCE_DIR}"
		"${PROJECT_BINARY_DIR}"
		"${PROJECT_BINARY_DIR}/Include"
		"${PROJECT_BINARY_DIR}/Source"
		"${PROJECT_SOURCE_DIR}"
		"${PROJECT_SOURCE_DIR}/Include"
		"${PROJECT_SOURCE_DIR}/Source"
		"${PATH_OBSStudio}/"
	)
endif()

################################################################################
# Build
################################################################################
ADD_LIBRARY(enc-vfw MODULE
	${enc-vfw_HEADERS}
	${enc-vfw_SOURCES}
)
TARGET_LINK_LIBRARIES(enc-vfw
	${LIBOBS_LIBRARIES}
	${enc-vfw_LIBRARIES}
)

# All Warnings, Extra Warnings, Pedantic
if(MSVC)
  # Force to always compile with W4
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  # Update if necessary
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -pedantic")
endif()

if(BUILD_VFW_ENCODER)
	install_obs_plugin_with_data(enc-vfw Resources)
else()
	math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
	add_custom_command(TARGET enc-vfw POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${PROJECT_SOURCE_DIR}/Resources/locale"
		"${PROJECT_SOURCE_DIR}/#Build/data/obs-plugins/enc-vfw/locale"
	)	
	add_custom_command(TARGET enc-vfw POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
		"$<TARGET_FILE:enc-vfw>"
		"${PROJECT_SOURCE_DIR}/#Build/obs-plugins/${BITS}bit/$<TARGET_FILE_NAME:enc-vfw>"
	)	
	add_custom_command(TARGET enc-vfw POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
		"$<TARGET_FILE_DIR:enc-vfw>/enc-vfw.pdb"
		"${PROJECT_SOURCE_DIR}/#Build/obs-plugins/${BITS}bit/enc-vfw.pdb"
	)
endif()
