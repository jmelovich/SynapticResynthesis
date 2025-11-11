/**
 * @file DeferredNumberBoxControl.h
 * @brief A number box control that defers parameter updates until mouse release
 *
 * This control is specifically designed for parameters that trigger expensive
 * operations (like rechunking). It provides more control to the user over
 * when to apply the changes to the param value.
 */

#pragma once

#include "IControl.h"
#include <chrono>

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

/** A number box control that defers parameter updates until mouse release
 * Extends IVNumberBoxControl behavior to prevent triggering expensive operations during drag
 * @ingroup IControls */
class DeferredNumberBoxControl : public IContainerBase
                                , public IVectorBase
{
public:
  DeferredNumberBoxControl(const IRECT& bounds, int paramIdx = kNoParameter, IActionFunction actionFunc = nullptr, const char* label = "", const IVStyle& style = DEFAULT_STYLE, bool buttons = false, double defaultValue = 50.f, double minValue = 1.f, double maxValue = 100.f, const char* fmtStr = "%0.0f", bool drawTriangle = true)
  : IContainerBase(bounds, paramIdx, actionFunc)
  , IVectorBase(style.WithDrawShadows(false)
                .WithDrawFrame(true)
                .WithValueText(style.valueText.WithVAlign(EVAlign::Middle))
                .WithLabelText(style.labelText.WithVAlign(EVAlign::Middle)))
  , mFmtStr(fmtStr)
  , mButtons(buttons)
  , mMinValue(minValue)
  , mMaxValue(maxValue)
  , mRealValue(defaultValue)
  , mDrawTriangle(drawTriangle)
  , mDirtyColor(COLOR_ORANGE) // Red/orange color for pending changes
  , mDeferredDelayMs(1000.0) // 1 second delay for deferred application (wheel and drag)
  {
    assert(defaultValue >= minValue && defaultValue <= maxValue);

    AttachIControl(this, label);
  }

  void OnInit() override
  {
    if (GetParam())
    {
      mMinValue = GetParam()->GetMin();
      mMaxValue = GetParam()->GetMax();
      // Get current value, not default, to avoid triggering updates on init
      mRealValue = GetParam()->Value();
    }
  }

  void Draw(IGraphics& g) override
  {
    DrawLabel(g);

    // Check if enough time has elapsed since last deferred event (wheel or drag release)
    CheckDeferredTimer();

    // Keep control dirty while deferred timer is active to ensure continuous checking
    if (mDeferredTimerActive)
    {
      SetDirty(false); // Queue another draw on next frame
    }

    // Use dirty color when there are pending unapplied changes
    if (mDeferredTimerActive)
    {
      // Draw with red/orange highlight to indicate pending changes
      g.FillRect(mDirtyColor, mTextReadout->GetRECT());

      // Draw undo button on the right side
      DrawUndoButton(g);
    }
    else
    {
      if (mMouseIsOver)
        g.FillRect(GetColor(kHL), mTextReadout->GetRECT());

      if (mMouseIsDown)
        g.FillRect(GetColor(kFG), mTextReadout->GetRECT());
    }

    if (mDrawTriangle)
    {
      auto triangleRect = mTextReadout->GetRECT().GetPadded(-2.0f);

      g.FillTriangle(GetColor(mMouseIsOver ? kX1 : kSH), triangleRect.L, triangleRect.T, triangleRect.L + triangleRect.H(), triangleRect.MH(), triangleRect.L, triangleRect.B);
    }
  }

  void DrawUndoButton(IGraphics& g)
  {
    IRECT undoRect = GetUndoButtonRect();

    // Draw button background
    IColor btnColor = mUndoButtonHover ? COLOR_WHITE : IColor(220, 220, 220);
    g.FillRoundRect(btnColor, undoRect, 2.0f);

    // Draw border
    g.DrawRoundRect(COLOR_BLACK, undoRect, 2.0f, nullptr, 1.0f);

    // Draw "X" or undo symbol
    IColor symbolColor = COLOR_BLACK;
    float centerX = undoRect.MW();
    float centerY = undoRect.MH();
    float size = 4.0f;

    // Draw X
    g.DrawLine(symbolColor, centerX - size, centerY - size, centerX + size, centerY + size, nullptr, 2.0f);
    g.DrawLine(symbolColor, centerX + size, centerY - size, centerX - size, centerY + size, nullptr, 2.0f);
  }

  IRECT GetUndoButtonRect() const
  {
    if (!mTextReadout) return IRECT();

    IRECT textRect = mTextReadout->GetRECT();
    float btnSize = textRect.H() - 4.0f;
    float btnLeft = textRect.R - btnSize - 4.0f;
    float btnTop = textRect.T + 2.0f;

    return IRECT(btnLeft, btnTop, btnLeft + btnSize, btnTop + btnSize);
  }

  void OnResize() override
  {
    MakeRects(mRECT, false);

    IRECT sections = mWidgetBounds;
    sections.Pad(-1.f);

    if (mTextReadout)
    {
      mTextReadout->SetTargetAndDrawRECTs(sections.ReduceFromLeft(sections.W() * (mButtons ? 0.75f : 1.f)));

      if (mButtons)
      {
        mIncButton->SetTargetAndDrawRECTs(sections.FracRectVertical(0.5f, true).GetPadded(-2.f, 0.f, 0.f, -1.f));
        mDecButton->SetTargetAndDrawRECTs(sections.FracRectVertical(0.5f, false).GetPadded(-2.f, -1.f, 0.f, 0.f));
      }

      SetTargetRECT(mTextReadout->GetRECT());
    }
  }

  void OnAttached() override
  {
    IRECT sections = mWidgetBounds;
    sections.Pad(-1.f);

    AddChildControl(mTextReadout = new IVLabelControl(sections.ReduceFromLeft(sections.W() * (mButtons ? 0.75f : 1.f)), "0", mStyle.WithDrawFrame(true)));

    // Sync with current parameter value without triggering change
    if (GetParam())
    {
      mRealValue = GetParam()->Value();
    }
    mTextReadout->SetStrFmt(32, mFmtStr.Get(), mRealValue);

    if (mButtons)
    {
      AddChildControl(mIncButton = new IVButtonControl(sections.FracRectVertical(0.5f, true).GetPadded(-2.f, 0.f, 0.f, -1.f), SplashClickActionFunc, "+", mStyle))->SetAnimationEndActionFunction(mIncrementFunc);
      AddChildControl(mDecButton = new IVButtonControl(sections.FracRectVertical(0.5f, false).GetPadded(-2.f, -1.f, 0.f, 0.f), SplashClickActionFunc, "-", mStyle))->SetAnimationEndActionFunction(mDecrementFunc);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod &mod) override
  {
    // Check if clicking on undo button
    if (mDeferredTimerActive && GetUndoButtonRect().Contains(x, y))
    {
      UndoPendingChange();
      return; // Don't start a new drag
    }

    if (mHideCursorOnDrag)
      GetUI()->HideMouseCursor(true, true);

    // If we have pending changes, DON'T apply yet - we might be starting a new drag
    // We'll decide whether to apply in OnMouseUp based on whether it was a drag or click

    if (GetParam())
    {
      // Only begin parameter change if not already in a deferred state
      if (!mDeferredTimerActive)
      {
        GetDelegate()->BeginInformHostOfParamChangeFromUI(GetParamIdx());
        // Store the original value for potential undo (only first time)
        mOriginalValueBeforeDefer = mRealValue;
      }
      // If already pending, we'll continue with the same begin/end pair
    }

    mMouseIsDown = true;
    mIsDragging = false;
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);

    // Check if hovering over undo button
    bool wasHovering = mUndoButtonHover;
    mUndoButtonHover = mDeferredTimerActive && GetUndoButtonRect().Contains(x, y);

    if (wasHovering != mUndoButtonHover)
    {
      SetDirty(false); // Redraw to update hover state
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();

    if (mUndoButtonHover)
    {
      mUndoButtonHover = false;
      SetDirty(false);
    }
  }

  void OnMouseUp(float x, float y, const IMouseMod &mod) override
  {
    if (mHideCursorOnDrag)
      GetUI()->HideMouseCursor(false);

    if (GetParam())
    {
      if (mIsDragging)
      {
        // Was dragging - keep the deferred state
        // Check if value actually changed (compare to original)
        if (mRealValue != mOriginalValueBeforeDefer)
        {
          // Keep timer active (already set in OnMouseDrag)
          // DON'T call EndInformHostOfParamChangeFromUI yet - we'll call it when timer expires
        }
        else
        {
          // Value ended up back at original, cancel everything
          mDeferredTimerActive = false;
          GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx());
        }
      }
      else
      {
        // Just a click (no drag) - apply immediately if pending
        if (mDeferredTimerActive)
        {
          ApplyValue();
        }
        else
        {
          // Just a click with no pending changes - clean up
          GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx());
        }
      }
    }

    mMouseIsDown = false;
    mIsDragging = false;

    SetDirty(true);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod &mod) override
  {
    mIsDragging = true;
    mDeferredTimerActive = true; // Mark as having pending changes and timer active

    // Reset the timer on each drag event (extends the wait period)
    mLastDeferredTime = std::chrono::high_resolution_clock::now();

    double gearing = IsFineControl(mod, true) ? mSmallIncrement : mLargeIncrement;
    mRealValue -= (double(dY) * gearing);
    OnValueChanged(true); // Pass true to prevent parameter update during drag
  }

  void OnMouseDblClick(float x, float y, const IMouseMod &mod) override
  {
    // Apply any pending changes before opening text entry
    if (mDeferredTimerActive)
    {
      ApplyValue();
    }

    if (!IsDisabled() && mTextReadout->GetRECT().Contains(x, y))
      GetUI()->CreateTextEntry(*this, mText, mTextReadout->GetRECT(), mTextReadout->GetStr());
  }

  void OnTextEntryCompletion(const char* str, int valIdx) override
  {
    CancelDeferredTimer(); // Cancel any pending deferred timer
    mRealValue = atof(str);

    // For text entry, apply immediately with proper Begin/End sequence
    if (GetParam() && GetDelegate())
    {
      GetDelegate()->BeginInformHostOfParamChangeFromUI(GetParamIdx());

      double normalizedValue = GetParam()->ToNormalized(mRealValue);
      SetValue(normalizedValue);
      GetDelegate()->SendParameterValueFromUI(GetParamIdx(), normalizedValue);

      GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx());
    }
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    // For mouse wheel, defer with a timestamp-based delay
    double gearing = IsFineControl(mod, true) ? mSmallIncrement : mLargeIncrement;
    double inc = (d > 0.f ? 1. : -1.) * gearing;
    mRealValue += inc;

    // If this is the first event in a sequence, begin parameter change and store original value
    if (!mDeferredTimerActive && GetParam())
    {
      GetDelegate()->BeginInformHostOfParamChangeFromUI(GetParamIdx());
      mOriginalValueBeforeDefer = mRealValue - inc; // Store value before this wheel event
    }

    // Mark timer as active (indicates pending changes)
    mDeferredTimerActive = true;

    // Record/reset the time of this event
    mLastDeferredTime = std::chrono::high_resolution_clock::now();

    // Update visual display without applying parameter
    OnValueChanged(true);
  }

  void SetValueFromDelegate(double value, int valIdx = 0) override
  {
    // Cancel any pending deferred timer when value is set externally
    CancelDeferredTimer();

    if (GetParam())
    {
      mRealValue = GetParam()->FromNormalized(value);
      mTextReadout->SetStrFmt(32, mFmtStr.Get(), mRealValue);
    }

    // Call base class to update internal value, but this won't trigger OnParamChange
    IControl::SetValueFromDelegate(value, valIdx);
    SetDirty(false); // Redraw without triggering action
  }

  void SetValueFromUserInput(double value, int valIdx = 0) override
  {
    // Cancel any pending deferred timer when value is set externally
    CancelDeferredTimer();

    if (GetParam())
    {
      mRealValue = GetParam()->FromNormalized(value);
      mTextReadout->SetStrFmt(32, mFmtStr.Get(), mRealValue);
    }

    // Call base class to update internal value, but this won't trigger OnParamChange
    IControl::SetValueFromUserInput(value, valIdx);
    SetDirty(false); // Redraw without triggering action
  }

  void SetStyle(const IVStyle& style) override
  {
    IVectorBase::SetStyle(style);
    mTextReadout->SetStyle(style);

    if (mButtons)
    {
      mIncButton->SetStyle(style);
      mDecButton->SetStyle(style);
    }
  }

  bool IsFineControl(const IMouseMod& mod, bool wheel) const
  {
    #ifdef PROTOOLS
    #ifdef OS_WIN
      return mod.C;
    #else
      return wheel ? mod.C : mod.R;
    #endif
    #else
      return (mod.C || mod.S);
    #endif
  }

  void OnValueChanged(bool preventAction = false)
  {
    mRealValue = Clip(mRealValue, mMinValue, mMaxValue);

    mTextReadout->SetStrFmt(32, mFmtStr.Get(), mRealValue);

    // Only update parameter if not preventing action AND no timer active
    // This prevents SetValue from being called while timer is active
    if (!preventAction && !mDeferredTimerActive && GetParam())
    {
      SetValue(GetParam()->ToNormalized(mRealValue));
    }

    // Use SetDirty(false) when preventing action to avoid any potential parameter notifications
    SetDirty(!preventAction && !mDeferredTimerActive);
  }

  void ApplyValue()
  {
    if (GetParam() && mDeferredTimerActive)
    {
      // Clear timer BEFORE calling SetValue to prevent any recursion
      mDeferredTimerActive = false;

      // Apply the value change to the parameter
      double normalizedValue = GetParam()->ToNormalized(mRealValue);
      SetValue(normalizedValue);

      // Force the parameter update to be sent to the plugin
      if (GetDelegate())
      {
        GetDelegate()->SendParameterValueFromUI(GetParamIdx(), normalizedValue);
        GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx());
      }

      SetDirty(true); // Force redraw to show normal colors
    }
  }

  void CheckDeferredTimer()
  {
    if (mDeferredTimerActive)
    {
      auto now = std::chrono::high_resolution_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastDeferredTime).count();

      if (elapsed >= mDeferredDelayMs)
      {
        // Time elapsed, apply the value
        ApplyValue();
      }
    }
  }

  void CancelDeferredTimer()
  {
    // If we're cancelling an active deferred timer, end the parameter change notification
    if (mDeferredTimerActive && GetParam() && GetDelegate())
    {
      GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx());
    }

    mDeferredTimerActive = false;
    // Note: Don't clear mHasPendingChanges here - let the calling code decide
  }

  void UndoPendingChange()
  {
    if (mDeferredTimerActive && GetParam())
    {
      // Cancel the deferred timer (clears mDeferredTimerActive)
      CancelDeferredTimer();

      // Revert to original value
      mRealValue = mOriginalValueBeforeDefer;

      // Update visual display
      if (mTextReadout)
      {
        mTextReadout->SetStrFmt(32, mFmtStr.Get(), mRealValue);
      }

      SetDirty(true); // Redraw to show normal colors
    }
  }

  double GetRealValue() const { return mRealValue; }

  void SetDrawTriangle(bool draw) { mDrawTriangle = draw; SetDirty(false); }

