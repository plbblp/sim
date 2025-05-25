// main.cpp

#define NOMINMAX // MUST be at the very top, before any Windows.h or other conflicting headers
#define DIRECTINPUT_VERSION 0x0800

#include <windows.h>
#include <dinput.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm> // For std::max and std::min

#include <ViGEm/Client.h> // ViGEm Client SDK Header

#include <dxgi1_2.h>      // DXGI 1.2 for IDXGIOutputDuplication
#include <d3d11.h>        // Direct3D 11

#include <opencv2/opencv.hpp> // Main OpenCV header (or specific headers like highgui.hpp, imgproc.hpp)


// --- Global Variables ---

// DirectInput
LPDIRECTINPUT8        g_pDI = nullptr;
LPDIRECTINPUTDEVICE8  g_pJoystick = nullptr;

// ViGEm
PVIGEM_CLIENT         g_pVigemClient = nullptr;
PVIGEM_TARGET         g_pTargetX360 = nullptr;
XUSB_REPORT           g_virtualControllerReport;

// Screen Capture (DXGI Desktop Duplication)
ID3D11Device*           g_pD3DDevice = nullptr;
ID3D11DeviceContext*    g_pD3DImmediateContext = nullptr;
IDXGIOutputDuplication* g_pDeskDupl = nullptr;
DXGI_OUTPUT_DESC        g_dxgiOutputDesc;
BYTE*                   g_pCaptureBuffer = nullptr; // Raw pixel buffer
UINT                    g_captureBufferSize = 0;

// OpenCV
cv::Mat                 g_capturedFrameMat; // OpenCV Mat to store and display the captured frame
const std::string       g_opencvWindowName = "Game Capture Preview";

// Application State
HWND                    g_hDummyWindow = nullptr; // Dummy window for DirectInput

// Controller State Structure
struct RemoteChannels {
    long ch1, ch2, ch3, ch4, ch6, ch7, ch8, ch9;
    bool ch5, ch10;
    RemoteChannels() : ch1(0), ch2(0), ch3(0), ch4(0), ch5(false), ch6(0), ch7(0), ch8(0), ch9(0), ch10(false) {}
};
RemoteChannels g_physicalControllerState;


// --- Function Prototypes (Order might matter for definitions below) ---
BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext);
BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext);
bool InitializeDirectInput();
void CleanupDirectInput();
bool InitializeVirtualGamepad();
void CleanupVirtualGamepad();
bool InitializeScreenCapture();
void CleanupScreenCapture();
void PollAndMapController();
bool CaptureAndDisplayFrame();
void PrintControllerStates(); // Optional for debugging
HWND CreateDummyWindow();


// --- Function Definitions ---

HWND CreateDummyWindow() {
    WNDCLASSW wc = {0}; // Use WNDCLASSW for Unicode
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"MySimDummyWindowUnicode"; // Unique Unicode class name

    if (!RegisterClassW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            std::cerr << "Error: Failed to register dummy window class. Error code: " << GetLastError() << std::endl;
            return nullptr;
        }
    }
    g_hDummyWindow = CreateWindowExW(
        0, wc.lpszClassName, L"Sim Dummy Window", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr
    );
    if (!g_hDummyWindow) {
        std::cerr << "Error: Failed to create dummy window. Error code: " << GetLastError() << std::endl;
    }
    return g_hDummyWindow;
}

BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    HRESULT hr = g_pDI->CreateDevice(pdidInstance->guidInstance, &g_pJoystick, nullptr);
    return SUCCEEDED(hr) ? DIENUM_STOP : DIENUM_CONTINUE;
}

BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext) {
    if (pdidoi->dwType & DIDFT_AXIS) {
        DIPROPRANGE diprg;
        diprg.diph.dwSize = sizeof(DIPROPRANGE);
        diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        diprg.diph.dwHow = DIPH_BYID;
        diprg.diph.dwObj = pdidoi->dwType;
        diprg.lMin = -1000;
        diprg.lMax = +1000;
        g_pJoystick->SetProperty(DIPROP_RANGE, &diprg.diph);
    }
    return DIENUM_CONTINUE;
}

