# SynapticResynthesis – Transformer & Chunking Architecture Guide

(NOTE: I (Luke) used cursor to generate this document to make it a little easier to understand how the chunking works & the transformers. Its not perfect, but more detailed than what I would have written lol)

This document explains how the AudioStreamChunker and transformer layer work, how they integrate with the plugin, and how to implement new transformers safely and efficiently.

## High-level architecture

- AudioStreamChunker: Converts incoming deinterleaved audio buffers into fixed-size, per-channel chunks, maintains a lookahead window for context, and streams transformed chunks back out.
- Transformer interface (IChunkBufferTransformer): Pluggable algorithms that consume pending input chunks and produce output chunks (either by routing existing chunks or synthesizing new ones). Reports required lookahead and additional latency.
- Plugin glue: Feeds input to the chunker each audio callback, calls the transformer, renders the output queue, and reports latency to the host.

## Data model and memory management

All real-time (RT) code avoids heap allocations. The chunker preallocates:

- Pool (vector<PoolEntry>): Fixed-size array of reusable chunk entries.
- Rings (IndexRing, integer-only):
  - Free: indices available for reuse.
  - Pending: indices of newly completed input chunks waiting for transform.
  - Output: indices queued to be streamed to the host.
  - Window: fixed-capacity lookahead window (most recent completed input chunks) used for context.
- Scratch accumulation: per-channel buffers sized to chunkSize for building the next input chunk.

Each PoolEntry contains:
- AudioChunk chunk: `channelSamples[ch][frame]` and `numFrames`.
- int refCount: counts how many structures currently hold the index (window, pending, output). When it reaches 0, the index returns to `Free`.

### Chunk lifecycle (input ➜ window/pending ➜ transform ➜ output ➜ free)
1) The chunker copies incoming frames into per-channel scratch until `chunkSize` samples are accumulated.
2) A pool index is popped from `Free`, and scratch is copied to the pool entry.
3) The pool index is inserted into:
   - Window (adds one ref) and
   - Pending (adds one ref)
4) The transformer eventually moves indices from Pending to Output (adds one ref for output, and pending ref is dropped when popped), or it can allocate a fresh output chunk and synthesize new content.
5) The renderer plays Output; when a chunk finishes, the Output ref is dropped. If refCount becomes 0, the index returns to `Free`.

## Lookahead window semantics

- The window (`mWindow`) keeps up to `bufferWindowSize` most recent completed input chunks.
- Ordering: It is time-ordered. You can access from oldest to newest or from newest backwards.
- Relation to what you hear now: Playback is delayed by the system latency. With passthrough, reported latency equals `chunkSize`, so the newest window chunk is generally ahead of what is currently audible (lookahead). This enables algorithms to “see the future” relative to the current output.

## Public APIs (chunker)

These are RT-safe and allocation-free in steady-state.

- Configuration & control:
  - `void SetNumChannels(int numChannels)`
  - `void SetChunkSize(int chunkSize)`
  - `void SetBufferWindowSize(int windowSize)`
  - `void Reset()`
  - `int GetChunkSize() const`

- Audio IO:
  - `void PushAudio(sample** inputs, int nFrames)`
    - Accumulates frames and emits a new input chunk whenever `chunkSize` is reached.
  - `void RenderOutput(sample** outputs, int nFrames, int outChans)`
    - Streams queued output chunks into host buffers. Writes up to `min(outChans, numChannels)` and zero-fills others.

- Transformer-facing API (routing existing chunks):
  - `bool PopPendingInputChunkIndex(int& outIdx)`
    - Pops the next ready input chunk (if any). Drops its pending ref.
  - `void EnqueueOutputChunkIndex(int idx)`
    - Adds an output ref and appends to output queue.

- Transformer-facing API (synthesizing new output chunks):
  - `bool AllocateWritableChunkIndex(int& outIdx)`
    - Reserves a free pool entry for writing a brand-new chunk.
  - `AudioChunk* GetWritableChunkByIndex(int idx)`
    - Returns writable per-channel buffers for the reserved index.
  - `void ClearWritableChunkIndex(int idx, sample value = 0.0)`
    - Optional helper to clear all channels/frames before writing.
  - `void CommitWritableChunkIndex(int idx, int numFrames)`
    - Sets final frame count (clamped to `chunkSize`), adds output ref, and enqueues it for playback.

- Window/context access (read-only):
  - `int GetWindowCapacity() const` – Fixed capacity (`bufferWindowSize`).
  - `int GetWindowCount() const` – Current number of chunks in the window.
  - `int GetWindowIndexFromOldest(int ordinal) const` – 0 = oldest, `count-1` = newest. Returns -1 if out of range.
  - `int GetWindowIndexFromNewest(int ordinal) const` – 0 = newest, `count-1` = oldest. Returns -1 if out of range.
  - `const AudioChunk* GetChunkConstByIndex(int poolIdx) const` – Maps a pool index to a const chunk pointer, or nullptr if invalid.
  - `bool PeekCurrentOutput(int& outPoolIdx, int& outFrameIndex) const` – Returns the currently playing output chunk (if any) and the frame-into-chunk being played.

Notes:
- The transformer can read window chunks to inspect context around the “current input time”. Since output is delayed by latency, window contents are typically “future vs. the current audio output”.
- For synthesis, allocate a new output chunk via `AllocateWritableChunkIndex(...)`, write your data, then `CommitWritableChunkIndex(...)`.

## Transformer interface

