# Everything except main.cpp lives in a library so that tests can link it.
add_library(faxwerk_gui_lib STATIC
    AppImageUpdater.cpp
    AppImageUpdater.h
    AppSettings.cpp
    AppSettings.h
    ComposePage.cpp
    ComposePage.h
    Contacts.cpp
    Contacts.h
    ContactsPage.cpp
    ContactsPage.h
    FritzPhonebook.cpp
    FritzPhonebook.h
    Journal.cpp
    Journal.h
    JournalPage.cpp
    JournalPage.h
    LocationProbe.cpp
    LocationProbe.h
    MainWindow.cpp
    MainWindow.h
    Messages.cpp
    Messages.h
    SendReport.cpp
    SendReport.h
    SendingPage.cpp
    SendingPage.h
    SetupPage.cpp
    SetupPage.h
    Theme.cpp
    Theme.h
    Widgets.cpp
    Widgets.h
)

qt_add_resources(faxwerk_gui_lib "faxwerk_gui_resources"
    PREFIX "/faxwerk"
    BASE resources
    FILES resources/faxwerk.qss)

target_include_directories(faxwerk_gui_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(faxwerk_gui_lib PUBLIC faxwerk_core Qt6::Widgets)
target_compile_definitions(faxwerk_gui_lib PRIVATE QT_NO_CAST_FROM_ASCII)
faxwerk_set_warnings(faxwerk_gui_lib)

# Self-update of the Linux AppImage with libappimageupdate (AppImageUpdate
# project). The dev container builds it into /usr/local; see
# .devcontainer/Dockerfile.
if(UNIX AND NOT APPLE)
    set(FAXWERK_APPIMAGEUPDATE "AUTO" CACHE STRING
        "Update the AppImage from inside the app with libappimageupdate (AUTO, ON, OFF)")
    set_property(CACHE FAXWERK_APPIMAGEUPDATE PROPERTY STRINGS AUTO ON OFF)
    if(NOT FAXWERK_APPIMAGEUPDATE STREQUAL "OFF")
        find_path(AppImageUpdate_INCLUDE_DIR appimage/update.h)
        find_library(AppImageUpdate_LIBRARY NAMES appimageupdate)
        if(AppImageUpdate_INCLUDE_DIR AND AppImageUpdate_LIBRARY)
            target_include_directories(faxwerk_gui_lib SYSTEM PRIVATE "${AppImageUpdate_INCLUDE_DIR}")
            target_link_libraries(faxwerk_gui_lib PUBLIC "${AppImageUpdate_LIBRARY}")
            target_compile_definitions(faxwerk_gui_lib PRIVATE FAXWERK_HAVE_APPIMAGEUPDATE)
            message(STATUS "libappimageupdate: ${AppImageUpdate_LIBRARY}")
        elseif(FAXWERK_APPIMAGEUPDATE STREQUAL "ON")
            message(FATAL_ERROR "libappimageupdate not found (FAXWERK_APPIMAGEUPDATE=ON)")
        else()
            message(STATUS "libappimageupdate: not found, building without AppImage self-update")
        endif()
    endif()
endif()

add_executable(faxwerk_gui WIN32 MACOSX_BUNDLE main.cpp)
# macOS gets "Faxwerk.app". Elsewhere the GUI is "faxwerk-gui", because
# "Faxwerk.exe" and the CLI's "faxwerk.exe" would collide in one Windows folder.
if(APPLE)
    set(_faxwerk_gui_name "${FAXWERK_PRODUCT_NAME}")
else()
    set(_faxwerk_gui_name "faxwerk-gui")
endif()
set_target_properties(faxwerk_gui PROPERTIES
    OUTPUT_NAME "${_faxwerk_gui_name}"
    MACOSX_BUNDLE_BUNDLE_NAME "${FAXWERK_PRODUCT_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
target_link_libraries(faxwerk_gui PRIVATE faxwerk_gui_lib)
target_compile_definitions(faxwerk_gui PRIVATE QT_NO_CAST_FROM_ASCII)
faxwerk_set_warnings(faxwerk_gui)

if(WIN32)
    # Icon and version information of faxwerk-gui.exe.
    set(FAXWERK_ICO "${PROJECT_SOURCE_DIR}/packaging/windows/faxwerk.ico")
    configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/faxwerk.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/faxwerk.rc" @ONLY)
    target_sources(faxwerk_gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/faxwerk.rc")
endif()

if(APPLE)
    set(FAXWERK_BUNDLE_ID "org.faxwerk.Faxwerk" CACHE STRING "macOS bundle identifier of the desktop app")

    # faxwerk.icns from the 1024 px PNG, with the tools every Mac has.
    set(_png "${PROJECT_SOURCE_DIR}/packaging/icons/faxwerk-1024.png")
    set(_iconset "${CMAKE_CURRENT_BINARY_DIR}/faxwerk.iconset")
    set(_icns "${CMAKE_CURRENT_BINARY_DIR}/faxwerk.icns")
    set(_icon_commands COMMAND "${CMAKE_COMMAND}" -E make_directory "${_iconset}")
    foreach(_size 16 32 128 256 512)
        math(EXPR _double "${_size} * 2")
        list(APPEND _icon_commands
            COMMAND sips -z ${_size} ${_size} "${_png}" --out "${_iconset}/icon_${_size}x${_size}.png"
            COMMAND sips -z ${_double} ${_double} "${_png}" --out "${_iconset}/icon_${_size}x${_size}@2x.png")
    endforeach()
    add_custom_command(OUTPUT "${_icns}"
        ${_icon_commands}
        COMMAND iconutil -c icns "${_iconset}" -o "${_icns}"
        DEPENDS "${_png}"
        COMMENT "Creating faxwerk.icns"
        VERBATIM)
    target_sources(faxwerk_gui PRIVATE "${_icns}")
    set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    set_target_properties(faxwerk_gui PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${FAXWERK_BUNDLE_ID}"
        MACOSX_BUNDLE_ICON_FILE faxwerk.icns)
endif()

install(TARGETS faxwerk_gui
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .)

if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/faxwerk.desktop"
        DESTINATION share/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/faxwerk.svg"
        DESTINATION share/icons/hicolor/scalable/apps)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION share/doc/faxwerk)
endif()
