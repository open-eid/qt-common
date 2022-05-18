add_library( qtsingleapplication STATIC src/qtlocalpeer.cpp src/qtsingleapplication.cpp )
set_target_properties( qtsingleapplication PROPERTIES AUTOMOC TRUE )
target_compile_definitions( qtsingleapplication PRIVATE $<$<C_COMPILER_ID:MSVC>:UNICODE> )
target_link_libraries( qtsingleapplication Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network )
