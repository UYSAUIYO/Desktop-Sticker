#include "pch.h"
#include "desktopsticker/RenameDialog.h"

namespace desktopsticker {

namespace {

struct PromptState {
    std::wstring value;
    bool done = false;
    int result = 0;
};

LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        state = static_cast<PromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        CreateWindowExW(0, L"EDIT", state->value.c_str(),
                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                        10, 10, 210, 24, hwnd, reinterpret_cast<HMENU>(1001), cs->hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"确定",
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        30, 44, 80, 26, hwnd, reinterpret_cast<HMENU>(IDOK), cs->hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消",
                        WS_CHILD | WS_VISIBLE,
                        120, 44, 80, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), cs->hInstance, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        const int cmd = LOWORD(wp);
        if (cmd == IDOK || cmd == IDCANCEL) {
            if (cmd == IDOK) {
                wchar_t buf[256]{};
                GetDlgItemTextW(hwnd, 1001, buf, 256);
                state->value = buf;
                state->result = IDOK;
            } else {
                state->result = IDCANCEL;
            }
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        state->result = IDCANCEL;
        state->done = true;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool ShowRenameDialog(HWND owner, const std::wstring& title, std::wstring& value) {
    static bool registered = false;
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PromptWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"DesktopSticker.PromptWindow";
        RegisterClassExW(&wc);
        registered = true;
    }

    PromptState state{value, false, 0};
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"DesktopSticker.PromptWindow", title.c_str(),
                                WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 250, 110,
                                owner, nullptr, hInst, &state);
    if (!hwnd) return false;

    if (owner) EnableWindow(owner, FALSE);
    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    value = state.value;
    return state.result == IDOK;
}

} // namespace desktopsticker
