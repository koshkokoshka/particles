#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>

#define _USE_MATH_DEFINES
#include <math.h>

void ShowErrorMessage(DWORD error, HWND hWnd)
{
    LPVOID lpMsgBuf;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
    MessageBox(hWnd, (LPCTSTR)lpMsgBuf, TEXT("An error occurred!"), MB_ICONERROR | MB_OK);
    LocalFree(lpMsgBuf);
}

typedef struct
{
    void **data;
    UINT step;
    UINT size;
    UINT w;
    UINT h;
    UINT bits;

} Surface;

Surface* CreateSurface(UINT w, UINT h)
{
    Surface *s = malloc(sizeof(Surface));
    if (s) {
        s->size = w * h * sizeof(DWORD);
        if ((s->data = malloc(s->size))) {
            s->step = w;
            s->bits = sizeof(DWORD);
            s->w = w;
            s->h = h;
            return s;
        }
        free(s);
    }
    return NULL;
}

void DestroySurface(Surface *s)
{
    if (s) {
        if (s->data) {
            free(s->data);
        }
        free(s);
    }
}

Surface* LoadSurface(const WCHAR *path, HWND hWnd)
{
    Surface *surface = NULL;

    /* create imaging factory */
    IWICImagingFactory* wicImagingFactory;
    HRESULT r = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC,
                                 &IID_IWICImagingFactory, (LPVOID*)&wicImagingFactory);
    if (SUCCEEDED(r))
    {
        /* create image decoder */
        IWICBitmapDecoder* wicBitmapDecoder;
        r = wicImagingFactory->lpVtbl->CreateDecoderFromFilename(wicImagingFactory, path, NULL, GENERIC_READ,
                                                                 WICDecodeMetadataCacheOnDemand, &wicBitmapDecoder);
        if (SUCCEEDED(r))
        {
            /* read image frame */
            IWICBitmapFrameDecode* wicFrame;
            r = wicBitmapDecoder->lpVtbl->GetFrame(wicBitmapDecoder, 0, &wicFrame);
            if (SUCCEEDED(r))
            {
                /* create image converter */
                IWICFormatConverter* wicFormatConverter;
                r = wicImagingFactory->lpVtbl->CreateFormatConverter(wicImagingFactory, &wicFormatConverter);
                if (SUCCEEDED(r))
                {
                    /* convert to 32-bit image */
                    r = wicFormatConverter->lpVtbl->Initialize(wicFormatConverter, (IWICBitmapSource*)wicFrame,
                                                               &GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                                               NULL, 0, WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(r))
                    {
                        /* read image width/height */
                        UINT w, h;
                        r = wicFormatConverter->lpVtbl->GetSize(wicFormatConverter, &w, &h);
                        if (SUCCEEDED(r)) {
                            surface = CreateSurface(w, h);
                            if (surface) {
                                /* read image pixels */
                                UINT stride = w * 4;
                                r = wicFormatConverter->lpVtbl->CopyPixels(wicFormatConverter, NULL,
                                                                           stride, stride * h, (BYTE*)surface->data);
                                if (FAILED(r)) {
                                    DestroySurface(surface);
                                    surface = NULL;
                                }
                            }
                        }
                    }

                    wicFormatConverter->lpVtbl->Release(wicFormatConverter);
                }
                wicFrame->lpVtbl->Release(wicFrame);
            }
            wicBitmapDecoder->lpVtbl->Release(wicBitmapDecoder);
        }
        wicImagingFactory->lpVtbl->Release(wicImagingFactory);
    }

    if (FAILED(r)) {
        ShowErrorMessage(r, hWnd);
    }

    return surface;
}

BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER), 0, 0, 1, 32, 0, 0, 0, 0, 0, 0, 0 };

Surface *framebuffer;
Surface *image;

struct Mouse { float x, y; BYTE button; } mouse;

SIZE_T particlesN = 0;
void **particles  = NULL;

