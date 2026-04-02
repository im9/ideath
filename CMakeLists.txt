cmake_minimum_required(VERSION 3.22)

project(ideath VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Library -----------------------------------------------------------
add_library(ideath STATIC
    src/Biquad.cpp
    src/Oscillator.cpp
    src/Envelope.cpp
    src/Noise.cpp
    src/Saturation.cpp
    src/Wavetable.cpp
    src/BitCrusher.cpp
    src/DelayLine.cpp
    src/LFO.cpp
    src/Portamento.cpp
    src/Voice.cpp
    src/Polyphony.cpp
    src/FMSynth.cpp
    src/Reverb.cpp
    src/HallReverb.cpp
    src/ShimmerReverb.cpp
    src/PeakLimiter.cpp
    src/Compressor.cpp
    src/Wavefolder.cpp
    src/UnisonOscillator.cpp
    src/FeedbackBuffer.cpp
    src/SVFilter.cpp
)

target_include_directories(ideath
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# --- Tests (opt-in) ----------------------------------------------------
option(TN_DSP_BUILD_TESTS "Build unit tests" ON)

if(TN_DSP_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()

    add_executable(ideath_tests
        tests/main.cpp
        tests/test_Biquad.cpp
        tests/test_Oscillator.cpp
        tests/test_Envelope.cpp
        tests/test_Noise.cpp
        tests/test_Saturation.cpp
        tests/test_Wavetable.cpp
        tests/test_BitCrusher.cpp
        tests/test_DelayLine.cpp
        tests/test_LFO.cpp
        tests/test_Portamento.cpp
        tests/test_Voice.cpp
        tests/test_Polyphony.cpp
        tests/test_FMSynth.cpp
        tests/test_Reverb.cpp
        tests/test_HallReverb.cpp
        tests/test_ShimmerReverb.cpp
        tests/test_PeakLimiter.cpp
        tests/test_Compressor.cpp
        tests/test_Wavefolder.cpp
        tests/test_UnisonOscillator.cpp
        tests/test_FeedbackBuffer.cpp
        tests/test_SVFilter.cpp
        tests/test_AudioSafety.cpp
        tests/test_SeqClick.cpp
    )

    target_link_libraries(ideath_tests PRIVATE ideath Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(ideath_tests)
endif()

# --- REPL tool (opt-in) ------------------------------------------------
option(TN_DSP_BUILD_REPL "Build interactive REPL tool" OFF)

if(TN_DSP_BUILD_REPL)
    add_subdirectory(tools/repl)
endif()
