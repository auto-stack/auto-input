// AutoIME - language bar item (PLAN-078 acceptance follow-up).
//
// The taskbar input indicator shows a TSF text service's own icon only when the
// service registers an ITfLangBarItemButton; without it Windows falls back to
// the language label ("简体"). This item shows the shipped orange "A" icon and
// toggles Chinese/English on click. It is created per thread manager in
// CTextService::Activate and removed in Deactivate.
#ifndef AUTOIME_LANG_BAR_ITEM_H_
#define AUTOIME_LANG_BAR_ITEM_H_

#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>

class CTextService;

class CLangBarItem final : public ITfLangBarItemButton {
 public:
  CLangBarItem(CTextService* service, HICON icon);
  // Retry hook for the language-bar registration experiment.
  void set_style(DWORD style) { style_ = style; }

  // IUnknown
  STDMETHODIMP STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) STDMETHODCALLTYPE AddRef() override;
  STDMETHODIMP_(ULONG) STDMETHODCALLTYPE Release() override;

  // ITfLangBarItem
  STDMETHODIMP STDMETHODCALLTYPE GetInfo(TF_LANGBARITEMINFO* info) override;
  STDMETHODIMP STDMETHODCALLTYPE GetStatus(DWORD* status) override;
  STDMETHODIMP STDMETHODCALLTYPE Show(BOOL show) override;
  STDMETHODIMP STDMETHODCALLTYPE GetTooltipString(BSTR* text) override;

  // ITfLangBarItemButton
  STDMETHODIMP STDMETHODCALLTYPE OnClick(TfLBIClick click, POINT pt,
                                          const RECT* area) override;
  STDMETHODIMP STDMETHODCALLTYPE InitMenu(ITfMenu* menu) override;
  STDMETHODIMP STDMETHODCALLTYPE OnMenuSelect(UINT id) override;
  STDMETHODIMP STDMETHODCALLTYPE GetIcon(HICON* icon) override;
  STDMETHODIMP STDMETHODCALLTYPE GetText(BSTR* text) override;

 private:
  ~CLangBarItem() = default;

  LONG refs_ = 1;
  CTextService* service_ = nullptr;  // borrowed; the service outlives the item
  HICON icon_ = nullptr;
  DWORD style_ = 0x00010000;  // TF_LBI_STYLE_BTN_BUTTON
  GUID guid_ = {};
};

#endif  // AUTOIME_LANG_BAR_ITEM_H_
