/**
 * @file SynapticUI.h
 * @brief Main UI coordinator and layout manager for the Synaptic Resynthesis plugin
 *
 * Responsibilities:
 * - Builds and manages the complete UI hierarchy (header, tabs, controls)
 * - Coordinates tab switching between DSP and Brain views
 * - Manages dynamic parameter control lifecycle (creation, removal, resizing)
 * - Handles UI rebuild when transformers or morphs change
 * - Synchronizes control states with plugin parameters
 * - Resizes window to fit content dynamically
 *
 * This is the top-level UI class that orchestrates all other UI components.
 */

#pragma once

#include <vector>
#include <memory>
#include "IPlug_include_in_plug_hdr.h"
#include "IControls.h"

#include "../styles/UITheme.h"
#include "../styles/UIStyles.h"
#include "../layout/UILayout.h"
#include "../controls/UIControls.h"
#include "../dynamic/DynamicParamManager.h"

// Forward declarations
namespace synaptic {
  class IChunkBufferTransformer;
  struct IMorph;
  class ParameterManager;
}

namespace synaptic {
namespace ui {

enum class Tab { DSP, Brain };

enum class DynamicParamType { Transformer, Morph };

enum class ControlGroup { Global, DSP, Brain };

// Context for rebuilding dynamic parameters
// Uses shared_ptr to keep objects alive during UI rebuild (prevents race with audio thread)
struct RebuildContext {
  std::shared_ptr<const synaptic::IChunkBufferTransformer> transformer;
  std::shared_ptr<const synaptic::IMorph> morph;
  const synaptic::ParameterManager* paramManager { nullptr };
  iplug::Plugin* plugin { nullptr };
};

// Alias to shorten igraphics qualifiers in headers
namespace ig = iplug::igraphics;

class SynapticUI {
public:
  explicit SynapticUI(ig::IGraphics* graphics);

  void build();
  void rebuild();
  void setActiveTab(Tab tab);

  // Dynamic parameter rebuild (used internally)
  void rebuildDynamicParams(
    DynamicParamType type,
    const void* owner,
    const synaptic::ParameterManager& paramManager,
    iplug::Plugin* plugin);

  // Store references for rebuilding dynamic params on resize
  // Takes shared_ptr to keep objects alive during UI operations
  void setDynamicParamContext(
    std::shared_ptr<const synaptic::IChunkBufferTransformer> transformer,
    std::shared_ptr<const synaptic::IMorph> morph,
    const synaptic::ParameterManager* paramManager,
    iplug::Plugin* plugin)
  {
    mRebuildContext.transformer = transformer;
    mRebuildContext.morph = morph;
    mRebuildContext.paramManager = paramManager;
    mRebuildContext.plugin = plugin;
  }

  // Attachment helper
  ig::IControl* attach(ig::IControl* ctrl, ControlGroup group = ControlGroup::Global);

  // Accessors
  ig::IGraphics* graphics() const { return mGraphics; }
  const UILayout& layout() const { return mLayout; }

  // Store parameter area bounds for rebuilding
  void setTransformerParamBounds(const ig::IRECT& bounds) { mTransformerParamBounds = bounds; }
  void setMorphParamBounds(const ig::IRECT& bounds) { mMorphParamBounds = bounds; }

  // Brain file list management
  void setBrainFileListControl(class BrainFileListControl* ctrl);
  void setBrainStatusControl(class BrainStatusControl* ctrl);
  void setBrainDropControl(class BrainFileDropControl* ctrl);
  void setCreateNewBrainButton(ig::IControl* ctrl);
  void setCompactModeToggle(ig::IVToggleControl* ctrl);
  ig::IVToggleControl* getCompactModeToggle() const { return mCompactModeToggle; }
  void updateBrainFileList(const std::vector<struct BrainFileEntry>& files);
  void updateBrainState(bool useExternal, const std::string& externalPath);

  // Progress overlay management
  void ShowProgressOverlay(const std::string& title, const std::string& message, float progress = 0.0f);
  void UpdateProgressOverlay(const std::string& message, float progress);
  void HideProgressOverlay();

  // Window management
  void resizeWindowToFitContent();

  // Column layout configuration
  int numColumns() const { return mNumColumns; }

  // Public card panel references (accessed by DSPTabView during layout)
  ig::IControl* mTransformerCardPanel { nullptr };
  ig::IControl* mMorphCardPanel { nullptr };
  ig::IControl* mAudioProcessingCardPanel { nullptr };

private:
  void buildHeader(const ig::IRECT& bounds);

  // Helper methods for dynamic layout
  void repositionSubsequentCards(ig::IControl* startCard, float heightDelta);
  void anchorMorphLayoutToCard();

  // Common helpers for rebuild logic
  void RemoveAndClearControls(std::vector<ig::IControl*>& paramControls, std::vector<ig::IControl*>& dspControls);
  void AttachAndSyncControls(std::vector<ig::IControl*>& newControls, std::vector<ig::IControl*>& paramControls,
                             std::vector<ig::IControl*>& dspControls, iplug::Plugin* plugin);
  float ResizeCardToFitContent(ig::IControl* cardPanel, const ig::IRECT& paramBounds,
                               const std::vector<ig::IControl*>& controls, float minHeight);

  // Bulk control visibility helper
  void SetControlGroupVisibility(std::vector<ig::IControl*>& controls, bool visible);

  // Sync control with its parameter value
  void SyncControlWithParam(ig::IControl* ctrl, iplug::Plugin* plugin);

  // Ensure progress overlay is always on top
  void EnsureOverlayOnTop();

  ig::IGraphics* mGraphics;
  UILayout mLayout;
  int mNumColumns { 1 };
  Tab mCurrentTab { Tab::DSP };
  std::vector<ig::IControl*> mDSPControls;
  std::vector<ig::IControl*> mBrainControls;
  TabButton* mDSPTabButton { nullptr };
  TabButton* mBrainTabButton { nullptr };

  // Dynamic parameter management
  DynamicParamManager mDynamicParamMgr;
  std::vector<ig::IControl*> mTransformerParamControls;
  std::vector<ig::IControl*> mMorphParamControls;
  ig::IRECT mTransformerParamBounds;
  ig::IRECT mMorphParamBounds;

  // Brain file list control
  class BrainFileListControl* mBrainFileListControl { nullptr };
  class BrainStatusControl* mBrainStatusControl { nullptr };
  class BrainFileDropControl* mBrainDropControl { nullptr };
  ig::IControl* mCreateNewBrainButton { nullptr };
  ig::IVToggleControl* mCompactModeToggle { nullptr };
  bool mHasBrainLoaded { false }; // Tracks if brain is loaded (for Create New Brain button visibility)

  // Progress overlay
  class ProgressOverlay* mProgressOverlay { nullptr };

  // Cached context for rebuilding dynamic params on resize
  RebuildContext mRebuildContext;
};

} // namespace ui
} // namespace synaptic