bool InitializeDirectInput() {
    if (!g_hDummyWindow) {
        std::cerr << "Error: Dummy window not created before DirectInput initialization." << std::endl;
        return false;
    }
    HRESULT hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, nullptr);
    if (FAILED(hr)) { std::cerr << "Error: DirectInput8Create failed! HR: " << hr << std::endl; return false; }

    hr = g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, nullptr, DIEDFL_ATTACHEDONLY);
    if (FAILED(hr) || !g_pJoystick) {
        std::cerr << "Error: No joystick found or EnumDevices failed! HR: " << hr << std::endl;
        if(g_pDI) { g_pDI->Release(); g_pDI = nullptr; }
        return false;
    }
    hr = g_pJoystick->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) { std::cerr << "Error: SetDataFormat failed! HR: " << hr << std::endl; /* cleanup */ return false; }
    hr = g_pJoystick->SetCooperativeLevel(g_hDummyWindow, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) { std::cerr << "Error: SetCooperativeLevel failed! HR: " << hr << std::endl; /* cleanup */ return false; }
    g_pJoystick->EnumObjects(EnumAxesCallback, nullptr, DIDFT_AXIS);
    g_pJoystick->Acquire();
    std::cout << "DirectInput Joystick initialized." << std::endl;
    return true;
}

void CleanupDirectInput() {
    if (g_pJoystick) { g_pJoystick->Unacquire(); g_pJoystick->Release(); g_pJoystick = nullptr; }
    if (g_pDI) { g_pDI->Release(); g_pDI = nullptr; }
    std::cout << "DirectInput cleaned up." << std::endl;
}

bool InitializeVirtualGamepad() {
    g_pVigemClient = vigem_alloc();
    if (!g_pVigemClient) { std::cerr << "Error: Failed to allocate ViGEm client!" << std::endl; return false; }
    VIGEM_ERROR err = vigem_connect(g_pVigemClient);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "Error: ViGEm Bus connection failed! Code: " << err << std::endl;
        vigem_free(g_pVigemClient); g_pVigemClient = nullptr; return false;
    }
    g_pTargetX360 = vigem_target_x360_alloc();
    if (!g_pTargetX360) {
        std::cerr << "Error: Failed to allocate Xbox 360 target!" << std::endl;
        vigem_disconnect(g_pVigemClient); vigem_free(g_pVigemClient); g_pVigemClient = nullptr; return false;
    }
    err = vigem_target_add(g_pVigemClient, g_pTargetX360);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "Error: Failed to add Xbox 360 target! Code: " << err << std::endl;
        vigem_target_free(g_pTargetX360); g_pTargetX360 = nullptr;
        vigem_disconnect(g_pVigemClient); vigem_free(g_pVigemClient); g_pVigemClient = nullptr; return false;
    }
    XUSB_REPORT_INIT(&g_virtualControllerReport);
    std::cout << "Virtual Xbox 360 controller initialized." << std::endl;
    return true;
}

void CleanupVirtualGamepad() {
    if (g_pVigemClient && g_pTargetX360) {
        vigem_target_remove(g_pVigemClient, g_pTargetX360);
        vigem_target_free(g_pTargetX360); g_pTargetX360 = nullptr;
    }
    if (g_pVigemClient) { vigem_disconnect(g_pVigemClient); vigem_free(g_pVigemClient); g_pVigemClient = nullptr; }
    std::cout << "Virtual gamepad cleaned up." << std::endl;
}

