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
#include <opencv2/tracking.hpp>

// DirectX Includes for Desktop Duplication
#include <dxgi1_2.h>
#include <d3d11.h>

// --- RemoteChannels struct ---
struct RemoteChannels {
    long ch1, ch2, ch3, ch4; bool ch5; long ch6, ch7, ch8, ch9; bool ch10;
    RemoteChannels() : ch1(0), ch2(0), ch3(0), ch4(0), ch5(false), ch6(0), ch7(0), ch8(0), ch9(0), ch10(false) {}
};

struct TrackingOffset {
    int dx; // 水平偏移量 (x-direction)
    int dy; // 垂直偏移量 (y-direction)
    bool is_valid; // 标记当前偏移量是否有效 (例如，跟踪成功时为true)

    TrackingOffset() : dx(0), dy(0), is_valid(false) {} // 默认构造函数
};

// --- Global Variables ---
LPDIRECTINPUT8        g_pDI = nullptr;
LPDIRECTINPUTDEVICE8  g_pJoystick = nullptr;
RemoteChannels        g_joystickState;
RemoteChannels        ai_joystickState;
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
cv::Mat track_frame;          
const std::string PREVIEW_WINDOW_NAME = "Desktop Capture Preview"; 
const int DISPLAY_WIDTH = 800; 
std::chrono::steady_clock::time_point last_fps_time_point;
int frame_counter_fps = 0;
double display_fps = 0.0;

BOOL flag_track=0;
cv::Ptr<cv::Tracker> tracker; // OpenCV跟踪器对象
cv::Rect tracked_bbox;      // 存储跟踪到的边界框
bool tracker_initialized = false;
TrackingOffset g_current_tracking_offset;

// --- PID Controller Parameters and State (示例) ---
// 您需要为每个轴 (dx, dy) 分别设置这些参数
// 水平方向 (控制 ch1 来修正 dx)
double pid_kp_dx = 30;  // 比例增益 (Proportional gain) - 需要仔细调整
double pid_ki_dx = 0.01; // 积分增益 (Integral gain) - 需要仔细调整
double pid_kd_dx = 0.1;  // 微分增益 (Derivative gain) - 需要仔细调整
double pid_integral_dx = 0;
double pid_previous_error_dx = 0;

// 垂直方向 (控制 ch3 来修正 dy)
double pid_kp_dy = 50;  // 比例增益
double pid_ki_dy = 0.1; // 积分增益
double pid_kd_dy = 0;  // 微分增益
double pid_integral_dy = 0;
double pid_previous_error_dy = 0;

// PID输出限幅 (防止输出过大的控制信号，对应摇杆的 -1000 到 1000)
const long PID_OUTPUT_MIN = -1000;
const long PID_OUTPUT_MAX = 1000;

const int PLOT_HISTORY_LENGTH = 200; // 存储多少个历史数据点
std::deque<long> pid_ch1_history;      // 存储ch1的历史值
std::deque<long> pid_ch3_history;      // 存储ch3的历史值

// 绘图区域参数 (可以根据 display_frame 的大小调整)
const int PLOT_AREA_HEIGHT = 100; // 每条曲线的绘图区域高度
const int PLOT_AREA_WIDTH = PLOT_HISTORY_LENGTH; // 绘图区域宽度与历史点数一致
const int PLOT_MARGIN = 10;       // 绘图区域的边距
// --- PID End ---

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
//void PollJoystickAndMapToVirtual();

