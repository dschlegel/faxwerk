add_executable(faxwerk
    ExitCodes.h
    Terminal.cpp
    Terminal.h
    main.cpp
)

target_link_libraries(faxwerk PRIVATE faxwerk_core Qt6::Gui)
target_compile_definitions(faxwerk PRIVATE QT_NO_CAST_FROM_ASCII)
faxwerk_set_warnings(faxwerk)

install(TARGETS faxwerk RUNTIME DESTINATION bin)
