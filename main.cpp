#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <ViGEm/Client.h>
#include <algorithm> 
#include <chrono>    
#include <iomanip>   
#include <sstream>   

// OpenCV Includes
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

// DirectX Includes for Desktop Duplication
#include <dxgi1_2.h>
#include <d3d11.h>

// --- RemoteChannels struct ---
struct RemoteChannels {
    long ch1, ch2, ch3, ch4; bool ch5; long ch6, ch7, ch8, ch9; bool ch10;
    RemoteChannels() : ch1(0), ch2(0), ch3(0), ch4(0), ch5(false), ch6(0), ch7(0), ch8(0), ch9(0), ch10(false) {}
};

// --- Global Variables ---
LPDIRECTINPUT8        g_pDI = nullptr;
LPDIRECTINPUTDEVICE8  g_pJoystick = nullptr;
RemoteChannels        g_joystickState;
PVIGEM_CLIENT         g_pVigem = nullptr;
PVIGEM_TARGET         g_pTargetX360 = nullptr;
XUSB_REPORT           g_virtualReport;
ID3D11Device*           g_d3d11_device = nullptr;
ID3D11DeviceContext*    g_d3d11_device_context = nullptr;
IDXGIOutputDuplication* g_dxgi_output_duplication = nullptr;
DXGI_OUTPUT_DESC        g_dxgi_output_desc;
ID3D11Texture2D*        g_acquired_desktop_image = nullptr;
ID3D11Texture2D*        g_staging_texture = nullptr;
int                     g_monitor_capture_width = 0;
int                     g_monitor_capture_height = 0;
UINT                    g_output_number = 0; 
cv::Mat desktop_capture_full; 
cv::Mat display_frame;          
const std::string PREVIEW_WINDOW_NAME = "Desktop Capture Preview"; 
const int DISPLAY_WIDTH = 800; 
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
bool InitializeDesktopDuplication();
void CaptureFrameDXGI();
void CleanupDesktopDuplication(); // <--- 修正：添加原型声明
void DrawFrameInfo(cv::Mat& frame_to_draw);
void PollJoystickAndMapToVirtual();

// ... (所有函数的定义保持与我上一条回复中的代码一致) ...
// (InitializeDirectInput, CleanupDirectInput, InitializeVirtualGamepad, CleanupVirtualGamepad, CreateDummyWindow)
// (InitializeDesktopDuplication, CaptureFrameDXGI, CleanupDesktopDuplication DEFINITION)
// (DrawFrameInfo, PollJoystickAndMapToVirtual, main)

