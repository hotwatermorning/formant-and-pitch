/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

NS_HWM_BEGIN

//==============================================================================
PluginAudioProcessor::PluginAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
     , _apvts(*this, nullptr, "AudioProcessorState", createParameterLayout())
#endif
{
}

PluginAudioProcessor::~PluginAudioProcessor()
{
}

//==============================================================================
const juce::String PluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int PluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PluginAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String PluginAudioProcessor::getProgramName (int index)
{
    return {};
}

void PluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void PluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    auto const totalNumInputChannels  = getTotalNumInputChannels();
    auto const totalNumOutputChannels = getTotalNumOutputChannels();

    int const fftSize = getFFTSize();
    int const overlapSize = getOverlapSize();

    _fft = std::make_unique<juce::dsp::FFT>(_fftOrder);
    _signalBuffer.resize(fftSize);
    _frequencyBuffer.resize(fftSize);
    _cepstrumBuffer.resize(fftSize);

    _window.resize(fftSize);
    for(int i = 0; i < fftSize; ++i) {
        _window.data()[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (double)fftSize));
    }

    std::fill(_signalBuffer.begin(), _signalBuffer.end(), ComplexType{});
    std::fill(_frequencyBuffer.begin(), _frequencyBuffer.end(), ComplexType{});
    std::fill(_cepstrumBuffer.begin(), _cepstrumBuffer.end(), ComplexType{});

    _inputRingBuffer.resize(totalNumInputChannels, fftSize);
    _inputRingBuffer.discardAll();
    _inputRingBuffer.fill(fftSize - overlapSize);
    _bufferInfoList.resize(totalNumInputChannels);

    _outputRingBuffer.resize(totalNumInputChannels, fftSize + samplesPerBlock);
    _outputRingBuffer.discardAll();
    _outputRingBuffer.fill(fftSize + samplesPerBlock - overlapSize);

    _tmpBuffer.setSize(totalNumInputChannels, fftSize);
    _wetBuffer.setSize(totalNumInputChannels, samplesPerBlock);

    _tmpFFTBuffer.resize(fftSize);
    _tmpFFTBuffer2.resize(fftSize);
    _tmpPhaseBuffer.resize(fftSize);
    _prevInputPhases.setSize(totalNumInputChannels, fftSize);
    _prevOutputPhases.setSize(totalNumInputChannels, fftSize);
    _analysisMagnitude.resize(fftSize);
    _synthesizeMagnitude.resize(fftSize);
    _analysisFrequencies.resize(fftSize);
    _synthesizeFrequencies.resize(fftSize);

    {
        std::unique_lock lock(_mtxUIData);
        _uiRingBuffer.resize(totalNumInputChannels, samplesPerBlock);
        _uiRingBuffer.discardAll();
        
        _spectrums.resize(totalNumInputChannels);
        for(auto &s: _spectrums) {
            s.resize(fftSize);
            s.clear();
        }
    }

    {
        _tmpSpectrums.resize(totalNumInputChannels);
        for(auto &s: _tmpSpectrums) {
            s.resize(fftSize);
            s.clear();
        }
    }
}

void PluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void PluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    auto const wetLevel = dynamic_cast<juce::AudioParameterFloat*>(_apvts.getParameter(ParameterIds::dryWetRate))->get();
    auto const dryLevel = 1.0 - wetLevel;

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    {
        buffer.clear (i, 0, buffer.getNumSamples());
    }
    
#if 1
    int const fftSize = getFFTSize();
    int const overlapSize = getOverlapSize();
    int bufferConsumed = 0;
    int const bufferSize = buffer.getNumSamples();

    // TODO: buffer のサイズが prepareToPlay で指定されたサイズを超える場合への対処
    for( ; ; ) {
        if(bufferConsumed == bufferSize) { break; }

        auto const numWritable = _inputRingBuffer.getNumWritable();

        assert(numWritable != 0);

        auto const numToWrite = std::min(numWritable, (bufferSize - bufferConsumed));

        _inputRingBuffer.write(getSubBufferOf(buffer, totalNumInputChannels, bufferConsumed, numToWrite));

        if(_inputRingBuffer.isFull()) {
            processAudioBlock();
        }

        auto const readResult = _outputRingBuffer.read(getSubBufferOf(_wetBuffer, totalNumInputChannels, bufferConsumed, numToWrite));
        jassert(readResult);

        _outputRingBuffer.discard(numToWrite);

        bufferConsumed += numToWrite;
    }

    buffer.applyGain(dryLevel);

    for(int ch = 0; ch < totalNumInputChannels; ++ch) {
        buffer.addFrom(ch, 0, _wetBuffer.getReadPointer(ch), bufferSize, wetLevel);
    }

    auto *outputGainParam = dynamic_cast<juce::AudioParameterFloat *>(_apvts.getParameter(ParameterIds::outputGain));
    auto outputGain = juce::Decibels::decibelsToGain(outputGainParam->get(), Defines::outputGainSilent);

    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    {
        auto data = buffer.getWritePointer(channel);
        FloatVectorOperations::multiply(data, outputGain, buffer.getNumSamples());
        FloatVectorOperations::clip(data, data, -1.5, 1.5, buffer.getNumSamples());
    }
