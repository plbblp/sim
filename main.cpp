// main.cpp

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>       // For DirectInput
#include <windows.h>      // For basic Windows API, Sleep, GetAsyncKeyState, HWND, etc.
#include <iostream>       // For std::cout, std::cerr
#include <string>         // For std::string
#include <vector>         // Not strictly used in this version, but good for general C++
#include <algorithm>      // For std::max and std::min

// ViGEm Client SDK Header
// Make sure your CMakeLists.txt correctly sets up the include path for this
#include <ViGEm/Client.h>

// 用于存储遥控器通道值的结构体
struct RemoteChannels {
    long ch1;  // X-axis
    long ch2;  // Y-axis
    long ch3;  // Z-axis
    long ch4;  // X Rotation
    bool ch5;  // Button 1
    long ch6;  // Z Rotation
    long ch7;  // Slider 1
    long ch8;  // Slider 2
    long ch9;  // Y Rotation
    bool ch10; // Button 2

    RemoteChannels() :
        ch1(0), ch2(0), ch3(0), ch4(0),
        ch5(false),
        ch6(0), ch7(0), ch8(0), ch9(0),
        ch10(false) {}
};

// --- Global Variables ---

// DirectInput Globals
LPDIRECTINPUT8        g_pDI = nullptr;
LPDIRECTINPUTDEVICE8  g_pJoystick = nullptr;
RemoteChannels        g_physicalControllerState; // Stores the state of the physical controller

// ViGEm Globals
PVIGEM_CLIENT         g_pVigemClient = nullptr;     // ViGEm client object
PVIGEM_TARGET         g_pTargetX360 = nullptr; // ViGEm Xbox 360 target object
XUSB_REPORT           g_virtualControllerReport;   // Report structure for X360 controller

// --- DirectInput Functions ---

// DirectInput设备枚举回调函数
BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    HRESULT hr = g_pDI->CreateDevice(pdidInstance->guidInstance, &g_pJoystick, nullptr);
    if (SUCCEEDED(hr)) {
        return DIENUM_STOP; // Found one, stop enumerating
    }
    return DIENUM_CONTINUE; // Keep looking
}

// 枚举设备对象的函数 (用于设置轴的属性，如范围)
BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext) {
    if (pdidoi->dwType & DIDFT_AXIS) {
        DIPROPRANGE diprg;
        diprg.diph.dwSize = sizeof(DIPROPRANGE);
        diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        diprg.diph.dwHow = DIPH_BYID;
        diprg.diph.dwObj = pdidoi->dwType;
        diprg.lMin = -1000; // Standardized range for our use
        diprg.lMax = +1000;

        if (FAILED(g_pJoystick->SetProperty(DIPROP_RANGE, &diprg.diph))) {
            // std::cerr << "Warning: Failed to set property DIPROP_RANGE for axis: " << pdidoi->tszName << std::endl;
        }
    }
    return DIENUM_CONTINUE;
}

