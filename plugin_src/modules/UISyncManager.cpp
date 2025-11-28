/**
 * @file UISyncManager.cpp
 * @brief Implementation of UI synchronization and message handling
 */

#include "plugin_src/modules/UISyncManager.h"
#include "plugin_src/ui/core/SynapticUI.h"
#include "plugin_src/ui/core/ProgressOverlayManager.h"
#include "plugin_src/ui/core/UIConstants.h"
#include "plugin_src/audio/DSPContext.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/params/ParameterManager.h"
#include "plugin_src/modules/WindowCoordinator.h"
#include "plugin_src/modules/WindowModeHelpers.h"
#include "plugin_src/modules/DSPConfig.h"
#include "plugin_src/modules/AudioStreamChunker.h"
#include "plugin_src/transformers/BaseTransformer.h"
#include "plugin_src/transformers/TransformerFactory.h"
#include "plugin_src/morph/MorphFactory.h"
#include "plugin_src/ui_bridge/MessageTags.h"
#include "plugin_src/Structs.h"
#include "plugin_src/ui/controls/UIControls.h"

namespace synaptic {

UISyncManager::UISyncManager(iplug::Plugin* plugin,
                             Brain* brain,
                             BrainManager* brainManager,
                             ParameterManager* paramManager,
                             WindowCoordinator* windowCoordinator,
                             DSPConfig* dspConfig,
                             ui::ProgressOverlayManager* overlayMgr)
  : mPlugin(plugin)
  , mBrain(brain)
  , mBrainManager(brainManager)
  , mParamManager(paramManager)
  , mWindowCoordinator(windowCoordinator)
  , mDSPConfig(dspConfig)
  , mOverlayMgr(overlayMgr)
{
}

void UISyncManager::SetDSPContext(DSPContext* dspContext, AudioStreamChunker* chunker)
{
  mDSPContext = dspContext;
  mChunker = chunker;
}

void UISyncManager::SetUI(ui::SynapticUI* ui)
{
  mUI = ui;
  if (mUI && mOverlayMgr)
  {
    mOverlayMgr->SetSynapticUI(mUI);
  }
}

void UISyncManager::OnUIClose()
{
  if (mOverlayMgr)
  {
    mOverlayMgr->SetSynapticUI(nullptr);
  }
  mUI = nullptr;
  mNeedsInitialUIRebuild = true;
}

void UISyncManager::OnRestoreState()
{
  SyncAllUIState();
}

bool UISyncManager::CheckAndClearPendingUpdate(PendingUpdate flag)
{
  uint32_t expected = mPendingUpdates.load();
  uint32_t mask = static_cast<uint32_t>(flag);
  while ((expected & mask) && !mPendingUpdates.compare_exchange_weak(expected, expected & ~mask));
  return (expected & mask) != 0;
}

void UISyncManager::MarkHostStateDirty()
{
  if (!mPlugin || !mParamManager) return;

  int idx = mParamManager->GetDirtyFlagParamIdx();
  if (idx < 0) idx = mParamManager->GetBufferWindowParamIdx();

  if (idx >= 0 && mPlugin->GetParam(idx))
  {
    const bool cur = mPlugin->GetParam(idx)->Bool();
    const double norm = mPlugin->GetParam(idx)->ToNormalized(cur ? 0.0 : 1.0);
    mPlugin->BeginInformHostOfParamChangeFromUI(idx);
    mPlugin->SendParameterValueFromUI(idx, norm);
    mPlugin->EndInformHostOfParamChangeFromUI(idx);
  }
}

void UISyncManager::SyncAndSendDSPConfig()
{
  // Brain state is now managed by BrainManager, not DSPConfig
  if (!mDSPConfig || !mBrainManager) return;
}

void UISyncManager::SyncBrainUIState()
{
#if IPLUG_EDITOR
  if (!mUI || !mBrain || !mBrainManager) return;

  auto brainSummary = mBrain->GetSummary();
  std::vector<synaptic::ui::BrainFileEntry> uiEntries;
  for (const auto& s : brainSummary)
  {
    uiEntries.push_back({s.id, s.name, s.chunkCount});
  }
  mUI->updateBrainFileList(uiEntries);

  mUI->updateBrainState(mBrainManager->UseExternal(), mBrainManager->ExternalPath());

  auto* compactToggle = mUI->getCompactModeToggle();
  if (compactToggle)
  {
    compactToggle->SetValue(synaptic::Brain::sUseCompactBrainFormat ? 1.0 : 0.0);
    compactToggle->SetDirty(false);
  }
#endif
}

void UISyncManager::SyncAllUIState()
{
#if IPLUG_EDITOR
  if (!mUI || !mDSPContext || !mParamManager) return;

  auto transformer = mDSPContext->GetTransformer();
  auto morph = mDSPContext->GetMorph();

  if (transformer)
    mUI->setDynamicParamContext(transformer, morph, mParamManager, mPlugin);

  mUI->rebuildDynamicParams(synaptic::ui::DynamicParamType::Transformer,
                             mDSPContext->GetTransformerRaw(), *mParamManager, mPlugin);
  mUI->rebuildDynamicParams(synaptic::ui::DynamicParamType::Morph,
                             mDSPContext->GetMorphRaw(), *mParamManager, mPlugin);

  SyncBrainUIState();

  mUI->resizeWindowToFitContent();
#endif
}

void UISyncManager::DrainUiQueue()
{
  if (CheckAndClearPendingUpdate(PendingUpdate::MarkDirty))
    MarkHostStateDirty();

  // Apply any pending imported settings
  if (mBrainManager && mParamManager && mDSPConfig && mWindowCoordinator && mChunker)
  {
    const int impCS = mBrainManager->GetPendingImportedChunkSize();
    const int impAW = mBrainManager->GetPendingImportedAnalysisWindow();

    if (impCS > 0 || impAW > 0)
    {
      const int chunkSizeIdx = mParamManager->GetChunkSizeParamIdx();
      const int analysisWindowIdx = mParamManager->GetAnalysisWindowParamIdx();

      if (impCS > 0 && chunkSizeIdx >= 0)
      {
        synaptic::ParameterManager::SetParameterFromUI(mPlugin, chunkSizeIdx, (double)impCS);
        mDSPConfig->chunkSize = impCS;
        mChunker->SetChunkSize(mDSPConfig->chunkSize);

        if (mDSPContext)
        {
          auto transformer = mDSPContext->GetTransformer();
          auto morph = mDSPContext->GetMorph();

          if (transformer)
          {
            transformer->OnReset(mPlugin->GetSampleRate(), mDSPConfig->chunkSize,
                                 mDSPConfig->bufferWindowSize, mPlugin->NInChansConnected());
          }
          if (morph)
          {
            morph->OnReset(mPlugin->GetSampleRate(), mDSPConfig->chunkSize,
                           mPlugin->NInChansConnected());
          }
        }
      }

      if (impAW > 0 && analysisWindowIdx >= 0)
      {
        const int idx = WindowMode::ClampParam(impAW - 1);

        if (mPlugin->GetParam(kWindowLock)->Bool())
        {
          const int currentOutputWindowIdx = mPlugin->GetParam(kOutputWindow)->Int();

          if (idx != currentOutputWindowIdx)
          {
            mPlugin->GetParam(kWindowLock)->Set(0.0);
            synaptic::ParameterManager::SetParameterFromUI(mPlugin, kWindowLock, 0.0);
            MarkHostStateDirty();
          }
        }

        SetPendingUpdate(PendingUpdate::SuppressAnalysisReanalyze);
        synaptic::ParameterManager::SetParameterFromUI(mPlugin, analysisWindowIdx, (double)idx);
        mDSPConfig->analysisWindowMode = impAW;
      }

      mWindowCoordinator->UpdateBrainAnalysisWindow(*mDSPConfig);
      if (mUI) mWindowCoordinator->SyncWindowControls(mUI->graphics());

      if (mDSPContext)
        mWindowCoordinator->UpdateChunkerWindowing(*mDSPConfig, mDSPContext->GetTransformerRaw());

      // Update latency
      if (mDSPContext)
      {
        auto transformer = mDSPContext->GetTransformer();
        if (transformer)
        {
          int latency = mDSPConfig->chunkSize + transformer->GetAdditionalLatencySamples(
            mDSPConfig->chunkSize, mDSPConfig->bufferWindowSize);
          mPlugin->SetLatency(latency);
        }
      }

      SyncAndSendDSPConfig();

#if IPLUG_EDITOR
      SetPendingUpdate(PendingUpdate::RebuildTransformer);
#endif
    }
  }
}

void UISyncManager::OnIdle()
{
  DrainUiQueue();

#if IPLUG_EDITOR
  if (mUI)
  {
    if (mNeedsInitialUIRebuild)
    {
      SyncAllUIState();
      mNeedsInitialUIRebuild = false;
    }

    if (CheckAndClearPendingUpdate(PendingUpdate::BrainSummary))
    {
      SyncBrainUIState();
    }

    if (mOverlayMgr)
      mOverlayMgr->ProcessPendingUpdates(mUI);

      if (HasPendingUpdate(PendingUpdate::RebuildTransformer) || HasPendingUpdate(PendingUpdate::RebuildMorph))
      {
        if (mDSPContext)
        {
          // Use current or pending transformer/morph for UI rebuild
          // Prefer pending if available (will be swapped in next audio block)
          auto currentTransformer = mDSPContext->HasPendingTransformer()
            ? mDSPContext->GetPendingTransformer()
            : mDSPContext->GetTransformer();

          auto currentMorph = mDSPContext->HasPendingMorph()
            ? mDSPContext->GetPendingMorph()
            : mDSPContext->GetMorph();

          mUI->setDynamicParamContext(currentTransformer, currentMorph, mParamManager, mPlugin);
        }

        mUI->rebuild();
        SyncBrainUIState();
        mWindowCoordinator->SyncWindowControls(mUI->graphics());

        CheckAndClearPendingUpdate(PendingUpdate::RebuildTransformer);
        CheckAndClearPendingUpdate(PendingUpdate::RebuildMorph);
      }
  }
#endif

  // Coalesce pending dropped files
  if (mPendingImportScheduled.load())
  {
    if (mPendingImportIdleTicks > 0)
      --mPendingImportIdleTicks;

    if (mPendingImportIdleTicks <= 0)
    {
      if (!mBrainManager->IsOperationInProgress())
      {
        auto files = std::move(mPendingImportFiles);
        mPendingImportFiles.clear();
        mPendingImportScheduled = false;

        if (!files.empty())
        {
          mOverlayMgr->Show("Importing Files", "Starting...", 0.0f, true);
          mBrainManager->AddMultipleFilesAsync(
            std::move(files),
            (int)mPlugin->GetSampleRate(),
            mPlugin->NInChansConnected(),
            mDSPConfig->chunkSize,
            MakeProgressCallback(),
            [this](bool wasCancelled) {
              mOverlayMgr->Hide();
              if (!wasCancelled)
              {
                SetPendingUpdate(PendingUpdate::BrainSummary);
                MarkHostStateDirty();
              }
            }
          );
        }
      }
      else
      {
        mPendingImportIdleTicks = 1;
      }
    }
  }
}

bool UISyncManager::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagBrainAddFile: return HandleBrainAddFileMsg(dataSize, pData);
    case kMsgTagBrainRemoveFile: return HandleBrainRemoveFileMsg(ctrlTag);
    case kMsgTagBrainExport: return HandleBrainExportMsg();
    case kMsgTagBrainImport: return HandleBrainImportMsg();
    case kMsgTagBrainEject: return HandleBrainEjectMsg();
    case kMsgTagBrainDetach: return HandleBrainDetachMsg();
    case kMsgTagBrainCreateNew: return HandleBrainCreateNewMsg();
    case kMsgTagBrainSetCompactMode: return HandleBrainSetCompactModeMsg(ctrlTag);
    case kMsgTagCancelOperation: return HandleCancelOperationMsg();
    default: return false;
  }
}

