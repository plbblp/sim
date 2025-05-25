#define DIRECTINPUT_VERSION 0x0800 // 使用DirectInput 8
#include <dinput.h>
#include <iostream>
#include <vector>
#include <string>
#include <windows.h> // For Sleep and system("cls")

// 用于存储遥控器通道值的结构体 (新版)
struct RemoteChannels {
    long ch1;  // X-axis
    long ch2;  // Y-axis
    long ch3;  // Z-axis
    long ch4;  // X Rotation
    bool ch5;  // Button 1
    long ch6;  // Z Rotation
    long ch7;  // Slider 1 (通常是 rglSlider[0])
    long ch8;  // "拨号" (Dial) - 映射到 Slider 2 (rglSlider[1]) 或 POV hat (见注释)
    long ch9;  // Y Rotation
    bool ch10; // Button 2

    RemoteChannels() :
        ch1(0), ch2(0), ch3(0), ch4(0),
        ch5(false),
        ch6(0), ch7(0), ch8(0), ch9(0),
        ch10(false) {}
};

// 全局变量存储DirectInput对象和设备
LPDIRECTINPUT8        g_pDI = nullptr;
LPDIRECTINPUTDEVICE8  g_pJoystick = nullptr;
RemoteChannels        g_joystickState; // 存储当前手柄状态

// DirectInput设备枚举回调函数
BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    HRESULT hr;
    hr = g_pDI->CreateDevice(pdidInstance->guidInstance, &g_pJoystick, nullptr);
    if (FAILED(hr)) {
        return DIENUM_CONTINUE;
    }
    return DIENUM_STOP;
}

// 枚举设备对象的函数 (用于设置轴的属性，如范围)
BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext) {
    // 为每个轴设置范围属性
    // 我们将所有轴的范围设置为 -1000 到 +1000
    // 这包括 lX, lY, lZ, lRx, lRy, lRz 以及 rglSlider[0], rglSlider[1]
    if (pdidoi->dwType & DIDFT_AXIS) {
        DIPROPRANGE diprg;
        diprg.diph.dwSize = sizeof(DIPROPRANGE);
        diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        diprg.diph.dwHow = DIPH_BYID;
        diprg.diph.dwObj = pdidoi->dwType;
        diprg.lMin = -1000;
        diprg.lMax = +1000;

        if (FAILED(g_pJoystick->SetProperty(DIPROP_RANGE, &diprg.diph))) {
            std::cerr << "Failed to set property DIPROP_RANGE for axis: " << pdidoi->tszName << std::endl;
            // 不停止枚举，尝试设置其他轴
            // return DIENUM_STOP;
        }
    }
    return DIENUM_CONTINUE;
}


bool InitializeDirectInput(HWND hWnd) {
    HRESULT hr;

    hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, nullptr);
    if (FAILED(hr)) {
        std::cerr << "DirectInput8Create failed!" << std::endl;
        return false;
    }

    hr = g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, nullptr, DIEDFL_ATTACHEDONLY);
    if (FAILED(hr) || g_pJoystick == nullptr) {
        std::cerr << "No joystick/gamepad found or EnumDevices failed!" << std::endl;
        if(g_pDI) g_pDI->Release();
        g_pDI = nullptr;
        return false;
    }

    hr = g_pJoystick->SetDataFormat(&c_dfDIJoystick2); // DIJOYSTATE2 包含更多轴和按钮
    if (FAILED(hr)) {
        std::cerr << "SetDataFormat failed!" << std::endl;
        g_pJoystick->Release(); g_pJoystick = nullptr;
        g_pDI->Release(); g_pDI = nullptr;
        return false;
    }

    hr = g_pJoystick->SetCooperativeLevel(hWnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        std::cerr << "SetCooperativeLevel failed!" << std::endl;
        g_pJoystick->Release(); g_pJoystick = nullptr;
        g_pDI->Release(); g_pDI = nullptr;
        return false;
    }

    // 枚举设备对象 (轴) 并设置其范围
    hr = g_pJoystick->EnumObjects(EnumAxesCallback, (VOID*)hWnd, DIDFT_AXIS);
    if (FAILED(hr)) {
        std::cerr << "Warning: EnumObjects for axes failed or some axes could not be set!" << std::endl;
    }

    hr = g_pJoystick->Acquire();
    int retries = 5;
    while (hr == DIERR_INPUTLOST && retries > 0) {
        hr = g_pJoystick->Acquire();
        retries--;
        Sleep(100);
    }
    // 不将 Acquire 失败视为致命错误，因为 Poll 循环会尝试重新获取
    if (FAILED(hr) && hr != DIERR_INPUTLOST && hr != DIERR_OTHERAPPHASPRIO) {
         std::cerr << "Acquire failed initially with error: " << hr << std::endl;
    }


    std::cout << "Joystick initialized." << std::endl;
    DIDEVCAPS caps;
    caps.dwSize = sizeof(DIDEVCAPS);
    if (SUCCEEDED(g_pJoystick->GetCapabilities(&caps))) {
        std::cout << "Device: Axes=" << caps.dwAxes << ", Buttons="
                  << caps.dwButtons << ", POVs=" << caps.dwPOVs << std::endl;
    }
    return true;
}