float *particlesPX = NULL;
float *particlesSX = NULL;
float *particlesFX = NULL;
float *particlesPY = NULL;
float *particlesSY = NULL;
float *particlesFY = NULL;

void Reset()
{
    for (UINT y = 0; y < image->h; ++y) {
        for (UINT x = 0; x < image->w; ++x) {
            UINT i = x + y * image->step;
            particlesPX[i] = (float)x;
            particlesSX[i] = 0;
            particlesPY[i] = (float)y;
            particlesSY[i] = 0;
        }
    }
}

void Open(LPWSTR lpCmdLine, HWND hWnd) {

    OPENFILENAMEW ofn;
    WCHAR* fileStr;
    WCHAR szFileName[MAX_PATH];

    if (!lpCmdLine || !lstrlenW(lpCmdLine)) {

        szFileName[0] = '\0';

        ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = hWnd;
        ofn.lpstrFilter = u"All Files (*.*)\0*.*\0\0";
        ofn.lpstrFile = szFileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST;

        if (GetOpenFileNameW(&ofn)) {
            fileStr = szFileName;
        } else {
            return;
        }
    } else {
        fileStr = lpCmdLine;
    }

    Surface *newImage = LoadSurface(fileStr, hWnd);
    if (newImage) {
        image = newImage;
        particlesN = image->size / sizeof(DWORD);
        particles = realloc(particles, (particlesN * sizeof(float)) * 6);
        if (particles) {
            particlesPX = &((float*)particles)[particlesN * 0];
            particlesPY = &((float*)particles)[particlesN * 1];
            particlesSX = &((float*)particles)[particlesN * 2];
            particlesSY = &((float*)particles)[particlesN * 3];
            particlesFX = &((float*)particles)[particlesN * 4];
            particlesFY = &((float*)particles)[particlesN * 5];
            Reset();
        }
    }
}

static float drag = 8.000f;
static float mass = 0.003f;

void update_fxfy_lmb()
{
    for (SIZE_T i = 0; i < particlesN; ++i) {
        float px = particlesPX[i];
        float py = particlesPY[i];

        float dx = mouse.x - px;
        float dy = mouse.y - py;

        float s = sqrtf((dx * dx) + (dy * dy));
        s = s + 8.0f;
        s = s * s;
        s = 50000.0f / s;

        particlesFX[i] = ((mouse.x - px) * s) - (particlesSX[i] * drag);
        particlesFY[i] = ((mouse.y - py) * s) - (particlesSY[i] * drag);
    }
}

void update_fx()
{
    // fx = -(sx * drag)
    for (SIZE_T i = 0; i < particlesN; ++i) {
        particlesFX[i] = -(particlesSX[i] * drag);
    }
}

void update_fy()
{
    // fy = -(sy * drag)
    for (SIZE_T i = 0; i < particlesN; ++i) {
        particlesFY[i] = -(particlesSY[i] * drag);
    }
}

void update_sx()
{
    // sx += fx * mass
    for (SIZE_T i = 0; i < particlesN; ++i) {
        particlesSX[i] += particlesFX[i] * mass;
    }
}

void update_sy()
{
    // sy += fy * mass
    for (SIZE_T i = 0; i < particlesN; ++i) {
        particlesSY[i] += particlesFY[i] * mass;
    }
}

void update_px()
{
    // px += sx
    for (SIZE_T i = 0; i < particlesN; ++i) {
        particlesPX[i] += particlesSX[i];
    }
}

void update_py()
{
    // py += sy
    for (SIZE_T i = 0; i < particlesN; ++i) {
        particlesPY[i] += particlesSY[i];
    }
}

