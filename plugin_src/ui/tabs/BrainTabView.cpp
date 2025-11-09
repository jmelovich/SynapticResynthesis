#include "BrainTabView.h"
#include "../styles/UIStyles.h"
#include "../controls/UIControls.h"
#include "../styles/UITheme.h"
#include "../layout/UILayout.h"
#include "../SynapticResynthesis.h"

using namespace iplug;
using namespace igraphics;
using namespace synaptic::ui;

namespace synaptic {
namespace ui {
namespace tabs {

void BuildBrainTab(SynapticUI& ui, const IRECT& bounds, const UILayout& layout, float startY)
{
  float yPos = startY;

  // SAMPLE LIBRARY CARD
  float libraryCardHeight = 340.f; // Increased to accommodate file list
  IRECT libraryCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + libraryCardHeight);
  ui.attachBrain(new CardPanel(libraryCard, "SAMPLE LIBRARY"));

  // File drop zone
  IRECT dropArea = libraryCard.GetPadded(-layout.cardPadding).GetFromTop(100.f).GetTranslated(0.f, 28.f);
  ui.attachBrain(new BrainFileDropControl(dropArea));

  // Status line
  IRECT statusRect = IRECT(libraryCard.L + layout.cardPadding, dropArea.B + 8.f, libraryCard.R - layout.cardPadding, dropArea.B + 24.f);
  IText statusText = IText(12.f, kTextSecond, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
  ui.attachBrain(new ITextControl(statusRect, "Files: 0 | Storage: (inline)", statusText));

  // File list
  IRECT fileListRect = IRECT(
    libraryCard.L + layout.cardPadding,
    statusRect.B + 8.f,
    libraryCard.R - layout.cardPadding,
    libraryCard.B - layout.cardPadding
  );
  auto* fileList = new BrainFileListControl(fileListRect);
  ui.attachBrain(fileList);
  ui.setBrainFileListControl(fileList); // Store reference for updates

  yPos = libraryCard.B + layout.sectionGap;

  // MANAGEMENT CARD
  float managementCardHeight = 160.f;
  IRECT managementCard = IRECT(layout.padding, yPos, bounds.W() - layout.padding, yPos + managementCardHeight);
  ui.attachBrain(new CardPanel(managementCard, "BRAIN MANAGEMENT"));

  float btnWidth = 200.f;
  float btnHeight = 45.f;
  float btnGapH = 20.f;
  float btnGapV = 16.f;
  float btnGridWidth = (btnWidth * 2) + btnGapH;
  float btnStartX = managementCard.L + (managementCard.W() - btnGridWidth) / 2.f;
  float btnY = managementCard.T + layout.cardPadding + 32.f;

  IRECT importBtnRect = IRECT(btnStartX, btnY, btnStartX + btnWidth, btnY + btnHeight);
  ui.attachBrain(new IVButtonControl(importBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainImport, kNoTag, 0, nullptr);
    }
  }, "Import Brain", kButtonStyle));

  IRECT exportBtnRect = IRECT(btnStartX + btnWidth + btnGapH, btnY, btnStartX + btnWidth + btnGapH + btnWidth, btnY + btnHeight);
  ui.attachBrain(new IVButtonControl(exportBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainExport, kNoTag, 0, nullptr);
    }
  }, "Export Brain", kButtonStyle));

  btnY += btnHeight + btnGapV;

  IRECT resetBtnRect = IRECT(btnStartX, btnY, btnStartX + btnWidth, btnY + btnHeight);
  ui.attachBrain(new IVButtonControl(resetBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainReset, kNoTag, 0, nullptr);
    }
  }, "Reset Brain", kButtonStyle));

  IRECT detachBtnRect = IRECT(btnStartX + btnWidth + btnGapH, btnY, btnStartX + btnWidth + btnGapH + btnWidth, btnY + btnHeight);
  ui.attachBrain(new IVButtonControl(detachBtnRect, [](IControl* pCaller) {
    auto* pGraphics = pCaller->GetUI();
    auto* pDelegate = dynamic_cast<iplug::IEditorDelegate*>(pGraphics->GetDelegate());
    if (pDelegate) {
      pDelegate->SendArbitraryMsgFromUI(kMsgTagBrainDetach, kNoTag, 0, nullptr);
    }
  }, "Detach File Ref", kButtonStyle));
}

} // namespace tabs
} // namespace ui
} // namespace synaptic