// --- DirectInput Functions ---
BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    HRESULT hr = g_pDI->CreateDevice(pdidInstance->guidInstance, &g_pJoystick, nullptr); 
    return SUCCEEDED(hr) ? DIENUM_STOP : DIENUM_CONTINUE;
}
BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* pdidoi, VOID* pContext) {
    if (pdidoi->dwType & DIDFT_AXIS) {
        DIPROPRANGE diprg; diprg.diph.dwSize = sizeof(DIPROPRANGE); diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        diprg.diph.dwHow = DIPH_BYID; diprg.diph.dwObj = pdidoi->dwType; diprg.lMin = -1000; diprg.lMax = +1000;
        if (g_pJoystick) g_pJoystick->SetProperty(DIPROP_RANGE, &diprg.diph); 
    } return DIENUM_CONTINUE;
}
bool InitializeDirectInput(HWND hWnd) {
    HRESULT hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&g_pDI, nullptr); 
    if (FAILED(hr)) { std::cerr << "DirectInput8Create failed!" << std::endl; return false; }
    hr = g_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, nullptr, DIEDFL_ATTACHEDONLY); 
    if (FAILED(hr) || !g_pJoystick) { std::cerr << "No joystick/gamepad found or EnumDevices failed!" << std::endl; if(g_pDI) g_pDI->Release(); g_pDI = nullptr; return false; } 
    hr = g_pJoystick->SetDataFormat(&c_dfDIJoystick2); 
    if (FAILED(hr)) { std::cerr << "SetDataFormat failed!" << std::endl; g_pJoystick->Release(); g_pJoystick = nullptr; g_pDI->Release(); g_pDI = nullptr; return false; }
    hr = g_pJoystick->SetCooperativeLevel(hWnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE); 
    if (FAILED(hr)) { std::cerr << "SetCooperativeLevel failed!" << std::endl; g_pJoystick->Release(); g_pJoystick = nullptr; g_pDI->Release(); g_pDI = nullptr; return false; }
    g_pJoystick->EnumObjects(EnumAxesCallback, (VOID*)hWnd, DIDFT_AXIS); g_pJoystick->Acquire(); 
    std::cout << "DirectInput Joystick initialized." << std::endl; return true;
}
void CleanupDirectInput() { 
    if (g_pJoystick) { g_pJoystick->Unacquire(); g_pJoystick->Release(); g_pJoystick = nullptr; } 
    if (g_pDI) { g_pDI->Release(); g_pDI = nullptr; } 
    std::cout << "DirectInput cleaned up." << std::endl; 
}
bool InitializeVirtualGamepad() {
    g_pVigem = vigem_alloc(); if (!g_pVigem) { std::cerr << "Failed to allocate ViGEm client!" << std::endl; return false; } 
    VIGEM_ERROR err = vigem_connect(g_pVigem); 
    if (!VIGEM_SUCCESS(err)) { std::cerr << "ViGEm Bus connection failed! Error code: " << err << std::endl; vigem_free(g_pVigem); g_pVigem = nullptr; return false; }
    g_pTargetX360 = vigem_target_x360_alloc(); if (!g_pTargetX360) { std::cerr << "Failed to allocate Xbox 360 target!" << std::endl; vigem_disconnect(g_pVigem); vigem_free(g_pVigem); g_pVigem = nullptr; return false; } 
    err = vigem_target_add(g_pVigem, g_pTargetX360); 
    if (!VIGEM_SUCCESS(err)) { std::cerr << "Failed to add Xbox 360 target to ViGEm Bus! Error code: " << err << std::endl; vigem_target_free(g_pTargetX360); g_pTargetX360 = nullptr; vigem_disconnect(g_pVigem); vigem_free(g_pVigem); g_pVigem = nullptr; return false; }
    XUSB_REPORT_INIT(&g_virtualReport); std::cout << "Virtual Xbox 360 controller initialized." << std::endl; return true; 
}
void CleanupVirtualGamepad() { 
    if (g_pVigem && g_pTargetX360) { vigem_target_remove(g_pVigem, g_pTargetX360); vigem_target_free(g_pTargetX360); g_pTargetX360 = nullptr; } 
    if (g_pVigem) { vigem_disconnect(g_pVigem); vigem_free(g_pVigem); g_pVigem = nullptr; } 
    std::cout << "Virtual gamepad cleaned up." << std::endl; 
}
HWND CreateDummyWindow() {
    WNDCLASS wc = {0}; wc.lpfnWndProc = DefWindowProc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = TEXT("DummyDInputWindowForJoystick");
    if (!RegisterClass(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { std::cerr << "Failed to register dummy window class." << std::endl; return nullptr; }
    HWND hWnd = CreateWindow(wc.lpszClassName, TEXT("Dummy DInput Window"), 0,0,0,0,0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hWnd) { std::cerr << "Failed to create dummy window." << std::endl; } return hWnd;
}

// --- Desktop Duplication API Functions ---
bool InitializeDesktopDuplication() {
    HRESULT hr = S_OK;
    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION, &g_d3d11_device, nullptr, &g_d3d11_device_context);
    if (FAILED(hr)) { std::cerr << "D3D11CreateDevice failed. HR: " << std::hex << hr << std::endl; return false; }

    IDXGIDevice* dxgi_device = nullptr;
    hr = g_d3d11_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
    if (FAILED(hr)) { std::cerr << "QueryInterface for IDXGIDevice failed. HR: " << std::hex << hr << std::endl; CleanupDesktopDuplication(); return false; }

    IDXGIAdapter* dxgi_adapter = nullptr;
    hr = dxgi_device->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&dxgi_adapter));
    dxgi_device->Release(); 
    if (FAILED(hr)) { std::cerr << "GetParent for IDXGIAdapter failed. HR: " << std::hex << hr << std::endl; CleanupDesktopDuplication(); return false; }

    IDXGIOutput* dxgi_output = nullptr;
    hr = dxgi_adapter->EnumOutputs(g_output_number, &dxgi_output);
    dxgi_adapter->Release(); 
    if (FAILED(hr)) { std::cerr << "EnumOutputs failed for output " << g_output_number << ". HR: " << std::hex << hr << std::endl; CleanupDesktopDuplication(); return false; }

    hr = dxgi_output->GetDesc(&g_dxgi_output_desc);
    if (FAILED(hr)) { std::cerr << "GetDesc for DXGI_OUTPUT_DESC failed. HR: " << std::hex << hr << std::endl; dxgi_output->Release(); CleanupDesktopDuplication(); return false; }

    IDXGIOutput1* dxgi_output1 = nullptr;
    hr = dxgi_output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&dxgi_output1));
    dxgi_output->Release(); 
    if (FAILED(hr)) { std::cerr << "QueryInterface for IDXGIOutput1 failed. HR: " << std::hex << hr << std::endl; CleanupDesktopDuplication(); return false; }

    hr = dxgi_output1->DuplicateOutput(g_d3d11_device, &g_dxgi_output_duplication);
    dxgi_output1->Release(); 
    if (FAILED(hr)) {
        std::cerr << "DuplicateOutput failed. HR: " << std::hex << hr << std::endl;
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) { std::cerr << "Desktop Duplication not currently available." << std::endl; } 
        else if (hr == DXGI_ERROR_UNSUPPORTED) { std::cerr << "Desktop Duplication not supported." << std::endl; }
        CleanupDesktopDuplication(); return false;
    }

    g_monitor_capture_width = g_dxgi_output_desc.DesktopCoordinates.right - g_dxgi_output_desc.DesktopCoordinates.left;
    g_monitor_capture_height = g_dxgi_output_desc.DesktopCoordinates.bottom - g_dxgi_output_desc.DesktopCoordinates.top;

    D3D11_TEXTURE2D_DESC staging_desc; ZeroMemory(&staging_desc, sizeof(staging_desc));
    staging_desc.Width = g_monitor_capture_width; 
    staging_desc.Height = g_monitor_capture_height; 
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1; staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; 
    staging_desc.SampleDesc.Count = 1; staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = g_d3d11_device->CreateTexture2D(&staging_desc, nullptr, &g_staging_texture);
    if (FAILED(hr)) { std::cerr << "CreateTexture2D for staging failed. HR: " << std::hex << hr << std::endl; CleanupDesktopDuplication(); return false; }

    desktop_capture_full.create(g_monitor_capture_height, g_monitor_capture_width, CV_8UC4);

    std::cout << "Desktop Duplication initialized for output " << g_output_number << " (Full monitor: " 
              << g_monitor_capture_width << "x" << g_monitor_capture_height << ")" << std::endl;
    return true;
}

