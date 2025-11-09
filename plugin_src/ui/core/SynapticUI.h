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

// Context for rebuilding dynamic parameters
struct RebuildContext {
  const synaptic::IChunkBufferTransformer* transformer { nullptr };
  const synaptic::IMorph* morph { nullptr };
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

  // Dynamic parameter management
  void rebuildTransformerParams(
    const synaptic::IChunkBufferTransformer* transformer,
    const synaptic::ParameterManager& paramManager,
    iplug::Plugin* plugin);

  void rebuildMorphParams(
    const synaptic::IMorph* morph,
    const synaptic::ParameterManager& paramManager,
    iplug::Plugin* plugin);

  // Store references for rebuilding dynamic params on resize
  void setDynamicParamContext(
    const synaptic::IChunkBufferTransformer* transformer,
    const synaptic::IMorph* morph,
    const synaptic::ParameterManager* paramManager,
    iplug::Plugin* plugin)
  {
    mRebuildContext.transformer = transformer;
    mRebuildContext.morph = morph;
    mRebuildContext.paramManager = paramManager;
    mRebuildContext.plugin = plugin;
  }

  // Attachment helpers
  ig::IControl* attachGlobal(ig::IControl* ctrl);
  ig::IControl* attachDSP(ig::IControl* ctrl);
  ig::IControl* attachBrain(ig::IControl* ctrl);

  // Accessors
  ig::IGraphics* graphics() const { return mGraphics; }
  const UILayout& layout() const { return mLayout; }

  // Store parameter area bounds for rebuilding
  void setTransformerParamBounds(const ig::IRECT& bounds) { mTransformerParamBounds = bounds; }
  void setMorphParamBounds(const ig::IRECT& bounds) { mMorphParamBounds = bounds; }

  // Brain file list management
  void setBrainFileListControl(class BrainFileListControl* ctrl);
  void updateBrainFileList(const std::vector<struct BrainFileEntry>& files);

  // Window management
  void resizeWindowToFitContent();

  // Public card panel references (accessed by DSPTabView during layout)
  ig::IControl* mTransformerCardPanel { nullptr };
  ig::IControl* mMorphCardPanel { nullptr };
  ig::IControl* mAudioProcessingCardPanel { nullptr };

private:
  void buildHeader(const ig::IRECT& bounds);
  void buildDSP(const ig::IRECT& bounds, float startY);
  void buildBrain(const ig::IRECT& bounds, float startY);

  // Helper methods for dynamic layout
  void repositionCardsAfterTransformer(float heightDelta);
  void repositionCardsAfterMorph(float heightDelta);
  void repositionSubsequentCards(ig::IControl* startCard, float heightDelta);
  void anchorMorphLayoutToCard();

  // Common helpers for rebuild logic
  void RemoveAndClearControls(std::vector<ig::IControl*>& paramControls, std::vector<ig::IControl*>& dspControls);
  void AttachAndSyncControls(std::vector<ig::IControl*>& newControls, std::vector<ig::IControl*>& paramControls,
                             std::vector<ig::IControl*>& dspControls, iplug::Plugin* plugin);
  float ResizeCardToFitContent(ig::IControl* cardPanel, const ig::IRECT& paramBounds,
                               const std::vector<ig::IControl*>& controls, float minHeight);

  ig::IGraphics* mGraphics;
  UILayout mLayout;
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

  // Cached context for rebuilding dynamic params on resize
  RebuildContext mRebuildContext;
};

} // namespace ui
} // namespace synaptic


