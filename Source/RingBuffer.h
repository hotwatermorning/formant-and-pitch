#pragma once

#include <atomic>
#include <vector>
#include "prefix.h"

NS_HWM_BEGIN

template<class T>
struct RingBuffer
{
    template<class Iter1, class Iter2>
    static void add_n(Iter1 src, int n, Iter2 dest)
    {
        for(int i = 0; i < n; ++i) {
            *dest++ += *src++;
        }
    }

    RingBuffer()
    :   RingBuffer(0, 0)
    {}

    RingBuffer(int numChannels, int capacity)
    {
        resize(numChannels, capacity);
    }

    void resize(int numChannels, int capacity)
    {
        _buffer.setSize(numChannels, capacity+1);
        _buffer.clear();

        _capacity = capacity;
        _bufferLength = _capacity + 1;
        _numChannels = numChannels;
        _readPos = 0;
        _writePos = 0;
    }

    int getNumChannels() const
    {
        return _numChannels;
    }

    int getCapacity() const
    {
        return _capacity;
    }

    int getNumReadable() const
    {
        auto r = _readPos.load();
        auto w = _writePos.load();

        if(r <= w)
        {
            return w - r;
        }
        else
        {
            return w + (_capacity + 1) - r;
        }
    }

    int getNumWritable() const
    {
        return _capacity - getNumReadable();
    }

    bool isFull() const
    {
        return getNumWritable() == 0;
    }

    bool isEmpty() const
    {
        return getNumReadable() == 0;
    }

    void clear()
    {
        _buffer.clear();
        _readPos = 0;
        _writePos = 0;
    }

    /** 指定した値を指定した長さだけ書き込む */
    bool fill(int length, T value = T{})
    {
        if(length > getNumWritable()) {
            return false;
        }

        const int w = _writePos.load();
        const int bl = _bufferLength;

        const int numToCopy1 = std::min<int>(bl - w, length);
        for(int ch = 0, end = _numChannels; ch < end; ++ch)
        {
            auto& destBuffer = getBuffer()[ch];
            std::fill_n(destBuffer + w, numToCopy1, value);
        }

        const int numToCopy2 = std::max<int>(length, numToCopy1) - numToCopy1;
        if(numToCopy2 == 0)
        {
            _writePos.store(w + numToCopy1);
            jassert(getNumReadable() >= 0);
        }
        else
        {
            for(int ch = 0, end = _numChannels; ch < end; ++ch)
            {
                auto& destBuffer = getBuffer()[ch];
                std::fill_n(destBuffer, numToCopy2, value);
            }
            _writePos.store(numToCopy2);
            jassert(getNumReadable() >= 0);
        }

        return true;
    }

    /** オーディオデータを書き込む
     *
     *  buffer.getNumSamples() > getNumWritable() のときは、
     *  何もせずに false を返す。
     *
     *  @pre buffer.getNumChannels() == this->getNumChannels();
     *  @return データを書き込んだかどうかを bool 型の値で返す。
     */
    [[nodiscard]] bool write(const juce::AudioBuffer<T> &sourceBuffer, int sourceStartIndex = 0)
    {
        jassert(sourceBuffer.getNumChannels() == _numChannels);
        jassert(sourceBuffer.getNumSamples() > sourceStartIndex);

        const int length = sourceBuffer.getNumSamples() - sourceStartIndex;

        if(length > getNumWritable()) {
            return false;
        }

        const int w = _writePos.load();
        const int bl = _bufferLength;

        const int numToCopy1 = std::min<int>(bl - w, length);
        for(int ch = 0, end = _numChannels; ch < end; ++ch)
        {
            auto const * src = sourceBuffer.getReadPointer(ch) + sourceStartIndex;
            auto * dest = getBuffer()[ch] + w;
            std::copy_n(src, numToCopy1, dest);
        }

        const int numToCopy2 = std::max<int>(length, numToCopy1) - numToCopy1;
        if(numToCopy2 == 0)
        {
            _writePos.store(w + numToCopy1);
            jassert(getNumReadable() >= 0);
        }
        else
        {
            for(int ch = 0, end = _numChannels; ch < end; ++ch)
            {
                auto const * src = sourceBuffer.getReadPointer(ch) + sourceStartIndex + numToCopy1;
                auto * dest = getBuffer()[ch];
                std::copy_n(src, numToCopy2, dest);

            }
            _writePos.store(numToCopy2);
            jassert(getNumReadable() >= 0);
        }

        return true;
    }

