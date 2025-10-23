#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct AudioChunk;

using sample = iplug::sample;
using Chunk = std::vector<std::vector<float>>;

class Morph
{
public:
  enum Type
  {
    None,            // Passthrough
    CrossSynthesis,  // Cross-synthesis between two audio streams (log magnitude, geometric mean, other modes?)
    SpectralVocoder, // Apply input spectral envelope onto output
    CepstralMorph,   // Morph between cepstra
    HarmonicMorph,   // Morph between harmonic structures
    SpectralMasking, // Apply spectral masking effects

    // Proposed: (btw i have no idea what most of these mean theyre just what chatgpt recommended we check out)
    // Geometric Mean Magnitude Morph
    // Envelope Cross-Deformation (spectral energy of output with formant curvature of input)
    // Spectral Warping Morph (map formant shifts and warp smoothly)
    // Morph that emphasizes phase coherence, really blending two signals
    // Distribution Morph (magnitudes are probability distributions, interpolate via Earth Mover's Distance)
    // Noise-Tone Decomposition Morph (morph tonal and residual components separately)
    // Spectral Contrast Morph (extract "contrast" and interpolate contrast instead of magnitudes)
    // Iterative Projection Morph (project into shared manifold, nmf or pca, and interpolate in that space)
    //    Spectral Topology Morph (get each partial's features and morph on shortest path between manifolds)
    //    Optimal Transport Morph (treat magnitudes as mass distributions, wasserstein barycenter to morph)
    //    Laplacian Morph (construct graph laplacians and interpolate)
    // Spectral Entropy Morph (low entropy bins emphasize one, high entropy bins emphasize another, morph adapts to signal character)
  };

  // Morphing parameters structure
  struct Parameters
  {
    float morphAmount = 1.0f;        // 0.0 = a only, 1.0 = b only
    float phaseMorphAmount = 1.0f;   // 0.0 = a only, 1.0 = b only
    float vocoderSensitivity = 1.0f; // 0.0 = broad envelope, 1.0 = precise envelope
  };

  Morph() = default;

  Morph(Type type, int fftSize) { Configure(type, fftSize); }

  void Configure(Type type, int fftSize)
  {
    if (mType != type)
    {
      mType = type;
      morphScratch.clear();
      switch (type)
      {
      case (None):
        break;
      case (CrossSynthesis):
        break;
      }
    }
    mFFTSize = fftSize;
  }

  // Main processing function - applies morphing to input audio
  void Process(const Chunk& a, Chunk& b)
  {
    switch (mType)
    {
    case Type::None:
      return;
    case Type::CrossSynthesis:
      ProcessCrossSynthesis(a, b);
      break;
    case Type::SpectralVocoder:
      ProcessSpectralVocoder(a, b);
      break;
    case Type::CepstralMorph:
      ProcessCepstralMorph(a, b);
      break;
    case Type::HarmonicMorph:
      ProcessHarmonicMorph(a, b);
      break;
    case Type::SpectralMasking:
      ProcessSpectralMasking(a, b);
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

  // EXAMPLE MORPH ALGORITHM
  //void ProcessTest(const Chunk& a, Chunk& b)
  //{
  //  const int numChannels = a.size();
  //  const int numSamples = a[0].size();

  //  for (int c = 0; c < numChannels; c++)
  //  {
  //    const sample* __restrict aptr = a[c].data();
  //    sample* __restrict bptr = b[c].data();

  //    for (int s = 0; s < numSamples; s++)
  //    {
  //      bptr[s] *= aptr[s];
  //    }
  //  }
  //}

  void ProcessCrossSynthesis(const Chunk& a, Chunk& b) {
    const int numChannels = std::min(a.size(), b.size());

    const float magAmt = mParams.morphAmount;
    const float phaseAmt = mParams.phaseMorphAmount;
    const float oneMinusMagAmt = 1.0 - mParams.morphAmount;
    const float oneMinusPhaseAmt = 1.0 - mParams.phaseMorphAmount;

    for (int c = 0; c < numChannels; c++)
    {
      const float* __restrict aptr = a[c].data();
      float* __restrict bptr = b[c].data();

      bptr[0] = bptr[0] * magAmt + aptr[0] * oneMinusMagAmt; // dc
      bptr[1] = bptr[1] * magAmt + aptr[1] * oneMinusMagAmt; // nyquist

      for (int i = 2; i < mFFTSize; i += 2) // for interleaved real and imaginary
      {
        const float ma = sqrtf(aptr[i] * aptr[i] + aptr[i + 1] * aptr[i + 1]);
        const float mb = sqrtf(bptr[i] * bptr[i] + bptr[i + 1] * bptr[i + 1]);

        const float m = magAmt * ma + oneMinusMagAmt * mb;

        const float inv_ma = ma > 1e-12f ? 1.0f / ma : 0.0f;
        const float inv_mb = mb > 1e-12f ? 1.0f / mb : 0.0f;

        const float ua_r = aptr[i    ] * inv_ma;
        const float ua_i = aptr[i + 1] * inv_ma;
        const float ub_r = bptr[i    ] * inv_mb;
        const float ub_i = bptr[i + 1] * inv_mb;

        float u_r = phaseAmt * ua_r + oneMinusPhaseAmt * ub_r;
        float u_i = phaseAmt * ua_i + oneMinusPhaseAmt * ub_i;

        const float norm = 1.0/sqrtf(u_r * u_r + u_i * u_i + 1e-20f);
        u_r *= norm;
        u_i *= norm;

        bptr[i    ] = m * u_r;
        bptr[i + 1] = m * u_i;
      }
    }
  }
  void ProcessSpectralVocoder(const Chunk& a, Chunk& b) {}
  void ProcessCepstralMorph(const Chunk& a, Chunk& b) {}
  void ProcessHarmonicMorph(const Chunk& a, Chunk& b) {}
  void ProcessSpectralMasking(const Chunk& a, Chunk& b) {}

  // Member variables
  Type mType = Type::CrossSynthesis;
  int mFFTSize = 1024;
  Parameters mParams;

  std::map<std::string, Chunk> morphScratch;
};