```cpp
class IChunkBufferTransformer
{
public:
  virtual ~IChunkBufferTransformer() {}

  // Reinitialize on SR / size / channel changes
  virtual void OnReset(double sampleRate, int chunkSize, int bufferWindowSize, int numChannels) = 0;

  // Called each audio block (gated by lookahead requirement)
  virtual void Process(AudioStreamChunker& chunker) = 0;

  // Extra algorithmic latency (in samples), beyond chunk accumulation
  virtual int GetAdditionalLatencySamples(int chunkSize, int bufferWindowSize) const = 0;

  // Minimum number of chunks required in the window before processing
  virtual int GetRequiredLookaheadChunks() const = 0;
};
```

### PassthroughTransformer
- `GetRequiredLookaheadChunks() = 0`
- `GetAdditionalLatencySamples(...) = 0`
- `Process(...)` drains all pending input chunk indices and enqueues them to output unchanged.

### SineMatchTransformer (example synthesis)
- Demonstrates the synthesized-output API:
  - For each input chunk:
    - Estimates RMS amplitude (mono-averaged) and a crude frequency (zero-crossing rate).
    - Allocates a writable chunk, writes a sine wave at the estimated frequency and amplitude, and commits it to the output.
- No additional latency and no lookahead required (both return 0).
- Reads `chunkSize` and `numChannels` from `AudioStreamChunker` (`GetChunkSize()`, `GetNumChannels()`) for consistency with runtime configuration.

### Process call cadence
- The plugin calls `Process(...)` once per audio callback, after feeding input to the chunker, and only if `GetWindowCount() >= GetRequiredLookaheadChunks()`.
- If no full chunk has been accumulated yet, `PopPendingInputChunkIndex(...)` returns false and `Process(...)` becomes a no-op.

## Plugin integration (when things happen)

- `OnReset()` (plugin):
  - Called by the host/framework when SR, block size, or IO config changes.
  - Syncs chunker configuration, resets internal state, and calls `transformer->OnReset(...)`.
  - Updates host latency: `SetLatency( chunkSize + transformer->GetAdditionalLatencySamples(...) )`.

- `ProcessBlock(inputs, outputs, nFrames)` (plugin):
  - Early safety checks (valid IO, channel counts). Clears outputs if invalid.
  - `mChunker.PushAudio(inputs, nFrames)` accumulates incoming samples and emits pending chunks.
  - If lookahead requirement met, `mTransformer->Process(mChunker)`.
  - `mChunker.RenderOutput(outputs, nFrames, NOutChansConnected())` streams current output.
  - Apply per-channel smoothed gain.

- UI messages:
  - Chunk size/window size changes update the chunker configuration synchronously.
  - Algorithm switch creates a new transformer, calls `OnReset(...)`, and recomputes latency.

## Latency model

- Base latency equals `chunkSize` samples (buffer accumulation delay).
- Algorithm-specific latency is added via `GetAdditionalLatencySamples(chunkSize, bufferWindowSize)`.
- Total reported latency: `chunkSize + additionalLatency`.
- Host uses this to time-align output to input.

## Multichannel behavior

- Chunks store per-channel vectors. Chunker supports arbitrary channel counts.
- Rendering writes up to `min(inChans, outChans)` without downmixing.
- Gain is applied per channel.

## Real-time safety

- No heap allocations in the audio thread steady-state. All buffers/rings/pool are preallocated by `Configure()`/`Reset()`.
- Index-based queues avoid moving large buffers.
- Defensive bounds checks around all buffer/index accesses.

## Implementing a new transformer (typical patterns)

1) Define your class deriving from `IChunkBufferTransformer`.
2) Declare how much lookahead and additional latency you need.
3) In `Process(...)`:
   - Option A (routing/reordering):
     - Inspect window using `GetWindowIndexFromNewest/Oldest()` and `GetChunkConstByIndex()`.
     - Pop pending indices using `PopPendingInputChunkIndex(...)`.
     - Enqueue selected indices via `EnqueueOutputChunkIndex(idx)` in your desired order.
   - Option B (synthesis/new audio):
     - Reserve a pool entry via `AllocateWritableChunkIndex(outIdx)`.
     - Get writable buffers via `GetWritableChunkByIndex(outIdx)` and write per-channel samples.
     - Optionally `ClearWritableChunkIndex(outIdx)` first.
     - Call `CommitWritableChunkIndex(outIdx, numFrames)` once ready.

### Example: simple sine synthesis (pseudo-code)
```cpp
void SineLike::Process(AudioStreamChunker& chunker)
{
  int inIdx;
  while (chunker.PopPendingInputChunkIndex(inIdx))
  {
    // Inspect input (optional) via GetChunkConstByIndex(inIdx) or window APIs

    int outIdx;
    if (!chunker.AllocateWritableChunkIndex(outIdx))
      continue;

    AudioChunk* out = chunker.GetWritableChunkByIndex(outIdx);
    const int frames = chunker.GetChunkSize();
    const int chans = chunker.GetNumChannels();

    // Fill out->channelSamples[ch][i] ...

    chunker.CommitWritableChunkIndex(outIdx, frames);
  }
}
```

## FAQs

- Q: Can a transformer see both “ahead” and “behind” relative to audio playback?
  - A: Yes. The window exposes the most recent input chunks (lookahead relative to current output because of latency). The currently playing output head is available via `PeekCurrentOutput(...)`. If you need deeper history beyond the output head, we can add a history ring similarly to the window.

- Q: How often is `Process(...)` called?
  - A: Once per audio callback, after input is pushed and before output is rendered, gated by the lookahead requirement.

- Q: What if `chunkSize` changes mid-play?
  - A: Plugin calls `OnReset()`, reconfigures the chunker, resets the pool/rings/scratch, calls `transformer->OnReset(...)`, and updates latency. Consider crossfading in your algorithm if you want to avoid clicks.

- Q: How do I produce modified audio?
  - A: Use the synthesized output API: allocate a writable chunk, write to it, then commit it.