// === Message Handlers ===

bool UISyncManager::HandleBrainAddFileMsg(int dataSize, const void* pData)
{
  if (!mBrainManager->UseExternal()) return true;

  if (!pData || dataSize <= 2) return false;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(pData);
  uint16_t nameLen = (uint16_t)(bytes[0] | (bytes[1] << 8));
  if (2 + nameLen > dataSize) return false;

  std::string name(reinterpret_cast<const char*>(bytes + 2), reinterpret_cast<const char*>(bytes + 2 + nameLen));
  const void* fileData = bytes + 2 + nameLen;
  size_t fileSize = static_cast<size_t>(dataSize - (2 + nameLen));

  synaptic::BrainManager::FileData fd;
  fd.name = name;
  fd.data.resize(fileSize);
  memcpy(fd.data.data(), fileData, fileSize);
  mPendingImportFiles.push_back(std::move(fd));

  mPendingImportScheduled = true;
  mPendingImportIdleTicks = 2;

  return true;
}

bool UISyncManager::HandleBrainRemoveFileMsg(int fileId)
{
  mBrainManager->RemoveFile(fileId);
  SetPendingUpdate(PendingUpdate::BrainSummary);
  MarkHostStateDirty();
  return true;
}

bool UISyncManager::HandleBrainExportMsg()
{
  mBrainManager->ExportToFileAsync(MakeProgressCallback(), MakeStandardCompletionCallback());
  return true;
}

