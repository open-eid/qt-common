set_env( CONFIG_URL "https://id.eesti.ee/config.json" CACHE STRING "Set Config URL" )
set_env( BREAKPAD "https://cr.eesti.ee/" CACHE STRING "URL for breakpad crash reporting, empty for disabled" )
set_env( ENABLE_CRASH NO CACHE BOOL "Enable -crash parameter" )

find_package( OpenSSL REQUIRED )

if( UNIX AND NOT APPLE )
	find_package(PCSCLite REQUIRED)
	include_directories( ${PCSCLITE_INCLUDE_DIR} )
	set( LIBRARIES ${PCSCLITE_LIBRARY} )
endif()

include_directories(${OPENSSL_INCLUDE_DIR} ${PCSCLITE_INCLUDE_DIR})

if( CONFIG_URL )
	set_env( LAST_CHECK_DAYS 7 CACHE STRING "How often check configuration changes" )
	if( LAST_CHECK_DAYS )
		set_source_files_properties( Configuration.cpp PROPERTIES COMPILE_DEFINITIONS "LAST_CHECK_DAYS=${LAST_CHECK_DAYS}" )
	endif()
	add_definitions( -DCONFIG_URL="${CONFIG_URL}" )
	list( APPEND SOURCES Configuration.cpp )
	file( DOWNLOAD ${CONFIG_URL} ${CMAKE_CURRENT_BINARY_DIR}/config.json )
	string( REPLACE ".json" ".rsa" RSA_URL ${CONFIG_URL} )
	file( DOWNLOAD ${RSA_URL} ${CMAKE_CURRENT_BINARY_DIR}/config.rsa )
	string( REPLACE ".json" ".pub" PUB_URL ${CONFIG_URL} )
	file( DOWNLOAD ${PUB_URL} ${CMAKE_CURRENT_BINARY_DIR}/config.pub )
	configure_file( config.qrc config.qrc COPYONLY )
	qt5_add_resources( SOURCES ${CMAKE_CURRENT_BINARY_DIR}/config.qrc )
endif()

if( WIN32 )
	list( APPEND SOURCES Diagnostics_win.cpp )
	set( LIBRARIES Version SetupApi winscard )
	add_definitions( -DUNICODE -DWIN32_LEAN_AND_MEAN )
else()
	list( APPEND SOURCES Diagnostics_unix.cpp )
endif()

