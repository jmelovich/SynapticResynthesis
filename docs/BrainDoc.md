## Synaptic Resynthesis – Brain and SimpleSampleBrain Transformer

This document explains the current design and implementation of the in‑memory Brain and the SimpleSampleBrain transformer, plus the UI flows and host/plug messaging used to glue everything together.

### High‑level summary (what was added)

- Brain module (`plugin_src/samplebrain/Brain.h/.cpp`)
  - Imports dropped audio files (WAV/MP3/FLAC) using miniaudio, decodes to f32, planarizes to channel‑major, splits into fixed‑size chunks.
  - For each chunk, computes per‑channel RMS and zero‑crossing‑rate (ZCR) frequency; stores channel arrays and simple averages.
  - Maintains in‑memory chunk store with file bookkeeping and read‑only queries.
  - Remove‑file support with consistent reindexing.
  - Placeholder rechunk API (clears current chunks for now) when DSP chunk size changes.

- Web UI – Brain tab (`resources/web/index.html`)
  - Tabs to switch between DSP and Brain.
  - Brain dropzone accepts WAV/MP3/FLAC; sends bytes to C++ via base64 (chunked conversion for large files).
  - Displays a file list with chunk counts and an X button to remove.
  - Shows a spinner overlay while imports are processed; hides when summary arrives.
  - Displays the “Brain chunk size” reported from C++.

- Plugin integration (`SynapticResynthesis.cpp/.h`)
  - New message tags for add/remove brain files and chunk/algorithm controls.
  - Message handling: parse drag payload header `[uint16 LE nameLen][UTF‑8 name][file bytes]`, import and summarize.
  - Sends JSON updates (`brainSummary`, `brainChunkSize`) to UI with msgTag = -1.
  - On chunk size change, updates chunker + latency, informs UI of brain chunk size, and calls Brain rechunk placeholder.

- New transformer: SimpleSampleBrain (`plugin_src/ChunkBufferTransformer.h`)
  - Matches each input chunk against Brain chunks using weighted distance of frequency and amplitude.
  - Two modes:
    - Average mode: choose one best chunk (uses `avgFreqHz`, `avgRms`) and copy its channels.
    - Channel‑independent mode: for each output channel, select the best brain chunk and source channel independently.
  - Adjustable weights: frequency vs amplitude (default equal weighting).

---

## Brain design

### Data structures

```
struct BrainChunk {
  AudioChunk audio;              // [channel][frame], numFrames
  int fileId;                    // source file logical ID
  int chunkIndexInFile;          // position within its source file

  // Per‑channel analysis
  std::vector<float>  rmsPerChannel;     // RMS per channel
  std::vector<double> freqHzPerChannel;  // ZCR‑based frequency per channel

  // Aggregates across channels
  float  avgRms;
  double avgFreqHz;
};

struct BrainFile {
  int id;
  std::string displayName;       // filename (no path)
  int chunkCount;
  std::vector<int> chunkIndices; // indices into the global chunk vector
};

class Brain {
public:
  int  AddAudioFileFromMemory(const void* data, size_t size,
                              const std::string& displayName,
                              int targetSampleRate, int targetChannels,
                              int chunkSizeSamples);
  void RemoveFile(int fileId);

  // Queries
  std::vector<FileSummary> GetSummary() const;     // id, name, chunkCount
  int GetTotalChunks() const;                      // global chunk count
  const BrainChunk* GetChunkByGlobalIndex(int i) const;

  // Chunk size management
  void RechunkAllFiles(int newChunkSizeSamples, int targetSampleRate);
  int  GetChunkSize() const; // current (last used) chunk size

private: // impl details
  // analysis helpers: ComputeRMS(), ComputeZeroCrossingFreq()
};
```

Notes:
- Thread safety: a `std::mutex` guards `files_`/`chunks_` and related indices. `AddAudioFileFromMemory()` currently holds the lock while pushing chunks (simple, safe; future work can stage outside the lock then commit atomically).
- Chunk store: a single contiguous vector for all chunks; `BrainFile::chunkIndices` maps a file to its chunks.
- Rechunking: `RechunkAllFiles()` currently clears chunks and file mappings and updates the stored chunk size. In a future iteration, the Brain will retain the original decoded audio (or a cache path) to rebuild the chunk set without re‑dropping files.

### Import pipeline (AddAudioFileFromMemory)

1) Decode using miniaudio (f32, target sample rate & channels).
2) Convert interleaved to planar `[channel][frame]`.
3) With lock held:
   - Assign a new `fileId` and create a `BrainFile`.
   - Partition into `chunkSizeSamples` frames, zero‑pad last chunk.
   - For each chunk and each channel:
     - RMS: \( \sqrt{\frac{1}{N}\sum x^2} \)
     - Frequency: ZCR → approx Hz = `zeroCrossings * sampleRate / (2 * N)` with clamps `[20, nyquist-20]`.
   - Store per‑channel arrays and their averages, append chunk to `chunks_` and index to `BrainFile::chunkIndices`.

### Removal and reindexing

`RemoveFile(fileId)` builds a compacted `chunks_` vector excluding that file’s chunks, remaps indices for all remaining files, and drops the file’s entry.

### Queries

- `GetSummary()` → small JSON‑friendly vector `{id, name, chunkCount}` used by UI.
- `GetTotalChunks()` and `GetChunkByGlobalIndex()` are read‑only utilities for transformers.