bool UISyncManager::HandleBrainImportMsg()
{
  mBrainManager->ImportFromFileAsync(
    [this](const std::string& message, int current, int total) {
      const float progress = (total > 0) ? ((float)current / (float)total * ui::Progress::kMaxProgress) : 0.0f;
      mOverlayMgr->Show("Importing Brain", message, progress, false);
    },
    [this](bool wasCancelled) {
      mOverlayMgr->Hide();
      synaptic::Brain::sUseCompactBrainFormat = mBrain->WasLastLoadedInCompactFormat();
      SetPendingUpdate(PendingUpdate::BrainSummary);
      SetPendingUpdate(PendingUpdate::MarkDirty);
    });
  return true;
}

bool UISyncManager::HandleBrainEjectMsg()
{
  mBrainManager->Reset();
  SetPendingUpdate(PendingUpdate::BrainSummary);
  MarkHostStateDirty();
  return true;
}

bool UISyncManager::HandleBrainDetachMsg()
{
  mBrainManager->Detach();
  SetPendingUpdate(PendingUpdate::BrainSummary);
  MarkHostStateDirty();
  return true;
}

bool UISyncManager::HandleBrainCreateNewMsg()
{
  mBrainManager->CreateNewBrainAsync(MakeProgressCallback(), MakeStandardCompletionCallback());
  return true;
}

bool UISyncManager::HandleCancelOperationMsg()
{
  mBrainManager->RequestCancellation();
  return true;
}

bool UISyncManager::HandleBrainSetCompactModeMsg(int enabled)
{
  synaptic::Brain::sUseCompactBrainFormat = (enabled != 0);
  mBrainManager->SetDirty(true);
  MarkHostStateDirty();
  return true;
}

synaptic::BrainManager::ProgressFn UISyncManager::MakeProgressCallback()
{
  return [this](const std::string& fileName, int current, int total) {
    const float p = (total > 0)
      ? ((float)current / (float)total * ui::Progress::kMaxProgress)
      : ui::Progress::kDefaultProgress;
    mOverlayMgr->Update(fileName, p);
  };
}

synaptic::BrainManager::CompletionFn UISyncManager::MakeStandardCompletionCallback()
{
  return [this](bool wasCancelled) {
    mOverlayMgr->Hide();
    if (!wasCancelled)
    {
      SetPendingUpdate(PendingUpdate::BrainSummary);
      SetPendingUpdate(PendingUpdate::DSPConfig);
      SetPendingUpdate(PendingUpdate::MarkDirty);
    }
  };
}

} // namespace synaptic