void CaptureFrameDXGI() {
    if (!g_dxgi_output_duplication || !g_d3d11_device_context || !g_staging_texture) {
        CleanupDesktopDuplication(); 
        if (!InitializeDesktopDuplication()) { /* desktop_capture_full remains empty */ } 
        return;
    }

    DXGI_OUTDUPL_FRAME_INFO frame_info; IDXGIResource* desktop_resource = nullptr; HRESULT hr;
    if (g_acquired_desktop_image) { g_acquired_desktop_image->Release(); g_acquired_desktop_image = nullptr; }
    
    hr = g_dxgi_output_duplication->AcquireNextFrame(16, &frame_info, &desktop_resource); 
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) { return; } 
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) { 
            CleanupDesktopDuplication(); InitializeDesktopDuplication(); 
        } return; 
    }

    hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&g_acquired_desktop_image));
    desktop_resource->Release(); 
    if (FAILED(hr)) { g_dxgi_output_duplication->ReleaseFrame(); return; }

    g_d3d11_device_context->CopyResource(g_staging_texture, g_acquired_desktop_image);
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    hr = g_d3d11_device_context->Map(g_staging_texture, 0, D3D11_MAP_READ, 0, &mapped_resource);
    if (FAILED(hr)) { g_dxgi_output_duplication->ReleaseFrame(); return; }

    if (desktop_capture_full.empty() || desktop_capture_full.cols != g_monitor_capture_width || desktop_capture_full.rows != g_monitor_capture_height || desktop_capture_full.type() != CV_8UC4) {
        desktop_capture_full.create(g_monitor_capture_height, g_monitor_capture_width, CV_8UC4);
    }
    unsigned char* source_ptr = static_cast<unsigned char*>(mapped_resource.pData);
    unsigned char* dest_ptr = desktop_capture_full.data;
    UINT pitch = mapped_resource.RowPitch;
    for (int y = 0; y < g_monitor_capture_height; ++y) {
        memcpy(dest_ptr + y * desktop_capture_full.step, source_ptr + y * pitch, g_monitor_capture_width * 4);
    }
    g_d3d11_device_context->Unmap(g_staging_texture, 0);
    g_dxgi_output_duplication->ReleaseFrame();
}

