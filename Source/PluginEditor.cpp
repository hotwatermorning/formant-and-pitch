#include "PluginProcessor.h"
#include "PluginEditor.h"

NS_HWM_BEGIN

//==============================================================================

XYPad::XYPad(PluginAudioProcessor& processor)
:   _processor(processor)
{
    startTimer(30);
}

XYPad::~XYPad()
{}

void XYPad::paint(Graphics& g)
{
    auto x = getCoord(_cachedFormant);
    auto y = getHeight() - getCoord(_cachedPitch);

    g.fillAll(juce::Colours::black.withLightness(0.1));
    g.setColour(juce::Colours::white);
    g.fillEllipse(x - _radius, y - _radius, _radius * 2, _radius * 2);

    if(_dragging) {
        g.setColour(juce::Colours::white);
        g.drawEllipse(x - _radiusOuter, y - _radiusOuter, _radiusOuter * 2, _radiusOuter * 2, 2);
    }
}

void XYPad::resized()
{
}

void XYPad::mouseDown(MouseEvent const & mouse)
{
    mouseDrag(mouse);
}

void XYPad::mouseDrag(MouseEvent const & mouse)
{
    _dragging = true;
    
    auto f = getValue(mouse.x);
    auto p = getValue(getHeight() - mouse.y);

    *_processor.getFormantParameter() = f;
    *_processor.getPitchParameter() = p;

    updateParameterCaches();
    repaint();
}

void XYPad::mouseUp(MouseEvent const & mouse)
{
    _dragging = false;
    repaint();
}

void XYPad::timerCallback()
{
    if(updateParameterCaches()) {
        repaint();
    }
}

float XYPad::getCoord(float value) const
{
    auto w = getWidth() - _radius * 2;
    auto half = w / 2.0f;
    return (value / 100.0f) * half + half + _radius;;
}

float XYPad::getValue(float coord) const
{
    auto w = getWidth() - _radius * 2;
    auto half = w / 2.0f;
    jassert(half != 0);
    return jlimit(-100.0f, 100.0f, (coord - half - _radius) / half * 100.0f);
}

bool XYPad::updateParameterCaches()
{
    auto newFormant = _processor.getFormantParameter()->get();
    auto newPitch = _processor.getPitchParameter()->get();

    if(newFormant != _cachedFormant ||
       newPitch != _cachedPitch)
    {
        _cachedFormant = newFormant;
        _cachedPitch = newPitch;

        return true;
    }

    return false;
}

//==============================================================================

Oscilloscope::Oscilloscope(PluginAudioProcessor& processor)
:   _processor(processor)
{
    startTimer(30);
    _buffer.setSize(1, 1);
}

Oscilloscope::~Oscilloscope()
{}

void Oscilloscope::paint(Graphics& g)
{
    auto w = getWidth();
    auto h = getHeight();
    g.fillAll(juce::Colours::pink);

    Path p;
    p.startNewSubPath(0, 0.5 * h);

    auto const data = _buffer.getReadPointer(0);
    auto const N = _buffer.getNumSamples();
    for(int i = 0; i < N; ++i) {
        p.lineTo((float)i / N * w, -(data[i] / 2.0 - 0.5) * h);
    }

    g.setColour(Colours::black);
    g.strokePath(p, PathStrokeType(1.0));
}

void Oscilloscope::resized()
{

}

void Oscilloscope::timerCallback()
{
    _processor.getBufferDataForUI(_buffer);
    repaint();
}

//==============================================================================

Spectrum::Spectrum(PluginAudioProcessor& processor)
:   _processor(processor)
,   _skew(0, 0.5, 1.0)
{
    startTimer(30);

    _graphSettings[GraphIds::kOriginalSpectrum]    = GraphSetting { juce::Colours::black, true };
    _graphSettings[GraphIds::kShiftedSpectrum]     = GraphSetting { juce::Colours::grey, true };
    _graphSettings[GraphIds::kSynthesisSpectrum]   = GraphSetting { juce::Colours::green, true };
    _graphSettings[GraphIds::kOriginalCepstrum]    = GraphSetting { juce::Colours::blue, true };
    _graphSettings[GraphIds::kEnvelope]            = GraphSetting { juce::Colours::red, true };
    _graphSettings[GraphIds::kFineStructure]       = GraphSetting { juce::Colours::lightcyan, true };
}

Spectrum::~Spectrum()
{}

