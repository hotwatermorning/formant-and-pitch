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
    juce::ignoreUnused(index);
}

const juce::String PluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void PluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index);
    juce::ignoreUnused(newName);
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
        _window[i] = float(0.5f * (1.0 - cos(2.0 * M_PI * i / (double)fftSize)));
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

    _smoothedGain.reset(10);
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
    juce::ignoreUnused(midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    auto const wetLevel = dynamic_cast<juce::AudioParameterFloat*>(_apvts.getParameter(ParameterIds::dryWetRate))->get();
    auto const dryLevel = 1.0f - wetLevel;

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    {
        buffer.clear (i, 0, buffer.getNumSamples());
    }
    
#if 1
    int const fftSize = getFFTSize();
    int const overlapSize = getOverlapSize();
    int bufferConsumed = 0;
    int const bufferSize = buffer.getNumSamples();

    // TODO: buffer ??????????????? prepareToPlay ?????????????????????????????????????????????????????????
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

void PluginAudioProcessor::getSpectrumDataForUI(ReferenceableArray<SpectrumData> &dest)
{
    if(dest.size() != getTotalNumInputChannels()) {
        dest.resize(getTotalNumInputChannels());
    }

    auto fftSize = getFFTSize();
    for(auto &data: dest) {
        data.resize(fftSize);
        data.clear();
    }


    std::unique_lock lock(_mtxUIData);

    for(int i = 0; i < dest.size(); ++i) {
        auto const & src = _spectrums[i];
        auto & d = dest[i];
        d.copyFrom(src);
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
        return (float)(fmod(phaseIn + M_PI, 2.0 * M_PI) - M_PI);
    } else {
        return (float)(fmod(phaseIn - M_PI, -2.0 * M_PI) + M_PI);
    }
}

#define CEPSTRUM_WITH_FFT_FLAG true

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

    // ?????????????????????????????????????????????
    static int phaseReset = 0;
    if((phaseReset++ % 1000) == 0) {
//        _prevInputPhases.clear();
//        _prevOutputPhases.clear();
    }

    jassert(_signalBuffer.size() == fftSize);
    jassert(_frequencyBuffer.size() == fftSize);
    jassert(_cepstrumBuffer.size() == fftSize);

    _inputRingBuffer.readWithoutCopy([&, this](int ch, auto const &bi) {
        _bufferInfoList[ch] = bi;
        assert(bi._len1 + bi._len2 >= fftSize);
    });

    auto const validate_array = [](ReferenceableArray<ComplexType> const &arr) {
        return std::none_of(arr.begin(), arr.end(), [](ComplexType c) {
            auto n = std::norm(c);
            auto r = std::isnan(n) || std::isinf(n);
            assert(r == false);
            return r;
        });
    };

    _tmpBuffer.clear();
    for(int ch = 0; ch < numChannels; ++ch) {
        std::fill(_frequencyBuffer.begin(), _frequencyBuffer.end(), ComplexType{});
        std::fill(_cepstrumBuffer.begin(), _cepstrumBuffer.end(), ComplexType{});
        auto & specData = _tmpSpectrums[ch];
        auto &bi = _bufferInfoList[ch];

        jassert(_overlapCount >= 1);
        for(int i = 0, end = std::min(fftSize, bi._len1); i < end; ++i) {
            _signalBuffer[i] = ComplexType { bi._buf1[i] * _window[i] / _overlapCount, 0 };
        }

        for(int i = bi._len1, end = fftSize; i < end; ++i) {
            _signalBuffer[i] = ComplexType { bi._buf2[i - bi._len1] * _window[i] / _overlapCount, 0 };
        }

        double const powerOfFrameSignals = std::reduce(_signalBuffer.begin(),
                                                       _signalBuffer.end(),
                                                       0.0f,
                                                       [](double sum, ComplexType const &c) { return sum + std::norm(c); }
                                                       );

#if 1
        // ????????????????????????
        _fft->perform(_signalBuffer.data(), _frequencyBuffer.data(), false);

        for(int i = 0; i < fftSize; ++i) {
            specData._originalSpectrum[i] = _frequencyBuffer[i];
        }

#if 0
        // fft ???????????????????????????????????????????????????
        {
            _fft->perform(_frequencyBuffer.data(), _tmpFFTBuffer.data(), true);
            auto sumOriginal = std::reduce(_signalBuffer.begin(), _signalBuffer.end(), 0.0, [](double x, ComplexType c) { return x + std::norm(c); });
            auto sumTransformed = std::reduce(_frequencyBuffer.begin(), _frequencyBuffer.end(), 0.0, [](double x, ComplexType c) { return x + std::norm(c); });
            auto sumRestored = std::reduce(_tmpFFTBuffer.begin(), _tmpFFTBuffer.end(), 0.0, [](double x, ComplexType c) { return x + std::norm(c); });

            if(sumOriginal != 0) {
                double ratio1 = sumTransformed / sumOriginal; // ???????????? fftSize ???????????????????????????
                double ratio2 = sumRestored / sumOriginal;
                double y = ratio1;
            }
        }
#endif

#if 1
        // ???????????????????????????????????????????????????????????????????????????
        {
            for(int i = 0; i < fftSize; ++i) {
                auto amp = std::abs(_frequencyBuffer[i]);
                if(amp == 0) {
                    amp += std::numeric_limits<float>::min();
                }

                auto r = std::log(amp);
                _tmpFFTBuffer[i] = ComplexType { r, 0.0 };
            }

            _fft->perform(_tmpFFTBuffer.data(), _cepstrumBuffer.data(), CEPSTRUM_WITH_FFT_FLAG);

//            ReferenceableArray<ComplexType> tmp1;
//            ReferenceableArray<ComplexType> tmp2;

//            tmp1.resize(fftSize);
//            tmp2.resize(fftSize);
//            _fft->perform(_cepstrumBuffer.data(), tmp1.data(), !CEPSTRUM_WITH_FFT_FLAG);
//            _fft->perform(tmp1.data(), tmp2.data(), CEPSTRUM_WITH_FFT_FLAG);

            // assert(validate_array(_cepstrumBuffer));

            for(int i = 0; i < fftSize; ++i) {
                specData._originalCepstrum[i] = _cepstrumBuffer[i];
            }

            // ????????????????????? liftering ????????????????????????????????????

            // envelope
            _tmpFFTBuffer[0] = _cepstrumBuffer[0];
            for(int i = 1; i <= fftSize / 2; ++i) {
                if(i < envelopOrder) {
                    _tmpFFTBuffer[i] = _cepstrumBuffer[i];
                    _tmpFFTBuffer[fftSize - i] = _cepstrumBuffer[i];
                } else {
                    _tmpFFTBuffer[i] = _tmpFFTBuffer[fftSize - i] = ComplexType { 0, 0 };
                }
            }

//            for(int i = 0; i < fftSize; ++i) {
//                if(_tmpFFTBuffer[i] != _cepstrumBuffer[i]) {
//                    assert(false);
//                }
//            }

            _fft->perform(_tmpFFTBuffer.data(), _tmpFFTBuffer2.data(), !CEPSTRUM_WITH_FFT_FLAG);

            // assert(validate_array(_tmpFFTBuffer2));

            for(int i = 0; i < fftSize; ++i) {
                specData._envelope[i] = _tmpFFTBuffer2[i];
            }
        }

        // ???????????????????????????
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
                specData._envelope[i].real((float)newValue);
            }

            for(int i = 1; i <= fftSize / 2; ++i) {
                specData._envelope[fftSize - i].real(specData._envelope[i].real());
            }
        }
