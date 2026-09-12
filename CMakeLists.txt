cmake_minimum_required(VERSION 3.10)

project(Launcher)

if (WIN32)
    message(STATUS "MSVC -> forcing use of statically-linked runtime.")
    STRING(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
    STRING(REPLACE "/MDd" "/MTd" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
endif(WIN32)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)

file(GLOB source_files "src/*.cpp" "src/*/*.cpp" "src/*/*.hpp" "include/*.h"  "include/*/*.h" "include/*/*/*.h" "include/*.hpp"  "include/*/*.hpp" "include/*/*/*.hpp")
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL REQUIRED)

add_executable(${PROJECT_NAME} ${source_files})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "BeamMP-Launcher")

if (WIN32)
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto ws2_32 httplib::httplib nlohmann_json::nlohmann_json CURL::libcurl)
elseif (UNIX)
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_search_module(APPINDICATOR REQUIRED appindicator3-0.1)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto CURL::libcurl ${GTK3_LIBRARIES} ${APPINDICATOR_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${GTK3_INCLUDE_DIRS} ${APPINDICATOR_INCLUDE_DIRS})
    target_compile_options(${PROJECT_NAME} PRIVATE ${GTK3_CFLAGS_OTHER} ${APPINDICATOR_CFLAGS_OTHER})
else(WIN32) #MINGW
    add_definitions("-D_WIN32_WINNT=0x0600")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -s --static")
    target_link_libraries(${PROJECT_NAME} ssl crypto ws2_32 ssp crypt32 z CURL::libcurl)
endif(WIN32)
target_include_directories(${PROJECT_NAME} PRIVATE "include")
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# GResource compilation (Linux only) — embeds icons into the binary at build time
if (UNIX AND NOT APPLE)
    find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
    set(GRESOURCE_C ${CMAKE_CURRENT_BINARY_DIR}/resources.c)
    add_custom_command(
        OUTPUT  ${GRESOURCE_C}
        COMMAND ${GLIB_COMPILE_RESOURCES}
                --target=${GRESOURCE_C}
                --generate-source
                --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/data/resources.gresource.xml
        DEPENDS
                ${CMAKE_CURRENT_SOURCE_DIR}/data/resources.gresource.xml
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/beamng-logo.svg
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/beammp-logo.png
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/com.beammp.Launcher.png
        VERBATIM)
    target_sources(${PROJECT_NAME} PRIVATE ${GRESOURCE_C})

    install(FILES data/com.beammp.Launcher.desktop
        DESTINATION share/applications)
    # Scalable PNG icon for the app menu / .desktop Icon field
    install(FILES assets/com.beammp.Launcher.png
        DESTINATION share/icons/hicolor/512x512/apps)

endif()
