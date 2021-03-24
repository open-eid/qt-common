set_env( CONFIG_URL "https://id.eesti.ee/config.json" CACHE STRING "Set Config URL" )
find_package( OpenSSL REQUIRED )

configure_file( translations/common_tr.qrc common_tr.qrc COPYONLY )
qt5_add_translation( SOURCES  translations/qtbase_et.ts translations/qtbase_ru.ts
	translations/common_en.ts translations/common_et.ts translations/common_ru.ts )
qt5_add_resources( SOURCES ${CMAKE_CURRENT_BINARY_DIR}/common_tr.qrc )
add_library( qdigidoccommon STATIC ${SOURCES}
	Common.cpp
	DateTime.cpp
	IKValidator.cpp
	QPCSC.cpp
)
target_include_directories(qdigidoccommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qdigidoccommon PUBLIC Qt5::Network Qt5::Widgets OpenSSL::SSL)

if(UNIX AND NOT APPLE)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(PCSCLITE REQUIRED libpcsclite IMPORTED_TARGET)
	target_link_libraries(qdigidoccommon PRIVATE PkgConfig::PCSCLITE)
endif()

if(WIN32)
	target_compile_definitions(qdigidoccommon PUBLIC UNICODE WIN32_LEAN_AND_MEAN)
	target_link_libraries(qdigidoccommon PRIVATE Version SetupApi winscard)
endif()

if(APPLE)
	target_link_libraries(qdigidoccommon PRIVATE "-framework PCSC")
else()
	add_subdirectory(qtsingleapplication)
	target_link_libraries(qdigidoccommon PUBLIC qtsingleapplication)
endif()

if( CONFIG_URL )
	set_env( LAST_CHECK_DAYS 4 CACHE STRING "How often check configuration changes" )
	if( LAST_CHECK_DAYS )
		set_source_files_properties( Configuration.cpp PROPERTIES COMPILE_DEFINITIONS "LAST_CHECK_DAYS=${LAST_CHECK_DAYS}" )
	endif()
	file( DOWNLOAD ${CONFIG_URL} ${CMAKE_CURRENT_BINARY_DIR}/config.json )
	string( REPLACE ".json" ".rsa" RSA_URL ${CONFIG_URL} )
	file( DOWNLOAD ${RSA_URL} ${CMAKE_CURRENT_BINARY_DIR}/config.rsa )
	string( REPLACE ".json" ".pub" PUB_URL ${CONFIG_URL} )
	file( DOWNLOAD ${PUB_URL} ${CMAKE_CURRENT_BINARY_DIR}/config.pub )
	configure_file( config.qrc config.qrc COPYONLY )
	qt5_add_resources( CONFIG_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/config.qrc )
	target_compile_definitions(qdigidoccommon PUBLIC CONFIG_URL="${CONFIG_URL}")
	target_sources(qdigidoccommon PRIVATE ${CONFIG_SOURCES} Configuration.cpp )
endif()