#endif

    {
        std::unique_lock lock(_mtxUIData);
        if(_uiRingBuffer.getNumWritable() < buffer.getNumSamples()) {
            _uiRingBuffer.discard(buffer.getNumSamples() - _uiRingBuffer.getNumWritable());
        }

        _uiRingBuffer.write(buffer);
    }
}

//==============================================================================
bool PluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PluginAudioProcessor::createEditor()
{
    return new PluginAudioProcessorEditor (*this);
}

//==============================================================================
void PluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::unique_ptr<XmlElement> xmlState(new XmlElement("PluginState"));
    xmlState->setAttribute("Plugin_Version", JucePlugin_VersionString);
    {
        MemoryOutputStream mem(2048);
        std::unique_ptr<XmlElement> xmlElm(this->_apvts.copyState().createXml());
        xmlElm->writeTo(mem, {});
        xmlState->setAttribute("ProcessorState", mem.toUTF8());
    }

    if(xmlState)
    {
        copyXmlToBinary(*xmlState, destData);
    }
}

void PluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState{getXmlFromBinary(data, sizeInBytes)};
    if(!xmlState) { return; }

    auto versionXml = xmlState->getStringAttribute("Plugin_Version");
    if(versionXml.isNotEmpty())
    {
        if(versionXml != JucePlugin_VersionString)
        {
            juce::Logger::outputDebugString("Plugin versions are diffrent between program and stored setting.\n");
        }
    }

    auto processorStateXml = xmlState->getStringAttribute("ProcessorState");
    if(processorStateXml.isNotEmpty())
    {
        if(auto xml = juce::parseXML(processorStateXml))
        {
            this->_apvts.replaceState(ValueTree::fromXml(*xml));
        }
    }
}

void PluginAudioProcessor::getBufferDataForUI(AudioSampleBuffer &buf)
{
    if(buf.getNumChannels() != getTotalNumInputChannels() ||
       buf.getNumSamples() != getBlockSize()
       )
    {
        buf.setSize(getTotalNumInputChannels(), getBlockSize());
    }

    for( ; ; ) {
        juce::Thread::sleep(1);
        std::unique_lock lock(_mtxUIData);
        _uiRingBuffer.read(buf);
        break;
    }
}

void PluginAudioProcessor::getSpectrumDataForUI(std::vector<SpectrumData> &buf)
{
    if(buf.size() != getTotalNumInputChannels()) {
        buf.resize(getTotalNumInputChannels());
    }

    auto fftSize = getFFTSize();
    for(auto &data: buf) {
        data.resize(fftSize);
        data.clear();
    }


    std::unique_lock lock(_mtxUIData);

    for(int i = 0; i < buf.size(); ++i) {
        auto const & src = _spectrums[i];
        auto & dest = buf[i];
        dest.copyFrom(src);
    }
}

AudioParameterFloat * PluginAudioProcessor::getFormantParameter()
{
    return dynamic_cast<AudioParameterFloat*>(_apvts.getParameter(ParameterIds::formant));
}

AudioParameterFloat * PluginAudioProcessor::getPitchParameter()
{
    return dynamic_cast<AudioParameterFloat*>(_apvts.getParameter(ParameterIds::pitch));
}

// Helper function to wrap the phase between -pi and pi
float wrapPhase(float phaseIn)
{
    if (phaseIn >= 0) {
        return fmodf(phaseIn + M_PI, 2.0 * M_PI) - M_PI;
    } else {
        return fmodf(phaseIn - M_PI, -2.0 * M_PI) + M_PI;
    }
}

