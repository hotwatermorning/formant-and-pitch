cmake_minimum_required(VERSION 3.15)
set(CMAKE_CXX_STANDARD 17)
project(FormantAndPitch VERSION 0.0.1)

add_subdirectory(Dependencies/JUCE)                    # If you've put JUCE in a subdirectory called JUCE

set(TARGET_NAME FormantAndPitch)

juce_add_plugin(${TARGET_NAME}
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME "diatonic.jp"                  # Specify the name of the plugin's author
    COMPANY_WEBSITE "https://diatonic.jp/"
    COMPANY_EMAIL "hotwatermorning@gmail.com"
    # IS_SYNTH TRUE/FALSE                       # Is this a synth or an effect?
    # NEEDS_MIDI_INPUT TRUE/FALSE               # Does the plugin need midi input?
    # NEEDS_MIDI_OUTPUT TRUE/FALSE              # Does the plugin need midi output?
    # IS_MIDI_EFFECT TRUE/FALSE                 # Is this plugin a MIDI effect?
    # EDITOR_WANTS_KEYBOARD_FOCUS TRUE/FALSE    # Does the editor need keyboard focus?
    # COPY_PLUGIN_AFTER_BUILD TRUE/FALSE        # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE Dtjp               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE FMPT                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    MICROPHONE_PERMISSION_ENABLED TRUE
    PRODUCT_NAME ${TARGET_NAME})        # The name of the final executable, which can differ from the target name

juce_generate_juce_header(${TARGET_NAME})

set(SOURCE_FILES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/RingBuffer.h
    Source/ReferenceableArray.h
    Source/AudioBufferUtil.h
    Source/Prefix.h
    )

source_group(TREE Source FILES ${SOURCE_FILES})
target_sources(${TARGET_NAME} PRIVATE ${SOURCE_FILES})

target_compile_definitions(${TARGET_NAME}
    PUBLIC
    DONT_SET_USING_JUCE_NAMESPACE=1
    # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
    JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
    JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
    JUCE_VST3_CAN_REPLACE_VST2=0
    )

target_compile_options(${TARGET_NAME}
    PRIVATE
    $<$<CXX_COMPILER_ID:Clang,GNU>:-Werror=return-type>
    $<$<CXX_COMPILER_ID:MSVC>:/source-charset:utf-8>
    )

# juce_add_binary_data(AudioPluginData SOURCES ...)

target_link_libraries(${TARGET_NAME}
    PRIVATE
    # AudioPluginData           # If we'd created a binary data target, we'd link to it here
    juce::juce_audio_utils
    juce::juce_audio_basics
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    )