#endif

#if 1
        // ??????????????????
        {
            double hopSize = overlapSize;

            std::fill_n(_analysisMagnitude.begin(), fftSize, 0.0);
            std::fill_n(_analysisFrequencies.begin(), fftSize, 0.0);
            // ?????????????????????bin?????????????????????????????????
            for(int i = 0; i <= fftSize / 2; ++i) {
                auto magnitude = std::abs(_frequencyBuffer[i]);
                auto phase = std::arg(_frequencyBuffer[i]);
                double binCenterFrequency = 2.0 * M_PI * i / fftSize;

                double phaseDiff = phase - _prevInputPhases.getReadPointer(ch)[i];
                _prevInputPhases.getWritePointer(ch)[i] = phase;

                phaseDiff = wrapPhase(phaseDiff - binCenterFrequency * hopSize);
                double binDeviation = phaseDiff * fftSize / hopSize / (2 * M_PI);

                _analysisMagnitude[i] = magnitude;
                _analysisFrequencies[i] = (float)(i + binDeviation);
            }

            // ???????????????
            std::fill_n(_synthesizeMagnitude.begin(), fftSize, 0.0);
            std::fill_n(_synthesizeFrequencies.begin(), fftSize, 0.0);
            for(int i = 0; i <= fftSize / 2; ++i) {
                int shiftedBin = std::floor(i / pitchChangeAmount + 0.5);
                if(shiftedBin > fftSize / 2) { break; }

                _synthesizeMagnitude[i] += _analysisMagnitude[shiftedBin];
                _synthesizeFrequencies[i] = _analysisFrequencies[shiftedBin] * pitchChangeAmount;
            }

            for(int i = 0; i <= fftSize / 2; ++i) {
                double binDeviation = _synthesizeFrequencies[i] - i;
                double phaseDiff = binDeviation * 2.0 * M_PI * hopSize / fftSize;
                double binCenterFrequency = 2.0 * M_PI * i / fftSize;
                phaseDiff += binCenterFrequency * hopSize;

                auto phase = wrapPhase(_prevOutputPhases.getReadPointer(ch)[i] + phaseDiff);

                _frequencyBuffer[i] = ComplexType {
                    (float)(_synthesizeMagnitude[i] * std::cos(phase)),
                    (float)(_synthesizeMagnitude[i] * std::sin(phase))
                };

                _prevOutputPhases.getWritePointer(ch)[i] = phase;
            }

            for(int i = 1; i < fftSize / 2; ++i) {
                _frequencyBuffer[fftSize - i] = std::conj(_frequencyBuffer[i]);
            }

            // assert(validate_array(_frequencyBuffer));
        }