void is_track_on(void);
void get_track_frame(void);
void PollPhysicalJoystick();
void MapToVirtualJoystick();
void get_track_frame_and_init_tracker(const cv::Mat& current_display_frame); // 修改或新增
void update_tracker_and_draw(cv::Mat& current_display_frame); // 新增
void ControlAircraftWithPID();
void DrawPIDCurves(cv::Mat& frame_to_draw_on);
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
    if (frame_to_draw.empty() || frame_to_draw.cols <= 0 || frame_to_draw.rows <= 0) { return; }
    int font_face = cv::FONT_HERSHEY_SIMPLEX;
    double font_scale = 0.5;
    int thickness = 1;
    cv::Scalar text_color_info(0, 255, 0); // Green for FPS and Channel info
    cv::Scalar text_color_offset(0, 0, 255); // Red for Offset info (will be used later)
    cv::Scalar crosshair_color(0, 0, 255); // Red for Crosshair

    // --- FPS Calculation and Display (existing code) ---
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
    int baseline_fps = 0;
    cv::Size text_size_fps = cv::getTextSize(fps_text, font_face, font_scale, thickness, &baseline_fps);
    cv::Point fps_origin(frame_to_draw.cols - text_size_fps.width - 10, frame_to_draw.rows - 10);
    cv::putText(frame_to_draw, fps_text, fps_origin, font_face, font_scale, text_color_info, thickness, cv::LINE_AA);

    // --- Channel Data Display (existing code) ---
    std::ostringstream channels_stream;
    channels_stream << "CH1:" << g_joystickState.ch1 << " CH2:" << g_joystickState.ch2 << " CH3:" << g_joystickState.ch3 << " CH4:" << g_joystickState.ch4 << " CH8:" << g_joystickState.ch8;
    std::string channels_text = channels_stream.str();
    int baseline_channels = 0;
    cv::Size text_size_channels = cv::getTextSize(channels_text, font_face, font_scale, thickness, &baseline_channels);
    cv::Point channels_origin(10, frame_to_draw.rows - 10);
    cv::putText(frame_to_draw, channels_text, channels_origin, font_face, font_scale, text_color_info, thickness, cv::LINE_AA);

    // --- Overlay track_frame (existing code) ---
    if (flag_track == 1 && !track_frame.empty()) {
        int track_width = track_frame.cols;
        int track_height = track_frame.rows;
        if (track_width <= frame_to_draw.cols && track_height <= frame_to_draw.rows) {
            cv::Rect roi_top_right(frame_to_draw.cols - track_width - 5, 5, track_width, track_height);
            if (roi_top_right.x < 0) roi_top_right.x = 0;
            if (roi_top_right.y < 0) roi_top_right.y = 0;
            if (roi_top_right.x + roi_top_right.width > frame_to_draw.cols) {
                roi_top_right.width = frame_to_draw.cols - roi_top_right.x;
            }
            if (roi_top_right.y + roi_top_right.height > frame_to_draw.rows) {
                roi_top_right.height = frame_to_draw.rows - roi_top_right.y;
            }

            if (roi_top_right.width > 0 && roi_top_right.height > 0) {
                cv::Mat destination_roi = frame_to_draw(roi_top_right);
                cv::Mat track_frame_to_copy;
                if (track_frame.cols != roi_top_right.width || track_frame.rows != roi_top_right.height) {
                    cv::resize(track_frame, track_frame_to_copy, cv::Size(roi_top_right.width, roi_top_right.height));
                } else {
                    track_frame_to_copy = track_frame;
                }
                
                if (track_frame_to_copy.type() == destination_roi.type()) {
                    track_frame_to_copy.copyTo(destination_roi);
                } else if (track_frame_to_copy.type() == CV_8UC4 && destination_roi.type() == CV_8UC3) {
                    cv::Mat temp_bgr;
                    cv::cvtColor(track_frame_to_copy, temp_bgr, cv::COLOR_BGRA2BGR);
                    temp_bgr.copyTo(destination_roi);
                } else if (track_frame_to_copy.type() == CV_8UC3 && destination_roi.type() == CV_8UC4) {
                     cv::Mat temp_bgra;
                    cv::cvtColor(track_frame_to_copy, temp_bgra, cv::COLOR_BGR2BGRA);
                    temp_bgra.copyTo(destination_roi);
                } else {
                    std::cerr << "Warning: track_frame and destination_roi type mismatch for overlay." << std::endl;
                }
                cv::rectangle(frame_to_draw, roi_top_right, cv::Scalar(255, 0, 0), 1); 
            }
        } else {
            std::cerr << "Warning: track_frame is too large to be overlaid on display_frame." << std::endl;
        }
    }

    // --- Draw a red crosshair in the center (existing code) ---
    int crosshair_size = 40; 
    int line_length = crosshair_size / 2; 
    int crosshair_thickness = 1; 
    cv::Point center_point(frame_to_draw.cols / 2, frame_to_draw.rows / 2);
    cv::line(frame_to_draw, cv::Point(center_point.x - line_length, center_point.y), cv::Point(center_point.x + line_length, center_point.y), crosshair_color, crosshair_thickness, cv::LINE_AA);
    cv::line(frame_to_draw, cv::Point(center_point.x, center_point.y - line_length), cv::Point(center_point.x, center_point.y + line_length), crosshair_color, crosshair_thickness, cv::LINE_AA);
    
    // --- NEW MODIFICATION START: Display Tracking Offset at the top ---
    if (g_current_tracking_offset.is_valid) { // Only display if offset is valid
        std::ostringstream offset_stream;
        offset_stream << "Offset DX: " << g_current_tracking_offset.dx 
                      << " DY: " << g_current_tracking_offset.dy;
        std::string offset_text = offset_stream.str();

        int baseline_offset = 0;
        cv::Size text_size_offset = cv::getTextSize(offset_text, font_face, font_scale, thickness, &baseline_offset);
        
        // Position at the top-left
        cv::Point offset_origin(10, 20); // x=10, y=20 (from top)

        // Or, position at the top-center:
        // cv::Point offset_origin((frame_to_draw.cols - text_size_offset.width) / 2, 20);

        cv::putText(frame_to_draw, offset_text, offset_origin, font_face, font_scale, text_color_offset, thickness, cv::LINE_AA);
    }
    // --- NEW MODIFICATION END ---

    if (flag_track == 1) { // 只在跟踪模式下绘制PID曲线
        DrawPIDCurves(frame_to_draw);
    }
} // DrawFrameInfo 函数结束