bool InitializeScreenCapture() {
    HRESULT hr;
    D3D_DRIVER_TYPE driverTypes[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL featureLevel;

    for (UINT i = 0; i < ARRAYSIZE(driverTypes); ++i) {
        hr = D3D11CreateDevice(nullptr, driverTypes[i], nullptr, 0, featureLevels, ARRAYSIZE(featureLevels),
                               D3D11_SDK_VERSION, &g_pD3DDevice, &featureLevel, &g_pD3DImmediateContext);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) { std::cerr << "Error: D3D11CreateDevice failed! HR: " << hr << std::endl; return false; }

    IDXGIDevice* dxgiDevice = nullptr;
    hr = g_pD3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) { std::cerr << "Error: Query IDXGIDevice failed! HR: " << hr << std::endl; /* cleanup */ return false; }

    IDXGIAdapter* dxgiAdapter = nullptr;
    hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);
    dxgiDevice->Release();
    if (FAILED(hr)) { std::cerr << "Error: Get DXGI adapter failed! HR: " << hr << std::endl; /* cleanup */ return false; }

    IDXGIOutput* dxgiOutput = nullptr;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput); // Primary monitor
    dxgiAdapter->Release();
    if (FAILED(hr)) { std::cerr << "Error: Get DXGI output failed! HR: " << hr << std::endl; /* cleanup */ return false; }

    dxgiOutput->GetDesc(&g_dxgiOutputDesc);

    IDXGIOutput1* dxgiOutput1 = nullptr;
    hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
    dxgiOutput->Release();
    if (FAILED(hr)) { std::cerr << "Error: Query IDXGIOutput1 failed! HR: " << hr << std::endl; /* cleanup */ return false; }

    hr = dxgiOutput1->DuplicateOutput(g_pD3DDevice, &g_pDeskDupl);
    dxgiOutput1->Release();
    if (FAILED(hr)) {
        std::cerr << "Error: DuplicateOutput failed! HR: " << hr << std::endl;
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) std::cerr << "  Reason: Max applications or fullscreen app running." << std::endl;
        /* cleanup */ return false;
    }
    std::cout << "Screen Capture initialized. Output: "
              << (g_dxgiOutputDesc.DesktopCoordinates.right - g_dxgiOutputDesc.DesktopCoordinates.left)
              << "x" << (g_dxgiOutputDesc.DesktopCoordinates.bottom - g_dxgiOutputDesc.DesktopCoordinates.top) << std::endl;
    cv::namedWindow(g_opencvWindowName, cv::WINDOW_AUTOSIZE);
    return true;
}

void CleanupScreenCapture() {
    if (g_pDeskDupl) { g_pDeskDupl->Release(); g_pDeskDupl = nullptr; }
    if (g_pCaptureBuffer) { delete[] g_pCaptureBuffer; g_pCaptureBuffer = nullptr; g_captureBufferSize = 0; }
    if (g_pD3DImmediateContext) { g_pD3DImmediateContext->Release(); g_pD3DImmediateContext = nullptr; }
    if (g_pD3DDevice) { g_pD3DDevice->Release(); g_pD3DDevice = nullptr; }
    cv::destroyAllWindows(); // Destroy all OpenCV windows
    std::cout << "Screen Capture cleaned up." << std::endl;
}

void PollAndMapController() {
    if (!g_pJoystick) return;
    HRESULT hr = g_pJoystick->Poll();
    if (FAILED(hr)) {
        hr = g_pJoystick->Acquire();
        while (hr == DIERR_INPUTLOST) hr = g_pJoystick->Acquire();
        if (FAILED(hr)) return;
    }
    DIJOYSTATE2 rawState;
    hr = g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &rawState);
    if (FAILED(hr)) return;

    g_physicalControllerState.ch1 = rawState.lX;
    g_physicalControllerState.ch2 = rawState.lY;
    g_physicalControllerState.ch3 = rawState.lZ;
    g_physicalControllerState.ch4 = rawState.lRx;
    g_physicalControllerState.ch5 = (rawState.rgbButtons[0] & 0x80) != 0;
    g_physicalControllerState.ch6 = rawState.lRz;
    g_physicalControllerState.ch7 = rawState.rglSlider[0];
    g_physicalControllerState.ch8 = rawState.rglSlider[1];
    g_physicalControllerState.ch9 = rawState.lRy;
    g_physicalControllerState.ch10 = (rawState.rgbButtons[1] & 0x80) != 0;

    XUSB_REPORT_INIT(&g_virtualControllerReport);
    auto scale_axis = [](long v) { return static_cast<SHORT>((std::max)(-32767.0, (std::min)(32767.0, static_cast<double>(v) / 1000.0 * 32767.0))); };
    auto scale_trigger = [](long v) { return static_cast<BYTE>((std::max)(0.0, (std::min)(255.0, (static_cast<double>(v) + 1000.0) / 2000.0 * 255.0))); };

    // Your specified mapping:
    g_virtualControllerReport.sThumbLX = scale_axis(g_physicalControllerState.ch4);      // Left Stick X: ch4 (lRx)
    g_virtualControllerReport.sThumbLY = scale_axis(g_physicalControllerState.ch3 * -1);  // Left Stick Y: ch3 (lZ), inverted
    g_virtualControllerReport.sThumbRX = scale_axis(g_physicalControllerState.ch1);      // Right Stick X: ch1 (lX)
    g_virtualControllerReport.sThumbRY = scale_axis(g_physicalControllerState.ch2 * -1);  // Right Stick Y: ch2 (lY), inverted
    g_virtualControllerReport.bLeftTrigger = scale_trigger(g_physicalControllerState.ch6); // Left Trigger: ch6 (lRz)
    g_virtualControllerReport.bRightTrigger = scale_trigger(g_physicalControllerState.ch7);// Right Trigger: ch7 (Slider1)

    if (g_physicalControllerState.ch5)  g_virtualControllerReport.wButtons |= XUSB_GAMEPAD_A; // Button A: ch5
    if (g_physicalControllerState.ch10) g_virtualControllerReport.wButtons |= XUSB_GAMEPAD_B; // Button B: ch10

    // LB: ch8 (Slider2) - treat as button, pressed if value > threshold (e.g., 0 or 500)
    if (g_physicalControllerState.ch8 > 500) g_virtualControllerReport.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    // RB: ch9 (lRy) - treat as button, pressed if value > threshold (e.g., 500)
    if (g_physicalControllerState.ch9 > 500) g_virtualControllerReport.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;


    if (g_pVigemClient && g_pTargetX360) {
        vigem_target_x360_update(g_pVigemClient, g_pTargetX360, g_virtualControllerReport);
    }
}

