#pragma once

#include <vector>
#include <cmath>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace synaptic
{
  class Window
  {
  public:
    enum class Type
    {
      Hann,
      Hamming,
      Blackman,
      Rectangular
    };

    Window() = default;

    Window(Type type, int size)
    {
      Set(type, size);
    }

    void Set(Type type, int size)
    {
      mType = type;
      mSize = size;
      mCoeffs.resize(size);  // Only allocate what we need for windowing

      switch (type)
      {
        case Type::Hann:
          mOverlap = 0.5;
          mOverlapRescale = 1.0f;
          for (int n = 0; n < size; ++n)
            mCoeffs[n] = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * n / (size - 1)));
          break;
        case Type::Hamming:
          mOverlap = 0.5;
          mOverlapRescale = 1.0f / 1.08f;
          for (int n = 0; n < size; ++n)
            mCoeffs[n] = 0.54f - 0.46f * std::cos(2.0f * float(M_PI) * n / (size - 1));
          break;
        case Type::Blackman:
          mOverlap = 0.75;
          mOverlapRescale = 0.95f;
          for (int n = 0; n < size; ++n)
            mCoeffs[n] = 0.42f - 0.5f * std::cos(2.0f * float(M_PI) * n / (size - 1))
                               + 0.08f * std::cos(4.0f * float(M_PI) * n / (size - 1));
          break;
        case Type::Rectangular:
        default:
          mOverlap = 0.0;
          mOverlapRescale = 1.0f;
          for (int n = 0; n < size; ++n)
            mCoeffs[n] = 1.0f;
          break;
      }
    }

    int Size() const { return mSize; }
    Type GetType() const { return mType; }
    float GetOverlap() const { return mOverlap; }
    float GetOverlapRescale() const { return mOverlapRescale; }
    const std::vector<float>& Coeffs() const { return mCoeffs; }

    void operator()(float* data) const
    {
       for (int i = 0; i < mSize; ++i)
         data[i] *= mCoeffs.at(i);
    }

    // Utility: find the next valid FFT size (same logic as Brain.cpp)
    static int NextValidFFTSize(int minSize)
    {
      auto isGoodN = [](int n) -> bool {
        if (n <= 0) return false;
        int m = n;
        // Require multiple of 32 for SIMD-friendly real transform
        if ((m % 32) != 0) return false;
        // Factorize by 2, 3, 5 only
        for (int p : {2, 3, 5})
        {
          while ((m % p) == 0) m /= p;
        }
        return m == 1;
      };

      int n = std::max(32, minSize);
      // Round up to at least 32
      if (n < 32) n = 32;
      for (;; ++n)
      {
        if (isGoodN(n)) return n;
      }
    }

    // Utility: get string name for type
    static std::string TypeName(Type type)
    {
      switch (type)
      {
        case Type::Hann: return "Hann";
        case Type::Hamming: return "Hamming";
        case Type::Blackman: return "Blackman";
        case Type::Rectangular: return "Rectangular";
        default: return "Unknown";
      }
    }

    // Utility: convert integer mode to window type
    static Type IntToType(int mode)
    {
      switch (mode)
      {
        case 1: return Type::Hann;
        case 2: return Type::Hamming;
        case 3: return Type::Blackman;
        case 4: return Type::Rectangular;
        default: return Type::Hann;
      }
    }

    // Utility: convert window type to integer mode
    static int TypeToInt(Type type)
    {
      switch (type)
      {
        case Type::Hann: return 1;
        case Type::Hamming: return 2;
        case Type::Blackman: return 3;
        case Type::Rectangular: return 4;
        default: return 1;
      }
    }

  private:
    Type mType = Type::Hann;
    int mSize = 0;
    float mOverlap = 0.0f;
    float mOverlapRescale = 1.0f;
    std::vector<float> mCoeffs;
  };
}