    /** オーディオデータをオーバーラップして書き込む
     *
     *  buffer.getNumSamples() > getNumWritable() のときは、
     *  何もせずに false を返す。
     *
     *  @pre buffer.getNumChannels() == this->getNumChannels();
     *  @return データを書き込んだかどうかを bool 型の値で返す。
     *
     *  @note この関数は read() 関数の呼び出しに対してスレッドセーフではない。
     *  したがって、 read() 関数と overlapAdd() 関数の呼び出しはお互いに排他制御する必要がある。
     */
    [[nodiscard]] bool overlapAdd(const juce::AudioBuffer<T> &sourceBuffer, int overlapLength, int sourceStartIndex = 0)
    {
        jassert(sourceBuffer.getNumChannels() == _numChannels);
        jassert(sourceBuffer.getNumSamples() > sourceStartIndex);

        const int length = sourceBuffer.getNumSamples() - sourceStartIndex;

        // オーバーラップしたい領域に対してまだ書き込まれていない場合はエラーにする
        if(overlapLength > getNumReadable()) {
            return false;
        }

        // オーバーラップしたい量よりも sourceBuffer で利用可能なデータが少ないときはエラーにする
        if(overlapLength > length) {
            return false;
        }

        const int extLength = length - overlapLength;

        // 新しく拡張される領域のサイズが書き込みサイズを超えるときはエラーにする
        if(extLength > getNumWritable()) {
            return false;
        }

        auto const w = _writePos.load();

        auto const overlapPos = [w, overlapLength, this] {
            if(overlapLength > w) {
                return w + (_capacity + 1) - overlapLength;
            } else {
                return w - overlapLength;
            }
        }();

        const int bl = _bufferLength;

        // 拡張される領域を0クリアする
        {
            const int numToClear1 = std::min<int>(bl - w, extLength);
            const int numToClear2 = std::max<int>(extLength, numToClear1) - numToClear1;

            for(int ch = 0, end = _numChannels; ch < end; ++ch) {
                auto * dest1 = getBuffer()[ch] + w;
                auto * dest2 = getBuffer()[ch];

                std::fill_n(dest1, numToClear1, T{});
                std::fill_n(dest2, numToClear2, T{});
            }
        }

        const int numToCopy1 = std::min<int>(bl - overlapPos, length);
        const int numToCopy2 = std::max<int>(length, numToCopy1) - numToCopy1;

        for(int ch = 0, end = _numChannels; ch < end; ++ch)
        {
            auto const * src = sourceBuffer.getReadPointer(ch) + sourceStartIndex;
            auto * dest = getBuffer()[ch] + overlapPos;
            add_n(src, numToCopy1, dest);
        }

        if(numToCopy2 == 0)
        {
            _writePos.store(overlapPos + numToCopy1);
            jassert(getNumReadable() >= 0);
        }
        else
        {
            for(int ch = 0, end = _numChannels; ch < end; ++ch)
            {
                auto const * src = sourceBuffer.getReadPointer(ch) + sourceStartIndex + numToCopy1;
                auto * dest = getBuffer()[ch];
                add_n(src, numToCopy2, dest);
            }
            _writePos.store(numToCopy2);
            jassert(getNumReadable() >= 0);
        }

        return true;
    }

