# Everything except main.cpp lives in a library so that tests can link it.
add_library(faxwerk_gui_lib STATIC
    AppSettings.cpp
    AppSettings.h
    ComposePage.cpp
    ComposePage.h
    Journal.cpp
    Journal.h
    JournalPage.cpp
    JournalPage.h
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

install(TARGETS faxwerk_gui
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .)

if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/faxwerk.desktop"
        DESTINATION share/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/faxwerk.svg"
        DESTINATION share/icons/hicolor/scalable/apps)
endif()
