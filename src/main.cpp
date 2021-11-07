#include <windows.h>
#include <shellapi.h>
#include <assert.h>
#include <stdio.h>

typedef char          i8;
typedef unsigned char u8;
typedef int           i32;
typedef int unsigned  u32;

#define New(type) (type *)malloc(sizeof(type))
#define Free(ptr) free(ptr)

union file_time
{
    FILETIME Win;
    struct
    {
        __int64 Raw;
    };
};

static char *OverlayWindowClassName = "NTrayOverlayWindowClassName";
static char *TrayWindowClassName = "NTrayTrayWindowClassName";

#define Win32TrayIconMessage (WM_USER + 1)

void
Win32AddSubMenu(HMENU MenuHandle, char *SubMenuTitle, HMENU SubMenu)
{
    AppendMenu(MenuHandle, MF_POPUP, (UINT_PTR)SubMenu, SubMenuTitle);
}

i32
Win32AddMenuItem(HMENU MenuHandle, char *ItemText, bool Checked, bool Enabled, void *ExtraData)
{
    // Figure out whether the menu item should be checked or not
    UINT Flags = MF_STRING;
    if(Checked)
    {
        Flags |= MF_CHECKED;
    }

    MENUITEMINFO MenuItem;
    MenuItem.cbSize = sizeof(MenuItem);
    MenuItem.fMask = MIIM_ID | MIIM_STATE | MIIM_DATA | MIIM_TYPE;
    MenuItem.fType = MFT_STRING;
    MenuItem.fState = ((Checked ? MFS_CHECKED : MFS_UNCHECKED) |
                       (Enabled ? MFS_ENABLED : MFS_DISABLED));
    MenuItem.wID = GetMenuItemCount(MenuHandle) + 1;
    MenuItem.dwItemData = (LPARAM)ExtraData;
    MenuItem.dwTypeData = (char *)ItemText;

    InsertMenuItem(MenuHandle, GetMenuItemCount(MenuHandle), true, &MenuItem);

    return(MenuItem.wID);
}

void
Win32AddSeparator(HMENU MenuHandle)
{
    MENUITEMINFO MenuItem;
    MenuItem.cbSize = sizeof(MenuItem);
    MenuItem.fMask = MIIM_ID | MIIM_DATA | MIIM_TYPE;
    MenuItem.fType = MFT_SEPARATOR;
    MenuItem.wID = 0;
    MenuItem.dwItemData = 0;

    InsertMenuItem(MenuHandle, GetMenuItemCount(MenuHandle), true, &MenuItem);
}

void *
Win32GetMenuItemExtraData(HMENU MenuHandle, int Index)
{
    MENUITEMINFO MenuItemInfo;

    MenuItemInfo.cbSize = sizeof(MenuItemInfo);
    MenuItemInfo.fMask = MIIM_DATA;
    MenuItemInfo.dwItemData = 0;

    GetMenuItemInfo(MenuHandle, Index, false, &MenuItemInfo);

    return((void *)MenuItemInfo.dwItemData);
}

struct win32_dib_section
{
    HDC DrawDC;
    BITMAPINFOHEADER BitmapHeader;
    HBITMAP DrawBitmap, OldBitmap;

    i32 Width;
    i32 Height;

    i32 TotalBytesPerLine;
    i32 OverHang;
    i32 Stride;
    i32 BufferSize;

    u8 *PixelBuffer;
    u8 *TopOfFrame;
};

struct window {
  HWND Handle;
  int Opacity;
};

static win32_dib_section CountdownDIBSection;

void Win32FreeDIBSection(win32_dib_section &This)
{
    if(This.DrawBitmap)
    {
        SelectObject(This.DrawDC, This.OldBitmap);
        DeleteObject(This.DrawBitmap);
        This.DrawBitmap = 0;
    }

    if(This.DrawDC)
    {
        DeleteDC(This.DrawDC);
        This.DrawDC = 0;
    }
}

bool Win32IsInitialized(win32_dib_section &This) {
    return(This.DrawDC != 0);
}