if( APPLE )
	list( APPEND SOURCES MacMenuBar.cpp )
	set_source_files_properties( Application_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc" )
	set_source_files_properties( Configuration.cpp SslCertificate.cpp
		PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations" )
	set( LIBRARIES "-framework Cocoa -framework PCSC -fobjc-arc" )
else()
	include_directories( ${CMAKE_SOURCE_DIR} )
	add_subdirectory( qtsingleapplication )
	list( APPEND LIBRARIES qtsingleapplication )
endif()

configure_file( translations/common_tr.qrc common_tr.qrc COPYONLY )
qt5_add_translation( SOURCES translations/common_en.ts translations/common_et.ts translations/common_ru.ts translations/qtbase_et.ts translations/qtbase_ru.ts )
qt5_add_resources( SOURCES images/common_images.qrc ${CMAKE_CURRENT_BINARY_DIR}/common_tr.qrc )
qt5_wrap_ui( SOURCES AboutDialog.ui CertificateWidget.ui )
list( APPEND SOURCES
	AboutDialog.cpp
	CertificateWidget.cpp
	CliApplication.cpp
	ComboBox.cpp
	Common.cpp
	DateTime.cpp
	Diagnostics.cpp
	DiagnosticsTask.cpp
	IKValidator.cpp
	PinDialog.cpp
	QPCSC.cpp
	SOAPDocument.cpp
	Settings.h
	SslCertificate.cpp
	TokenData.cpp
	qasn1element.cpp
)

if( BREAKPAD )
	if( UNIX AND NOT APPLE )
		find_package( Threads )
	endif()

	if( ENABLE_CRASH )
		add_definitions( -DTESTING )
	endif()

	add_definitions( -DBREAKPAD="${BREAKPAD}" )
	list( APPEND CCSOURCES QBreakPad.cpp )
	set( BREAKPAD_PATH google-breakpad/src )
	include_directories( ${BREAKPAD_PATH} ${CMAKE_SOURCE_DIR} )

	if( UNIX )
		list( APPEND CCSOURCES
			${BREAKPAD_PATH}/client/minidump_file_writer.cc
			${BREAKPAD_PATH}/common/string_conversion.cc
			${BREAKPAD_PATH}/common/convert_UTF.c
			${BREAKPAD_PATH}/common/md5.cc
		)
		set( C_FLAGS "-D_FILE_OFFSET_BITS=64 -fno-exceptions -Wno-unused-parameter -Wno-missing-field-initializers" )
		set( CXX_FLAGS "-fno-rtti -fno-threadsafe-statics" )
		if( CMAKE_VERSION VERSION_LESS 3.2.0 )
			string( REGEX REPLACE "-std=c\\+\\+0x|-std=c\\+\\+11" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} )
		endif()
		set( SOURCES_FLAGS "-std=c++11 -Wno-deprecated-declarations" )
		if( APPLE )
			list( APPEND CCSOURCES
				${BREAKPAD_PATH}/client/mac/handler/exception_handler.cc
				${BREAKPAD_PATH}/client/mac/handler/minidump_generator.cc
				${BREAKPAD_PATH}/client/mac/handler/dynamic_images.cc
				${BREAKPAD_PATH}/client/mac/handler/breakpad_nlist_64.cc
				${BREAKPAD_PATH}/client/mac/crash_generation/crash_generation_client.cc
				${BREAKPAD_PATH}/common/mac/string_utilities.cc
				${BREAKPAD_PATH}/common/mac/file_id.cc
				${BREAKPAD_PATH}/common/mac/macho_id.cc
				${BREAKPAD_PATH}/common/mac/macho_utilities.cc
				${BREAKPAD_PATH}/common/mac/macho_walker.cc
				${BREAKPAD_PATH}/common/mac/bootstrap_compat.cc
				${BREAKPAD_PATH}/common/mac/MachIPC.mm
			)
		else()
			list( APPEND CCSOURCES
				${BREAKPAD_PATH}/client/linux/handler/exception_handler.cc
				${BREAKPAD_PATH}/client/linux/handler/minidump_descriptor.cc
				${BREAKPAD_PATH}/client/linux/log/log.cc
				${BREAKPAD_PATH}/client/linux/crash_generation/crash_generation_client.cc
				${BREAKPAD_PATH}/client/linux/dump_writer_common/seccomp_unwinder.cc
				${BREAKPAD_PATH}/client/linux/dump_writer_common/thread_info.cc
				${BREAKPAD_PATH}/client/linux/dump_writer_common/ucontext_reader.cc
				${BREAKPAD_PATH}/client/linux/microdump_writer/microdump_writer.cc
				${BREAKPAD_PATH}/client/linux/minidump_writer/minidump_writer.cc
				${BREAKPAD_PATH}/client/linux/minidump_writer/linux_ptrace_dumper.cc
				${BREAKPAD_PATH}/client/linux/minidump_writer/linux_dumper.cc
				${BREAKPAD_PATH}/common/linux/elfutils.cc
				${BREAKPAD_PATH}/common/linux/linux_libc_support.cc
				${BREAKPAD_PATH}/common/linux/memory_mapped_file.cc
				${BREAKPAD_PATH}/common/linux/safe_readlink.cc
				${BREAKPAD_PATH}/common/linux/guid_creator.cc
				${BREAKPAD_PATH}/common/linux/file_id.cc
			)
		endif()
	else()
		list( APPEND CCSOURCES
			${BREAKPAD_PATH}/client/windows/handler/exception_handler.cc
			${BREAKPAD_PATH}/client/windows/crash_generation/crash_generation_client.cc
			${BREAKPAD_PATH}/common/windows/guid_string.cc
		)
		set( C_FLAGS "-D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE /wd4800" )
	endif()

	list( APPEND CCSOURCES
		${BREAKPAD_PATH}/processor/basic_code_modules.cc
		${BREAKPAD_PATH}/processor/basic_source_line_resolver.cc
		${BREAKPAD_PATH}/processor/call_stack.cc
		${BREAKPAD_PATH}/processor/cfi_frame_info.cc
		${BREAKPAD_PATH}/processor/disassembler_x86.cc
		${BREAKPAD_PATH}/processor/dump_context.cc
		${BREAKPAD_PATH}/processor/dump_object.cc
		${BREAKPAD_PATH}/processor/exploitability.cc
		${BREAKPAD_PATH}/processor/exploitability_linux.cc
		${BREAKPAD_PATH}/processor/exploitability_win.cc
		${BREAKPAD_PATH}/processor/logging.cc
		${BREAKPAD_PATH}/processor/minidump.cc
		${BREAKPAD_PATH}/processor/minidump_processor.cc
		${BREAKPAD_PATH}/processor/pathname_stripper.cc
		${BREAKPAD_PATH}/processor/process_state.cc
		${BREAKPAD_PATH}/processor/source_line_resolver_base.cc
		${BREAKPAD_PATH}/processor/stack_frame_symbolizer.cc
		${BREAKPAD_PATH}/processor/stackwalker.cc
		${BREAKPAD_PATH}/processor/stackwalker_amd64.cc
		${BREAKPAD_PATH}/processor/stackwalker_arm.cc
		${BREAKPAD_PATH}/processor/stackwalker_arm64.cc
		${BREAKPAD_PATH}/processor/stackwalker_mips.cc
		${BREAKPAD_PATH}/processor/stackwalker_ppc.cc
		${BREAKPAD_PATH}/processor/stackwalker_ppc64.cc
		${BREAKPAD_PATH}/processor/stackwalker_sparc.cc
		${BREAKPAD_PATH}/processor/stackwalker_x86.cc
		${BREAKPAD_PATH}/processor/tokenize.cc
	)
	list( APPEND CSOURCES
		${BREAKPAD_PATH}/third_party/libdisasm/ia32_implicit.c
		${BREAKPAD_PATH}/third_party/libdisasm/ia32_insn.c
		${BREAKPAD_PATH}/third_party/libdisasm/ia32_invariant.c
		${BREAKPAD_PATH}/third_party/libdisasm/ia32_modrm.c
		${BREAKPAD_PATH}/third_party/libdisasm/ia32_opcode_tables.c
		${BREAKPAD_PATH}/third_party/libdisasm/ia32_operand.c
		${BREAKPAD_PATH}/third_party/libdisasm/ia32_reg.c
		${BREAKPAD_PATH}/third_party/libdisasm/ia32_settings.c
		${BREAKPAD_PATH}/third_party/libdisasm/x86_disasm.c
		${BREAKPAD_PATH}/third_party/libdisasm/x86_imm.c
		${BREAKPAD_PATH}/third_party/libdisasm/x86_insn.c
		${BREAKPAD_PATH}/third_party/libdisasm/x86_misc.c
		${BREAKPAD_PATH}/third_party/libdisasm/x86_operand_list.c
	)
	set_source_files_properties( ${CSOURCES} PROPERTIES COMPILE_FLAGS "${C_FLAGS}" )
	set_source_files_properties( ${CCSOURCES} PROPERTIES COMPILE_FLAGS "${C_FLAGS} ${CXX_FLAGS}" )
	set_source_files_properties( ${SOURCES} PROPERTIES COMPILE_FLAGS "${SOURCES_FLAGS}" )
endif()

add_library( qdigidoccommon STATIC ${SOURCES} ${CSOURCES} ${CCSOURCES} )
if( CMAKE_VERSION VERSION_GREATER 3.1.0 AND BREAKPAD )
	set_target_properties( qdigidoccommon PROPERTIES CXX_STANDARD 98 )
endif()
target_link_libraries( qdigidoccommon Qt5::Network Qt5::Widgets ${OPENSSL_LIBRARIES} ${LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} )