    struct ConstBufferInfo
    {
        T const * _buf1 = nullptr;
        int _len1 = 0;
        T const * _buf2 = nullptr;
        int _len2 = 0;
    };

    /** 書き込まれたオーディオデータのバッファ情報を、引数に指定された関数に渡す
     *
     *  @param f 次のシグネチャを持つ関数 `void (int channelIndex, ConstBufferInfo const &bi)`
     */
    template<class F>
    void readWithoutCopy(F f) const
    {
        const int length = getNumReadable();
        const int r = _readPos.load();
        const int bl = _bufferLength;

        const int len1 = std::min<int>(bl - r, length);
        const int len2 = std::max<int>(length, len1) - len1;

        for(int ch = 0, end = _numChannels; ch < end; ++ch)
        {
            ConstBufferInfo bufferInfo;
            bufferInfo._buf1 = getBuffer()[ch] + r;
            bufferInfo._len1 = len1;

            if(len2 != 0) {
                bufferInfo._buf2 = getBuffer()[ch];
                bufferInfo._len2 = len2;
            }

            f(ch, bufferInfo);
        }
    }

    /** オーディオデータを読み込む
     *
     *  buffer.getNumSamples() > getNumReadable() のときは、
     *  何もせずに false を返す。
     *
     *  @pre buffer.getNumChannels() == this->getNumChannels();
     *  @return データを読み込んだかどうかを bool 型の値で返す。
     */
    [[nodiscard]] bool read(juce::AudioBuffer<T> &destBuffer, int destStartIndex = 0)
    {
        jassert(destBuffer.getNumChannels() == _numChannels);
        jassert(destBuffer.getNumSamples() >= destStartIndex);

        const int length = destBuffer.getNumSamples() - destStartIndex;

        if(length > getNumReadable()) {
            return false;
        }

        readWithoutCopy([&](int channelIndex, const ConstBufferInfo& bufferInfo) {
            const int numToCopy1 = std::min(bufferInfo._len1, length);
            const int numToCopy2 = length - numToCopy1;
            if(numToCopy1 > 0) {
                std::copy_n(bufferInfo._buf1, numToCopy1, destBuffer.getWritePointer(channelIndex) + destStartIndex);
            }

            if(numToCopy2 > 0) {
                std::copy_n(bufferInfo._buf2, numToCopy2, destBuffer.getWritePointer(channelIndex) + destStartIndex + numToCopy1);
            }
        });

        return true;
    }

    [[nodiscard]] bool read(juce::AudioBuffer<T> &&destBuffer, int destStartIndex = 0)
    {
        return read(destBuffer, destStartIndex);
    }

    /** 書き込まれたオーディオデータを捨てる。
     *  @pre length <= getNumReadable()
     */
    void discard(int length)
    {
        jassert(length <= getNumReadable());

        const int r = _readPos.load();
        const int bl = _bufferLength;

        const int numToCopy1 = std::min<int>(bl - r, length);
        const int numToCopy2 = std::max<int>(length, numToCopy1) - numToCopy1;
        if(numToCopy2 == 0)
        {
            _readPos.store(r + numToCopy1);
        }
        else
        {
            _readPos.store(numToCopy2);
        }

        jassert(getNumReadable() >= 0);
    }

    void discardAll()
    {
        discard(getNumReadable());
    }

private:
    juce::AudioBuffer<T> _buffer;
    T ** getBuffer() { return _buffer.getArrayOfWritePointers(); }
    T const * const * getBuffer() const { return _buffer.getArrayOfReadPointers(); }
    T const * const * getConstBuffer() const { return _buffer.getArrayOfReadPointers(); }

    int _capacity = 0;      // RingBuffer に書込み可能なデータの量
    int _bufferLength = 0;  // _buffer メンバ変数内の 1チャンネルのバッファの長さ
    int _numChannels = 0;
    std::atomic<int> _readPos {0};
    std::atomic<int> _writePos {0};
};

NS_HWM_END