#endif
        for(int i = 0; i < fftSize; ++i) {
            _tmpPhaseBuffer[i] = std::arg(_frequencyBuffer[i]);
        }

        // ???????????????????????????????????????
        for(int i = 0; i < fftSize; ++i) {
            specData._shiftedSpectrum[i] = _frequencyBuffer[i];
        }

        // ???????????????????????????????????????????????????
        // ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
        // ???????????? Envelope ???????????????????????????????????????????????????????????????????????????????????????????????????????????? FineStructure ??????????????????????????????
        // ?????????????????? FineStructure ?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
        // ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
        if(pitchChangeAmount < 1.0) {
            auto newNyquistPos = (int)std::round(fftSize * 0.5 * pitchChangeAmount);
            for(int i = 0; i < fftSize / 2; ++i) {
                if(newNyquistPos + i >= fftSize / 2) { break; }
                if(newNyquistPos - i < 0) { break; }

                _frequencyBuffer[newNyquistPos + i] = _frequencyBuffer[newNyquistPos - i];
            }

            for(int i = 1; i < fftSize / 2; ++i) {
                _frequencyBuffer[fftSize - i] = _frequencyBuffer[i];
            }
        }

#if 1
        // ??????????????????????????????????????????????????????????????????????????????????????????????????????
        {
            // ?????????????????????????????? FFT ?????????????????????????????????
            for(int i = 0; i < fftSize; ++i) {
                auto amp = std::abs(_frequencyBuffer[i]);
                auto r = log(amp + std::numeric_limits<float>::epsilon());
                _tmpFFTBuffer[i] = ComplexType { r, 0.0 };
            }

            _fft->perform(_tmpFFTBuffer.data(), _cepstrumBuffer.data(), CEPSTRUM_WITH_FFT_FLAG);

            // assert(validate_array(_cepstrumBuffer));

            // fine structure
            _tmpFFTBuffer[0] = ComplexType { 0, 0 };
            for(int i = 1; i <= fftSize / 2; ++i) {
                if(i >= envelopOrder) {
                    _tmpFFTBuffer[i] = _cepstrumBuffer[i];
                    _tmpFFTBuffer[fftSize - i] = _cepstrumBuffer[i];
                } else {
                    _tmpFFTBuffer[i] = _tmpFFTBuffer[fftSize - i] = ComplexType { 0, 0 };
                }
            }

            _fft->perform(_tmpFFTBuffer.data(), _tmpFFTBuffer2.data(), !CEPSTRUM_WITH_FFT_FLAG);

            // assert(validate_array(_tmpFFTBuffer2));

            // ???????????????????????????????????????????????????
            if(pitchChangeAmount < 1.0) {
                auto newNyquistPos = (int)std::round(fftSize * 0.5 * pitchChangeAmount);

                for(int i = newNyquistPos; i < fftSize / 2; ++i) {
                    _tmpFFTBuffer2[i] = ComplexType{};
                }

                for(int i = 1; i < fftSize / 2; ++i) {
                    _tmpFFTBuffer2[fftSize - i] = _tmpFFTBuffer2[i];
                }
            }

            for(int i = 0; i < fftSize; ++i) {
                specData._fineStructure[i] = _tmpFFTBuffer2[i];
            }

#if 0
            // use pitch shifted envelope
            _tmpFFTBuffer[0] = _cepstrumBuffer[0];
            for(int i = 1; i <= fftSize / 2; ++i) {
                if(i < envelopOrder) {
                    _tmpFFTBuffer[i] = _cepstrumBuffer[i];
                    _tmpFFTBuffer[fftSize - i] = _cepstrumBuffer[i];
                } else {
                    _tmpFFTBuffer[i] = _tmpFFTBuffer[fftSize - i] = ComplexType { 0, 0 };
                }
            }

            // assert(validate_array(_tmpFFTBuffer));

            _fft->perform(_tmpFFTBuffer.data(), _tmpFFTBuffer2.data(), !CEPSTRUM_WITH_FFT_FLAG);

            // assert(validate_array(_tmpFFTBuffer2));

            for(int i = 0; i < fftSize; ++i) {
                specData._envelope[i] = _tmpFFTBuffer2[i];
            }
#endif
        }

        // ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????

        for(int i = 0; i <= fftSize / 2; ++i) {
            auto const amp = exp(specData._envelope[i].real() * envelopAmount + specData._fineStructure[i].real() * fineStructureAmount);
            // assert(std::isinf(amp) == false);

            _frequencyBuffer[i] = ComplexType {
                (float)(amp * std::cos(_tmpPhaseBuffer[i])),
                (float)(amp * std::sin(_tmpPhaseBuffer[i]))
            };

            // assert(std::isinf(std::norm(_frequencyBuffer[i])) == false);
        }

        for(int i = 1; i < fftSize / 2; ++i) {
            _frequencyBuffer[fftSize - i] = std::conj(_frequencyBuffer[i]);
        }

        // assert(validate_array(_frequencyBuffer));

        // ?????????????????????????????????
        for(int i = 0; i < fftSize; ++i) {
            specData._synthesisSpectrum[i] = _frequencyBuffer[i];
        }