void PluginAudioProcessor::processAudioBlock()
{
    auto const fftSize = getFFTSize();
    auto const overlapSize = getOverlapSize();
    auto const numChannels = _inputRingBuffer.getNumChannels();

#if 1
    auto const formant = dynamic_cast<juce::AudioParameterFloat*>(_apvts.getParameter(ParameterIds::formant))->get();
    auto const formantExpandAmount = std::pow(2.0, formant / 100.0);
    auto const pitch = dynamic_cast<juce::AudioParameterFloat*>(_apvts.getParameter(ParameterIds::pitch))->get();
    auto const pitchChangeAmount = std::pow(2.0, pitch / 100.0);
    auto const envelopOrder = dynamic_cast<juce::AudioParameterInt*>(_apvts.getParameter(ParameterIds::envelopeOrder))->get();
    auto const envelopAmount = 1.0;
    auto const fineStructureAmount = 1.0;

    // 定期的に位相をリセット（暫定）
    static int phaseReset = 0;
    if((phaseReset++ % 1000) == 0) {
//        _prevInputPhases.clear();
//        _prevOutputPhases.clear();
    }

    jassert(_signalBuffer.size() == fftSize);
    jassert(_frequencyBuffer.size() == fftSize);
    jassert(_cepstrumBuffer.size() == fftSize);

    _inputRingBuffer.readWithoutCopy([&, this](int ch, auto const &bi) {
        _bufferInfoList.data()[ch] = bi;
        assert(bi._len1 + bi._len2 >= fftSize);
    });

    _tmpBuffer.clear();
    for(int ch = 0; ch < numChannels; ++ch) {
        std::fill(_frequencyBuffer.begin(), _frequencyBuffer.end(), ComplexType{});
        std::fill(_cepstrumBuffer.begin(), _cepstrumBuffer.end(), ComplexType{});
        auto & specData = _tmpSpectrums.data()[ch];
        auto &bi = _bufferInfoList.data()[ch];

        jassert(_overlapCount >= 1);
        for(int i = 0, end = std::min(fftSize, bi._len1); i < end; ++i) {
            _signalBuffer.data()[i] = ComplexType { bi._buf1[i] * _window[i] / _overlapCount, 0 };
        }

        for(int i = bi._len1, end = fftSize; i < end; ++i) {
            _signalBuffer.data()[i] = ComplexType { bi._buf2[i - bi._len1] * _window.data()[i] / _overlapCount, 0 };
        }

#if 1
        // スペクトルに変換
        _fft->perform(_signalBuffer.data(), _frequencyBuffer.data(), false);

        for(int i = 0; i < fftSize; ++i) {
            specData._originalSpectrum.data()[i] = _frequencyBuffer.data()[i];
        }

#if 0
        // fft 後の係数がどれくらいになるかを確認
        {
            _fft->perform(_frequencyBuffer.data(), _tmpFFTBuffer.data(), true);
            auto sumOriginal = std::reduce(_signalBuffer.begin(), _signalBuffer.end(), 0.0, [](double x, ComplexType c) { return x + std::norm(c); });
            auto sumTransformed = std::reduce(_frequencyBuffer.begin(), _frequencyBuffer.end(), 0.0, [](double x, ComplexType c) { return x + std::norm(c); });
            auto sumRestored = std::reduce(_tmpFFTBuffer.begin(), _tmpFFTBuffer.end(), 0.0, [](double x, ComplexType c) { return x + std::norm(c); });

            if(sumOriginal != 0) {
                double ratio1 = sumTransformed / sumOriginal; // パワーが fftSize 倍された状態になる
                double ratio2 = sumRestored / sumOriginal;
                double y = ratio1;
            }
        }
#endif

#if 1
        // ピッチシフト前のスペクトルからスペクトル包絡を計算
        {
            for(int i = 0; i < fftSize; ++i) {
                auto amp = std::abs(_frequencyBuffer[i]);
                auto r = log(amp + std::numeric_limits<float>::epsilon());
                _tmpFFTBuffer.data()[i] = ComplexType { r, 0.0 };
            }

            _fft->perform(_tmpFFTBuffer.data(), _cepstrumBuffer.data(), false);

            for(int i = 0; i < fftSize; ++i) {
                specData._originalCepstrum.data()[i] = _cepstrumBuffer[i];
            }

            // ケプストラムを liftering してスペクトル包絡を取得

            // envelope
            _tmpFFTBuffer.data()[0] = _cepstrumBuffer[0];
            for(int i = 1; i <= fftSize / 2; ++i) {
                if(i < envelopOrder) {
                    _tmpFFTBuffer.data()[i] = _cepstrumBuffer[i];
                    _tmpFFTBuffer.data()[fftSize - i] = _cepstrumBuffer[i];
                } else {
                    _tmpFFTBuffer.data()[i] = _tmpFFTBuffer[fftSize - i] = ComplexType { 0, 0 };
                }
            }

            _fft->perform(_tmpFFTBuffer.data(), _tmpFFTBuffer2.data(), true);

            for(int i = 0; i < fftSize; ++i) {
                specData._envelope.data()[i] = _tmpFFTBuffer2[i];
            }
        }

        // フォルマントシフト
        {
            std::copy(specData._envelope.begin(), specData._envelope.end(), _tmpFFTBuffer.begin());

            for(int i = 0; i <= fftSize / 2; ++i) {
                double shiftedPos = i / formantExpandAmount;
                int leftIndex = (int)std::floor(shiftedPos);
                int rightIndex = (int)std::ceil(shiftedPos);
                double diff = shiftedPos - leftIndex;

                double leftValue = -1000.0;
                double rightValue = -1000.0;

                if(leftIndex <= fftSize / 2) {
                    leftValue = _tmpFFTBuffer[leftIndex].real();
                }

                if(rightIndex <= fftSize / 2) {
                    rightValue = _tmpFFTBuffer[rightIndex].real();
                }

                double newValue = (1.0 - diff) * leftValue + diff * rightValue;
                specData._envelope.data()[i].real(newValue);
            }

            for(int i = 1; i <= fftSize / 2; ++i) {
                specData._envelope.data()[fftSize - i].real(specData._envelope[i].real());
            }
        }
#endif

#if 1
        // ピッチシフト
        {
            double hopSize = overlapSize;

            std::fill_n(_analysisMagnitude.begin(), fftSize, 0.0);
            std::fill_n(_analysisFrequencies.begin(), fftSize, 0.0);
            // 瞬時周波数からbin内の正確な周波数を解析
            for(int i = 0; i <= fftSize / 2; ++i) {
                auto magnitude = std::abs(_frequencyBuffer[i]);
                auto phase = std::arg(_frequencyBuffer[i]);
                double binCenterFrequency = 2.0 * M_PI * i / fftSize;

                double phaseDiff = phase - _prevInputPhases.getReadPointer(ch)[i];
                _prevInputPhases.getWritePointer(ch)[i] = phase;

                phaseDiff = wrapPhase(phaseDiff - binCenterFrequency * hopSize);
                double binDeviation = phaseDiff * fftSize / hopSize / (2 * M_PI);

                _analysisMagnitude.data()[i] = magnitude;
                _analysisFrequencies.data()[i] = (float)(i + binDeviation);
            }

            // 周波数変更
            std::fill_n(_synthesizeMagnitude.begin(), fftSize, 0.0);
            std::fill_n(_synthesizeFrequencies.begin(), fftSize, 0.0);
            for(int i = 0; i <= fftSize / 2; ++i) {
                int shiftedBin = std::floor(i / pitchChangeAmount + 0.5);
                if(shiftedBin > fftSize / 2) { break; }

                _synthesizeMagnitude.data()[i] += _analysisMagnitude.data()[shiftedBin];
                _synthesizeFrequencies.data()[i] = _analysisFrequencies.data()[shiftedBin] * pitchChangeAmount;
            }

            for(int i = 0; i <= fftSize / 2; ++i) {
                double binDeviation = _synthesizeFrequencies[i] - i;
                double phaseDiff = binDeviation * 2.0 * M_PI * hopSize / fftSize;
                double binCenterFrequency = 2.0 * M_PI * i / fftSize;
                phaseDiff += binCenterFrequency * hopSize;

                auto phase = wrapPhase(_prevOutputPhases.getReadPointer(ch)[i] + phaseDiff);

                _frequencyBuffer.data()[i] = ComplexType {
                    (float)(_synthesizeMagnitude[i] * std::cos(phase)),
                    (float)(_synthesizeMagnitude[i] * std::sin(phase))
                };

                _prevOutputPhases.getWritePointer(ch)[i] = phase;
            }

            for(int i = 1; i < fftSize / 2; ++i) {
                _frequencyBuffer.data()[fftSize - i] = std::conj(_frequencyBuffer.data()[i]);
            }
        }
#endif
        for(int i = 0; i < fftSize; ++i) {
            _tmpPhaseBuffer.data()[i] = std::arg(_frequencyBuffer.data()[i]);
        }

        // ピッチシフト後のスペクトル
        for(int i = 0; i < fftSize; ++i) {
            specData._shiftedSpectrum.data()[i] = _frequencyBuffer.data()[i];
        }

        // ピッチが低い方にシフトされたとき、
        // シフト後のスペクトルはナイキスト周波数のシフトされた位置で急激に値が下がるため、スペクトルを波形として捉えたときにその波形が不連続になる。
        // このとき Envelope の次数が小さいと、不連続な部分での値の変動に追従できないため、その差分が FineStructure の方に現れてしまう。
        // これによって FineStructure がナイキスト周波数のシフトされた位置付近で値が大きくなってしまい、高域のノイズになる。
        // これを防ぐため、ナイキスト周波数のシフトされた位置の対数振幅スペクトルは、それ以下の振幅スペクトルのミラーとして計算するようにする。
        if(pitchChangeAmount < 1.0) {
            auto newNyquistPos = (int)std::round(fftSize * 0.5 * pitchChangeAmount);
            for(int i = 0; i < fftSize / 2; ++i) {
                if(newNyquistPos + i >= fftSize / 2) { break; }
                if(newNyquistPos - i < 0) { break; }

                _frequencyBuffer.data()[newNyquistPos + i] = _frequencyBuffer.data()[newNyquistPos - i];
            }

            for(int i = 1; i < fftSize / 2; ++i) {
                _frequencyBuffer.data()[fftSize - i] = _frequencyBuffer.data()[i];
            }
        }

#if 1
        // ピッチシフト後の波形からケプストラムを計算し、微細構造だけを取り出す
        {
            // 対数振幅スペクトルを FFT してケプストラムを計算
            for(int i = 0; i < fftSize; ++i) {
                auto amp = std::abs(_frequencyBuffer[i]);
                auto r = log(amp + std::numeric_limits<float>::epsilon());
                _tmpFFTBuffer.data()[i] = ComplexType { r, 0.0 };
            }

            _fft->perform(_tmpFFTBuffer.data(), _cepstrumBuffer.data(), false);

            // fine structure
            _tmpFFTBuffer.data()[0] = ComplexType { 0, 0 };
            for(int i = 1; i <= fftSize / 2; ++i) {
                if(i >= envelopOrder) {
                    _tmpFFTBuffer.data()[i] = _cepstrumBuffer.data()[i];
                    _tmpFFTBuffer.data()[fftSize - i] = _cepstrumBuffer.data()[i];
                } else {
                    _tmpFFTBuffer.data()[i] = _tmpFFTBuffer.data()[fftSize - i] = ComplexType { 0, 0 };
                }
            }

            _fft->perform(_tmpFFTBuffer.data(), _tmpFFTBuffer2.data(), true);

            // ミラーした領域の微細構造は無視する
            if(pitchChangeAmount < 1.0) {
                auto newNyquistPos = (int)std::round(fftSize * 0.5 * pitchChangeAmount);

                for(int i = newNyquistPos; i < fftSize / 2; ++i) {
                    _tmpFFTBuffer2.data()[i] = ComplexType{};
                }

                for(int i = 1; i < fftSize / 2; ++i) {
                    _tmpFFTBuffer2.data()[fftSize - i] = _tmpFFTBuffer2.data()[i];
                }
            }

            for(int i = 0; i < fftSize; ++i) {
                specData._fineStructure.data()[i] = _tmpFFTBuffer2.data()[i];
            }

#if 0
            // use pitch shifted envelope
            _tmpFFTBuffer.data()[0] = _cepstrumBuffer.data()[0];
            for(int i = 1; i <= fftSize / 2; ++i) {
                if(i < envelopOrder) {
                    _tmpFFTBuffer.data()[i] = _cepstrumBuffer.data()[i];
                    _tmpFFTBuffer.data()[fftSize - i] = _cepstrumBuffer.data()[i];
                } else {
                    _tmpFFTBuffer.data()[i] = _tmpFFTBuffer.data()[fftSize - i] = ComplexType { 0, 0 };
                }
            }

            _fft->perform(_tmpFFTBuffer.data(), _tmpFFTBuffer2.data(), true);

            for(int i = 0; i < fftSize; ++i) {
                specData._envelope.data()[i] = _tmpFFTBuffer2.data()[i];
            }
#endif
        }

        // フォルマントシフトしたスペクトル包絡とピッチシフト後の微細構造からスペクトルを再構築

        for(int i = 0; i <= fftSize / 2; ++i) {
            auto const amp = exp(specData._envelope[i].real() * envelopAmount + specData._fineStructure[i].real() * fineStructureAmount);

            _frequencyBuffer.data()[i] = ComplexType {
                (float)(amp * std::cos(_tmpPhaseBuffer[i])),
                (float)(amp * std::sin(_tmpPhaseBuffer[i]))
            };
        }

        for(int i = 1; i < fftSize / 2; ++i) {
            _frequencyBuffer.data()[fftSize - i] = std::conj(_frequencyBuffer.data()[i]);
        }

        // 再合成されたスペクトル
        for(int i = 0; i < fftSize; ++i) {
            specData._synthesisSpectrum.data()[i] = _frequencyBuffer.data()[i];
        }
#endif

        _fft->perform(_frequencyBuffer.data(), _signalBuffer.data(), true);
#endif

        for(int i = 0; i < fftSize; ++i) {
            _signalBuffer.data()[i] *= _window.data()[i];
        }

        std::transform(_signalBuffer.begin(),
                       _signalBuffer.end(),
                       _tmpBuffer.getWritePointer(ch),
                       [](auto x) { return x.real(); }
                       );
    }
    if(_outputRingBuffer.overlapAdd(_tmpBuffer, fftSize - overlapSize) == false) {
        assert("should never fail" && false);
    }
    
    _inputRingBuffer.discard(overlapSize);

    {
        std::unique_lock lock(_mtxUIData);
        for(int i = 0; i < _spectrums.size(); ++i) {
            _spectrums.data()[i].copyFrom(_tmpSpectrums.data()[i]);
        }
    }

#else
    _tmpBuffer.clear();

    auto tmp = getSubBufferOf(_tmpBuffer, numChannels, 0, overlapSize);
    _inputRingBuffer.read(tmp);
    _inputRingBuffer.discard(tmp.getNumSamples());
    _outputRingBuffer.write(tmp);
#endif
}