protected:

  void ApplyButtonIncrement(double increment)
  {
    CancelDeferredTimer();
    mRealValue += increment;

    // Apply immediately with proper Begin/End sequence
    if (GetParam() && GetDelegate())
    {
      GetDelegate()->BeginInformHostOfParamChangeFromUI(GetParamIdx());

      mRealValue = Clip(mRealValue, mMinValue, mMaxValue);
      double normalizedValue = GetParam()->ToNormalized(mRealValue);
      SetValue(normalizedValue);
      GetDelegate()->SendParameterValueFromUI(GetParamIdx(), normalizedValue);

      GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx());

      mTextReadout->SetStrFmt(32, mFmtStr.Get(), mRealValue);
    }
  }

  IActionFunction mIncrementFunc = [this](IControl* pCaller) {
    ApplyButtonIncrement(mLargeIncrement);
  };

  IActionFunction mDecrementFunc = [this](IControl* pCaller) {
    ApplyButtonIncrement(-mLargeIncrement);
  };
  IVLabelControl* mTextReadout = nullptr;
  IVButtonControl* mIncButton = nullptr;
  IVButtonControl* mDecButton = nullptr;
  WDL_String mFmtStr;
  double mLargeIncrement = 1.f;
  double mSmallIncrement = 0.1f;
  double mMinValue;
  double mMaxValue;
  double mRealValue = 0.f;
  bool mHideCursorOnDrag = true;
  bool mButtons = false;
  bool mDrawTriangle = true;
  bool mMouseIsDown = false;
  bool mIsDragging = false; // Track if we're currently dragging
  bool mDeferredTimerActive = false; // Track if we have pending changes with active deferred timer
  bool mUndoButtonHover = false; // Track if mouse is hovering over undo button
  IColor mDirtyColor; // Color to show when there are pending changes
  double mDeferredDelayMs; // Delay in milliseconds for deferred application
  double mOriginalValueBeforeDefer = 0.0; // Original value before deferred changes (for both undo and change detection)
  std::chrono::time_point<std::chrono::high_resolution_clock> mLastDeferredTime; // Timestamp of last deferred event
};

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE

