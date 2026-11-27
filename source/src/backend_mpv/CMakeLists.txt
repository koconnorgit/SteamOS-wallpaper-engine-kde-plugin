project(mpvbackend)

find_package(Qt6 COMPONENTS Gui Quick OpenGL REQUIRED)

find_package(PkgConfig REQUIRED)
# an imported target carries mpv's include and library directories, for
# installs outside the default paths
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

set(CMAKE_AUTOMOC ON)

add_library(${PROJECT_NAME}
	STATIC
	MpvBackend.cpp
	qthelper.hpp
)
target_link_libraries(${PROJECT_NAME}
	PUBLIC
	# the public header uses QtQuick types
	Qt::Quick
	PRIVATE
	Qt::Gui
	Qt::OpenGL
	PkgConfig::MPV
)
target_include_directories(${PROJECT_NAME} PUBLIC .)