bool CaptureAndDisplayFrame() {
    if (!g_pDeskDupl) return false;
    IDXGIResource* pDesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    HRESULT hr = g_pDeskDupl->AcquireNextFrame(1, &frameInfo, &pDesktopResource); // 1ms timeout

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return true; // No new frame yet, not an error
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            std::cerr << "Screen Capture: Access lost. Attempting to reinitialize..." << std::endl;
            CleanupScreenCapture(); // Release old resources
            if (!InitializeScreenCapture()) { // Try to re-init
                 std::cerr << "Screen Capture: Reinitialization failed." << std::endl;
                 return false; // Critical if re-init fails
            }
            return true; // Re-initialized, try next frame
        }
        std::cerr << "Error: AcquireNextFrame failed! HR: " << hr << std::endl;
        return false;
    }

    ID3D11Texture2D* pAcquiredDesktopImage = nullptr;
    hr = pDesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pAcquiredDesktopImage);
    pDesktopResource->Release();
    if (FAILED(hr)) { g_pDeskDupl->ReleaseFrame(); return false; }

    D3D11_TEXTURE2D_DESC frameDesc;
    pAcquiredDesktopImage->GetDesc(&frameDesc);

    ID3D11Texture2D* pStagingTexture = nullptr;
    D3D11_TEXTURE2D_DESC stagingDesc = frameDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    hr = g_pD3DDevice->CreateTexture2D(&stagingDesc, nullptr, &pStagingTexture);
    if (FAILED(hr)) { pAcquiredDesktopImage->Release(); g_pDeskDupl->ReleaseFrame(); return false; }

    g_pD3DImmediateContext->CopyResource(pStagingTexture, pAcquiredDesktopImage);
    pAcquiredDesktopImage->Release();

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = g_pD3DImmediateContext->Map(pStagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) { pStagingTexture->Release(); g_pDeskDupl->ReleaseFrame(); return false; }

    if (frameDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        // Create a Mat pointing to the data, then clone it
        cv::Mat tempMat(frameDesc.Height, frameDesc.Width, CV_8UC4, mappedResource.pData, mappedResource.RowPitch);
        g_capturedFrameMat = tempMat.clone(); // Clone is important!
    } else {
        std::cerr << "Warning: Captured frame format is not BGRA8. Format: " << frameDesc.Format << std::endl;
    }

    g_pD3DImmediateContext->Unmap(pStagingTexture, 0);
    pStagingTexture->Release();
    g_pDeskDupl->ReleaseFrame();

    if (!g_capturedFrameMat.empty()) {
        cv::Mat displayedFrame;
        double original_width = g_capturedFrameMat.cols;
        double original_height = g_capturedFrameMat.rows;
        int target_width = 600; // Your desired target width

        if (original_width > target_width) { // Only resize if wider than target
            double scale_factor = static_cast<double>(target_width) / original_width;
            int target_height = static_cast<int>(original_height * scale_factor);
            cv::resize(g_capturedFrameMat, displayedFrame, cv::Size(target_width, target_height), 0, 0, cv::INTER_AREA);
        } else {
            displayedFrame = g_capturedFrameMat; // No need to resize if already smaller or equal
        }
        cv::imshow(g_opencvWindowName, displayedFrame);
    }
    return true;
}

