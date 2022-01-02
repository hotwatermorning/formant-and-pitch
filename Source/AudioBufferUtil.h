#pragma once

#include "Prefix.h"

NS_HWM_BEGIN

template<class T>
juce::AudioBuffer<T>
getSubBufferOf(juce::AudioBuffer<T> &src,
               int numChannels,
               int startSample,
               int length)
{
    return juce::AudioBuffer<T>(src.getArrayOfWritePointers(),
                                numChannels,
                                startSample,
                                length);
}

template<class T>
juce::AudioBuffer<T>
getSubBufferOf(juce::AudioBuffer<T> &src,
               int numChannels,
               int length)
{
    return getSubBufferOf(src, numChannels, 0, length);
}

NS_HWM_END