void PollPhysicalJoystick() {
    if (!g_pJoystick) {
        // std::cerr << "Physical joystick not initialized!" << std::endl; // 可选的错误提示
        return;
    }

    HRESULT hr = g_pJoystick->Poll();
    if (FAILED(hr)) {
        hr = g_pJoystick->Acquire();
        while (hr == DIERR_INPUTLOST) {
            hr = g_pJoystick->Acquire();
        }
        if (FAILED(hr)) {
            // std::cerr << "Failed to acquire physical joystick." << std::endl; // 可选
            return;
        }
    }

    DIJOYSTATE2 js;
    hr = g_pJoystick->GetDeviceState(sizeof(DIJOYSTATE2), &js);
    if (FAILED(hr)) {
        // std::cerr << "Failed to get physical joystick state." << std::endl; // 可选
        return;
    }

    // 将物理遥控器数据存入 g_joystickState
    g_joystickState.ch1 = js.lX;
    g_joystickState.ch2 = js.lY;
    g_joystickState.ch3 = js.lZ;
    g_joystickState.ch4 = js.lRx;
    g_joystickState.ch5 = (js.rgbButtons[0] & 0x80); // Button 1
    g_joystickState.ch6 = js.lRz;
    g_joystickState.ch7 = js.rglSlider[1]; // 假设 Slider 1 对应 DIJOYSTATE2 的 rglSlider[1]
    g_joystickState.ch8 = js.rglSlider[0]; // 假设 Slider 0 对应 DIJOYSTATE2 的 rglSlider[0]
    g_joystickState.ch9 = js.lRy;
    g_joystickState.ch10 = (js.rgbButtons[1] & 0x80); // Button 2
}

// 函数二：将 g_joystickState 的值映射到虚拟摇杆
void MapToVirtualJoystick() {
    if (!g_pVigem || !g_pTargetX360) {
        // std::cerr << "Virtual joystick not initialized!" << std::endl; // 可选
        return;
    }

    XUSB_REPORT_INIT(&g_virtualReport); // 每次映射前都初始化报告

    // 定义缩放函数 (可以放在函数内部，或者作为辅助全局函数/lambda)
    auto scale_axis = [](long val) -> SHORT {
        double s = static_cast<double>(val) / 1000.0 * 32767.0;
        return static_cast<SHORT>(std::max(-32767.0, std::min(32767.0, s)));
    };
    auto scale_trigger = [](long val) -> BYTE {
        double s = (static_cast<double>(val) + 1000.0) / 2000.0 * 255.0; // Maps -1000..1000 to 0..255
        return static_cast<BYTE>(std::max(0.0, std::min(255.0, s)));
    };

    if (flag_track == 0) {
        // 当 flag_track 为 0 时，直接将 g_joystickState 的值映射到虚拟摇杆
        g_virtualReport.sThumbLX = scale_axis(g_joystickState.ch4);      // X-Axis
        g_virtualReport.sThumbLY = scale_axis(g_joystickState.ch3);  // Y-Axis (XInput Y通常反向)
        g_virtualReport.sThumbRX = scale_axis(g_joystickState.ch1);      // X-Rotation
        g_virtualReport.sThumbRY = scale_axis(g_joystickState.ch2);  // Y-Rotation (XInput Y通常反向)

        g_virtualReport.bLeftTrigger  = scale_trigger(g_joystickState.ch9); // Z-Axis
        g_virtualReport.bRightTrigger = scale_trigger(g_joystickState.ch6); // Z-Rotation

        if (g_joystickState.ch5)  g_virtualReport.wButtons |= XUSB_GAMEPAD_A;             // Button 1
        if (g_joystickState.ch10) g_virtualReport.wButtons |= XUSB_GAMEPAD_B;             // Button 2
        
        // ch7 和 ch8 (滑块) 映射到肩键 (示例逻辑)
        if (g_joystickState.ch7 > 500) g_virtualReport.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
        if (g_joystickState.ch8 > 500) g_virtualReport.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;

    } else if (flag_track == 1) {
        // 当 flag_track 为 1 时，这里是您未来实现AI控制逻辑的地方
        // 目前，我们可以先做一个占位符，例如：
        // 1. AI完全接管：AI计算所有轴和按钮的值，然后填充 g_virtualReport
        // 2. AI辅助：AI修改 g_joystickState 中的某些值，然后再进行标准映射
        // 3. AI只控制部分：例如，AI控制油门和方向，按钮由物理手柄决定

        // --- 示例：AI占位符 - 让AI控制左摇杆，其他来自物理手柄 ---
        // (这是一个非常简单的示例，您需要替换为实际的AI逻辑)
        
        // 假设AI输出以下值 (这些值应该由您的AI算法计算得出)
        //long ai_lx = 0;    // AI控制的左摇杆X (范围 -1000 到 1000)
        //long ai_ly = 500;  // AI控制的左摇杆Y (范围 -1000 到 1000)
        
        // 如果您的AI直接输出XInput兼容的值，就不需要scale_axis了
        // g_virtualReport.sThumbLX = ai_calculated_lx_xinput_value;
        // g_virtualReport.sThumbLY = ai_calculated_ly_xinput_value;
        
        //g_virtualReport.sThumbLX = scale_axis(ai_lx);
        //g_virtualReport.sThumbLY = scale_axis(ai_ly * -1); // 假设AI输出也需要Y轴反转

        g_virtualReport.sThumbLX = scale_axis(ai_joystickState.ch4);      // X-Axis
        g_virtualReport.sThumbLY = scale_axis(ai_joystickState.ch3);  // Y-Axis (XInput Y通常反向)
        g_virtualReport.sThumbRX = scale_axis(ai_joystickState.ch1);      // X-Rotation
        g_virtualReport.sThumbRY = scale_axis(ai_joystickState.ch2);  // Y-Rotation (XInput Y通常反向)    
        
        // 您也可以在这里根据AI的输出来设置按钮
        // bool ai_button_A_pressed = your_ai_logic_for_button_A();
        // if (ai_button_A_pressed) g_virtualReport.wButtons |= XUSB_GAMEPAD_A;

        std::cout << "AI Control Active (Placeholder)" << std::endl; // 提示AI控制已激活
    }
    // else: 可以处理其他 flag_track 值的情况，如果需要

    // 更新虚拟手柄状态
    vigem_target_x360_update(g_pVigem, g_pTargetX360, g_virtualReport);
}

