if( WIN32 )
	add_definitions( -DUNICODE )
endif()
add_library( qtsingleapplication STATIC src/qtlocalpeer.cpp src/qtsingleapplication.cpp )
set_target_properties( qtsingleapplication PROPERTIES AUTOMOC TRUE )
target_link_libraries( qtsingleapplication Qt5::Widgets Qt5::Network )
