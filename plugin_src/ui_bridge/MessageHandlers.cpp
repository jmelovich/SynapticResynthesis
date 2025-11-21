// Message Handler Implementations for SynapticResynthesis
// This file contains all the HandleXxxMsg methods to keep the main file cleaner

#include "SynapticResynthesis.h"
#include "json.hpp"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/modules/WindowCoordinator.h"

// NOTE: Do NOT include "IPlug_include_in_plug_src.h" here - it defines global symbols
// that should only be included in the main SynapticResynthesis.cpp file

// === Brain Message Handlers ===

bool SynapticResynthesis::HandleBrainAddFileMsg(int dataSize, const void* pData)
{
  // Reject drops/imports unless an external brain file reference is set
  if (!mBrainManager.UseExternal())
  {
    DBGMSG("BrainAddFile ignored: external brain not set\n");
    return true; // treated as handled but intentionally ignored
  }

  // pData holds raw bytes: [uint16_t nameLenLE][name bytes UTF-8][file bytes]
  if (!pData || dataSize <= 2)
    return false;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pData);
  uint16_t nameLen = (uint16_t)(bytes[0] | (bytes[1] << 8));
  if (2 + nameLen > dataSize)
    return false;

  std::string name(reinterpret_cast<const char*>(bytes + 2), reinterpret_cast<const char*>(bytes + 2 + nameLen));
  const void* fileData = bytes + 2 + nameLen;
  size_t fileSize = static_cast<size_t>(dataSize - (2 + nameLen));

  DBGMSG("BrainAddFile: name=%s size=%zu SR=%d CH=%d chunk=%d\n", name.c_str(), fileSize, (int)GetSampleRate(), NInChansConnected(), mDSPConfig.chunkSize);

  // Enqueue into pending vector for batched async import (coalesced in OnIdle)
  synaptic::BrainManager::FileData fd;
  fd.name = name;
  fd.data.resize(fileSize);
  memcpy(fd.data.data(), fileData, fileSize);
  mPendingImportFiles.push_back(std::move(fd));

  // Schedule batch start after a brief idle window to catch multi-file drops
  mPendingImportScheduled = true;
  mPendingImportIdleTicks = 2; // ~100ms at IDLE_TIMER_RATE=50

  return true;
}

bool SynapticResynthesis::HandleBrainRemoveFileMsg(int fileId)
{
  DBGMSG("BrainRemoveFile: id=%d\n", fileId);
  mBrainManager.RemoveFile(fileId);
  SetPendingUpdate(PendingUpdate::BrainSummary);
  MarkHostStateDirty();
  return true;
}

bool SynapticResynthesis::HandleBrainExportMsg()
{
  mBrainManager.ExportToFileAsync(
    [this](const std::string& message, int current, int total)
    {
      // Progress callback - calculate progress percentage from current/total
      // Starts at 0% (waiting for file selection), then jumps to 50% after selection
      const float progress = (total > 0) ? ((float)current / (float)total * 100.0f) : 0.0f;
      mProgressOverlayMgr.Show("Exporting Brain", message, progress);
    },
    [this](bool wasCancelled)
    {
      // Completion callback (export doesn't support cancellation yet)
      mProgressOverlayMgr.Hide();
      SetPendingUpdate(PendingUpdate::BrainSummary);  // Update brain UI state (includes storage label)
      SetPendingUpdate(PendingUpdate::DSPConfig);
      SetPendingUpdate(PendingUpdate::MarkDirty);
    });
  return true;
}

bool SynapticResynthesis::HandleBrainImportMsg()
{
  mBrainManager.ImportFromFileAsync(
    [this](const std::string& message, int current, int total)
    {
      // Progress callback - calculate progress percentage from current/total
      // Starts at 0% (waiting for file selection), then jumps to 50% after selection
      const float progress = (total > 0) ? ((float)current / (float)total * 100.0f) : 0.0f;
      mProgressOverlayMgr.Show("Importing Brain", message, progress, false);  // Brain import doesn't support cancellation yet
    },
    [this](bool wasCancelled)
    {
      // Completion callback (import doesn't support cancellation yet)
      mProgressOverlayMgr.Hide();

      // When importing a brain, sync the compact mode setting from the imported brain
      // This ensures the toggle matches what format was loaded
      synaptic::Brain::sUseCompactBrainFormat = mBrain.WasLastLoadedInCompactFormat();

      SetPendingUpdate(PendingUpdate::BrainSummary);
      SetPendingUpdate(PendingUpdate::MarkDirty);
    });
  return true;
}

bool SynapticResynthesis::HandleBrainEjectMsg()
{
  mBrainManager.Reset();
  SetPendingUpdate(PendingUpdate::BrainSummary);
  MarkHostStateDirty();
  return true;
}

bool SynapticResynthesis::HandleBrainDetachMsg()
{
  mBrainManager.Detach();
  SetPendingUpdate(PendingUpdate::BrainSummary);
  MarkHostStateDirty();
  return true;
}

bool SynapticResynthesis::HandleBrainCreateNewMsg()
{
  mBrainManager.CreateNewBrainAsync(
    [this](const std::string& message, int current, int total)
    {
      // Progress callback - calculate progress percentage from current/total
      const float progress = (total > 0) ? ((float)current / (float)total * 100.0f) : 0.0f;
      mProgressOverlayMgr.Show("Creating New Brain", message, progress);
    },
    [this](bool wasCancelled)
    {
      // Completion callback (create new doesn't support cancellation yet)
      mProgressOverlayMgr.Hide();
      SetPendingUpdate(PendingUpdate::BrainSummary);  // Update brain UI state (includes storage label and loaded state)
      SetPendingUpdate(PendingUpdate::DSPConfig);
      SetPendingUpdate(PendingUpdate::MarkDirty);
    });
  return true;
}

bool SynapticResynthesis::HandleCancelOperationMsg()
{
  mBrainManager.RequestCancellation();
  return true;
}

bool SynapticResynthesis::HandleBrainSetCompactModeMsg(int enabled)
{
  // Update the static flag that controls brain serialization format
  synaptic::Brain::sUseCompactBrainFormat = (enabled != 0);

  // Mark the brain as dirty so it will be resaved with the new format on next serialization
  mBrainManager.SetDirty(true);
  MarkHostStateDirty();

  return true;
}
