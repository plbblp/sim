#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <ViGEm/Client.h>
#include <algorithm> // For std::max and std::min
#include <chrono>    // For FPS calculation
#include <iomanip>   // For std::fixed, std::setprecision
#include <sstream>   // For std::ostringstream

// OpenCV Includes
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

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

// DirectInput Globals
LPDIRECTINPUT8        g_pDI = nullptr;
LPDIRECTINPUTDEVICE8  g_pJoystick = nullptr;
RemoteChannels        g_joystickState;

// ViGEm Globals
PVIGEM_CLIENT         g_pVigem = nullptr;
PVIGEM_TARGET         g_pTargetX360 = nullptr;
XUSB_REPORT           g_virtualReport;

// Screen Capture Globals
cv::Mat captured_frame;
std::string CAPTURE_WINDOW_TITLE = "Liftoff: Micro Drones"; // *** 您的游戏窗口标题 ***
HWND game_window_hwnd = nullptr;
HDC game_window_hdc = nullptr;
HDC compatible_hdc = nullptr;
HBITMAP compatible_bitmap = nullptr;
BITMAPINFOHEADER bi;
int frame_width = 0;
int frame_height = 0;
const std::string PREVIEW_WINDOW_NAME = "Game Capture Preview";


// FPS Calculation Globals
std::chrono::steady_clock::time_point last_fps_time_point;
int frame_counter_fps = 0;
double display_fps = 0.0;


// --- Function Prototypes ---
BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext);
BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext);
bool InitializeDirectInput(HWND hWnd);
void CleanupDirectInput();
bool InitializeVirtualGamepad();
void CleanupVirtualGamepad();
HWND CreateDummyWindow();
bool InitializeScreenCapture(const std::string& window_title);
void CaptureFrame();
void CleanupScreenCapture();
void DrawFrameInfo(cv::Mat& frame_to_draw);
void PollJoystickAndMapToVirtual();


// --- DirectInput Functions ---
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

bool InitializeDirectInput(HWND hWnd) {
    HRESULT hr;
    hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, nullptr);
    if (FAILED(hr)) { std::cerr << "DirectInput8Create failed!" << std::endl; return false; }
    hr = g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, nullptr, DIEDFL_ATTACHEDONLY);
    if (FAILED(hr) || g_pJoystick == nullptr) {
        std::cerr << "No joystick/gamepad found or EnumDevices failed!" << std::endl;
        if(g_pDI) g_pDI->Release(); g_pDI = nullptr;
        return false;
    }
    hr = g_pJoystick->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) { std::cerr << "SetDataFormat failed!" << std::endl; g_pJoystick->Release(); g_pJoystick = nullptr; g_pDI->Release(); g_pDI = nullptr; return false; }
    hr = g_pJoystick->SetCooperativeLevel(hWnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) { std::cerr << "SetCooperativeLevel failed!" << std::endl; g_pJoystick->Release(); g_pJoystick = nullptr; g_pDI->Release(); g_pDI = nullptr; return false; }
    g_pJoystick->EnumObjects(EnumAxesCallback, (VOID*)hWnd, DIDFT_AXIS);
    g_pJoystick->Acquire();
    std::cout << "DirectInput Joystick initialized." << std::endl;
    return true;
}

void CleanupDirectInput() {
    if (g_pJoystick) { g_pJoystick->Unacquire(); g_pJoystick->Release(); g_pJoystick = nullptr; }
    if (g_pDI) { g_pDI->Release(); g_pDI = nullptr; }
    std::cout << "DirectInput cleaned up." << std::endl;
}

// --- ViGEm Functions ---
bool InitializeVirtualGamepad() {
    g_pVigem = vigem_alloc();
    if (g_pVigem == nullptr) { std::cerr << "Failed to allocate ViGEm client!" << std::endl; return false; }
    VIGEM_ERROR err = vigem_connect(g_pVigem);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "ViGEm Bus connection failed! Error code: " << err << std::endl; // MODIFIED
        vigem_free(g_pVigem); g_pVigem = nullptr; return false;
    }
    g_pTargetX360 = vigem_target_x360_alloc();
    if (g_pTargetX360 == nullptr) { std::cerr << "Failed to allocate Xbox 360 target!" << std::endl; vigem_disconnect(g_pVigem); vigem_free(g_pVigem); g_pVigem = nullptr; return false; }
    err = vigem_target_add(g_pVigem, g_pTargetX360);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "Failed to add Xbox 360 target to ViGEm Bus! Error code: " << err << std::endl; // MODIFIED
        vigem_target_free(g_pTargetX360); g_pTargetX360 = nullptr; vigem_disconnect(g_pVigem); vigem_free(g_pVigem); g_pVigem = nullptr; return false;
    }
    XUSB_REPORT_INIT(&g_virtualReport);
    std::cout << "Virtual Xbox 360 controller initialized and added to ViGEm Bus." << std::endl;
    return true;
}