void update()
{
    if (mouse.button) {
        update_fxfy_lmb();
    } else {
        update_fx();
        update_fy();
    }
    update_sx();
    update_sy();
    update_px();
    update_py();
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg) {
        case WM_CREATE: {
            /* preallocate buffers */
            framebuffer = CreateSurface(0, 0);
            particles = malloc(0);

            /* get image path from the 1'st command line argument */
            const WCHAR *wCmdLine = GetCommandLineW();
            int argc = 0;
            WCHAR *path = CommandLineToArgvW(wCmdLine, &argc)[1];
            if (path) {
                Open(path, hWnd);
            }

            /* set update timer */
            SetTimer(hWnd, 0, (int)ceilf(1000.0f / 120.0f), NULL);
            break;
        }
        case WM_DESTROY: {
            if (framebuffer) { free(framebuffer); }
            if (particles) { free(particles); }
            PostQuitMessage(0);
            break;
        }
        case WM_TIMER:
            update();
            RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);

            /* clear surface */
            ZeroMemory(framebuffer->data, framebuffer->size);

            /* draw particles */
            for (SIZE_T i = 0; i < particlesN; ++i)
            {
                unsigned int x = (unsigned int)particlesPX[i];
                unsigned int y = (unsigned int)particlesPY[i];

                if (x < framebuffer->w && y < framebuffer->h) {
                    ((DWORD *)framebuffer->data)[x + y * framebuffer->step] = ((DWORD *)image->data)[i];
                }
            }

            /* draw buffer */
            StretchDIBits(ps.hdc,
                          0, 0, framebuffer->w, framebuffer->h,
                          0, 0, framebuffer->w, framebuffer->h, framebuffer->data, &bmi, DIB_RGB_COLORS, SRCCOPY);

            EndPaint(hWnd, &ps);
            break;
        }
        case WM_SIZE: {
            LONG newW = LOWORD(lParam);
            LONG newH = HIWORD(lParam);

            bmi.bmiHeader.biWidth  =  newW;
            bmi.bmiHeader.biHeight = -newH;

            framebuffer->size = newW * newH * framebuffer->bits;
            framebuffer->step = newW;
            if (    (unsigned)newW > framebuffer->w
                 || (unsigned)newH > framebuffer->h)
            {
                framebuffer->data = realloc(framebuffer->data, framebuffer->size);
            }
            framebuffer->w = newW;
            framebuffer->h = newH;
            break;
        }
        case WM_MOUSEMOVE:
            mouse.x = (float)GET_X_LPARAM(lParam);
            mouse.y = (float)GET_Y_LPARAM(lParam);
            break;
        case WM_LBUTTONDOWN:
            mouse.button = 1;
            break;
        case WM_LBUTTONUP:
            mouse.button = 0;
            break;
        case WM_KEYDOWN:
            switch (wParam) {
                case 'R': Reset(); break;
                case 'O': Open(NULL, hWnd); break;
                default: break;
            }
            break;
        default:
            return DefWindowProc(hWnd, Msg, wParam, lParam);
    }
    return 0;
}

static WNDCLASS wc = {
    .style = CS_DBLCLKS,
    .lpfnWndProc = WndProc,
    .cbClsExtra = 0,
    .cbWndExtra = 0,
    .hInstance = NULL,
    .hIcon = NULL,
    .hCursor = NULL,
    .hbrBackground = NULL,
    .lpszMenuName = NULL,
    .lpszClassName = "PARTICLES_WND"
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    MSG msg;

    if (CoInitialize(NULL) == S_OK) { /* required for WIC (LoadSurface) */

        /* register window class */
        wc.hInstance = hInstance;
        wc.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        if (RegisterClass(&wc)) {

            /* create window */
            HWND hWnd = CreateWindow(wc.lpszClassName, "R - reset, O - open", WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                     NULL, NULL, wc.hInstance, NULL);
            if (hWnd) {
                ShowWindow(hWnd, nShowCmd);

                /* start event loop */
                while (GetMessage(&msg, NULL, 0, 0)) {
                    DispatchMessage(&msg);
                }
            }

            UnregisterClass(wc.lpszClassName, wc.hInstance);
        }

        CoUninitialize();
    }

    return msg.wParam;
}
