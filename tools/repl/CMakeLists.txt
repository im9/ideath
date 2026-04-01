add_executable(ideath_repl
    main.cpp
    AudioEngine.cpp
    CommandParser.cpp
    NoteUtils.cpp
    TcpServer.cpp
)

target_link_libraries(ideath_repl PRIVATE ideath)

target_include_directories(ideath_repl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/miniaudio
)

# macOS audio frameworks
if(APPLE)
    target_link_libraries(ideath_repl PRIVATE
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
endif()

# Linux audio libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(ideath_repl PRIVATE pthread dl m)
endif()