void PrintControllerStates() { // Optional, call if needed for debugging
    system("cls");
    std::cout << "--- Physical Controller (RemoteChannels) ---" << std::endl;
    std::cout << "ch1(lX): " << g_physicalControllerState.ch1 << "  ch2(lY): " << g_physicalControllerState.ch2
              << "  ch3(lZ): " << g_physicalControllerState.ch3 << "  ch4(lRx): " << g_physicalControllerState.ch4 << std::endl;
    std::cout << "ch5(B0): " << (g_physicalControllerState.ch5?"ON":"OFF") << "  ch6(lRz): " << g_physicalControllerState.ch6
              << "  ch7(Sl0): " << g_physicalControllerState.ch7 << "  ch8(Sl1): " << g_physicalControllerState.ch8 << std::endl;
    std::cout << "ch9(lRy): " << g_physicalControllerState.ch9 << "  ch10(B1): " << (g_physicalControllerState.ch10?"ON":"OFF") << std::endl;
    std::cout << "\n--- Virtual Xbox 360 ---" << std::endl;
    std::cout << "LX: " << g_virtualControllerReport.sThumbLX << " LY: " << g_virtualControllerReport.sThumbLY
              << " RX: " << g_virtualControllerReport.sThumbRX << " RY: " << g_virtualControllerReport.sThumbRY << std::endl;
    std::cout << "LT: " << (int)g_virtualControllerReport.bLeftTrigger << " RT: " << (int)g_virtualControllerReport.bRightTrigger
              << " Buttons: 0x" << std::hex << g_virtualControllerReport.wButtons << std::dec << std::endl;
    std::cout << "----------------------------------------------------" << std::endl;
}


int main() {
    std::cout << "Starting SimApp..." << std::endl;

    if (!CreateDummyWindow()) return 1; // CreateDummyWindow now sets g_hDummyWindow

    if (!InitializeDirectInput()) { CleanupDirectInput(); DestroyWindow(g_hDummyWindow); return 1; }
    if (!InitializeVirtualGamepad()) { CleanupDirectInput(); CleanupVirtualGamepad(); DestroyWindow(g_hDummyWindow); return 1; }
    if (!InitializeScreenCapture()) {
        CleanupDirectInput(); CleanupVirtualGamepad(); CleanupScreenCapture(); DestroyWindow(g_hDummyWindow); return 1;
    }

    std::cout << "\nInitialization Complete. Running..." << std::endl;
    std::cout << "Press CTRL+Q to quit." << std::endl;

    while (true) {
        PollAndMapController();
        if (!CaptureAndDisplayFrame()) {
            // Potentially handle critical capture error, e.g., by trying to re-init less aggressively or exiting
            std::cerr << "A screen capture error occurred that might require attention." << std::endl;
        }

        int key = cv::waitKey(1); // Essential for imshow and window events
        if (key == 'q' || key == 'Q' || key == 27) { // Allow q/Q or ESC from OpenCV window to quit
             std::cout << "Quit key pressed in OpenCV window. Exiting..." << std::endl;
            break;
        }

        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState('Q') & 0x8000)) {
            std::cout << "CTRL+Q pressed. Exiting..." << std::endl;
            break;
        }
        // PrintControllerStates(); // Uncomment for debugging controller values
    }

    CleanupScreenCapture();
    CleanupVirtualGamepad();
    CleanupDirectInput();
    if (g_hDummyWindow) DestroyWindow(g_hDummyWindow);

    std::cout << "Application terminated." << std::endl;
    return 0;
}