bool InitializeDirectInput(HWND hWnd) {
    HRESULT hr;

    hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Error: DirectInput8Create failed! HR: " << hr << std::endl;
        return false;
    }

    hr = g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, nullptr, DIEDFL_ATTACHEDONLY);
    if (FAILED(hr) || g_pJoystick == nullptr) {
        std::cerr << "Error: No joystick/gamepad found or EnumDevices failed! HR: " << hr << std::endl;
        if (g_pDI) { g_pDI->Release(); g_pDI = nullptr; }
        return false;
    }

    hr = g_pJoystick->SetDataFormat(&c_dfDIJoystick2); // DIJOYSTATE2 provides more axes/buttons
    if (FAILED(hr)) {
        std::cerr << "Error: SetDataFormat failed! HR: " << hr << std::endl;
        g_pJoystick->Release(); g_pJoystick = nullptr;
        g_pDI->Release(); g_pDI = nullptr;
        return false;
    }

    hr = g_pJoystick->SetCooperativeLevel(hWnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        std::cerr << "Error: SetCooperativeLevel failed! HR: " << hr << std::endl;
        g_pJoystick->Release(); g_pJoystick = nullptr;
        g_pDI->Release(); g_pDI = nullptr;
        return false;
    }

    // Enumerate and set axis ranges
    g_pJoystick->EnumObjects(EnumAxesCallback, (VOID*)hWnd, DIDFT_AXIS);

    // Acquire the joystick
    hr = g_pJoystick->Acquire();
    // It's okay if Acquire fails here, Poll() will attempt to re-acquire.
    // However, if it's a more serious error, it might indicate a problem.
    if (FAILED(hr) && hr != DIERR_INPUTLOST && hr != DIERR_OTHERAPPHASPRIO && hr != DIERR_NOTACQUIRED) {
         std::cerr << "Warning: Acquire failed initially with error: " << hr << std::endl;
    }

    std::cout << "DirectInput Joystick initialized." << std::endl;
    DIDEVCAPS caps;
    caps.dwSize = sizeof(DIDEVCAPS);
    if (SUCCEEDED(g_pJoystick->GetCapabilities(&caps))) {
        std::cout << "Physical Device: Axes=" << caps.dwAxes << ", Buttons="
                  << caps.dwButtons << ", POVs=" << caps.dwPOVs << std::endl;
    }
    return true;
}

void CleanupDirectInput() {
    if (g_pJoystick) {
        g_pJoystick->Unacquire();
        g_pJoystick->Release();
        g_pJoystick = nullptr;
    }
    if (g_pDI) {
        g_pDI->Release();
        g_pDI = nullptr;
    }
    std::cout << "DirectInput cleaned up." << std::endl;
}

// --- ViGEm Functions ---
bool InitializeVirtualGamepad() {
    g_pVigemClient = vigem_alloc();
    if (g_pVigemClient == nullptr) {
        std::cerr << "Error: Failed to allocate ViGEm client!" << std::endl;
        return false;
    }

    VIGEM_ERROR err = vigem_connect(g_pVigemClient);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "Error: ViGEm Bus connection failed! Error code: " << err << std::endl;
        vigem_free(g_pVigemClient);
        g_pVigemClient = nullptr;
        return false;
    }

    g_pTargetX360 = vigem_target_x360_alloc();
    if (g_pTargetX360 == nullptr) {
        std::cerr << "Error: Failed to allocate Xbox 360 target!" << std::endl;
        vigem_disconnect(g_pVigemClient);
        vigem_free(g_pVigemClient);
        g_pVigemClient = nullptr;
        return false;
    }

    err = vigem_target_add(g_pVigemClient, g_pTargetX360);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "Error: Failed to add Xbox 360 target to ViGEm Bus! Error code: " << err << std::endl;
        vigem_target_free(g_pTargetX360); g_pTargetX360 = nullptr;
        vigem_disconnect(g_pVigemClient);
        vigem_free(g_pVigemClient); g_pVigemClient = nullptr;
        return false;
    }

    XUSB_REPORT_INIT(&g_virtualControllerReport); // Initialize the report structure
    std::cout << "Virtual Xbox 360 controller initialized and added to ViGEm Bus." << std::endl;
    std::cout << "Check Device Manager or Game Controllers (joy.cpl) for 'Xbox 360 Controller for Windows'." << std::endl;
    return true;
}

void CleanupVirtualGamepad() {
    if (g_pVigemClient && g_pTargetX360) {
        vigem_target_remove(g_pVigemClient, g_pTargetX360);
        vigem_target_free(g_pTargetX360);
        g_pTargetX360 = nullptr;
    }
    if (g_pVigemClient) {
        vigem_disconnect(g_pVigemClient);
        vigem_free(g_pVigemClient);
        g_pVigemClient = nullptr;
    }
    std::cout << "Virtual gamepad cleaned up." << std::endl;
}