void Spectrum::paint(Graphics& g)
{
    auto w = getWidth();
    auto h = getHeight();
    g.fillAll(juce::Colours::lightgreen);

    if(_spectrums.empty()) { return; }

    // 現在は 0 番目のチャンネルのデータのみ描画
    auto specData = _spectrums[0];

    int const N = specData._originalSpectrum.size();

    // オリジナル spectrum の描画
    if(auto const &gs = getGraphSetting(GraphIds::kOriginalSpectrum); gs._enabled) {
        auto const data = specData._originalSpectrum.data();
        float valueMax = 6.0;
        float valueMin = -24.0;
        float valueRange = valueMax - valueMin;

        Path p;

        for(int i = 0; i <= N / 2; ++i) {
            auto v = std::clamp(std::log(std::abs(data[i])), valueMin, valueMax);
            auto x = _skew.convertFrom0to1((float)i / (N / 2)) * w;
            auto y = -((v - valueMin) / valueRange) * h + h;
            if(i == 0) {
                p.startNewSubPath(0, y);
            }

            p.lineTo(x, y);
        }

        g.setColour(gs._color);
        g.strokePath(p, PathStrokeType(1.0));
    }

    // オリジナルのケプストラムの描画
    if(auto const &gs = getGraphSetting(GraphIds::kOriginalCepstrum); gs._enabled) {
        auto const data = specData._originalCepstrum.data();
        float valueMax = 300;
        float valueMin = 0;
        float valueRange = valueMax - valueMin;

        Path p;

        for(int i = 0; i <= N / 2; ++i) {
            auto v = std::clamp(std::abs(data[i]), valueMin, valueMax);
            auto y = -((v - valueMin) / valueRange) * h + h;
            auto x = _skew.convertFrom0to1((float)i / (N / 2)) * w;
            if(i == 0) {
                p.startNewSubPath(0, y);
            }

            p.lineTo(x, y);
        }

        g.setColour(gs._color);
        g.strokePath(p, PathStrokeType(1.0));
    }

    // スペクトル包絡の描画
    if(auto const &gs = getGraphSetting(GraphIds::kEnvelope); gs._enabled) {
        auto const data = specData._envelope.data();
        float valueMax = 6.0;
        float valueMin = -24.0;
        float valueRange = valueMax - valueMin;

        Path p;

        for(int i = 0; i <= N / 2; ++i) {
            auto v = std::clamp(data[i].real(), valueMin, valueMax);
            auto y = -((v - valueMin) / valueRange) * h + h;
            auto x = _skew.convertFrom0to1((float)i / (N / 2)) * w;
            if(i == 0) {
                p.startNewSubPath(0, y);
            }

            p.lineTo(x, y);
        }

        g.setColour(gs._color);
        g.strokePath(p, PathStrokeType(1.0));
    }

    // 微細構造の描画
    if(auto const &gs = getGraphSetting(GraphIds::kFineStructure); gs._enabled) {
        auto const data = specData._fineStructure.data();
        float valueMax = 6.0;
        float valueMin = -24.0;
        float valueRange = valueMax - valueMin;

        Path p;

        for(int i = 0; i <= N / 2; ++i) {
            auto v = std::clamp(data[i].real(), valueMin, valueMax);
            auto y = -((v - valueMin) / valueRange) * h + h;
            auto x = _skew.convertFrom0to1((float)i / (N / 2)) * w;
            if(i == 0) {
                p.startNewSubPath(0, y);
            }

            p.lineTo(x, y);
        }

        g.setColour(gs._color);
        g.strokePath(p, PathStrokeType(1.0));
    }

    // ピッチシフト後のスペクトルの描画
    if(auto const &gs = getGraphSetting(GraphIds::kShiftedSpectrum); gs._enabled) {
        auto const data = specData._shiftedSpectrum.data();
        float valueMax = 6.0;
        float valueMin = -24.0;
        float valueRange = valueMax - valueMin;

        Path p;

        for(int i = 0; i <= N / 2; ++i) {
            auto v = std::clamp(std::log(std::abs(data[i])), valueMin, valueMax);
            auto y = -((v - valueMin) / valueRange) * h + h;
            auto x = _skew.convertFrom0to1((float)i / (N / 2)) * w;
            if(i == 0) {
                p.startNewSubPath(0, y);
            }

            p.lineTo(x, y);
        }

        g.setColour(gs._color);
        g.strokePath(p, PathStrokeType(1.0));
    }

    // 再合成用スペクトルの描画
    if(auto const &gs = getGraphSetting(GraphIds::kSynthesisSpectrum); gs._enabled) {
        auto const data = specData._synthesisSpectrum.data();
        float valueMax = 6.0;
        float valueMin = -24.0;
        float valueRange = valueMax - valueMin;

        Path p;

        for(int i = 0; i <= N / 2; ++i) {
            auto v = std::clamp(std::log(std::abs(data[i])), valueMin, valueMax);
            auto y = -((v - valueMin) / valueRange) * h + h;
            auto x = _skew.convertFrom0to1((float)i / (N / 2)) * w;
            if(i == 0) {
                p.startNewSubPath(0, y);
            }

            p.lineTo(x, y);
        }

        g.setColour(gs._color);
        g.strokePath(p, PathStrokeType(1.0));
    }
}

void Spectrum::resized()
{

}

void Spectrum::timerCallback()
{
    _processor.getSpectrumDataForUI(_spectrums);
    repaint();
}


Spectrum::GraphSetting & Spectrum::getGraphSetting(GraphIds gid)
{
    auto found = _graphSettings.find(gid);
    assert(found != _graphSettings.end());

    return found->second;
}

Spectrum::GraphSetting const & Spectrum::getGraphSetting(GraphIds gid) const
{
    auto found = _graphSettings.find(gid);
    assert(found != _graphSettings.end());

    return found->second;
}

//==============================================================================
PluginAudioProcessorEditor::PluginAudioProcessorEditor (PluginAudioProcessor& p)
:   AudioProcessorEditor (&p)
,   _processorRef (p)
,   _xyPad(p)
,   _genericEdior(p)
,   _oscilloscope(p)
,   _spectrum(p)
{
    addAndMakeVisible(_genericEdior);
    addAndMakeVisible(_xyPad);
    addAndMakeVisible(_oscilloscope);
    addAndMakeVisible(_spectrum);

    juce::ignoreUnused (_processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (1000, 600);
    setResizable(true, true);
}

PluginAudioProcessorEditor::~PluginAudioProcessorEditor()
{
}

//==============================================================================
void PluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void PluginAudioProcessorEditor::resized()
{
    auto b = getLocalBounds();

    auto top = b.removeFromTop(b.getHeight() / 2);
    auto topLeft = top.removeFromLeft(top.getWidth() - top.getHeight());
    auto topRight = top;
    auto bottomLeft = b.removeFromLeft(b.getWidth() / 2);
    auto bottomRight = b;

    _genericEdior.setBounds(topLeft);
    _xyPad.setBounds(topRight);
    _oscilloscope.setBounds(bottomLeft);
    _spectrum.setBounds(bottomRight);
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}

NS_HWM_END