bool Win32ResizeDIBSection(win32_dib_section &This, i32 Width, i32 Height)
{
    bool Result = false;

    if(!Win32IsInitialized(This))
    {
        // One-time only initialization
        This.BitmapHeader.biSize = sizeof(This.BitmapHeader);
        This.BitmapHeader.biPlanes = 1;
        This.BitmapHeader.biBitCount = 32;
        This.BitmapHeader.biCompression = BI_RGB;
        This.BitmapHeader.biSizeImage = 0;
        This.BitmapHeader.biClrUsed = 0;
        This.BitmapHeader.biClrImportant = 0;

        This.DrawBitmap = 0;
        This.OldBitmap = 0;

        This.DrawDC = CreateCompatibleDC(0);
    }

    if((Width > 0) && (Height > 0))
    {
        if(This.DrawBitmap)
        {
            SelectObject(This.DrawDC, This.OldBitmap);
            DeleteObject(This.DrawBitmap);

            This.OldBitmap = 0;
            This.DrawBitmap = 0;
        }

        This.BitmapHeader.biWidth = Width;
        This.BitmapHeader.biHeight = Height;

        This.PixelBuffer = 0;
        HDC ScreenDC = GetDC(0);
        This.DrawBitmap = CreateDIBSection(ScreenDC,
                                           (BITMAPINFO *)&This.BitmapHeader,
                                           DIB_RGB_COLORS,
                                           (void **)&This.PixelBuffer,
                                           0, 0);
        if(This.DrawBitmap)
        {
            This.OldBitmap = (HBITMAP)SelectObject(This.DrawDC, This.DrawBitmap);

            This.Width = Width;
            This.Height = Height;

            i32 SizeOfPixel = 4;
            i32 PadTo = 4;

            This.TotalBytesPerLine = (((This.Width * SizeOfPixel) + PadTo - 1) / PadTo) * PadTo;
            This.OverHang = This.TotalBytesPerLine - This.Width;
            This.Stride = -This.TotalBytesPerLine;
            This.BufferSize = This.Height * This.TotalBytesPerLine;
            This.TopOfFrame = ((u8 *)This.PixelBuffer + This.BufferSize - This.TotalBytesPerLine);

            Result = true;
        }
        ReleaseDC(0, ScreenDC);

        assert(This.PixelBuffer);
    }

    return(Result);
}

void Win32BlitWholeDIBToDC(win32_dib_section &This, HDC ToDC)
{
    if(Win32IsInitialized(This))
    {
        HDC FromDC = This.DrawDC;
        BitBlt(ToDC, 0, 0, This.Width, This.Height, FromDC, 0, 0, SRCCOPY);
    }
}

void Win32BlitWholeDIBToWindow(win32_dib_section &This, HWND Window)
{
    if(Win32IsInitialized(This))
    {
        HDC ToDC = GetDC(Window);
        Win32BlitWholeDIBToDC(This, ToDC);
        ReleaseDC(Window, ToDC);
    }
}

void Win32BlitWholeDIBToDCAtXY(win32_dib_section &This, HDC ToDC, i32 X, i32 Y)
{
    if(Win32IsInitialized(This))
    {
        HDC FromDC = This.DrawDC;
        BitBlt(ToDC, X, Y, This.Width, This.Height, FromDC, 0, 0, SRCCOPY);
    }
}

void Win32BlitDIBToDC(win32_dib_section &This, i32 FromX, i32 FromY, i32 Width, i32 Height, HDC ToDC, i32 ToX, i32 ToY)
{
    if(Win32IsInitialized(This))
    {
        if(Width > This.Width)
        {
            Width = This.Width;
        }

        if(Height > This.Height)
        {
            Height = This.Height;
        }

        HDC FromDC = This.DrawDC;
        BitBlt(ToDC, ToX, ToY, Width, Height, FromDC, FromX, FromY, SRCCOPY);
    }
}

static bool Win32RegisterWindowClass(char *Name, HINSTANCE HInstance, WNDPROC Callback, DWORD Style) {
  WNDCLASSEX WindowClass = {sizeof(WindowClass)};
  WindowClass.style = Style;
  WindowClass.lpfnWndProc = Callback;
  WindowClass.hInstance = HInstance;
  WindowClass.hIcon = LoadIcon(WindowClass.hInstance, MAKEINTRESOURCE(101));
  WindowClass.hCursor = LoadCursor(0, IDC_ARROW);
  WindowClass.lpszClassName = Name;

  return RegisterClassEx(&WindowClass) != 0;
}

static bool Win32RegisterWindowClass(char *Name, HINSTANCE HInstance, WNDPROC Callback) {
  return Win32RegisterWindowClass(Name, HInstance, Callback, CS_HREDRAW | CS_VREDRAW);
}

static HFONT MainFont = 0;
static HFONT TimeFont = 0;

