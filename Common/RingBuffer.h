#pragma once

#include <cstring>
#include <cstdint>
#include <algorithm>

namespace Size {
    constexpr uint32_t OVER = 0;
    constexpr uint32_t BYTE_1K = 0x400;
    constexpr uint32_t BYTE_32K = 32 * BYTE_1K;
};

template <typename T>
class RingBuffer
{
public:
    RingBuffer(uint32_t minSize);
    virtual ~RingBuffer() noexcept;

    inline uint32_t push(const T* data, uint32_t wSize) noexcept;
    inline uint32_t push(uint32_t wSize);
    inline uint32_t pop(T* buf, uint32_t rSize) noexcept;
    inline uint32_t pop(uint32_t rSize);
    inline uint32_t set(uint32_t begin, const T* data, uint32_t wSize) noexcept;
    inline uint32_t get(uint32_t begin, T* buf, uint32_t rSize) noexcept;
    inline uint32_t begin() const noexcept { return out;}
    inline uint32_t end() const noexcept { return in; }

    inline uint32_t freeSize() const noexcept { return nSize - in + out;}
    inline bool empty() const noexcept { return in == out;}
    inline void reset() noexcept { in = out = 0;}

private:
    inline uint32_t AlignedRoundup(uint32_t val) noexcept;

private:
    T* pBuffer;
    const uint32_t nSize;
    uint32_t in;
    uint32_t out;
};

template <typename T>
RingBuffer<T>::RingBuffer(uint32_t minSize)
    : nSize(AlignedRoundup(minSize)), in(0), out(0)
{
    if (nSize == (uint32_t)Size::OVER)
    {
        throw minSize;
    }
    this->pBuffer = new T[nSize];
    if (this->pBuffer == nullptr)
    {
        throw minSize;
    }
}

template <typename T>
RingBuffer<T>::~RingBuffer() noexcept
{
    delete[] pBuffer;
    pBuffer = nullptr;
}

template <typename T>
inline uint32_t RingBuffer<T>::AlignedRoundup(uint32_t val) noexcept
{
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val++;
    return val;
}

template<typename T>
inline uint32_t RingBuffer<T>::set(uint32_t begin, const T* data, uint32_t wSize) noexcept
{
    wSize = std::min(wSize, nSize - begin + out);
    uint32_t start = begin & (nSize - 1);
    uint32_t l = std::min(wSize, nSize - start);
    memcpy(pBuffer + start, data, l);
    memcpy(pBuffer, data + l, wSize - l);
    return wSize;
}

template<typename T>
inline uint32_t RingBuffer<T>::get(uint32_t begin, T* buf, uint32_t rSize) noexcept
{
    rSize = std::min(rSize, in - begin);
    uint32_t start = begin & (nSize - 1);
    uint32_t l = std::min(rSize, nSize - start);
    memcpy(buf, pBuffer + start, l);
    memcpy(buf + l, pBuffer, rSize - l);
    return rSize;
}

template<typename T>
inline uint32_t RingBuffer<T>::push(uint32_t wSize)
{
    wSize = std::min(wSize, nSize - in + out);
    in += wSize;
    return wSize;
}

template <typename T>
inline uint32_t RingBuffer<T>::push(const T* data, uint32_t wSize) noexcept
{
    wSize = set(in,data,wSize);
    in += wSize;
    return wSize;
}

template <typename T>
inline uint32_t RingBuffer<T>::pop(T* buf, uint32_t rSize) noexcept
{
    rSize = get(out,buf,rSize);
    out += rSize;
    return rSize;
}

template<typename T>
inline uint32_t RingBuffer<T>::pop(uint32_t rSize)
{
    rSize = std::min(rSize, in - out);
    out += rSize;
    return rSize;
}