void CleanupVirtualGamepad() {
    if (g_pVigem && g_pTargetX360) { vigem_target_remove(g_pVigem, g_pTargetX360); vigem_target_free(g_pTargetX360); g_pTargetX360 = nullptr; }
    if (g_pVigem) { vigem_disconnect(g_pVigem); vigem_free(g_pVigem); g_pVigem = nullptr; }
    std::cout << "Virtual gamepad cleaned up." << std::endl;
}

// --- Helper Functions ---
HWND CreateDummyWindow() {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = TEXT("DummyDInputWindowForJoystick");
    if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
         std::cerr << "Failed to register dummy window class. Error: " << GetLastError() << std::endl; return nullptr;
    }
    HWND hWnd = CreateWindow(wc.lpszClassName, TEXT("Dummy DInput Window"), 0,0,0,0,0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hWnd) { std::cerr << "Failed to create dummy window. Error: " << GetLastError() << std::endl; }
    return hWnd;
}

// --- Screen Capture Functions (using GDI BitBlt from your existing code) ---
bool InitializeScreenCapture(const std::string& window_title) {
    game_window_hwnd = FindWindowA(NULL, window_title.c_str());
    if (game_window_hwnd == NULL) {
        std::cerr << "ERROR: Could not find game window: " << window_title << std::endl;
        return false;
    }
    RECT window_rect;
    GetClientRect(game_window_hwnd, &window_rect);
    frame_width = window_rect.right - window_rect.left;
    frame_height = window_rect.bottom - window_rect.top;

    if (frame_width <= 0 || frame_height <= 0) {
        std::cerr << "ERROR: Invalid window dimensions for capture (" << frame_width << "x" << frame_height << ")" << std::endl;
        return false;
    }

    game_window_hdc = GetDC(game_window_hwnd);
    if (game_window_hdc == NULL) { std::cerr << "ERROR: Could not get DC of game window." << std::endl; return false; }
    compatible_hdc = CreateCompatibleDC(game_window_hdc);
    if (compatible_hdc == NULL) { std::cerr << "ERROR: Could not create compatible DC." << std::endl; ReleaseDC(game_window_hwnd, game_window_hdc); return false; }
    compatible_bitmap = CreateCompatibleBitmap(game_window_hdc, frame_width, frame_height);
    if (compatible_bitmap == NULL) { std::cerr << "ERROR: Could not create compatible bitmap." << std::endl; DeleteDC(compatible_hdc); ReleaseDC(game_window_hwnd, game_window_hdc); return false; }
    SelectObject(compatible_hdc, compatible_bitmap);

    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = frame_width;
    bi.biHeight = -frame_height;
    bi.biPlanes = 1;
    bi.biBitCount = 32; // Capture as BGRA
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    captured_frame.create(frame_height, frame_width, CV_8UC4); // BGRA for 32-bit capture

    std::cout << "Screen capture initialized for window: " << window_title << " (" << frame_width << "x" << frame_height << ")" << std::endl;
    return true;
}