void CleanupDesktopDuplication() { // This is the DEFINITION
    if (g_acquired_desktop_image) { g_acquired_desktop_image->Release(); g_acquired_desktop_image = nullptr; }
    if (g_staging_texture) { g_staging_texture->Release(); g_staging_texture = nullptr; }
    if (g_dxgi_output_duplication) { g_dxgi_output_duplication->Release(); g_dxgi_output_duplication = nullptr; }
    if (g_d3d11_device_context) { g_d3d11_device_context->Release(); g_d3d11_device_context = nullptr; }
    if (g_d3d11_device) { g_d3d11_device->Release(); g_d3d11_device = nullptr; }
    std::cout << "Desktop Duplication cleaned up." << std::endl;
}

void DrawFrameInfo(cv::Mat& frame_to_draw) {
    if (frame_to_draw.empty() || frame_to_draw.cols <=0 || frame_to_draw.rows <=0) { return; }
    int font_face = cv::FONT_HERSHEY_SIMPLEX; double font_scale = 0.5; int thickness = 1;
    cv::Scalar text_color(0, 255, 0);
    frame_counter_fps++; auto current_time = std::chrono::steady_clock::now();
    double elapsed_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(current_time - last_fps_time_point).count();
    if (elapsed_seconds >= 1.0) { display_fps = static_cast<double>(frame_counter_fps) / elapsed_seconds; frame_counter_fps = 0; last_fps_time_point = current_time; }
    std::ostringstream fps_stream; fps_stream << "FPS: " << std::fixed << std::setprecision(1) << display_fps; std::string fps_text = fps_stream.str();
    std::ostringstream channels_stream; 
    channels_stream << "CH1:" << g_joystickState.ch1 << " CH2:" << g_joystickState.ch2 << " CH3:" << g_joystickState.ch3 << " CH4:" << g_joystickState.ch4 << " CH8:" << g_joystickState.ch8;
    std::string channels_text = channels_stream.str();
    int baseline_fps = 0; cv::Size text_size_fps = cv::getTextSize(fps_text, font_face, font_scale, thickness, &baseline_fps);
    cv::Point fps_origin(frame_to_draw.cols - text_size_fps.width - 10, frame_to_draw.rows - 10);
    cv::putText(frame_to_draw, fps_text, fps_origin, font_face, font_scale, text_color, thickness, cv::LINE_AA);
    int baseline_channels = 0; cv::Size text_size_channels = cv::getTextSize(channels_text, font_face, font_scale, thickness, &baseline_channels);
    cv::Point channels_origin(10, frame_to_draw.rows - 10);
    cv::putText(frame_to_draw, channels_text, channels_origin, font_face, font_scale, text_color, thickness, cv::LINE_AA);
}
void PollJoystickAndMapToVirtual() {
    if (!g_pJoystick) return; HRESULT hr = g_pJoystick->Poll(); 
    if (FAILED(hr)) { hr = g_pJoystick->Acquire(); while (hr == DIERR_INPUTLOST) hr = g_pJoystick->Acquire(); if (FAILED(hr)) return; } 
    DIJOYSTATE2 js; hr = g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js); if (FAILED(hr)) return; 
    g_joystickState.ch1 = js.lX; g_joystickState.ch2 = js.lY; g_joystickState.ch3 = js.lZ; g_joystickState.ch4 = js.lRx; 
    g_joystickState.ch5 = (js.rgbButtons[0] & 0x80); g_joystickState.ch6 = js.lRz; g_joystickState.ch7 = js.rglSlider[0];
    g_joystickState.ch8 = js.rglSlider[1]; g_joystickState.ch9 = js.lRy; g_joystickState.ch10 = (js.rgbButtons[1] & 0x80);
    XUSB_REPORT_INIT(&g_virtualReport);
    auto scale_axis = [](long val) -> SHORT { double s = (double)val / 1000.0 * 32767.0; return (SHORT)std::max(-32767., std::min(32767., s)); };
    auto scale_trigger = [](long val) -> BYTE { double s = ((double)val + 1000.0) / 2000.0 * 255.0; return (BYTE)std::max(0., std::min(255., s)); };
    g_virtualReport.sThumbLX = scale_axis(g_joystickState.ch1); g_virtualReport.sThumbLY = scale_axis(g_joystickState.ch2 * -1);
    g_virtualReport.sThumbRX = scale_axis(g_joystickState.ch4); g_virtualReport.sThumbRY = scale_axis(g_joystickState.ch9 * -1);
    g_virtualReport.bLeftTrigger  = scale_trigger(g_joystickState.ch3); g_virtualReport.bRightTrigger = scale_trigger(g_joystickState.ch6);
    if (g_joystickState.ch5) g_virtualReport.wButtons |= XUSB_GAMEPAD_A; if (g_joystickState.ch10) g_virtualReport.wButtons |= XUSB_GAMEPAD_B;
    if (g_joystickState.ch7 > 500) g_virtualReport.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER; if (g_joystickState.ch8 > 500) g_virtualReport.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (g_pVigem && g_pTargetX360) vigem_target_x360_update(g_pVigem, g_pTargetX360, g_virtualReport); 
}