// --- Main Logic: Polling and Mapping ---
// --- Main Logic: Polling and Mapping ---
void PollAndMapController() {
    if (g_pJoystick == nullptr) return;

    HRESULT hr = g_pJoystick->Poll();
    if (FAILED(hr)) {
        hr = g_pJoystick->Acquire(); // Try to re-acquire
        while (hr == DIERR_INPUTLOST) { // Loop if input is lost
            hr = g_pJoystick->Acquire();
        }
        if (FAILED(hr)) { // If re-acquire still fails, return
            return;
        }
    }

    DIJOYSTATE2 physicalStateRaw; // Raw state from DirectInput
    hr = g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &physicalStateRaw);
    if (FAILED(hr)) {
        return;
    }

    // --- Map physical joystick raw state to RemoteChannels structure ---
    g_physicalControllerState.ch1 = physicalStateRaw.lX;    // For Right Stick X
    g_physicalControllerState.ch2 = physicalStateRaw.lY;    // For Right Stick Y
    g_physicalControllerState.ch3 = physicalStateRaw.lZ;    // For Left Stick Y
    g_physicalControllerState.ch4 = physicalStateRaw.lRx;   // For Left Stick X
    g_physicalControllerState.ch5 = (physicalStateRaw.rgbButtons[0] & 0x80) ? true : false; // Button A
    g_physicalControllerState.ch6 = physicalStateRaw.lRz;   // For Left Trigger
    g_physicalControllerState.ch7 = physicalStateRaw.rglSlider[0]; // For Right Trigger
    g_physicalControllerState.ch8 = physicalStateRaw.rglSlider[1]; // For LB
    g_physicalControllerState.ch9 = physicalStateRaw.lRy;   // For RB
    g_physicalControllerState.ch10 = (physicalStateRaw.rgbButtons[1] & 0x80) ? true : false; // Button B

    // --- Map RemoteChannels to XUSB_REPORT for virtual Xbox 360 controller ---
    XUSB_REPORT_INIT(&g_virtualControllerReport); // Initialize report to default

    // Axis scaling function: DirectInput (-1000 to 1000) to XInput (-32768 to 32767)
    auto scale_axis = [](long val) -> SHORT {
        double scaled_val = static_cast<double>(val) / 1000.0 * 32767.0;
        return static_cast<SHORT>((std::max)(-32767.0, (std::min)(32767.0, scaled_val)));
    };

    // Trigger scaling function: DirectInput (-1000 to 1000) to XInput (0 to 255)
    // IMPORTANT: Adjust this function if your trigger source axes (ch6, ch7) are not bipolar (-1000 to 1000)
    auto scale_trigger = [](long val) -> BYTE {
        // Assumes bipolar input (-1000 to 1000).
        // If val is 0 (center), trigger is half-pressed. If -1000, trigger is 0. If 1000, trigger is 255.
        double scaled_val = (static_cast<double>(val) + 1000.0) / 2000.0 * 255.0;
        return static_cast<BYTE>((std::max)(0.0, (std::min)(255.0, scaled_val)));
    };

    // --- Apply the new mapping ---

    // Left Stick X: ch4 (from lRx)
    g_virtualControllerReport.sThumbLX = scale_axis(g_physicalControllerState.ch4);

    // Left Stick Y: ch3 (from lZ) - XInput Y is often inverted (up = positive)
    g_virtualControllerReport.sThumbLY = scale_axis(g_physicalControllerState.ch3 * -1);

    // Right Stick X: ch1 (from lX)
    g_virtualControllerReport.sThumbRX = scale_axis(g_physicalControllerState.ch1);

    // Right Stick Y: ch2 (from lY) - XInput Y is often inverted
    g_virtualControllerReport.sThumbRY = scale_axis(g_physicalControllerState.ch2 * -1);

    // Left Trigger: ch6 (from lRz)
    // Check the actual range of lRz. If it's a throttle (0 to 1000 or -1000 to 0),
    // you'll need a different scaling function for the trigger.
    g_virtualControllerReport.bLeftTrigger = scale_trigger(g_physicalControllerState.ch6);

    // Right Trigger: ch7 (from rglSlider[0])
    // Check the actual range of rglSlider[0].
    g_virtualControllerReport.bRightTrigger = scale_trigger(g_physicalControllerState.ch7);

    // Button A: ch5 (from Button 0)
    if (g_physicalControllerState.ch5) {
        g_virtualControllerReport.wButtons |= XUSB_GAMEPAD_A;
    }

    // Button B: ch10 (from Button 1)
    if (g_physicalControllerState.ch10) {
        g_virtualControllerReport.wButtons |= XUSB_GAMEPAD_B;
    }

    // LB (Left Bumper/Shoulder): ch8 (from rglSlider[1])
    // Assuming ch8 > 0 means pressed. Adjust threshold as needed.
    // If rglSlider[1] centers at 0, then > some_positive_threshold or < some_negative_threshold.
    // If rglSlider[1] is unipolar (e.g. 0 to 1000), then > some_threshold_above_zero.
    // For simplicity, let's assume positive half of the range means pressed.
    if (g_physicalControllerState.ch8 > 0) { // Example threshold: if slider is moved from center (0) to positive
    // if (g_physicalControllerState.ch8 > 500) { // Or a more distinct threshold
        g_virtualControllerReport.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    }

    // RB (Right Bumper/Shoulder): ch9 (from lRy)
    // Assuming ch9 > 0 means pressed. Adjust threshold as needed.
    // lRy is an axis, so it ranges from -1000 to 1000.
    // You need to decide what part of its range constitutes an RB press.
    if (g_physicalControllerState.ch9 > 500) { // Example: If lRy is pushed more than halfway up/forward
        g_virtualControllerReport.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    }
    // Alternatively, if lRy is used like a two-way switch:
    // else if (g_physicalControllerState.ch9 < -500) {
    //     // Map to another button or action if needed
    // }


    // Update the virtual controller state
    if (g_pVigemClient && g_pTargetX360) {
        vigem_target_x360_update(g_pVigemClient, g_pTargetX360, g_virtualControllerReport);
    }
}