---

## UI and messaging

### Web UI (Brain tab)

- Drag‑and‑drop reads file bytes into an `ArrayBuffer` and constructs a lightweight header:
  - Header wire format: `[uint16 little‑endian: nameLen][UTF‑8 filename bytes][raw file bytes]`.
  - The whole buffer is base64‑encoded in 32KB chunks and sent to C++ via `SAMFUI(kMsgTagBrainAddFile, ctrlTag=-1, base64)`.
- While processing, an overlay spinner is displayed and user interaction is disabled. The spinner is hidden when the plugin sends `brainSummary`.
- The Brain tab displays:
  - A line “Brain chunk size: X”, updated by `brainChunkSize` JSON.
  - A list of imported files with `(chunkCount)` and an X delete button. Clicking X triggers `SAMFUI(kMsgTagBrainRemoveFile, ctrlTag=fileId)`.

### Host/plugin glue (C++)

Message tags:

- UI → C++
  - `kMsgTagSetChunkSize` (4): `ctrlTag` = int chunk size
  - `kMsgTagSetBufferWindowSize` (5): `ctrlTag` = int
  - `kMsgTagSetAlgorithm` (6): `ctrlTag` = 0 passthrough, 1 sine match, 2 simple sample brain
  - `kMsgTagBrainAddFile` (100): `pData` = base64 decoded header + file bytes
  - `kMsgTagBrainRemoveFile` (101): `ctrlTag` = fileId

- C++ → UI (`msgTag = -1`, base64 JSON)
  - `{ id: "brainSummary", files: [{ id, name, chunks }, ...] }`
  - `{ id: "brainChunkSize", size: <int> }`

Handlers:

- Add file: parse header → name, bytes → `Brain.AddAudioFileFromMemory()` → on success, send `brainSummary`.
- Remove file: `Brain.RemoveFile(fileId)` → send `brainSummary`.
- Chunk size change: update chunker and latency → send `brainChunkSize` → `Brain.RechunkAllFiles(newSize, sr)` → send `brainSummary`.

---

## SimpleSampleBrain transformer

Location: `plugin_src/ChunkBufferTransformer.h`

Purpose: For each input chunk, pick a chunk from the Brain that best matches the input’s frequency and amplitude, with adjustable weighting. Two processing modes are supported.

Configuration:

```
class SimpleSampleBrainTransformer : public IChunkBufferTransformer {
  void SetBrain(const Brain* brain);
  void SetWeights(double weightFreq, double weightAmp);     // default 1.0, 1.0
  void SetChannelIndependent(bool enabled);                  // default false
}
```

Analysis (input side): for each channel in the input chunk, compute:

- `inRms[ch] = sqrt(sum(x^2)/N)`
- `inFreq[ch] = ZCR * sampleRate / (2*N)` clamped to `[20, nyquist-20]`, default `440.0` if invalid

Distance metric (per comparison):

- Frequency distance \( d_f = |f_{in} - f_{brain}| / nyquist \in [0, 1] \)
- Amplitude distance \( d_a = |rms_{in} - rms_{brain}| \) (clamped to `[0, 1]`)
- Score \( s = w_f \cdot d_f + w_a \cdot d_a \)

Modes:

1) Average mode (channelIndependent = false)
   - Compute average `inFreqAvg` and `inRmsAvg` across channels.
   - For each brain chunk, use `avgFreqHz` and `avgRms` to compute score; select best chunk.
   - Copy channels from that chunk to all output channels (with fallback if channel counts differ).

2) Channel‑independent mode (channelIndependent = true)
   - For each output channel `ch`:
     - For each brain chunk and for each of its source channels `bch`, compute score using `inFreq[ch]`/`inRms[ch]` vs `freqHzPerChannel[bch]`/`rmsPerChannel[bch]` (fallback to averages if missing).
     - Select best `(chunk, sourceChannel)` pair and copy that source channel into output channel `ch`.

Scheduling & latency:

- `GetRequiredLookaheadChunks() = 0`
- `GetAdditionalLatencySamples() = 0`

---

## Known limitations & next steps

- Rechunking currently clears the Brain and metadata. To fully support live chunk size changes, the Brain should retain either the original decoded audio in RAM or a reference to a cached file path from which to re‑decode.
- Matching is O(totalChunks × channels) per input chunk. For large Brains, consider indexing (e.g., multidimensional trees over `(freq, rms)`), approximation (LSH), or pre‑bucketing bins.
- ZCR frequency is a coarse estimate. Future: FFT‑based spectral centroid/fundamental estimation.
- UI: modify the transformer interface to support exposing certain parameters to the UI, in a modular way that will automatically support other parameters in other transformers down the line

---

## Wire formats (for reference)

- Drag‑drop payload UI → C++ (binary in base64):

```
struct HeaderAndFileBytes {
  uint16_t nameLenLE;          // little‑endian
  uint8_t  name[nameLenLE];    // UTF‑8 filename bytes (no NUL)
  uint8_t  fileBytes[...];     // raw audio file contents
};
```

- C++ → UI JSON messages (base64):

```
// Brain summary
{ "id": "brainSummary", "files": [ { "id": <int>, "name": "...", "chunks": <int> }, ... ] }

// Brain chunk size
{ "id": "brainChunkSize", "size": <int> }
```