void is_track_on(void)
{
    if(g_joystickState.ch8>-1000)flag_track = 1;
    else {
        flag_track = 0;
        track_frame.release();
    }
}

void get_track_frame(void) {
    // 检查标志位，并且确保 track_frame 当前是空的 (避免重复截取或覆盖)
    // 同时也要确保 display_frame 不是空的，并且足够大以进行截取
    if (flag_track == 1 && track_frame.empty() && !display_frame.empty()) {
        int roi_width = 32;
        int roi_height = 32;

        // 检查 display_frame 是否足够大以截取 32x32 的区域
        if (display_frame.cols >= roi_width && display_frame.rows >= roi_height) {
            // 计算中心位置的左上角坐标 (cx, cy)
            int center_x = display_frame.cols / 2;
            int center_y = display_frame.rows / 2;

            // 计算ROI (Region of Interest) 的左上角坐标
            int roi_x = center_x - (roi_width / 2);
            int roi_y = center_y - (roi_height / 2);

            // 确保ROI的坐标不会超出 display_frame 的边界
            // (虽然对于中心截取且display_frame足够大的情况，通常不会超出，但加上检查更安全)
            if (roi_x < 0) roi_x = 0;
            if (roi_y < 0) roi_y = 0;
            if (roi_x + roi_width > display_frame.cols) {
                // 如果ROI超出右边界，可以调整roi_x或roi_width，或者不进行截取
                // 这里简单处理：如果调整后宽度不足，则不截取
                roi_width = display_frame.cols - roi_x;
                if (roi_width < 32) { // 如果调整后宽度不足期望值，可以选择不截取
                     std::cerr << "Warning: display_frame too small on width after boundary adjustment for track_frame." << std::endl;
                     return;
                }
            }
            if (roi_y + roi_height > display_frame.rows) {
                // 如果ROI超出下边界，类似处理
                roi_height = display_frame.rows - roi_y;
                 if (roi_height < 32) { // 如果调整后高度不足期望值
                     std::cerr << "Warning: display_frame too small on height after boundary adjustment for track_frame." << std::endl;
                     return;
                }
            }
            
            // 如果调整后的 roi_width 或 roi_height 不是32了，您可能需要重新考虑逻辑
            // 这里我们假设仍然尝试截取，即使它可能不是严格的32x32了（如果display_frame非常小）
            // 或者，您可以坚持必须是32x32，如果不是则不截取

            // 定义ROI矩形
            cv::Rect roi(roi_x, roi_y, roi_width, roi_height);

            // 从 display_frame 截取ROI，并克隆到 track_frame
            // 使用 clone() 是为了确保 track_frame 拥有自己的数据副本，
            // 而不是仅仅指向 display_frame 的一部分内存。
            try {
                track_frame = display_frame(roi).clone();
                std::cout << "Track frame (32x32) captured from display_frame center." << std::endl;
            } catch (const cv::Exception& e) {
                std::cerr << "OpenCV exception while creating ROI for track_frame: " << e.what() << std::endl;
                std::cerr << "ROI params: x=" << roi_x << ", y=" << roi_y << ", w=" << roi_width << ", h=" << roi_height << std::endl;
                std::cerr << "Display_frame size: " << display_frame.cols << "x" << display_frame.rows << std::endl;
                track_frame.release(); // 确保出错时 track_frame 是空的
            }

        } else {
            std::cerr << "Warning: display_frame is smaller than 32x32, cannot capture track_frame." << std::endl;
            // 确保 track_frame 保持为空
            track_frame.release();
        }
    } else if (flag_track == 1 && !track_frame.empty()) {
        // 如果标志位为1但track_frame已经有内容了，可以选择什么都不做，
        // 或者根据需求决定是否要更新它。当前逻辑是只有当它是空的时候才截取。
        // std::cout << "Track frame already exists, not capturing new one." << std::endl;
    }
}