void PollJoystick() {
    if (g_pJoystick == nullptr) {
        return;
    }

    HRESULT hr;
    hr = g_pJoystick->Poll();
    if (FAILED(hr)) {
        hr = g_pJoystick->Acquire();
        while (hr == DIERR_INPUTLOST) {
            hr = g_pJoystick->Acquire();
        }
        if (FAILED(hr)) {
            return;
        }
    }

    DIJOYSTATE2 js;
    hr = g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js);
    if (FAILED(hr)) {
        return;
    }

    // --- 将DIJOYSTATE2的值映射到新的RemoteChannels结构体 ---
    // ch1对应x轴
    g_joystickState.ch1 = js.lX;
    // ch2对应y轴
    g_joystickState.ch2 = js.lY;
    // ch3对应z轴
    g_joystickState.ch3 = js.lZ;
    // ch4对应x旋转
    g_joystickState.ch4 = js.lRx;
    // ch5对应按钮1
    g_joystickState.ch5 = (js.rgbButtons[0] & 0x80) ? true : false;
    // ch6对应z旋转
    g_joystickState.ch6 = js.lRz;
    // ch7对应滑块 (通常是第一个滑块 rglSlider[0])
    g_joystickState.ch7 = js.rglSlider[1];
    // ch8对应拨号 (Dial) - 这里映射到第二个滑块 rglSlider[1])
    // 如果您的“拨号”是POV hat，您可能需要读取 js.rgdwPOV[0] 并进行不同处理。
    // 例如: g_joystickState.ch8 = js.rgdwPOV[0]; (这将是一个角度值或-1)
    // DIJOYSTATE2有两个滑块 rglSlider[0] 和 rglSlider[1]。
    // 如果您的设备只有一个滑块，rglSlider[1] 可能总是0或未定义。
    g_joystickState.ch8 = js.rglSlider[0];
    // ch9对应y旋转
    g_joystickState.ch9 = js.lRy;
    // ch10对应按钮2
    g_joystickState.ch10 = (js.rgbButtons[1] & 0x80) ? true : false;
}

void PrintJoystickState() {
    system("cls"); // 清屏 (Windows specific)

    std::cout << "Remote Controller State (New Structure):" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "ch1 (X-Axis):       " << g_joystickState.ch1 << std::endl;
    std::cout << "ch2 (Y-Axis):       " << g_joystickState.ch2 << std::endl;
    std::cout << "ch3 (Z-Axis):       " << g_joystickState.ch3 << std::endl;
    std::cout << "ch4 (X-Rotation):   " << g_joystickState.ch4 << std::endl;
    std::cout << "ch5 (Button 1):     " << (g_joystickState.ch5 ? "ON" : "OFF") << std::endl;
    std::cout << "ch6 (Z-Rotation):   " << g_joystickState.ch6 << std::endl;
    std::cout << "ch7 (Slider 1):     " << g_joystickState.ch7 << std::endl;
    std::cout << "ch8 (Dial/Slider2): " << g_joystickState.ch8 << std::endl;
    std::cout << "ch9 (Y-Rotation):   " << g_joystickState.ch9 << std::endl;
    std::cout << "ch10 (Button 2):    " << (g_joystickState.ch10 ? "ON" : "OFF") << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Press ESC to quit." << std::endl;
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
}

// 创建一个简单的隐藏窗口供DirectInput使用
HWND CreateDummyWindow() {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = TEXT("DummyDInputWindowForJoystick"); // 使用唯一的类名
    if (!RegisterClass(&wc)) {
        // 如果注册失败，可能是因为类名已存在于此进程中（例如，如果多次调用此函数）
        // 尝试使用一个已注册的窗口或处理错误
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
             std::cerr << "Failed to register dummy window class. Error: " << GetLastError() << std::endl;
             return nullptr;
        }
    }
    HWND hWnd = CreateWindow(wc.lpszClassName, TEXT("Dummy DInput Window"), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hWnd) {
        std::cerr << "Failed to create dummy window. Error: " << GetLastError() << std::endl;
    }
    return hWnd;
}


int main() {
    HWND hWnd = CreateDummyWindow();
    if (!hWnd) {
        return 1;
    }

    if (!InitializeDirectInput(hWnd)) {
        std::cerr << "Failed to initialize DirectInput." << std::endl;
        CleanupDirectInput();
        DestroyWindow(hWnd);
        return 1;
    }

    std::cout << "Reading joystick input with new channel structure. Move your controller..." << std::endl;
    std::cout << "Mapping: ch1=X, ch2=Y, ch3=Z, ch4=Rx, ch5=Btn1, ch6=Rz, ch7=Slider1, ch8=Slider2/Dial, ch9=Ry, ch10=Btn2" << std::endl;

    while (true) {
        PollJoystick();
        PrintJoystickState();

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            break;
        }
        Sleep(50); // 轮询间隔
    }

    CleanupDirectInput();
    DestroyWindow(hWnd);
    std::cout << "Exiting." << std::endl;
    return 0;
}