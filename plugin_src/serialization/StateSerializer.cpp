#include "StateSerializer.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/ui/core/ProgressOverlayManager.h"
#include "IPlugPaths.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace synaptic
{
  // Initialize inline brain feature flag (disabled by default to prevent freezes)
  bool StateSerializer::sEnableInlineBrains = false;
  bool StateSerializer::SerializeBrainState(iplug::IByteChunk& chunk,
                                           const Brain& brain,
                                           const BrainManager& brainMgr,
                                           ui::ProgressOverlayManager* progressMgr) const
  {
    // Append brain section with tag
    chunk.Put(&kBrainSectionTag);

    // Reserve space for section size (will be filled at end)
    int32_t sectionSize = 0;
    int sizePos = chunk.Size();
    chunk.Put(&sectionSize);
    int start = chunk.Size();

    // Write mode: 1=external, 0=inline
    uint8_t mode = brainMgr.UseExternal() ? 1 : 0;
    chunk.Put(&mode);

    if (brainMgr.UseExternal() && !brainMgr.ExternalPath().empty())
    {
      // External mode: store path
      chunk.PutStr(brainMgr.ExternalPath().c_str());

      // If brain has changed, sync it to external file now to persist on project save
      // BUT: skip saving if a rechunk/reanalysis operation is in progress or pending
      // because the brain's metadata might not match the actual analyzed data yet
      if (brainMgr.IsDirty() && !brainMgr.IsOperationInProgress())
      {
        // Show progress overlay immediately before the blocking save operation
        // This ensures the overlay is visible during the file write
        if (progressMgr)
        {
          progressMgr->ShowImmediate("Saving Brain", "Writing brain to external file...");
        }

        iplug::IByteChunk blob;
        brain.SerializeSnapshotToChunk(blob);

        FILE* fp = fopen(brainMgr.ExternalPath().c_str(), "wb");
        if (fp)
        {
          fwrite(blob.GetData(), 1, (size_t)blob.Size(), fp);
          fclose(fp);
          brainMgr.SetDirty(false);
        }

        // Hide progress overlay immediately after save completes
        if (progressMgr)
        {
          progressMgr->HideImmediate();
        }
      }
    }
    else
    {
      // Inline mode: check if inline brains are enabled
      if (sEnableInlineBrains)
      {
        // Store full brain snapshot
        iplug::IByteChunk brainChunk;
        brain.SerializeSnapshotToChunk(brainChunk);

        int32_t sz = brainChunk.Size();
        chunk.Put(&sz);
        if (sz > 0)
          chunk.PutBytes(brainChunk.GetData(), sz);
      }
      else
      {
        // Inline brains disabled - write empty brain data
        int32_t sz = 0;
        chunk.Put(&sz);
      }
    }

    // Fill in section size
    int end = chunk.Size();
    sectionSize = end - start;
    memcpy(chunk.GetData() + sizePos, &sectionSize, sizeof(sectionSize));

    return true;
  }

  int StateSerializer::DeserializeBrainState(const iplug::IByteChunk& chunk,
                                            int startPos,
                                            Brain& brain,
                                            BrainManager& brainMgr)
  {
    int pos = startPos;

    // Look for brain section tag
    uint32_t tag = 0;
    int next = chunk.Get(&tag, pos);
    if (next < 0) return pos; // No extra data

    if (tag != kBrainSectionTag)
    {
      // Not our tag; leave pos unchanged (backwards compatibility)
      return pos;
    }

    pos = next;

    // Read section size
    int32_t sectionSize = 0;
    pos = chunk.Get(&sectionSize, pos);
    if (pos < 0 || sectionSize < 0) return pos;

    int start = pos;

    // Read mode
    uint8_t mode = 0;
    pos = chunk.Get(&mode, pos);
    if (pos < 0) return start + sectionSize;

    if (mode == 1)
    {
      // External mode: read path and try to load
      WDL_String p;
      pos = chunk.GetStr(p, pos);
      if (pos < 0) return start + sectionSize;

      std::string externalPath = p.Get();
      bool useExternal = !externalPath.empty();
      brainMgr.SetExternalRef(externalPath, useExternal);

      // Try to load from path if readable
      if (useExternal)
      {
        FILE* fp = fopen(externalPath.c_str(), "rb");
        if (fp)
        {
          fseek(fp, 0, SEEK_END);
          long sz = ftell(fp);
          fseek(fp, 0, SEEK_SET);

          std::vector<char> data;
          data.resize((size_t)sz);
          fread(data.data(), 1, (size_t)sz, fp);
          fclose(fp);

          // Deserialize brain
          iplug::IByteChunk in;
          in.PutBytes(data.data(), (int)data.size());
          brain.DeserializeSnapshotFromChunk(in, 0, nullptr);
        }
      }
    }
    else
    {
      // Inline mode: read brain data directly
      int32_t sz = 0;
      pos = chunk.Get(&sz, pos);
      if (pos < 0 || sz < 0) return start + sectionSize;

      // Check if inline brains are enabled
      if (sEnableInlineBrains && sz > 0)
      {
        int consumed = brain.DeserializeSnapshotFromChunk(chunk, pos, nullptr);
        if (consumed >= 0)
          pos = consumed;
        else
          pos = start + sectionSize;
      }
      else
      {
        // Inline brains disabled - skip loading inline brain data
        // Just advance position by the stored size
        pos += sz;
      }
    }

    return pos;
  }
}

