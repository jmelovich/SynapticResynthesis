/**
 * @file ProgressOverlayManager.cpp
 * @brief Implementation of thread-safe progress overlay manager
 */

#include "ProgressOverlayManager.h"
#include "SynapticUI.h"
#include "plugin_src/ui_bridge/UIBridge.h"

namespace synaptic {
namespace ui {

ProgressOverlayManager::ProgressOverlayManager(UIBridge* uiBridge)
  : mUIBridge(uiBridge)
{
}

void ProgressOverlayManager::Show(const std::string& title, const std::string& message, float progress)
{
  if (mUIBridge)
  {
    // WebUI: use bridge directly (already thread-safe via queue)
    mUIBridge->ShowProgressOverlay(title, message, progress);
  }
  else
  {
    // C++ UI: queue for main thread processing
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mPendingUpdate.type = UpdateType::Show;
      mPendingUpdate.title = title;
      mPendingUpdate.message = message;
      mPendingUpdate.progress = progress;
    }
    mHasUpdate = true;
  }
}

void ProgressOverlayManager::Update(const std::string& message, float progress)
{
  if (mUIBridge)
  {
    // WebUI: use bridge directly
    mUIBridge->UpdateProgressOverlay(message, progress);
  }
  else
  {
    // C++ UI: queue for main thread processing
    {
      std::lock_guard<std::mutex> lock(mMutex);
      // Only change to Update if not currently showing (preserve Show if pending)
      if (mPendingUpdate.type != UpdateType::Show)
      {
        mPendingUpdate.type = UpdateType::Update;
      }
      mPendingUpdate.message = message;
      mPendingUpdate.progress = progress;
    }
    mHasUpdate = true;
  }
}

void ProgressOverlayManager::Hide()
{
  if (mUIBridge)
  {
    // WebUI: use bridge directly
    mUIBridge->HideOverlay();
  }
  else
  {
    // C++ UI: queue for main thread processing
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mPendingUpdate.type = UpdateType::Hide;
    }
    mHasUpdate = true;
  }
}

void ProgressOverlayManager::ProcessPendingUpdates(SynapticUI* ui)
{
  if (!ui || !mHasUpdate.exchange(false))
    return;

  // Copy pending update under lock
  PendingUpdate update;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    update = mPendingUpdate;

    // After processing a Show, mark that we've shown the overlay
    // so subsequent Updates don't try to show again
    if (mPendingUpdate.type == UpdateType::Show)
    {
      mPendingUpdate.type = UpdateType::None;
    }
  }

  // Apply update on UI thread
  switch (update.type)
  {
    case UpdateType::Show:
      ui->ShowProgressOverlay(update.title, update.message, update.progress);
      break;

    case UpdateType::Update:
      ui->UpdateProgressOverlay(update.message, update.progress);
      break;

    case UpdateType::Hide:
      ui->HideProgressOverlay();
      break;

    case UpdateType::None:
      break;
  }
}

} // namespace ui
} // namespace synaptic

