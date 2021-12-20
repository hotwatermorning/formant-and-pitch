#pragma once

#include "prefix.h"
#include "PluginProcessor.h"

NS_HWM_BEGIN

class XYPad
:   public Component
,   public Timer
{
public:
    XYPad(PluginAudioProcessor& processor);
    ~XYPad() override;

private:
    void paint(Graphics& g) override;
    void resized() override;
    void mouseDown(MouseEvent const & mouse) override;
    void mouseDrag(MouseEvent const & mouse) override;
    void mouseUp(MouseEvent const & mouse) override;

    void timerCallback() override;
    PluginAudioProcessor& _processor;

    float _cachedFormant = -1;
    float _cachedPitch = -1;
    int _radius = 10;
    int _radiusOuter = 13;
    bool _dragging = false;

    float getCoord(float value) const;
    float getValue(float coord) const;

    //! @return caches were updated.
    bool updateParameterCaches();
};

class Oscilloscope
:   public Component
,   public Timer
{
public:
    Oscilloscope(PluginAudioProcessor& processor);
    ~Oscilloscope() override;

private:
    void paint(Graphics& g) override;
    void resized() override;

    void timerCallback() override;

    juce::AudioSampleBuffer _buffer;
    PluginAudioProcessor& _processor;
};

class Spectrum
:   public Component
,   public Timer
{
public:
    Spectrum(PluginAudioProcessor& processor);
    ~Spectrum() override;

private:
    void paint(Graphics& g) override;
    void resized() override;

    void timerCallback() override;

    SkewedValue _skew;
    std::vector<PluginAudioProcessor::SpectrumData> _spectrums;

    PluginAudioProcessor& _processor;

    enum class GraphIds {
        kOriginalSpectrum,
        kShiftedSpectrum,
        kSynthesisSpectrum,
        kOriginalCepstrum,
        kFineStructure,
        kEnvelope,
        kMaximumValue,
    };

    struct GraphSetting
    {
        juce::Colour _color;
        bool _enabled;
    };

    std::map<GraphIds, GraphSetting> _graphSettings;

    GraphSetting & getGraphSetting(GraphIds gid);
    GraphSetting const & getGraphSetting(GraphIds gid) const;
};

//==============================================================================
class PluginAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit PluginAudioProcessorEditor (PluginAudioProcessor&);
    ~PluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    PluginAudioProcessor& _processorRef;
    juce::GenericAudioProcessorEditor _genericEdior;
    XYPad _xyPad;
    Oscilloscope _oscilloscope;
    Spectrum _spectrum;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginAudioProcessorEditor)
};

NS_HWM_END