int main() {
    HWND hDummyWnd = CreateDummyWindow();
    if (!hDummyWnd) return 1;

    if (!InitializeDirectInput(hDummyWnd)) { CleanupDirectInput(); DestroyWindow(hDummyWnd); return 1; }
    if (!InitializeVirtualGamepad()) { CleanupDirectInput(); CleanupVirtualGamepad(); DestroyWindow(hDummyWnd); return 1; }
    if (!InitializeDesktopDuplication()) { CleanupDirectInput(); CleanupVirtualGamepad(); CleanupDesktopDuplication(); DestroyWindow(hDummyWnd); return 1; }
    
    last_fps_time_point = std::chrono::steady_clock::now();
    cv::namedWindow(PREVIEW_WINDOW_NAME, cv::WINDOW_AUTOSIZE); 

    std::cout << "All systems initialized. Using Desktop Duplication API." << std::endl;
    std::cout << "Displaying full desktop capture resized to width: " << DISPLAY_WIDTH << std::endl;
    std::cout << "Press ESC in preview window or close it to quit." << std::endl;

    while (true) {
        PollJoystickAndMapToVirtual();
        CaptureFrameDXGI(); 

        if (!desktop_capture_full.empty()) {
            double aspect_ratio = (double)desktop_capture_full.cols / (double)desktop_capture_full.rows;
            int display_height = static_cast<int>(DISPLAY_WIDTH / aspect_ratio);
            if (display_height <= 0) { 
                 display_height = static_cast<int>(DISPLAY_WIDTH * ( (double)g_monitor_capture_height / g_monitor_capture_width));
                 if(display_height <= 0) display_height = DISPLAY_WIDTH * 9 / 16; 
            }
            cv::resize(desktop_capture_full, display_frame, cv::Size(DISPLAY_WIDTH, display_height));
            DrawFrameInfo(display_frame); 
            cv::imshow(PREVIEW_WINDOW_NAME, display_frame);
        } else {
            cv::Mat waiting_img = cv::Mat::zeros(DISPLAY_WIDTH * 9 / 16, DISPLAY_WIDTH, CV_8UC3); 
            cv::putText(waiting_img, "Waiting for desktop frame...", cv::Point(10,30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255,255,255), 2);
            cv::imshow(PREVIEW_WINDOW_NAME, waiting_img);
        }

        int key = cv::waitKey(1);
        if (key == 27 || cv::getWindowProperty(PREVIEW_WINDOW_NAME, cv::WND_PROP_VISIBLE) < 1) {
            std::cout << "Exit requested via preview window." << std::endl;
            break;
        }
    }

    CleanupDesktopDuplication(); 
    CleanupDirectInput();
    CleanupVirtualGamepad();
    DestroyWindow(hDummyWnd);
    cv::destroyAllWindows();
    std::cout << "Exiting." << std::endl;
    return 0;
}