/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include "prefix.h"
#include "RingBuffer.h"

NS_HWM_BEGIN

struct Defines {
    inline static constexpr float outputGainMin = -48.0f;
    inline static constexpr float outputGainMax = 6.0f;
    inline static constexpr float outputGainDefault = 0.0f;
    inline static constexpr float outputGainSilent = -47.9f;
};

struct ParameterIds
{
    inline static const String formant = "Formant";
    inline static const String pitch = "Pitch";
    inline static const String envelopeOrder = "Envelope Order";
    inline static const String dryWetRate = "Dry/Wet";
    inline static const String outputGain = "Output Gain";
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
        // オリジナルの対数振幅スペクトル
        juce::Array<ComplexType> _originalSpectrum;
        // ピッチシフト後のスペクトル
        juce::Array<ComplexType> _shiftedSpectrum;
        // 合成用のスペクトル
        juce::Array<ComplexType> _synthesisSpectrum;

        // オリジナルのケプストラム
        juce::Array<ComplexType> _originalCepstrum;
        // スペクトル包絡
        juce::Array<ComplexType> _envelope;
        // 微細構造
        juce::Array<ComplexType> _fineStructure;

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
            auto const copyImpl = [](juce::Array<ComplexType> & destArray, juce::Array<ComplexType> const & srcArray) {
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

    void getSpectrumDataForUI(std::vector<SpectrumData> &buf);

    AudioParameterFloat * getFormantParameter();
    AudioParameterFloat * getPitchParameter();

private:
    AudioProcessorValueTreeState _apvts;

    using RingBufferType = RingBuffer<float>;

    static constexpr int _fftOrder = 9;
    static constexpr int _overlapCount = 8;
    int getFFTSize() const { return 1 << _fftOrder; }
    int getOverlapSize() const { return getFFTSize() / _overlapCount; }

    juce::Array<ComplexType> _signalBuffer;
    juce::Array<ComplexType> _frequencyBuffer;
    juce::Array<ComplexType> _cepstrumBuffer;
    juce::Array<ComplexType> _tmpFFTBuffer;
    juce::Array<ComplexType> _tmpFFTBuffer2;
    juce::Array<float> _tmpPhaseBuffer;
    std::unique_ptr<juce::dsp::FFT> _fft;
    juce::Array<float> _window;
    AudioSampleBuffer _prevInputPhases;
    AudioSampleBuffer _prevOutputPhases;
    juce::Array<double> _analysisMagnitude;
    juce::Array<double> _synthesizeMagnitude;
    juce::Array<double> _analysisFrequencies;
    juce::Array<double> _synthesizeFrequencies;

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
    juce::Array<RingBufferType::ConstBufferInfo> _bufferInfoList;
    RingBufferType _outputRingBuffer;

    AudioSampleBuffer _tmpBuffer;
    AudioSampleBuffer _wetBuffer;

    std::mutex _mtxUIData;
    RingBufferType _uiRingBuffer;

    juce::Array<SpectrumData> _spectrums;
    juce::Array<SpectrumData> _tmpSpectrums; // DSP 中に mutex をロックしないでデータを書き込んでおくためのバッファ

    void processAudioBlock();
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    JUCE_DECLARE_WEAK_REFERENCEABLE(PluginAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginAudioProcessor)
};

NS_HWM_END