AudioProcessorValueTreeState::ParameterLayout PluginAudioProcessor::createParameterLayout()
{
    auto group = std::make_unique<AudioProcessorParameterGroup>("Group", "Global", "|");

    group->addChild(
        std::make_unique<AudioParameterFloat>(
            ParameterIds::formant,
            ParameterIds::formant,
            NormalisableRange<float>{-100.0f, 100.0f},
            0.0f,
            "%",
            AudioProcessorParameter::genericParameter,
            [](float value, int /*maxLength*/) {
                return String(value, 2);
            },
            nullptr));

    group->addChild(
        std::make_unique<AudioParameterFloat>(
            ParameterIds::pitch,
            ParameterIds::pitch,
            NormalisableRange<float>{-100.0f, 100.0f},
            0.0f,
            "%",
            AudioProcessorParameter::genericParameter,
            [](float value, int /*maxLength*/) {
                return String(value, 0);
            },
            nullptr));

    group->addChild(
        std::make_unique<AudioParameterInt>(
            ParameterIds::envelopeOrder,
            ParameterIds::envelopeOrder,
            2, 90, 20, ""));

    group->addChild(
        std::make_unique<AudioParameterFloat>(
            ParameterIds::dryWetRate,
            ParameterIds::dryWetRate,
            NormalisableRange<float>{0.0f, 1.0f},
            0.5f,
            "%",
            AudioProcessorParameter::genericParameter,
            [](float value, int /*maxLength*/) {
                return String(value * 100.0f, 0);
            },
            nullptr));

    group->addChild(
        std::make_unique<AudioParameterFloat>(
            ParameterIds::outputGain,
            ParameterIds::outputGain,
            NormalisableRange<float>{Defines::outputGainMin, Defines::outputGainMax},
            Defines::outputGainDefault,
            "dB",
            AudioProcessorParameter::genericParameter,
            [](float value, int /*maxLength*/) {
                return String(value, 0);
            },
            nullptr));

    return AudioProcessorValueTreeState::ParameterLayout(std::move(group));
}

NS_HWM_END

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hwm::PluginAudioProcessor();
}
