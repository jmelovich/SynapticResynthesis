#pragma once

#include "plugin_src/ui/core/ProgressOverlayManager.h"
#include "plugin_src/modules/UISyncManager.h"
#include "plugin_src/audio/DSPContext.h"
#include "plugin_src/Structs.h" // For EParams and AudioChunk
#include "plugin_src/modules/AudioStreamChunker.h" // Needed for mDSPContext chunker ref?
#include "plugin_src/brain/Brain.h"
#include "plugin_src/audio/Window.h"
#include "plugin_src/modules/DSPConfig.h"
#include "plugin_src/modules/WindowCoordinator.h"
#include "plugin_src/params/ParameterManager.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/serialization/StateSerializer.h"

#include "IPlug_include_in_plug_hdr.h"

#if IPLUG_EDITOR
  #include "IGraphics_include_in_plug_hdr.h"
  #include "IControls.h"
#endif

using namespace iplug;

const int kNumPresets = 3;

class SynapticResynthesis; // Forward declaration

// Message tags are now defined in plugin_src/ui_bridge/MessageTags.h
#include "plugin_src/ui_bridge/MessageTags.h"

class SynapticResynthesis final : public Plugin
{
public:
  SynapticResynthesis(const InstanceInfo& info);

  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  void OnUIOpen() override;
  void OnUIClose() override;
  void OnIdle() override;
  void OnRestoreState() override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  void OnParamChange(int paramIdx) override;
  // state serialization
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  // === Helper Methods ===
  // None currently needed in public header, delegates are in managers

  // === Core State ===
  synaptic::Brain mBrain;
  synaptic::Window mAnalysisWindow;  // For brain analysis

  // === Modules ===
  synaptic::DSPConfig mDSPConfig;
  synaptic::ParameterManager mParamManager;
  synaptic::BrainManager mBrainManager;
  synaptic::WindowCoordinator mWindowCoordinator;
  synaptic::StateSerializer mStateSerializer;

  // === Managers ===
  synaptic::DSPContext mDSPContext;
  synaptic::UISyncManager mUISyncManager;

  // === UI State ===
#if IPLUG_EDITOR
  // Instance-owned UI (each plugin instance has its own UI)
  std::unique_ptr<synaptic::ui::SynapticUI> mUI;
#endif

  // === Progress overlay management ===
  mutable synaptic::ui::ProgressOverlayManager mProgressOverlayMgr;
};
