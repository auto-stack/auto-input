// AutoIME - language bar item implementation (PLAN-078 acceptance follow-up).
#include "lang_bar_item.h"

#include <atomic>
#include <new>

#include "diag.h"
#include "ime.h"

namespace {
// Stable per-service item identity (AutoIME language bar item).
const GUID kLangBarItemGuid = {
    0x6f1d2c84, 0x9a3b, 0x4c57, {0x8e, 0x21, 0x3f, 0x5a, 0x7c, 0x11, 0x9d, 0x42}};
}  // namespace

CLangBarItem::CLangBarItem(CTextService* service, HICON icon)
    : service_(service), icon_(icon) {
  guid_ = kLangBarItemGuid;
}

STDMETHODIMP CLangBarItem::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_INVALIDARG;
  *ppv = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_ITfLangBarItem)) {
    *ppv = static_cast<ITfLangBarItem*>(this);
  } else if (IsEqualIID(riid, IID_ITfLangBarItemButton)) {
    *ppv = static_cast<ITfLangBarItemButton*>(this);
  } else {
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) CLangBarItem::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&refs_));
}

STDMETHODIMP_(ULONG) CLangBarItem::Release() {
  const LONG refs = InterlockedDecrement(&refs_);
  if (refs == 0) {
    if (icon_) DestroyIcon(icon_);
    delete this;
  }
  return static_cast<ULONG>(refs);
}

STDMETHODIMP CLangBarItem::GetInfo(TF_LANGBARITEMINFO* info) {
  if (!info) return E_INVALIDARG;
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) ImeLog("langbar GetInfo called");
  *info = TF_LANGBARITEMINFO{};
  info->clsidService = CLSID_AutoIMETextService;
  info->guidItem = guid_;
  info->dwStyle = style_;
  info->ulSort = 0;
  wcscpy_s(info->szDescription, L"AutoInput");
  return S_OK;
}

STDMETHODIMP CLangBarItem::GetStatus(DWORD* status) {
  if (!status) return E_INVALIDARG;
  *status = 0;
  if (service_ && service_->LangBarAsciiMode()) *status |= TF_LBI_STATUS_BTN_TOGGLED;
  return S_OK;
}

STDMETHODIMP CLangBarItem::Show(BOOL show) {
  (void)show;
  return S_OK;
}

STDMETHODIMP CLangBarItem::GetTooltipString(BSTR* text) {
  if (!text) return E_INVALIDARG;
  *text = SysAllocString(service_ && service_->LangBarAsciiMode() ? L"AutoInput - English"
                                                           : L"AutoInput - 中文");
  return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CLangBarItem::OnClick(TfLBIClick click, POINT pt,
                                   const RECT* area) {
  (void)pt;
  (void)area;
  diag::Event("langbar", "click");
  if (service_ && click == TF_LBI_CLK_LEFT) service_->OnLangBarToggleMode();
  return S_OK;
}

STDMETHODIMP CLangBarItem::InitMenu(ITfMenu* menu) {
  (void)menu;
  return S_OK;
}

STDMETHODIMP CLangBarItem::OnMenuSelect(UINT id) {
  (void)id;
  return S_OK;
}

STDMETHODIMP CLangBarItem::GetIcon(HICON* icon) {
  if (!icon) return E_INVALIDARG;
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) ImeLog("langbar GetIcon called");
  // The caller owns (and destroys) the returned icon.
  *icon = icon_ ? CopyIcon(icon_) : nullptr;
  return S_OK;
}

STDMETHODIMP CLangBarItem::GetText(BSTR* text) {
  if (!text) return E_INVALIDARG;
  *text = SysAllocString(L"AutoInput");
  return *text ? S_OK : E_OUTOFMEMORY;
}