void get_track_frame_and_init_tracker(const cv::Mat& current_display_frame_orig) { // Renamed param for clarity
    if (flag_track == 1 && !tracker_initialized && !current_display_frame_orig.empty()) {
        int roi_width = 32; 
        int roi_height = 32;

        if (current_display_frame_orig.cols >= roi_width && current_display_frame_orig.rows >= roi_height) {
            
            // --- MODIFICATION START: Ensure 3-channel image for tracker ---
            cv::Mat frame_for_tracker_input;
            if (current_display_frame_orig.channels() == 4) {
                cv::cvtColor(current_display_frame_orig, frame_for_tracker_input, cv::COLOR_BGRA2BGR);
            } else if (current_display_frame_orig.channels() == 3) {
                frame_for_tracker_input = current_display_frame_orig; // Already 3 channels, can use directly (or clone if modification is a concern)
            } else {
                std::cerr << "Error: display_frame for tracker init has " << current_display_frame_orig.channels() 
                          << " channels. Expected 3 or 4." << std::endl;
                return;
            }
            // Now frame_for_tracker_input is guaranteed to be 3 channels (BGR)
            // --- MODIFICATION END ---


            int center_x = frame_for_tracker_input.cols / 2; // Use dimensions of the (potentially resized) input frame
            int center_y = frame_for_tracker_input.rows / 2;
            int roi_x = center_x - (roi_width / 2);
            int roi_y = center_y - (roi_height / 2);

            roi_x = std::max(0, roi_x);
            roi_y = std::max(0, roi_y);
            // Use frame_for_tracker_input.cols/rows for boundary checks
            int actual_roi_width = std::min(roi_width, frame_for_tracker_input.cols - roi_x);
            int actual_roi_height = std::min(roi_height, frame_for_tracker_input.rows - roi_y);

            if (actual_roi_width < 10 || actual_roi_height < 10) { 
                std::cerr << "Selected ROI is too small for tracking." << std::endl;
                return;
            }
            
            cv::Rect initial_bbox(roi_x, roi_y, actual_roi_width, actual_roi_height);   
            
            // track_frame (the visual ROI) should also be from the 3-channel image if consistency is needed
            track_frame = frame_for_tracker_input(initial_bbox).clone(); 

            tracker = cv::TrackerKCF::create(); 
            if (tracker) { 
                try {
                    // Pass the EXPLICITLY 3-channel frame to init
                    tracker->init(frame_for_tracker_input, initial_bbox); 
                    
                    tracked_bbox = initial_bbox; 
                    tracker_initialized = true;
                    ai_joystickState.ch3 = g_joystickState.ch3;                    
                    std::cout << "Tracker initialized with ROI from 3-channel frame." << std::endl;
                } catch (const cv::Exception& e) {
                    std::cerr << "OpenCV Exception during tracker init: " << e.what() << std::endl;
                    tracker.release(); 
                    track_frame.release();
                    tracker_initialized = false; 
                }
            } else {
                std::cerr << "Failed to create tracker (cv::TrackerCSRT::create() returned null)." << std::endl;
                track_frame.release(); 
                tracker_initialized = false;
            }
        } else {
            std::cerr << "Display frame too small to select ROI for tracking." << std::endl;
        }
    }
}

void update_tracker_and_draw(cv::Mat& frame_to_draw_on) {
    // 先将全局偏移量标记为无效，除非跟踪成功并计算出新值
    g_current_tracking_offset.is_valid = false; 

    if (flag_track == 1 && tracker_initialized && tracker && !frame_to_draw_on.empty()) {
        cv::Mat frame_for_tracker_update;
        if (frame_to_draw_on.channels() == 4) {
            cv::cvtColor(frame_to_draw_on, frame_for_tracker_update, cv::COLOR_BGRA2BGR);
        } else if (frame_to_draw_on.channels() == 3) {
            frame_for_tracker_update = frame_to_draw_on; 
        } else {
            std::cerr << "Error: Frame for tracker update has " << frame_to_draw_on.channels()
                      << " channels. Expected 3 or 4." << std::endl;
            return;
        }

        bool success = tracker->update(frame_for_tracker_update, tracked_bbox);

        if (success) {
            // 跟踪成功，绘制边界框
            cv::rectangle(frame_to_draw_on, tracked_bbox, cv::Scalar(0, 0, 255), 2, 1);

            // --- 计算偏移量 ---
            // 1. 获取图像中心点
            cv::Point frame_center(frame_to_draw_on.cols / 2, frame_to_draw_on.rows / 2);

            // 2. 获取跟踪框中心点
            cv::Point tracked_box_center(tracked_bbox.x + tracked_bbox.width / 2,
                                         tracked_bbox.y + tracked_bbox.height / 2);

            // 3. 计算偏移量
            g_current_tracking_offset.dx = tracked_box_center.x - frame_center.x;
            g_current_tracking_offset.dy = tracked_box_center.y - frame_center.y; // 通常 y 向上为负，向下为正。如果需要屏幕坐标系（y向下为正），这个减法顺序是对的。
                                                                                // 如果您希望 y 向上为正的偏移量，可以是 frame_center.y - tracked_box_center.y
            g_current_tracking_offset.is_valid = true;

            // --- 可选：在屏幕上显示偏移量 (用于调试或信息展示) ---
            std::ostringstream offset_stream;
            offset_stream << "Offset X: " << g_current_tracking_offset.dx 
                          << " Y: " << g_current_tracking_offset.dy;
            cv::putText(frame_to_draw_on, offset_stream.str(), 
                        cv::Point(10, frame_to_draw_on.rows - 30), // 放在通道数据上面一点
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 1, cv::LINE_AA);
            // --- 偏移量计算结束 ---

        } else {
            cv::putText(frame_to_draw_on, "Tracking Failure", cv::Point(100, 80),
                        cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(0, 0, 255), 2);
            // 当跟踪失败时，g_current_tracking_offset.is_valid 保持 false
        }
    } else if (flag_track == 0 && tracker_initialized) {
        if (tracker) tracker.release();
        tracker_initialized = false;
        track_frame.release();
        std::cout << "Tracker stopped and reset." << std::endl;
        // 当跟踪停止时，也可以将全局偏移量标记为无效
        g_current_tracking_offset.is_valid = false; 
    }
}