void CaptureFrame() {
    if (!game_window_hwnd || !IsWindow(game_window_hwnd)) { 
        std::cerr << "Game window not found or closed. Attempting to reinitialize capture..." << std::endl;
        CleanupScreenCapture(); 
        if (!InitializeScreenCapture(CAPTURE_WINDOW_TITLE)) {
            captured_frame = cv::Mat::zeros(480, 640, CV_8UC3); 
            cv::putText(captured_frame, "Capture Error - Window Lost", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0,0,255), 2);
            return;
        }
    }
    
    RECT current_rect;
     GetClientRect(game_window_hwnd, ¤t_rect); // <--- 正确的调用
    int new_width = current_rect.right - current_rect.left;
    int new_height = current_rect.bottom - current_rect.top;

    if (new_width != frame_width || new_height != frame_height) {
        if (new_width > 0 && new_height > 0) {
            std::cout << "Window resized to " << new_width << "x" << new_height << ". Reinitializing capture..." << std::endl;
            CleanupScreenCapture();
            if (!InitializeScreenCapture(CAPTURE_WINDOW_TITLE)) { 
                captured_frame = cv::Mat::zeros(480, 640, CV_8UC3);
                cv::putText(captured_frame, "Capture Re-init Error", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0,0,255), 2);
                return;
            }
        } else {
            captured_frame = cv::Mat::zeros(480, 640, CV_8UC3);
            cv::putText(captured_frame, "Invalid Window Size", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0,0,255), 2);
            return;
        }
    }

    if (!BitBlt(compatible_hdc, 0, 0, frame_width, frame_height, game_window_hdc, 0, 0, SRCCOPY)) {
        return; 
    }
    if (captured_frame.empty() || captured_frame.cols != frame_width || captured_frame.rows != frame_height || captured_frame.type() != CV_8UC4) {
         captured_frame.create(frame_height, frame_width, CV_8UC4); 
    }
    GetDIBits(compatible_hdc, compatible_bitmap, 0, (UINT)frame_height, captured_frame.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
}

void CleanupScreenCapture() {
    if (compatible_bitmap) { DeleteObject(compatible_bitmap); compatible_bitmap = nullptr; }
    if (compatible_hdc) { DeleteDC(compatible_hdc); compatible_hdc = nullptr; }
    if (game_window_hwnd && game_window_hdc) { ReleaseDC(game_window_hwnd, game_window_hdc); game_window_hdc = nullptr; }
    std::cout << "Screen capture GDI objects cleaned up." << std::endl;
}

// --- FPS and Channel Info Drawing ---
void DrawFrameInfo(cv::Mat& frame_to_draw) {
    if (frame_to_draw.empty() || frame_to_draw.cols <=0 || frame_to_draw.rows <=0) {
        return;
    }

    int font_face = cv::FONT_HERSHEY_SIMPLEX;
    double font_scale = 0.5; 
    int thickness = 1;
    cv::Scalar text_color(0, 255, 0); 
    cv::Scalar bg_color(0,0,0); 

    frame_counter_fps++;
    auto current_time = std::chrono::steady_clock::now();
    double elapsed_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(current_time - last_fps_time_point).count();

    if (elapsed_seconds >= 1.0) {
        display_fps = static_cast<double>(frame_counter_fps) / elapsed_seconds;
        frame_counter_fps = 0;
        last_fps_time_point = current_time;
    }

    std::ostringstream fps_stream;
    fps_stream << "FPS: " << std::fixed << std::setprecision(1) << display_fps;
    std::string fps_text = fps_stream.str();

    std::ostringstream channels_stream;
    channels_stream << "CH1:" << g_joystickState.ch1
                    << " CH2:" << g_joystickState.ch2
                    << " CH3:" << g_joystickState.ch3
                    << " CH4:" << g_joystickState.ch4
                    << " CH8:" << g_joystickState.ch8;
    std::string channels_text = channels_stream.str();

    int baseline_fps = 0;
    cv::Size text_size_fps = cv::getTextSize(fps_text, font_face, font_scale, thickness, &baseline_fps);
    cv::Point fps_origin(frame_to_draw.cols - text_size_fps.width - 10, frame_to_draw.rows - 10);
    cv::putText(frame_to_draw, fps_text, fps_origin, font_face, font_scale, text_color, thickness, cv::LINE_AA);

    int baseline_channels = 0;
    cv::Size text_size_channels = cv::getTextSize(channels_text, font_face, font_scale, thickness, &baseline_channels);
    cv::Point channels_origin(10, frame_to_draw.rows - 10); 
    cv::putText(frame_to_draw, channels_text, channels_origin, font_face, font_scale, text_color, thickness, cv::LINE_AA);
}