#endif

        _fft->perform(_frequencyBuffer.data(), _signalBuffer.data(), true);
#endif

        for(int i = 0; i < fftSize; ++i) {
            _signalBuffer[i] *= _window[i];
        }

        std::transform(_signalBuffer.begin(),
                       _signalBuffer.end(),
                       _tmpBuffer.getWritePointer(ch),
                       [](auto x) { return x.real(); }
                       );

        double const powerOfSynthesizedSignals = std::reduce(_tmpBuffer.getReadPointer(ch),
                                                             _tmpBuffer.getReadPointer(ch) + fftSize,
                                                             0.0f,
                                                             [](double sum, double x) { return sum + (x * x); }
                                                             );

        float const expectedGainAmount = (float)std::sqrt((powerOfSynthesizedSignals == 0) ? 1.0 : powerOfFrameSignals / powerOfSynthesizedSignals);
        _smoothedGain.setTargetValue(expectedGainAmount);
        float const newGainAmount = _smoothedGain.getNextValue();
        juce::FloatVectorOperations::multiply(_tmpBuffer.getWritePointer(ch), newGainAmount, fftSize);

//        for(int i = 0; i < fftSize; ++i) {
//            auto x = _tmpBuffer.getReadPointer(ch)[i];
//            assert(std::isnan(x) == false && std::isinf(x) == false);
//        }
    }

    if(_outputRingBuffer.overlapAdd(_tmpBuffer, fftSize - overlapSize) == false) {
        assert("should never fail" && false);
    }
    
    _inputRingBuffer.discard(overlapSize);

    {
        std::unique_lock lock(_mtxUIData);
        for(int i = 0; i < _spectrums.size(); ++i) {
            _spectrums[i].copyFrom(_tmpSpectrums[i]);
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