void ControlAircraftWithPID() {
    if (!g_current_tracking_offset.is_valid) {
        // ... (保持不变：重置ai_joystickState和PID状态) ...
        ai_joystickState.ch1 = 0; 
        ai_joystickState.ch3 = 0;
        ai_joystickState.ch2 = g_joystickState.ch5; 
        ai_joystickState.ch4 = 0; 
        ai_joystickState.ch5 = g_joystickState.ch5; 
        ai_joystickState.ch6 = 0; 
        ai_joystickState.ch7 = 0;
        ai_joystickState.ch8 = 0;
        ai_joystickState.ch9 = 0;
        ai_joystickState.ch10 = g_joystickState.ch10;
        pid_integral_dx = 0; pid_previous_error_dx = 0;
        pid_integral_dy = 0; pid_previous_error_dy = 0;
        return;
    }

    double error_dx = static_cast<double>(g_current_tracking_offset.dx); 
    double error_dy = static_cast<double>(g_current_tracking_offset.dy); 

    // --- PID 计算 for dx (控制 ch1) ---
    pid_integral_dx += error_dx;
    double derivative_dx = error_dx - pid_previous_error_dx;
    pid_previous_error_dx = error_dx;
    double pid_output_dx = (pid_kp_dx * error_dx) + (pid_ki_dx * pid_integral_dx) + (pid_kd_dx * derivative_dx);
    // 假设增大ch1使目标左移。如果error_dx > 0 (目标在右)，需要增大ch1。
    // 所以，如果Kp为正，这里的符号可能是对的。如果反了，调整Kp符号或在这里取反。
    ai_joystickState.ch1 = static_cast<long>(pid_output_dx); 


    // --- PID 计算 for dy (控制 ch3) ---
    pid_integral_dy += error_dy; 
    // 可选：更精细的积分抗饱和，例如：
    // const double MAX_INTEGRAL_DY = 5000.0; // 根据经验设定
    // pid_integral_dy = std::max(-MAX_INTEGRAL_DY, std::min(MAX_INTEGRAL_DY, pid_integral_dy));

    double derivative_dy = error_dy - pid_previous_error_dy;
    pid_previous_error_dy = error_dy;

    double pid_output_dy_raw = (pid_kp_dy * error_dy) + (pid_ki_dy * pid_integral_dy) + (pid_kd_dy * derivative_dy);
    const long hover_bias_ch3 = 300; // 示例：您实验得到的值
    // 控制方向调整：增大ch3使目标框向下。
    // 如果 error_dy > 0 (目标在下方)，我们需要一个使目标框上移的控制，即减小ch3。
    // 所以，如果 pid_output_dy_raw 为正，我们需要一个负的控制努力。
    double control_effort_dy = -pid_output_dy_raw; 

    // 可选：添加前馈/偏置 (如果CH3是油门，可能需要一个基础油门值)
    // const long hover_bias_ch3 = 0; // 如果希望稳定在0，则为0。如果是悬停油门，设为该值。
    // ai_joystickState.ch3 = static_cast<long>(control_effort_dy) + hover_bias_ch3;
    ai_joystickState.ch3 = static_cast<long>(control_effort_dy)+hover_bias_ch3; // 当前不加偏置


    // --- 限幅PID输出 ---
    ai_joystickState.ch1 = std::max(PID_OUTPUT_MIN, std::min(PID_OUTPUT_MAX, ai_joystickState.ch1));
    ai_joystickState.ch3 = std::max(PID_OUTPUT_MIN, std::min(PID_OUTPUT_MAX, ai_joystickState.ch3));

    // --- 其他通道 ---
    ai_joystickState.ch2 = g_joystickState.ch2;
    ai_joystickState.ch4 = 0;
    ai_joystickState.ch5 = g_joystickState.ch5;  
    ai_joystickState.ch6 = 0; 
    ai_joystickState.ch7 = 0;
    ai_joystickState.ch8 = 0;
    ai_joystickState.ch9 = 0;
    ai_joystickState.ch10 = g_joystickState.ch10;

    // --- 更新PID输出历史数据 ---
    // (保持不变)
    pid_ch1_history.push_back(ai_joystickState.ch1);
    if (pid_ch1_history.size() > PLOT_HISTORY_LENGTH) pid_ch1_history.pop_front();
    pid_ch3_history.push_back(ai_joystickState.ch3);
    if (pid_ch3_history.size() > PLOT_HISTORY_LENGTH) pid_ch3_history.pop_front();
// 在 ControlAircraftWithPID() 的末尾，更新历史数据之前
if (g_current_tracking_offset.is_valid) { // 只在有效时打印
    std::cout << "PID_DY: err=" << error_dy
              << ", integral=" << pid_integral_dy
              << ", raw_out=" << pid_output_dy_raw
              << ", effort=" << control_effort_dy
              << ", CH3_final=" << ai_joystickState.ch3
              << std::endl;
}

}

// --- Function Prototypes ---
// ...
void DrawPIDCurves(cv::Mat& frame_to_draw_on);
// ...

