#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct AudioChunk;

class Morph
{
public:
  enum Type
  {
    None,            // Passthrough
    CrossSynthesis,  // Cross-synthesis between two audio streams
    SpectralVocoder, // Apply input spectral envelope onto output
    CepstralMorph,   // Morph between cepstra
    HarmonicMorph,   // Morph between harmonic structures
    SpectralMasking, // Apply spectral masking effects
  };

  // Morphing parameters structure
  struct Parameters
  {
    float morphAmount = 1.0f;        // 0.0 = input only, 1.0 = output only
    float phaseMorphAmount = 1.0f;   // 0.0 = input only, 1.0 = output only
    float vocoderSensitivity = 1.0f; // 0.0 = broad envelope, 1.0 = precise envelope
  };

  Morph() = default;

  Morph(Type type, int fftSize) { Configure(type, fftSize); }

  void Configure(Type type, int fftSize)
  {
    mType = type;
    mFFTSize = fftSize;

    // Initialize internal buffers
    mInputBuffer.resize(fftSize, 0.0f);
    mOutputBuffer.resize(fftSize, 0.0f);
    mInputMagnitudeSpectrum.resize(fftSize / 2 + 1, 0.0f);
    mInputPhaseSpectrum.resize(fftSize / 2 + 1, 0.0f);
    mOutputMagnitudeSpectrum.resize(fftSize / 2 + 1, 0.0f);
    mOutputPhaseSpectrum.resize(fftSize / 2 + 1, 0.0f);

    // Initialize FFT setup
    InitializeFFT();

    // Set default parameters based on morph type
    SetDefaultParameters();
  }

  // Main processing function - applies morphing to input audio
  void Process(AudioChunk& chunk, int numSamples, const Parameters& params)
  {
    switch (mType)
    {
    case Type::None:
      break;
    case Type::CrossSynthesis:
      ProcessCrossSynthesis(chunk, numSamples, params);
      break;
    case Type::SpectralVocoder:
      ProcessSpectralVocoder(chunk, numSamples, params);
      break;
    case Type::CepstralMorph:
      ProcessCepstralMorph(chunk, numSamples, params);
      break;
    case Type::HarmonicMorph:
      ProcessHarmonicMorph(chunk, numSamples, params);
      break;
    case Type::SpectralMasking:
      ProcessSpectralMasking(chunk, numSamples, params);
      break;
    }
  }

  // Accessor methods
  Type GetType() const { return mType; }
  int GetFFTSize() const { return mFFTSize; }
  const Parameters& GetParameters() const { return mParams; }

  // Set morphing parameters
  void SetParameters(const Parameters& params) { mParams = params; }

  // Utility: get string name for morph type
  static std::string TypeName(Type type)
  {
    switch (type)
    {
    case Type::None:
      return "None";
    case Type::CrossSynthesis:
      return "Cross Synthesis";
    case Type::SpectralVocoder:
      return "Spectral Vocoder";
    case Type::CepstralMorph:
      return "Cepstral Morph";
    case Type::HarmonicMorph:
      return "Harmonic Morph";
    case Type::SpectralMasking:
      return "Spectral Masking";
    default:
      return "Unknown";
    }
  }

  // Utility: convert integer mode to morph type
  // Convert integer mode to the correct enum Type
  static Type IntToType(int mode)
  {
    switch (mode)
    {
    case 0:
      return Type::None;
    case 1:
      return Type::CrossSynthesis;
    case 2:
      return Type::SpectralVocoder;
    case 3:
      return Type::CepstralMorph;
    case 4:
      return Type::HarmonicMorph;
    case 5:
      return Type::SpectralMasking;
    default:
      return Type::None;
    }
  }

  // Convert enum Type to integer mode
  static int TypeToInt(Type type)
  {
    switch (type)
    {
    case Type::None:
      return 0;
    case Type::CrossSynthesis:
      return 1;
    case Type::SpectralVocoder:
      return 2;
    case Type::CepstralMorph:
      return 3;
    case Type::HarmonicMorph:
      return 4;
    case Type::SpectralMasking:
      return 5;
    default:
      return 0;
    }
  }

private:
  // Morphing algorithm implementations (declarations only)
  void ProcessCrossSynthesis(AudioChunk& chunk, int numSamples, const Parameters& params);
  void ProcessSpectralVocoder(AudioChunk& chunk, int numSamples, const Parameters& params);
  void ProcessCepstralMorph(AudioChunk& chunk, int numSamples, const Parameters& params);
  void ProcessHarmonicMorph(AudioChunk& chunk, int numSamples, const Parameters& params);
  void ProcessSpectralMasking(AudioChunk& chunk, int numSamples, const Parameters& params);

  // Helper functions
  void InitializeFFT();
  void SetDefaultParameters();

  // Member variables
  Type mType = Type::CrossSynthesis;
  int mFFTSize = 1024;
  Parameters mParams;

  // FFT setup and buffers
  // PFFFT_Setup* mFFTSetup = nullptr;
  std::vector<float> mInputBuffer;
  std::vector<float> mOutputBuffer;
  std::vector<float> mInputMagnitudeSpectrum;
  std::vector<float> mOutputMagnitudeSpectrum;
  std::vector<float> mInputPhaseSpectrum;
  std::vector<float> mOutputPhaseSpectrum;
  std::vector<float> mTargetAudioBuffer;
};