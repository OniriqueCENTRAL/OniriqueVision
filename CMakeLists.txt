cmake_minimum_required(VERSION 3.22)

# Mac : binaire universel (Intel + Apple Silicon), à définir avant project()
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(OniriqueVision VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

option(VISION_PLUGIN "Compiler le plugin" ON)
option(VISION_TESTS "Compiler le banc de mesure" ON)
# Licence ONIRIQUE : toujours dans les builds publiés. OFF seulement pour un build d'essai local.
option(VISION_LICENCE "Brancher la licence ONIRIQUE" ON)
# Build local de captures et de démo (jamais publié) : ONIRIQUE_UI_TEST=nolicense,demo
option(ONIRIQUE_UI_TEST "Build de captures et de démo" OFF)

set(VISION_FONTS License/Fonts/Unbounded800.ttf License/Fonts/HankenGrotesk600.ttf
               License/Fonts/JetBrainsMono500.ttf License/Fonts/JetBrainsMono600.ttf)

if(VISION_PLUGIN)
    set(VISION_FORMATS VST3)
    if(APPLE)
        list(APPEND VISION_FORMATS AU)
    endif()

    juce_add_plugin(OniriqueVision
        COMPANY_NAME                Onirique
        PLUGIN_MANUFACTURER_CODE    Onir
        PLUGIN_CODE                 Ovsn
        FORMATS                     ${VISION_FORMATS}
        AU_MAIN_TYPE                kAudioUnitType_Effect
        PRODUCT_NAME                "ONIRIQUE VISION"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        VST3_CATEGORIES             Fx Distortion
        COPY_PLUGIN_AFTER_BUILD     FALSE)

    target_sources(OniriqueVision PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    juce_add_binary_data(VisionAssets SOURCES Resources/logo.png)
    set_target_properties(VisionAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

    target_include_directories(OniriqueVision PRIVATE Source ${CMAKE_CURRENT_SOURCE_DIR})

    if(VISION_LICENCE)
        include(License/OniriqueLicense.cmake)
        onirique_add_license(OniriqueVision)
        target_compile_definitions(OniriqueVision PRIVATE VISION_LICENCE=1)
    else()
        juce_add_binary_data(OniriqueVisionLicenseData
            HEADER_NAME OniriqueLicenseData.h
            NAMESPACE OniriqueLicenseData
            SOURCES ${VISION_FONTS})
        set_target_properties(OniriqueVisionLicenseData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
        target_link_libraries(OniriqueVision PRIVATE OniriqueVisionLicenseData)
        target_compile_definitions(OniriqueVision PRIVATE VISION_LICENCE=0)
    endif()

    if(ONIRIQUE_UI_TEST)
        target_compile_definitions(OniriqueVision PRIVATE ONIRIQUE_UI_TEST=1)
    endif()

    target_compile_definitions(OniriqueVision PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    if(MSVC)
        target_compile_options(OniriqueVision PRIVATE /utf-8)
    endif()
    if(APPLE)
        target_compile_definitions(OniriqueVision PUBLIC JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1)
    endif()

    target_link_libraries(OniriqueVision
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            VisionAssets
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# Banc de mesure du moteur (console) : séparation, filtres, saturations, CURRENT / DRIFT / LEAN, couleurs, clics, stabilité, CPU
if(VISION_TESTS)
    juce_add_console_app(VisionBench PRODUCT_NAME "VisionBench")
    target_sources(VisionBench PRIVATE Tests/VisionBench.cpp)
    target_include_directories(VisionBench PRIVATE Source)
    target_compile_definitions(VisionBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    if(MSVC)
        target_compile_options(VisionBench PRIVATE /utf-8)
    endif()
    target_link_libraries(VisionBench PRIVATE juce::juce_dsp juce::juce_audio_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# Test du plugin entier (console) : état du projet, A / B / C, presets, paramètres, bus, BYPASS, latence
if(VISION_PLUGIN AND VISION_TESTS)
    juce_add_console_app(VisionPluginTest PRODUCT_NAME "VisionPluginTest")
    # les sources du plugin compilées dans le test (sans la licence), pas le binaire du plugin
    target_sources(VisionPluginTest PRIVATE Tests/PluginTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(VisionPluginTest PRIVATE VISION_LICENCE=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_include_directories(VisionPluginTest PRIVATE Source ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(VisionPluginTest PRIVATE /utf-8)
    endif()
    target_link_libraries(VisionPluginTest PRIVATE VisionAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    if(TARGET OniriqueVisionLicenseData)
        target_link_libraries(VisionPluginTest PRIVATE OniriqueVisionLicenseData)
    else()
        juce_add_binary_data(VisionTestFonts HEADER_NAME OniriqueLicenseData.h NAMESPACE OniriqueLicenseData SOURCES ${VISION_FONTS})
        target_link_libraries(VisionPluginTest PRIVATE VisionTestFonts)
    endif()
endif()