// --- Function to Draw PID Curves ---
// --- Function to Draw PID Curves ---
void DrawPIDCurves(cv::Mat& frame_to_draw_on) {
    if (frame_to_draw_on.empty() || frame_to_draw_on.cols <= (2 * PLOT_MARGIN + PLOT_AREA_WIDTH)) {
        return;
    }

    int font_face = cv::FONT_HERSHEY_SIMPLEX;
    double param_font_scale = 0.35; // 稍小一点的字体用于显示参数
    int param_thickness = 1;
    cv::Scalar param_text_color(200, 200, 200); // 浅灰色用于参数

    // --- 准备绘制区域1 (for ch1) ---
    int plot1_start_x = PLOT_MARGIN;
    int plot1_start_y = PLOT_MARGIN + 30; 
    if (plot1_start_y + PLOT_AREA_HEIGHT > frame_to_draw_on.rows - PLOT_MARGIN) {
        plot1_start_y = frame_to_draw_on.rows - PLOT_MARGIN - PLOT_AREA_HEIGHT;
    }
    if (plot1_start_y < PLOT_MARGIN) plot1_start_y = PLOT_MARGIN;

    cv::Rect plot_area1_rect(plot1_start_x, plot1_start_y, PLOT_AREA_WIDTH, PLOT_AREA_HEIGHT);
    cv::rectangle(frame_to_draw_on, plot_area1_rect, cv::Scalar(50, 50, 50), cv::FILLED); 
    cv::rectangle(frame_to_draw_on, plot_area1_rect, cv::Scalar(200, 200, 200), 1);    
    
    // 显示 CH1 标题和PID参数
    std::ostringstream title1_stream;
    title1_stream << "CH1 PID: P=" << std::fixed << std::setprecision(3) << pid_kp_dx
                  << " I=" << std::fixed << std::setprecision(3) << pid_ki_dx
                  << " D=" << std::fixed << std::setprecision(3) << pid_kd_dx;
    cv::putText(frame_to_draw_on, title1_stream.str(), cv::Point(plot1_start_x, plot1_start_y - 5), 
                font_face, param_font_scale, param_text_color, param_thickness, cv::LINE_AA);


    // 绘制ch1历史数据
    if (!pid_ch1_history.empty()) {
        cv::Point prev_point_ch1; // Renamed to avoid conflict
        for (size_t i = 0; i < pid_ch1_history.size(); ++i) {
            double normalized_val = static_cast<double>(pid_ch1_history[i] - PID_OUTPUT_MIN) / (PID_OUTPUT_MAX - PID_OUTPUT_MIN); 
            int y_pixel = static_cast<int>((1.0 - normalized_val) * (PLOT_AREA_HEIGHT - 1)); 
            cv::Point current_point(plot1_start_x + static_cast<int>(i), plot1_start_y + y_pixel);
            if (i > 0) {
                cv::line(frame_to_draw_on, prev_point_ch1, current_point, cv::Scalar(0, 255, 255), 1, cv::LINE_AA); 
            }
            prev_point_ch1 = current_point;
        }
        int zero_line_y1 = plot1_start_y + static_cast<int>((1.0 - (0.0 - PID_OUTPUT_MIN) / (PID_OUTPUT_MAX - PID_OUTPUT_MIN)) * (PLOT_AREA_HEIGHT - 1));
        cv::line(frame_to_draw_on, cv::Point(plot1_start_x, zero_line_y1), cv::Point(plot1_start_x + PLOT_AREA_WIDTH -1 , zero_line_y1), cv::Scalar(128,128,128), 1);
    }

    // --- 准备绘制区域2 (for ch3) ---
    int plot2_start_y = plot1_start_y + PLOT_AREA_HEIGHT + PLOT_MARGIN; 
    if (plot2_start_y + PLOT_AREA_HEIGHT > frame_to_draw_on.rows - PLOT_MARGIN) {
        return; 
    }
    if (plot2_start_y < PLOT_MARGIN) plot2_start_y = PLOT_MARGIN;

    cv::Rect plot_area2_rect(plot1_start_x, plot2_start_y, PLOT_AREA_WIDTH, PLOT_AREA_HEIGHT);
    cv::rectangle(frame_to_draw_on, plot_area2_rect, cv::Scalar(50, 50, 50), cv::FILLED);
    cv::rectangle(frame_to_draw_on, plot_area2_rect, cv::Scalar(200, 200, 200), 1);
    
    // 显示 CH3 标题和PID参数
    std::ostringstream title2_stream;
    title2_stream << "CH3 PID: P=" << std::fixed << std::setprecision(3) << pid_kp_dy
                  << " I=" << std::fixed << std::setprecision(3) << pid_ki_dy
                  << " D=" << std::fixed << std::setprecision(3) << pid_kd_dy;
    cv::putText(frame_to_draw_on, title2_stream.str(), cv::Point(plot1_start_x, plot2_start_y - 5), 
                font_face, param_font_scale, param_text_color, param_thickness, cv::LINE_AA);

    // 绘制ch3历史数据
    if (!pid_ch3_history.empty()) {
        cv::Point prev_point_ch3; // Renamed to avoid conflict
        for (size_t i = 0; i < pid_ch3_history.size(); ++i) {
            double normalized_val = static_cast<double>(pid_ch3_history[i] - PID_OUTPUT_MIN) / (PID_OUTPUT_MAX - PID_OUTPUT_MIN);
            int y_pixel = static_cast<int>((1.0 - normalized_val) * (PLOT_AREA_HEIGHT - 1));
            cv::Point current_point(plot1_start_x + static_cast<int>(i), plot2_start_y + y_pixel);
            if (i > 0) {
                cv::line(frame_to_draw_on, prev_point_ch3, current_point, cv::Scalar(255, 0, 255), 1, cv::LINE_AA); 
            }
            prev_point_ch3 = current_point;
        }
        int zero_line_y2 = plot2_start_y + static_cast<int>((1.0 - (0.0 - PID_OUTPUT_MIN) / (PID_OUTPUT_MAX - PID_OUTPUT_MIN)) * (PLOT_AREA_HEIGHT - 1));
        cv::line(frame_to_draw_on, cv::Point(plot1_start_x, zero_line_y2), cv::Point(plot1_start_x + PLOT_AREA_WIDTH -1 , zero_line_y2), cv::Scalar(128,128,128), 1);
    }
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
        PollPhysicalJoystick();
        CaptureFrameDXGI(); 
        is_track_on();
        if (!desktop_capture_full.empty()) {
            double aspect_ratio = (double)desktop_capture_full.cols / (double)desktop_capture_full.rows;
            int display_height = static_cast<int>(DISPLAY_WIDTH / aspect_ratio);
            if (display_height <= 0) { 
                 display_height = static_cast<int>(DISPLAY_WIDTH * ( (double)g_monitor_capture_height / g_monitor_capture_width));
                 if(display_height <= 0) display_height = DISPLAY_WIDTH * 9 / 16; 
            }
            cv::resize(desktop_capture_full, display_frame, cv::Size(DISPLAY_WIDTH, display_height));
            if (flag_track == 1) {
                if (!tracker_initialized) { // 如果跟踪启动且跟踪器未初始化
                    get_track_frame_and_init_tracker(display_frame); // 使用当前的显示帧初始化
                }
                update_tracker_and_draw(display_frame); // 更新跟踪器并在display_frame上绘制
            } else if (tracker_initialized) { // 如果跟踪关闭但跟踪器仍处于初始化状态
                 update_tracker_and_draw(display_frame); // 调用一次以重置跟踪器
            }
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
        static int key_process_counter = 0;
        const int KEY_PROCESS_INTERVAL = 5; // 每5帧处理一次按键调整，降低灵敏度

        bool pid_param_changed_this_cycle = false; // 重命名以避免与函数外的变量冲突

        if (key_process_counter % KEY_PROCESS_INTERVAL == 0) {
            double pid_adjust_step_p = 1; 
            double pid_adjust_step_i = 0.01;
            double pid_adjust_step_d = 0.5;  

            // CH1 (dx) PID参数调整
            if (GetAsyncKeyState('Q') & 0x8000) { pid_kp_dx += pid_adjust_step_p; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('A') & 0x8000) { pid_kp_dx -= pid_adjust_step_p; if(pid_kp_dx < 0) pid_kp_dx = 0; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('W') & 0x8000) { pid_ki_dx += pid_adjust_step_i; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('S') & 0x8000) { pid_ki_dx -= pid_adjust_step_i; if(pid_ki_dx < 0) pid_ki_dx = 0; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('E') & 0x8000) { pid_kd_dx += pid_adjust_step_d; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('D') & 0x8000) { pid_kd_dx -= pid_adjust_step_d; if(pid_kd_dx < 0) pid_kd_dx = 0; pid_param_changed_this_cycle = true; }

            // CH3 (dy) PID参数调整
            if (GetAsyncKeyState('I') & 0x8000) { pid_kp_dy += pid_adjust_step_p; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('J') & 0x8000) { pid_kp_dy -= pid_adjust_step_p; if(pid_kp_dy < 0) pid_kp_dy = 0; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('O') & 0x8000) { pid_ki_dy += pid_adjust_step_i; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('K') & 0x8000) { pid_ki_dy -= pid_adjust_step_i; if(pid_ki_dy < 0) pid_ki_dy = 0; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('P') & 0x8000) { pid_kd_dy += pid_adjust_step_d; pid_param_changed_this_cycle = true; }
            if (GetAsyncKeyState('L') & 0x8000) { pid_kd_dy -= pid_adjust_step_d; if(pid_kd_dy < 0) pid_kd_dy = 0; pid_param_changed_this_cycle = true; }
            
            if (pid_param_changed_this_cycle) {
                pid_integral_dx = 0; pid_previous_error_dx = 0;
                pid_integral_dy = 0; pid_previous_error_dy = 0;
                std::cout << "PID Params Updated (Global Keys):" << std::endl;
                std::cout << "  CH1 (dx): P=" << pid_kp_dx << ", I=" << pid_ki_dx << ", D=" << pid_kd_dx << std::endl;
                std::cout << "  CH3 (dy): P=" << pid_kp_dy << ", I=" << pid_ki_dy << ", D=" << pid_kd_dy << std::endl;
            }
        }
        key_process_counter++;
        // --- PID参数调整结束 ---


        // --- 切换 flag_track 的逻辑 (示例) ---
        static bool t_key_pressed_last_frame = false;
        bool t_key_currently_pressed = (GetAsyncKeyState('T') & 0x8000) != 0;
        if (t_key_currently_pressed && !t_key_pressed_last_frame) {
            flag_track = 1 - flag_track; 
            std::cout << "flag_track toggled to: " << flag_track << std::endl;
            if (flag_track == 0 && tracker_initialized) { 
                // update_tracker_and_draw 会处理重置
            }
        }
        t_key_pressed_last_frame = t_key_currently_pressed;
        
        ControlAircraftWithPID();
         
        MapToVirtualJoystick();
    }

    CleanupDesktopDuplication(); 
    CleanupDirectInput();
    CleanupVirtualGamepad();
    DestroyWindow(hDummyWnd);
    cv::destroyAllWindows();
    std::cout << "Exiting." << std::endl;
    return 0;
}