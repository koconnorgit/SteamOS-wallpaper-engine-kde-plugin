find_package(Qt6 REQUIRED COMPONENTS Quick Qml Core DBus)

add_subdirectory(backend_mpv)

SET(BUILD_QML ON CACHE BOOL "")
add_subdirectory(backend_scene)

set(CMAKE_AUTOMOC ON)

add_library(${PROJECT_NAME}
	SHARED
	plugin.cpp
	MouseGrabber.cpp
	TTYSwitchMonitor.cpp
	SunSchedule.cpp
	PlaylistStore.cpp
	WallpaperPresence.cpp
	PluginInfo.cpp
	FileHelper.cpp
)

target_link_libraries(${PROJECT_NAME} 
	PRIVATE
	Qt::Quick
	Qt::Qml 
	mpvbackend
	wescene-renderer-qml
	Qt::Core
	Qt::DBus
)
# Sunrise/sunset times for the wallpaper schedule (Plasma 6.5+). Without it
# the plugin still builds, but sunrise/sunset slots are unavailable.
find_package(KNightTime QUIET)
if(KNightTime_FOUND)
	target_link_libraries(${PROJECT_NAME} PRIVATE KNightTime)
	target_compile_definitions(${PROJECT_NAME} PRIVATE WEKDE_HAVE_KNIGHTTIME)
	message(STATUS "KNightTime found: sunrise/sunset schedule slots enabled")
else()
	message(STATUS "KNightTime not found: sunrise/sunset schedule slots disabled")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
set_target_properties(${PROJECT_NAME}
                       PROPERTIES LINK_FLAGS "-Wl,--exclude-libs,ALL")

# During plugin searching Qt will be looking for 'qmldir' file
# So we should place it next to our plugin lib.
add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_LIST_DIR}/qmldir
		$<TARGET_FILE_DIR:${PROJECT_NAME}>/qmldir
)

install(
	TARGETS ${PROJECT_NAME}
    DESTINATION ${KDE_INSTALL_QMLDIR}/${QMLPLUGIN_INSTALL_URI}
)

install(FILES
    ${CMAKE_CURRENT_LIST_DIR}/qmldir
    DESTINATION ${KDE_INSTALL_QMLDIR}/${QMLPLUGIN_INSTALL_URI}
)