static void GetFonts(void) {
  if (!MainFont) {
    MainFont = CreateFont(32, 0, 0, 0,
                           FW_NORMAL,
                           FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Arial");

    TimeFont = CreateFont(64, 0, 0, 0,
                           FW_NORMAL,
                           FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Arial");
  }
}

inline i32
GetPixelsPerLine(HDC DC)
{
    TEXTMETRIC FontMetric;
    GetTextMetrics(DC, &FontMetric);
    return FontMetric.tmHeight;
}

static void DropShadowDraw(HDC DrawDC, RECT &TextRect, char *Text)
{
    TextRect.top += 1;
    TextRect.left += 1;
    SetTextColor(DrawDC, 0x00000000);
    DrawText(DrawDC, Text, strlen(Text), &TextRect, DT_LEFT | DT_NOPREFIX);

    TextRect.top -= 1;
    TextRect.left -= 1;
    SetTextColor(DrawDC, 0x00FFFFFF);
    DrawText(DrawDC, Text, strlen(Text), &TextRect, DT_LEFT | DT_NOPREFIX);
}

void UpdateOverlayImage(HWND Handle, int Opacity, HDC UpdateDC)
{
    RECT ClientRect;
    GetClientRect(Handle, &ClientRect);
    
    BLENDFUNCTION Blend;
    Blend.BlendOp = AC_SRC_OVER;
    Blend.BlendFlags = 0;
    Blend.AlphaFormat = AC_SRC_ALPHA;
    Blend.SourceConstantAlpha = Opacity;

    POINT Origin = {0};
    SIZE Size;
    Size.cx = ClientRect.right;
    Size.cy = ClientRect.bottom;
    HDC ScreenDC = GetDC(0);
    UpdateLayeredWindow(Handle, ScreenDC, 0, &Size, UpdateDC, &Origin, 0, &Blend, ULW_ALPHA);
    ReleaseDC(0, ScreenDC);
}


static void
SetOpacity(HWND Handle, int Opacity)
{
  BLENDFUNCTION Blend;
  Blend.BlendOp = AC_SRC_OVER;
  Blend.BlendFlags = 0;
  Blend.AlphaFormat = AC_SRC_ALPHA;
  Blend.SourceConstantAlpha = Opacity;

  UpdateLayeredWindow(Handle, 0, 0, 0, 0, 0, 0, &Blend, ULW_ALPHA);
}

const u32 SpecialPixel = 0xFF000000;

static void PreClear(win32_dib_section &DIBSection)
{
    u8 *Line = DIBSection.TopOfFrame;
    {for(i32 Y = 0;
         Y < DIBSection.Height;
         ++Y)
        {
            u32 *Pixel = (u32 *)Line;
            {for(i32 X = 0;
                 X < DIBSection.Width;
                 ++X)
                {
                    *Pixel++ = SpecialPixel;
                }}
            Line += DIBSection.Stride;
        }}
}

static void
PostClear(win32_dib_section &DIBSection, bool Gradient)
{
    u8 *Line = DIBSection.TopOfFrame;
    {for(i32 Y = 0;
         Y < DIBSection.Height;
         ++Y)
        {
            u32 *Pixel = (u32 *)Line;
            {for(i32 X = 0;
                 X < DIBSection.Width;
                 ++X)
                {
                    if(*Pixel == SpecialPixel)
                    {
                        i32 Alpha = 255;
                        if(Gradient)
                        {
                            Alpha = 255 - (X * 255 / DIBSection.Width);
                        }
                        *Pixel++ = Alpha << 24;
                    }
                    else
                    {
                        *Pixel++ |= 0xFF000000;
                    }
                }}
            Line += DIBSection.Stride;
        }}
}

static window the_window = {};


#define FILETIME_SECOND ((__int64)10000000)
#define FILETIME_MINUTE (60 * FILETIME_SECOND)
#define FILETIME_HOUR (60 * FILETIME_MINUTE)
#define FILETIME_DAY (24 * FILETIME_HOUR)

static bool RepeatEnabled = true;
static int RepeatInMinutes = 60;
static int RepeatWindowInMinutes = 10;
static bool SkipCurrentWindow = false;

static bool PrevSkipCurrentWindow = false;

static file_time RepeatTimeOffset = {};

static void SetRepeatMinutes10()
{
  RepeatEnabled = true;
  RepeatInMinutes = 10;
}

static void SetRepeatMinutes30()
{
  RepeatEnabled = true;
  RepeatInMinutes = 30;
}

static void SetRepeatMinute60()
{
  RepeatEnabled = true;
  RepeatInMinutes = 60;
}

static void SetRepeatMinute120()
{
  RepeatEnabled = true;
  RepeatInMinutes = 120;
}

static void SetRepeatDisabled()
{
  RepeatEnabled = false;
}

static void SetRepeatOffsetToNow()
{
  GetSystemTimeAsFileTime(&RepeatTimeOffset.Win);
  FileTimeToLocalFileTime(&RepeatTimeOffset.Win, &RepeatTimeOffset.Win);
}

static void SetRepeatOffsetReset()
{
  RepeatTimeOffset = {};
}

static void ToggleSkipCurrentWindow()
{
  SkipCurrentWindow = !SkipCurrentWindow;
}

static void Paint() {
  GetFonts();

  file_time Current;
  GetSystemTimeAsFileTime(&Current.Win);
  FileTimeToLocalFileTime(&Current.Win, &Current.Win);

  __int64 TodaySeconds = ((Current.Raw - RepeatTimeOffset.Raw) / FILETIME_SECOND) % 86400;

  __int64 TodayMinute = (TodaySeconds / 60);
  __int64 TodaySecond = TodaySeconds % 60;

  assert(RepeatInMinutes > 0);
  TodayMinute %= RepeatInMinutes;

  bool ShowTimer = false;

  if (RepeatEnabled && TodayMinute >= 0 && TodayMinute < RepeatWindowInMinutes) {
    ShowTimer = true;
  }

  if (SkipCurrentWindow != PrevSkipCurrentWindow)
  {
    if (SkipCurrentWindow)
    {
      ShowWindow(the_window.Handle, SW_HIDE);
    } 
    else
    {
      ShowWindow(the_window.Handle, SW_SHOW);
    }

    PrevSkipCurrentWindow = SkipCurrentWindow;
  }

  int PrevOpacity = the_window.Opacity;

  if (ShowTimer) {
    if (the_window.Opacity == 0) {
      ShowWindow(the_window.Handle, SW_SHOW);
    }

    if (the_window.Opacity < 255) the_window.Opacity += 5;
    else the_window.Opacity = 255;
  } else {
    if (the_window.Opacity > 0) the_window.Opacity -= 5;
    else {
      the_window.Opacity = 0;
      ShowWindow(the_window.Handle, SW_HIDE);
      SkipCurrentWindow = false;
    }
  }

  if (PrevOpacity == 0 && PrevOpacity == the_window.Opacity) {
    return;
  }

  char Line0[256];
  wsprintf(Line0, "Time to stretch!");

  char Line1[256];
  wsprintf(Line1, "%02d:%02d", TodayMinute, TodaySecond);

  win32_dib_section &DIBSection = CountdownDIBSection;
  HDC DrawDC = DIBSection.DrawDC;

  PreClear(DIBSection);

  SetTextColor(DrawDC, 0x00FFFFFF);
  SetBkMode(DrawDC, TRANSPARENT);

  int PadX = 30;
  int PadY = 30;

  RECT TextRect;
  TextRect.top = PadY;
  TextRect.left = PadX;
  TextRect.right = DIBSection.Width - PadX;
  TextRect.bottom = DIBSection.Height - PadY;

  HGDIOBJ OldFont = SelectObject(DrawDC, MainFont);

  DropShadowDraw(DrawDC, TextRect, Line0);
  TextRect.top += GetPixelsPerLine(DrawDC);
  SelectObject(DrawDC, TimeFont);
  DropShadowDraw(DrawDC, TextRect, Line1);

  SelectObject(DrawDC, OldFont);

  PostClear(DIBSection, true);

  UpdateOverlayImage(the_window.Handle, the_window.Opacity, DrawDC);
}

#define PAINT_TIMER_ID 1

static void
MenuExit(void)
{
    PostQuitMessage(0);
}

typedef void menu_callback(void);
static HMENU Menu = 0;

LRESULT CALLBACK TrayWindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
  LRESULT Result = 0;

  switch(Message)
  {
      case Win32TrayIconMessage:
      {
          switch(LParam)
          {
              case WM_LBUTTONDOWN:
              case WM_RBUTTONDOWN:
              {
                  if (Menu) {
                    DestroyMenu(Menu);
                    Menu = 0;
                  }

                  Menu = CreatePopupMenu();

                  Win32AddMenuItem(Menu, "Set repeat offset to now ", false, true, SetRepeatOffsetToNow);
                  Win32AddMenuItem(Menu, "Reset repeat offset", false, true, SetRepeatOffsetReset);
                  Win32AddMenuItem(Menu, "Skip", SkipCurrentWindow, true, ToggleSkipCurrentWindow);
                  Win32AddSeparator(Menu);
                  Win32AddMenuItem(Menu, "Repeat", false, false, 0);
                  Win32AddMenuItem(Menu, "Every 2 hours", RepeatEnabled && RepeatInMinutes == 120, true, SetRepeatMinute120);
                  Win32AddMenuItem(Menu, "Every 1 hour", RepeatEnabled && RepeatInMinutes == 60, true, SetRepeatMinute60);
                  Win32AddMenuItem(Menu, "Every 30 minutes", RepeatEnabled && RepeatInMinutes == 30, true, SetRepeatMinutes30);
                  Win32AddMenuItem(Menu, "Every 10 minutes", RepeatEnabled && RepeatInMinutes == 10, true, SetRepeatMinutes10);
                  Win32AddMenuItem(Menu, "Disabled", !RepeatEnabled, true, SetRepeatDisabled);
                  Win32AddSeparator(Menu);
                  Win32AddMenuItem(Menu, "Exit", false, true, MenuExit);

                  SetForegroundWindow(Window);

                  POINT MousePosition = {0, 0};
                  GetCursorPos(&MousePosition);

                  TrackPopupMenu(
                      Menu,
                      TPM_LEFTBUTTON |
                      TPM_RIGHTALIGN |
                      TPM_TOPALIGN,
                      MousePosition.x, MousePosition.y,
                      0, Window, 0);
              } break;

              default: {
                // An ignored tray message
              } break;
          }
      } break;

      case WM_COMMAND: {
          i32 PickedIndex = (i32)WParam;

          menu_callback *Callback = (menu_callback *)Win32GetMenuItemExtraData(Menu, PickedIndex);
          if (Callback)
          {
              Callback();
          }

          if (Menu) {
              DestroyMenu(Menu);
              Menu = 0;
          }
      } break;

      case WM_CREATE: {
        SetTimer(Window, PAINT_TIMER_ID, 100, 0);
      } break;

      case WM_TIMER: {
        if (WParam == PAINT_TIMER_ID) {
          Paint();
        }
      } break;

      #if 0
      case WM_HOTKEY:
      {
          int32x HotKeyIndex = WParam;
          if(HotKeyIndex < HotKeyCount)
          {
              HotKeys[HotKeyIndex].Callback();
          }
      } break;
      #endif

      default:
      {
          Result = DefWindowProc(Window, Message, WParam, LParam);
      } break;
  }

  return(Result);
}

LRESULT CALLBACK WindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
  return DefWindowProc(Window, Message, WParam, LParam);
}

