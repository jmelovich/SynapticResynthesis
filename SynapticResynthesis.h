/**
 * @file SynapticResynthesis.h
 * @brief Main plugin class for the Synaptic Resynthesis audio effect
 */

#pragma once

#include "plugin_src/ui/core/ProgressOverlayManager.h"
#include "plugin_src/modules/UISyncManager.h"
#include "plugin_src/audio/DSPContext.h"
#include "plugin_src/Structs.h"
#include "plugin_src/modules/AudioStreamChunker.h"
#include "plugin_src/brain/Brain.h"
#include "plugin_src/audio/Window.h"
#include "plugin_src/modules/DSPConfig.h"
#include "plugin_src/modules/WindowCoordinator.h"
#include "plugin_src/params/ParameterManager.h"
#include "plugin_src/brain/BrainManager.h"
#include "plugin_src/serialization/StateSerializer.h"
#include "plugin_src/ui_bridge/MessageTags.h"

#include "IPlug_include_in_plug_hdr.h"

#if IPLUG_EDITOR
  #include "IGraphics_include_in_plug_hdr.h"
  #include "IControls.h"
#endif

using namespace iplug;

const int kNumPresets = 3;

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
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  // === Core State ===
  synaptic::Brain mBrain;
  synaptic::Window mAnalysisWindow;

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
  std::unique_ptr<synaptic::ui::SynapticUI> mUI;
#endif

  // === Progress overlay management ===
  mutable synaptic::ui::ProgressOverlayManager mProgressOverlayMgr;
};