// --- Joystick Polling and Virtual Gamepad Update ---
void PollJoystickAndMapToVirtual() {
    if (g_pJoystick == nullptr) return;
    HRESULT hr = g_pJoystick->Poll();
    if (FAILED(hr)) {
        hr = g_pJoystick->Acquire();
        while (hr == DIERR_INPUTLOST) hr = g_pJoystick->Acquire();
        if (FAILED(hr)) return;
    }
    DIJOYSTATE2 js;
    hr = g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js);
    if (FAILED(hr)) return;

    g_joystickState.ch1 = js.lX;
    g_joystickState.ch2 = js.lY;
    g_joystickState.ch3 = js.lZ;
    g_joystickState.ch4 = js.lRx;
    g_joystickState.ch5 = (js.rgbButtons[0] & 0x80) ? true : false;
    g_joystickState.ch6 = js.lRz;
    g_joystickState.ch7 = js.rglSlider[0];
    g_joystickState.ch8 = js.rglSlider[1];
    g_joystickState.ch9 = js.lRy;
    g_joystickState.ch10 = (js.rgbButtons[1] & 0x80) ? true : false;

    XUSB_REPORT_INIT(&g_virtualReport);
    auto scale_axis = [](long val) -> SHORT {
        double scaled_val = static_cast<double>(val) / 1000.0 * 32767.0;
        return static_cast<SHORT>(std::max(-32767.0, std::min(32767.0, scaled_val)));
    };
    auto scale_trigger = [](long val) -> BYTE {
        double scaled_val = (static_cast<double>(val) + 1000.0) / 2000.0 * 255.0;
        return static_cast<BYTE>(std::max(0.0, std::min(255.0, scaled_val)));
    };

    g_virtualReport.sThumbLX = scale_axis(g_joystickState.ch1);
    g_virtualReport.sThumbLY = scale_axis(g_joystickState.ch2 * -1);
    g_virtualReport.sThumbRX = scale_axis(g_joystickState.ch4);
    g_virtualReport.sThumbRY = scale_axis(g_joystickState.ch9 * -1);
    g_virtualReport.bLeftTrigger  = scale_trigger(g_joystickState.ch3);
    g_virtualReport.bRightTrigger = scale_trigger(g_joystickState.ch6);
    if (g_joystickState.ch5) g_virtualReport.wButtons |= XUSB_GAMEPAD_A;
    if (g_joystickState.ch10) g_virtualReport.wButtons |= XUSB_GAMEPAD_B;
    if (g_joystickState.ch7 > 500) g_virtualReport.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (g_joystickState.ch8 > 500) g_virtualReport.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;

    if (g_pVigem && g_pTargetX360) {
        vigem_target_x360_update(g_pVigem, g_pTargetX360, g_virtualReport);
    }
}


int main() {
    HWND hDummyWnd = CreateDummyWindow();
    if (!hDummyWnd) return 1;

    if (!InitializeDirectInput(hDummyWnd)) {
        CleanupDirectInput(); DestroyWindow(hDummyWnd); return 1;
    }
    if (!InitializeVirtualGamepad()) {
        CleanupDirectInput(); CleanupVirtualGamepad(); DestroyWindow(hDummyWnd); return 1;
    }
    if (!InitializeScreenCapture(CAPTURE_WINDOW_TITLE)) { 
        std::cerr << "Screen capture could not be initialized. Exiting." << std::endl;
        CleanupDirectInput(); CleanupVirtualGamepad(); DestroyWindow(hDummyWnd); return 1;
    }

    last_fps_time_point = std::chrono::steady_clock::now();
    cv::namedWindow(PREVIEW_WINDOW_NAME, cv::WINDOW_AUTOSIZE);

    std::cout << "All systems initialized. Reading input, capturing screen, and updating virtual gamepad..." << std::endl;
    std::cout << "Game Window: " << CAPTURE_WINDOW_TITLE << std::endl;
    std::cout << "Preview Window: " << PREVIEW_WINDOW_NAME << std::endl;
    std::cout << "Press ESC in preview window or close it to quit." << std::endl;

    while (true) {
        PollJoystickAndMapToVirtual();
        CaptureFrame(); 

        if (!captured_frame.empty()) {
            DrawFrameInfo(captured_frame); 
            cv::imshow(PREVIEW_WINDOW_NAME, captured_frame);
        } else {
            cv::Mat error_img = cv::Mat::zeros(480, 640, CV_8UC3);
            cv::putText(error_img, "Waiting for frame...", cv::Point(10,30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255,255,255), 2);
            cv::imshow(PREVIEW_WINDOW_NAME, error_img);
        }

        int key = cv::waitKey(1); 
        if (key == 27 || cv::getWindowProperty(PREVIEW_WINDOW_NAME, cv::WND_PROP_VISIBLE) < 1) {
            std::cout << "Exit requested via preview window." << std::endl;
            break;
        }
    }

    CleanupScreenCapture();
    CleanupDirectInput();
    CleanupVirtualGamepad();
    DestroyWindow(hDummyWnd);
    cv::destroyAllWindows();
    std::cout << "Exiting." << std::endl;
    return 0;
}