static HWND TrayWindow = 0;

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

  Win32RegisterWindowClass(TrayWindowClassName, GetModuleHandle(0), TrayWindowCallback);

  TrayWindow = CreateWindowEx(0, TrayWindowClassName, 0, 0,
                              0, 0, 1, 1,
                              0, 0, GetModuleHandle(0), 0);

  int ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
  int ScreenHeight = GetSystemMetrics(SM_CYSCREEN);

  i32 WindowWidth = 1000;
  i32 WindowHeight = 150;

  Win32ResizeDIBSection(CountdownDIBSection, WindowWidth, WindowHeight);

  int X = 0;
  int Y = ScreenHeight - WindowHeight;
  int Width = WindowWidth;
  int Height = WindowHeight;

  Win32RegisterWindowClass(OverlayWindowClassName, GetModuleHandle(0), WindowCallback);

  HWND hwnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                  OverlayWindowClassName, 0,
                                  WS_POPUP,
                                  X, Y, Width, Height,
                                  0, 0, GetModuleHandle(0), NULL);

  SetRepeatOffsetToNow();

  the_window = {hwnd, 0};
  Paint();

  if (hwnd && TrayWindow) {
    // Insert ourselves into the system tray
    static NOTIFYICONDATA TrayIconData;
    TrayIconData.cbSize = sizeof(NOTIFYICONDATA);
    TrayIconData.hWnd = TrayWindow;
    TrayIconData.uID = 0;
    TrayIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    TrayIconData.uCallbackMessage = Win32TrayIconMessage;
    TrayIconData.hIcon = LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(101));
    TrayIconData.szTip[0] = '\0';

    Shell_NotifyIcon(NIM_ADD, &TrayIconData);

    MSG Message;
    while (GetMessage(&Message, 0, 0, 0) > 0) {
      TranslateMessage(&Message);
      DispatchMessage(&Message);
    }

    Shell_NotifyIcon(NIM_DELETE, &TrayIconData);
  }

  ExitProcess(0);
  return 0;
}