void PrintControllerStates() {
    system("cls"); // Clear console (Windows specific)

    std::cout << "--- Physical Controller (RemoteChannels) ---" << std::endl;
    std::cout << "ch1 (X-Axis):       " << g_physicalControllerState.ch1 << std::endl;
    std::cout << "ch2 (Y-Axis):       " << g_physicalControllerState.ch2 << std::endl;
    std::cout << "ch3 (Z-Axis):       " << g_physicalControllerState.ch3 << std::endl;
    std::cout << "ch4 (X-Rotation):   " << g_physicalControllerState.ch4 << std::endl;
    std::cout << "ch5 (Button 1):     " << (g_physicalControllerState.ch5 ? "ON" : "OFF") << std::endl;
    std::cout << "ch6 (Z-Rotation):   " << g_physicalControllerState.ch6 << std::endl;
    std::cout << "ch7 (Slider 1):     " << g_physicalControllerState.ch7 << std::endl;
    std::cout << "ch8 (Slider 2):     " << g_physicalControllerState.ch8 << std::endl;
    std::cout << "ch9 (Y-Rotation):   " << g_physicalControllerState.ch9 << std::endl;
    std::cout << "ch10 (Button 2):    " << (g_physicalControllerState.ch10 ? "ON" : "OFF") << std::endl;
    std::cout << std::endl;

    std::cout << "--- Virtual Xbox 360 Controller (XUSB_REPORT) ---" << std::endl;
    std::cout << "Left Stick X: " << g_virtualControllerReport.sThumbLX << std::endl;
    std::cout << "Left Stick Y: " << g_virtualControllerReport.sThumbLY << std::endl;
    std::cout << "Right Stick X: " << g_virtualControllerReport.sThumbRX << std::endl;
    std::cout << "Right Stick Y: " << g_virtualControllerReport.sThumbRY << std::endl;
    std::cout << "Left Trigger:  " << static_cast<int>(g_virtualControllerReport.bLeftTrigger) << std::endl;
    std::cout << "Right Trigger: " << static_cast<int>(g_virtualControllerReport.bRightTrigger) << std::endl;
    std::cout << "Buttons (Hex): 0x" << std::hex << g_virtualControllerReport.wButtons << std::dec << std::endl;
    // You can add more detailed button printing if needed
    if (g_virtualControllerReport.wButtons & XUSB_GAMEPAD_A) std::cout << "  A Pressed" << std::endl;
    if (g_virtualControllerReport.wButtons & XUSB_GAMEPAD_B) std::cout << "  B Pressed" << std::endl;
    if (g_virtualControllerReport.wButtons & XUSB_GAMEPAD_LEFT_SHOULDER) std::cout << "  LS Pressed" << std::endl;
    if (g_virtualControllerReport.wButtons & XUSB_GAMEPAD_RIGHT_SHOULDER) std::cout << "  RS Pressed" << std::endl;
    // ... and so on for other XUSB_GAMEPAD_... buttons

    std::cout << "----------------------------------------------------" << std::endl;
    std::cout << "Press ESC to quit." << std::endl;
}

