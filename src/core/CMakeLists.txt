configure_file(ProductInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/ProductInfo.h" @ONLY)

add_library(faxwerk_core STATIC
    document/FaxTiff.cpp
    document/FaxTiff.h
    document/PdfRasterizer.cpp
    document/PdfRasterizer.h
    fax/FaxEngine.cpp
    fax/FaxEngine.h
    rtp/AudioPort.h
    rtp/G711.cpp
    rtp/G711.h
    rtp/RtpSession.cpp
    rtp/RtpSession.h
    sip/DigestAuth.cpp
    sip/DigestAuth.h
    sip/Sdp.cpp
    sip/Sdp.h
    sip/SipMessage.cpp
    sip/SipMessage.h
    sip/SipUserAgent.cpp
    sip/SipUserAgent.h
)

target_include_directories(faxwerk_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(faxwerk_core
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Gui
        Qt6::Pdf
    PRIVATE
        SpanDSP::SpanDSP
        TIFF::TIFF)

target_compile_definitions(faxwerk_core PRIVATE
    FAXWERK_SPANDSP_NEW_API=$<BOOL:${FAXWERK_SPANDSP_NEW_API}>
    FAXWERK_SPANDSP_LOG_USER_DATA=$<BOOL:${FAXWERK_SPANDSP_LOG_USER_DATA}>
    QT_NO_CAST_FROM_ASCII)

faxwerk_set_warnings(faxwerk_core)
