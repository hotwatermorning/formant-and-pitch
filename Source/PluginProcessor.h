/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include "prefix.h"
#include "RingBuffer.h"

NS_HWM_BEGIN

using ComplexType = dsp::Complex<float>;

struct ParameterIds
{
    inline static const String formant = "Formant";
    inline static const String pitch = "Pitch";
    inline static const String dryWetRate = "Dry/Wet";
    inline static const String envelopeOrder = "Envelope Order";
};

struct SkewedValue
{
    SkewedValue(double minValue, double midValue, double maxValue);

    double convertFrom0to1(double x) const;
    double convertTo0to1(double x) const;

private:
    double _minValue;
    double _midValue;
    double _maxValue;
    double _skewFactor;
};

template<class T>
AudioBuffer<T> getSubBufferOf(AudioBuffer<T> &src,
                              int numChannels,
                              int startSample,
                              int length)
{
    return AudioBuffer<T>(src.getArrayOfWritePointers(),
                          numChannels,
                          startSample,
                          length);
}

template<class T>
AudioBuffer<T> getSubBufferOf(AudioBuffer<T> &src,
                              int numChannels,
                              int length)
{
    return getSubBufferOf(src, numChannels, 0, length);
}


class PluginAudioProcessor  : public juce::AudioProcessor
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

    void getBufferDataForUI(AudioSampleBuffer &buf);

    struct SpectrumData
    {
        // 対数振幅スペクトル
        std::vector<ComplexType> _spectrum;
        // ケプストラム
        std::vector<ComplexType> _cepstrum;
        // スペクトル包絡
        std::vector<ComplexType> _envelope;
        // 微細構造
        std::vector<ComplexType> _fineStructure;

        std::vector<float> _amplitudeForSynthesis;
        std::vector<ComplexType> _spectrumSynthesized;

        void resize(int n)
        {
            _spectrum.resize(n);
            _cepstrum.resize(n);
            _envelope.resize(n);
            _fineStructure.resize(n);
            _amplitudeForSynthesis.resize(n);
            _spectrumSynthesized.resize(n);
        }

        void clear()
        {
            std::fill(_spectrum.begin(), _spectrum.end(), ComplexType{});
            std::fill(_cepstrum.begin(), _cepstrum.end(), ComplexType{});
            std::fill(_envelope.begin(), _envelope.end(), ComplexType{});
            std::fill(_fineStructure.begin(), _fineStructure.end(), ComplexType{});
            std::fill(_amplitudeForSynthesis.begin(), _amplitudeForSynthesis.end(), float{});
            std::fill(_spectrumSynthesized.begin(), _spectrumSynthesized.end(), ComplexType{});
        }

        void copyFrom(SpectrumData const &src)
        {
            auto copyImpl = [](auto& dest, auto const & src) {
                assert(src.size() == dest.size());
                std::copy_n(src.begin(), src.size(), dest.begin());
            };

            copyImpl(_spectrum, src._spectrum);
            copyImpl(_cepstrum, src._cepstrum);
            copyImpl(_envelope, src._envelope);
            copyImpl(_fineStructure, src._fineStructure);
            copyImpl(_amplitudeForSynthesis, src._amplitudeForSynthesis);
            copyImpl(_spectrumSynthesized, src._spectrumSynthesized);
        }
    };

    void getSpectrumDataForUI(std::vector<SpectrumData> &buf);

    AudioParameterFloat * getFormantParameter();
    AudioParameterFloat * getPitchParameter();

private:
    AudioProcessorValueTreeState _apvts;

    using RingBufferType = RingBuffer<float>;

    static constexpr int _fftOrder = 9;
    static constexpr int _overlapCount = 8;
    int getFFTSize() const { return pow(2, _fftOrder); }
    int getOverlapSize() const { return getFFTSize() / _overlapCount; }

    std::vector<ComplexType> _signalBuffer;
    std::vector<ComplexType> _frequencyBuffer;
    std::vector<ComplexType> _cepstrumBuffer;
    std::vector<ComplexType> _tmpFFTBuffer;
    std::vector<ComplexType> _tmpFFTBuffer2;
    std::vector<float> _tmpPhaseBuffer;
    std::unique_ptr<juce::dsp::FFT> _fft;
    std::vector<float> _window;
    AudioSampleBuffer _prevInputPhases;
    AudioSampleBuffer _prevOutputPhases;
    std::vector<double> _analysisMagnitude;
    std::vector<double> _synthesizeMagnitude;
    std::vector<double> _analysisFrequencies;
    std::vector<double> _synthesizeFrequencies;

    // * _inputBuffer に ShiftSize ずつデータ追加。
    //     * （_inputBuffer は初期状態では FFTSize - ShiftSize だけデータが埋まっている状態にする）
    // * _inputAudioBuffer がフルになったらフレーム処理
    // * 処理した1フレーム分のデータを _outputAudioBuffer に overlapAdd

    // * BufferSize > ShiftSize のとき
    //     * ShiftSize 単位で何度か処理を行う
    //     * (ShiftSize - n) のデータが _inputBuffer に追加され、 _overlappedBuffer からその分のデータが取り出される
    // * BufferSize < ShiftSize のとき
    //     * 何回か BufferSize 分のデータが _inputBuffer に追加される
    //     * ShiftSize に達したときはその分が FFTSize 分のフレーム処理が走って _overlappedBuffer に ShiftSize のデータが追加される
    //     * _overlappedBuffer から BufferSize 分のデータが取り出される
    // => _overlappedBuffer は初期状態で BufferSize + FFTSize - ShiftSize 分の無音を追加しておく必要あり
    RingBufferType _inputRingBuffer;
    std::vector<RingBufferType::ConstBufferInfo> _bufferInfoList;
    RingBufferType _outputRingBuffer;

    AudioSampleBuffer _tmpBuffer;
    AudioSampleBuffer _wetBuffer;

    std::mutex _mtxUIData;
    RingBufferType _uiRingBuffer;

    std::vector<SpectrumData> _spectrums;
    std::vector<SpectrumData> _tmpSpectrums; // DSP 中に mutex をロックしないでデータを書き込んでおくためのバッファ

    void processAudioBlock();

    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    JUCE_DECLARE_WEAK_REFERENCEABLE(PluginAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginAudioProcessor)
};

NS_HWM_END