// --- Utility: Dummy Window for DirectInput ---
HWND CreateDummyWindow() {
    WNDCLASSW wc = {0}; // Use WNDCLASSW for Unicode
wc.lpfnWndProc = DefWindowProc;
wc.hInstance = GetModuleHandle(nullptr);
wc.lpszClassName = L"MyDummyDInputWindowUnicode"; // Use L"" for wide string literal

if (!RegisterClassW(&wc)) { // Use RegisterClassW
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
         std::cerr << "Error: Failed to register dummy window class. Error code: " << GetLastError() << std::endl;
         return nullptr;
    }
}

HWND hWnd = CreateWindowExW( // Explicitly call CreateWindowExW
    0,
    wc.lpszClassName,       // Now LPCWSTR
    L"Dummy DInput Window", // Now LPCWSTR
    0,
    0, 0, 0, 0,
    HWND_MESSAGE,
    nullptr,
    wc.hInstance,
    nullptr
);

    if (!hWnd) {
        std::cerr << "Error: Failed to create dummy window. Error code: " << GetLastError() << std::endl;
    }
    return hWnd;
}


// --- Main Application Entry Point ---
int main() {
    std::cout << "Starting Joystick Reader and Virtual Gamepad Application..." << std::endl;

    HWND hWnd = CreateDummyWindow();
    if (!hWnd) {
        std::cerr << "Critical Error: Could not create dummy window for DirectInput. Exiting." << std::endl;
        return 1;
    }

    if (!InitializeDirectInput(hWnd)) {
        std::cerr << "Critical Error: Failed to initialize DirectInput. Exiting." << std::endl;
        CleanupDirectInput(); // Attempt cleanup even on partial init
        DestroyWindow(hWnd);
        return 1;
    }

    if (!InitializeVirtualGamepad()) {
        std::cerr << "Critical Error: Failed to initialize Virtual Gamepad. Exiting." << std::endl;
        CleanupDirectInput();
        CleanupVirtualGamepad(); // Attempt cleanup even on partial init
        DestroyWindow(hWnd);
        return 1;
    }

    std::cout << "\nInitialization Complete. Reading physical joystick and sending to virtual Xbox 360 controller..." << std::endl;
    std::cout << "Mapping Example: ch1(X)->LX, ch2(Y)->LY(inv), ch3(Z)->LT, ch4(Rx)->RX, ch5(B1)->A, etc." << std::endl;
    std::cout << "You may need to adjust the mapping in PollAndMapController() for your specific needs.\n" << std::endl;


    while (true) {
        PollAndMapController(); // Poll physical, map, and update virtual
        PrintControllerStates();  // Display current states

        // Check for ESC key press to quit
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState('Q') & 0x8000)) {
            std::cout << "ESC key pressed. Exiting..." << std::endl;
            break;
        }

        Sleep(16); // ~60Hz polling/update rate. Adjust as needed. (1000ms / 60fps ~= 16.6ms)
    }

    // Cleanup resources
    CleanupDirectInput();
    CleanupVirtualGamepad();
    DestroyWindow(hWnd); // Destroy the dummy window

    std::cout << "Application terminated gracefully." << std::endl;
    return 0;
}