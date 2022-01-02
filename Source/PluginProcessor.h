#pragma once

#include "Prefix.h"
#include "RingBuffer.h"
#include "AudioBufferUtil.h"
#include "ReferenceableArray.h"

NS_HWM_BEGIN

struct Defines {
    inline static constexpr float outputGainMin = -48.0f;
    inline static constexpr float outputGainMax = 6.0f;
    inline static constexpr float outputGainDefault = 0.0f;
    inline static constexpr float outputGainSilent = -47.9f;
};

struct ParameterIds
{
    inline static const juce::String formant = "Formant";
    inline static const juce::String pitch = "Pitch";
    inline static const juce::String envelopeOrder = "Envelope Order";
    inline static const juce::String dryWetRate = "Dry/Wet";
    inline static const juce::String outputGain = "Output Gain";
};

class PluginAudioProcessor
:   public juce::AudioProcessor
{
public:
    //==============================================================================
    PluginAudioProcessor();
    ~PluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void getBufferDataForUI(juce::AudioSampleBuffer &buf);

    struct SpectrumData
    {
        // オリジナルの対数振幅スペクトル
        ReferenceableArray<ComplexType> _originalSpectrum;
        // ピッチシフト後のスペクトル
        ReferenceableArray<ComplexType> _shiftedSpectrum;
        // 合成用のスペクトル
        ReferenceableArray<ComplexType> _synthesisSpectrum;

        // オリジナルのケプストラム
        ReferenceableArray<ComplexType> _originalCepstrum;
        // スペクトル包絡
        ReferenceableArray<ComplexType> _envelope;
        // 微細構造
        ReferenceableArray<ComplexType> _fineStructure;

        void resize(int n)
        {
            _originalSpectrum.resize(n);
            _shiftedSpectrum.resize(n);
            _synthesisSpectrum.resize(n);
            _originalCepstrum.resize(n);
            _envelope.resize(n);
            _fineStructure.resize(n);
        }

        void clear()
        {
            _originalSpectrum.fill(ComplexType{});
            _shiftedSpectrum.fill(ComplexType{});
            _synthesisSpectrum.fill(ComplexType{});
            _originalCepstrum.fill(ComplexType{});
            _envelope.fill(ComplexType{});
            _fineStructure.fill(ComplexType{});
        }

        void copyFrom(SpectrumData const &src)
        {
            auto const copyImpl = [](auto & destArray, auto const & srcArray) {
                assert(destArray.size() == srcArray.size());
                std::copy_n(srcArray.data(), srcArray.size(), destArray.data());
            };

            copyImpl(_originalSpectrum, src._originalSpectrum);
            copyImpl(_shiftedSpectrum, src._shiftedSpectrum);
            copyImpl(_synthesisSpectrum, src._synthesisSpectrum);
            copyImpl(_originalCepstrum, src._originalCepstrum);
            copyImpl(_envelope, src._envelope);
            copyImpl(_fineStructure, src._fineStructure);
        }
    };

    void getSpectrumDataForUI(ReferenceableArray<SpectrumData> &buf);

    juce::AudioParameterFloat * getFormantParameter();
    juce::AudioParameterFloat * getPitchParameter();

private:
    juce::AudioProcessorValueTreeState _apvts;

    using RingBufferType = RingBuffer<float>;

    static constexpr int _fftOrder = 9;
    static constexpr int _overlapCount = 8;
    int getFFTSize() const { return 1 << _fftOrder; }
    int getOverlapSize() const { return getFFTSize() / _overlapCount; }

    ReferenceableArray<ComplexType> _signalBuffer;
    ReferenceableArray<ComplexType> _frequencyBuffer;
    ReferenceableArray<ComplexType> _cepstrumBuffer;
    ReferenceableArray<ComplexType> _tmpFFTBuffer;
    ReferenceableArray<ComplexType> _tmpFFTBuffer2;
    ReferenceableArray<float> _tmpPhaseBuffer;
    std::unique_ptr<juce::dsp::FFT> _fft;
    ReferenceableArray<float> _window;
    juce::AudioSampleBuffer _prevInputPhases;
    juce::AudioSampleBuffer _prevOutputPhases;
    ReferenceableArray<double> _analysisMagnitude;
    ReferenceableArray<double> _synthesizeMagnitude;
    ReferenceableArray<double> _analysisFrequencies;
    ReferenceableArray<double> _synthesizeFrequencies;

    RingBufferType _inputRingBuffer;
    ReferenceableArray<RingBufferType::ConstBufferInfo> _bufferInfoList;
    RingBufferType _outputRingBuffer;

    juce::AudioSampleBuffer _tmpBuffer;
    juce::AudioSampleBuffer _wetBuffer;

    std::mutex _mtxUIData;
    RingBufferType _uiRingBuffer;

    ReferenceableArray<SpectrumData> _spectrums;
    ReferenceableArray<SpectrumData> _tmpSpectrums; // DSP 中に mutex をロックしないでデータを書き込んでおくためのバッファ

    void processAudioBlock();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // 変換した信号の音量が変わってしまうのを補正するための係数。
    // 毎回の解析でこれをやると音量の変化が大きくなりすぎることがあるのでスムーズに変換するようにしている。
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> _smoothedGain;

    //==============================================================================
    JUCE_DECLARE_WEAK_REFERENCEABLE(PluginAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginAudioProcessor)
};

NS_HWM_END
