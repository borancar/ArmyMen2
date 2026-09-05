/* ddraw.h -- DirectDraw as the game sees it: IDirectDraw, IDirectDraw2, one
 * surface interface, a palette and a clipper. C-style vtables in the SDK's
 * slot order, with the COBJMACROS the reconstruction calls through.
 *
 * DDSURFACEDESC is 0x6C bytes and DDBLTFX 0x64, both asserted by
 * surface.cpp against the values the original passes as dwSize. */
#ifndef AM2_PLATFORM_DDRAW_H
#define AM2_PLATFORM_DDRAW_H

#include <windows.h>

AM2_EXTERN_C_BEGIN

typedef struct IDirectDraw        IDirectDraw,        *LPDIRECTDRAW;
typedef struct IDirectDraw2       IDirectDraw2,       *LPDIRECTDRAW2;
typedef struct IDirectDrawSurface IDirectDrawSurface, *LPDIRECTDRAWSURFACE;
typedef struct IDirectDrawPalette IDirectDrawPalette, *LPDIRECTDRAWPALETTE;
typedef struct IDirectDrawClipper IDirectDrawClipper, *LPDIRECTDRAWCLIPPER;

extern const GUID IID_IDirectDraw;
extern const GUID IID_IDirectDraw2;
extern const GUID IID_IDirectDrawSurface;
extern const GUID IID_IDirectDrawPalette;

/* ---- results --------------------------------------------------------------- */

#define _FACDD 0x876
#define MAKE_DDHRESULT(code) MAKE_HRESULT(1, _FACDD, code)
#define DD_OK                    S_OK
#define DD_FALSE                 S_FALSE
#define DDERR_GENERIC            E_FAIL
#define DDERR_UNSUPPORTED        E_NOTIMPL
#define DDERR_OUTOFMEMORY        E_OUTOFMEMORY
#define DDERR_INVALIDPARAMS      E_INVALIDARG
#define DDERR_INVALIDOBJECT      MAKE_DDHRESULT(130)
#define DDERR_INVALIDRECT        MAKE_DDHRESULT(150)
#define DDERR_NOPALETTEATTACHED  MAKE_DDHRESULT(400)
#define DDERR_SURFACEBUSY        MAKE_DDHRESULT(430)
#define DDERR_SURFACELOST        MAKE_DDHRESULT(450)
#define DDERR_NOTFOUND           MAKE_DDHRESULT(255)
#define DDERR_WASSTILLDRAWING    MAKE_DDHRESULT(540)
#define DDERR_NODC               MAKE_DDHRESULT(570)
#define DDERR_NOTLOCKED          MAKE_DDHRESULT(584)

/* ---- structures -------------------------------------------------------------- */

typedef struct _DDSCAPS { DWORD dwCaps; } DDSCAPS, *LPDDSCAPS;

typedef struct _DDCOLORKEY {
    DWORD dwColorSpaceLowValue;
    DWORD dwColorSpaceHighValue;
} DDCOLORKEY, *LPDDCOLORKEY;

typedef struct _DDPIXELFORMAT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    union { DWORD dwRGBBitCount; DWORD dwYUVBitCount; DWORD dwZBufferBitDepth; DWORD dwAlphaBitDepth; };
    union { DWORD dwRBitMask; DWORD dwYBitMask; };
    union { DWORD dwGBitMask; DWORD dwUBitMask; };
    union { DWORD dwBBitMask; DWORD dwVBitMask; };
    union { DWORD dwRGBAlphaBitMask; DWORD dwYUVAlphaBitMask; DWORD dwRGBZBitMask; DWORD dwYUVZBitMask; };
} DDPIXELFORMAT, *LPDDPIXELFORMAT;

typedef struct _DDSURFACEDESC {
    DWORD  dwSize;
    DWORD  dwFlags;
    DWORD  dwHeight;
    DWORD  dwWidth;
    union { LONG lPitch; DWORD dwLinearSize; };
    DWORD  dwBackBufferCount;
    union { DWORD dwMipMapCount; DWORD dwZBufferBitDepth; DWORD dwRefreshRate; };
    DWORD  dwAlphaBitDepth;
    DWORD  dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay;
    DDCOLORKEY ddckCKDestBlt;
    DDCOLORKEY ddckCKSrcOverlay;
    DDCOLORKEY ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS ddsCaps;
} DDSURFACEDESC, *LPDDSURFACEDESC;

typedef struct _DDBLTFX {
    DWORD dwSize;
    DWORD dwDDFX;
    DWORD dwROP;
    DWORD dwDDROP;
    DWORD dwRotationAngle;
    DWORD dwZBufferOpCode;
    DWORD dwZBufferLow;
    DWORD dwZBufferHigh;
    DWORD dwZBufferBaseDest;
    DWORD dwZDestConstBitDepth;
    union { DWORD dwZDestConst; LPDIRECTDRAWSURFACE lpDDSZBufferDest; };
    DWORD dwZSrcConstBitDepth;
    union { DWORD dwZSrcConst; LPDIRECTDRAWSURFACE lpDDSZBufferSrc; };
    DWORD dwAlphaEdgeBlendBitDepth;
    DWORD dwAlphaEdgeBlend;
    DWORD dwReserved;
    DWORD dwAlphaDestConstBitDepth;
    union { DWORD dwAlphaDestConst; LPDIRECTDRAWSURFACE lpDDSAlphaDest; };
    DWORD dwAlphaSrcConstBitDepth;
    union { DWORD dwAlphaSrcConst; LPDIRECTDRAWSURFACE lpDDSAlphaSrc; };
    union { DWORD dwFillColor; DWORD dwFillDepth; DWORD dwFillPixel; LPDIRECTDRAWSURFACE lpDDSPattern; };
    DDCOLORKEY ddckDestColorkey;
    DDCOLORKEY ddckSrcColorkey;
} DDBLTFX, *LPDDBLTFX;

typedef struct _DDCAPS { DWORD dwSize; DWORD dwCaps; BYTE rest[0x170 - 8]; } DDCAPS, *LPDDCAPS;

/* ---- flags ------------------------------------------------------------------- */

#define DDSD_CAPS            0x00000001
#define DDSD_HEIGHT          0x00000002
#define DDSD_WIDTH           0x00000004
#define DDSD_PITCH           0x00000008
#define DDSD_BACKBUFFERCOUNT 0x00000020
#define DDSD_PIXELFORMAT     0x00001000
#define DDSD_LPSURFACE       0x00000800

#define DDSCAPS_BACKBUFFER      0x00000004
#define DDSCAPS_COMPLEX         0x00000008
#define DDSCAPS_FLIP            0x00000010
#define DDSCAPS_FRONTBUFFER     0x00000020
#define DDSCAPS_OFFSCREENPLAIN  0x00000040
#define DDSCAPS_PALETTE         0x00000100
#define DDSCAPS_PRIMARYSURFACE  0x00000200
#define DDSCAPS_SYSTEMMEMORY    0x00000800
#define DDSCAPS_VIDEOMEMORY     0x00004000
#define DDSCAPS_LOCALVIDMEM     0x10000000

#define DDSCL_FULLSCREEN   0x00000001
#define DDSCL_ALLOWREBOOT  0x00000002
#define DDSCL_NOWINDOWCHANGES 0x00000004
#define DDSCL_NORMAL       0x00000008
#define DDSCL_EXCLUSIVE    0x00000010
#define DDSCL_ALLOWMODEX   0x00000040

#define DDPF_RGB           0x00000040
#define DDPF_PALETTEINDEXED8 0x00000020

#define DDPCAPS_4BIT       0x00000001
#define DDPCAPS_8BITENTRIES 0x00000002
#define DDPCAPS_8BIT       0x00000004
#define DDPCAPS_INITIALIZE 0x00000008
#define DDPCAPS_PRIMARYSURFACE 0x00000010
#define DDPCAPS_ALLOW256   0x00000040

#define DDCKEY_COLORSPACE  0x00000001
#define DDCKEY_DESTBLT     0x00000002
#define DDCKEY_DESTOVERLAY 0x00000004
#define DDCKEY_SRCBLT      0x00000008
#define DDCKEY_SRCOVERLAY  0x00000010

#define DDBLT_ALPHADEST    0x00000001
#define DDBLT_ASYNC        0x00000200
#define DDBLT_COLORFILL    0x00000400
#define DDBLT_KEYDEST      0x00002000
#define DDBLT_KEYSRC       0x00008000
#define DDBLT_KEYSRCOVERRIDE 0x00010000
#define DDBLT_WAIT         0x01000000

#define DDBLTFAST_NOCOLORKEY   0x00000000
#define DDBLTFAST_SRCCOLORKEY  0x00000001
#define DDBLTFAST_DESTCOLORKEY 0x00000002
#define DDBLTFAST_WAIT         0x00000010

#define DDFLIP_WAIT        0x00000001

#define DDLOCK_SURFACEMEMORYPTR 0x00000000
#define DDLOCK_WAIT        0x00000001
#define DDLOCK_READONLY    0x00000010
#define DDLOCK_WRITEONLY   0x00000020

#define DDSDM_STANDARDVGAMODE 0x00000001

/* ---- IDirectDraw (v1) ------------------------------------------------------- */

typedef HRESULT (WINAPI *LPDDENUMMODESCALLBACK)(LPDDSURFACEDESC, LPVOID);
typedef HRESULT (WINAPI *LPDDENUMSURFACESCALLBACK)(LPDIRECTDRAWSURFACE, LPDDSURFACEDESC, LPVOID);

#define AM2_DDRAW_METHODS(T) \
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(T *, REFIID, LPVOID *); \
    ULONG   (STDMETHODCALLTYPE *AddRef)(T *); \
    ULONG   (STDMETHODCALLTYPE *Release)(T *); \
    HRESULT (STDMETHODCALLTYPE *Compact)(T *); \
    HRESULT (STDMETHODCALLTYPE *CreateClipper)(T *, DWORD, LPDIRECTDRAWCLIPPER *, IUnknown *); \
    HRESULT (STDMETHODCALLTYPE *CreatePalette)(T *, DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE *, IUnknown *); \
    HRESULT (STDMETHODCALLTYPE *CreateSurface)(T *, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE *, IUnknown *); \
    HRESULT (STDMETHODCALLTYPE *DuplicateSurface)(T *, LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE *); \
    HRESULT (STDMETHODCALLTYPE *EnumDisplayModes)(T *, DWORD, LPDDSURFACEDESC, LPVOID, LPDDENUMMODESCALLBACK); \
    HRESULT (STDMETHODCALLTYPE *EnumSurfaces)(T *, DWORD, LPDDSURFACEDESC, LPVOID, LPDDENUMSURFACESCALLBACK); \
    HRESULT (STDMETHODCALLTYPE *FlipToGDISurface)(T *); \
    HRESULT (STDMETHODCALLTYPE *GetCaps)(T *, LPDDCAPS, LPDDCAPS); \
    HRESULT (STDMETHODCALLTYPE *GetDisplayMode)(T *, LPDDSURFACEDESC); \
    HRESULT (STDMETHODCALLTYPE *GetFourCCCodes)(T *, LPDWORD, LPDWORD); \
    HRESULT (STDMETHODCALLTYPE *GetGDISurface)(T *, LPDIRECTDRAWSURFACE *); \
    HRESULT (STDMETHODCALLTYPE *GetMonitorFrequency)(T *, LPDWORD); \
    HRESULT (STDMETHODCALLTYPE *GetScanLine)(T *, LPDWORD); \
    HRESULT (STDMETHODCALLTYPE *GetVerticalBlankStatus)(T *, LPBOOL); \
    HRESULT (STDMETHODCALLTYPE *Initialize)(T *, GUID *); \
    HRESULT (STDMETHODCALLTYPE *RestoreDisplayMode)(T *); \
    HRESULT (STDMETHODCALLTYPE *SetCooperativeLevel)(T *, HWND, DWORD)

/* THE SLOT ORDER IS THE SDK'S, AND IT IS NOT PRIVATE. This used to put
 * SetDisplayMode last on both interfaces so one macro could declare the
 * shared prefix, on the reasoning that the reconstruction reaches every
 * method through the macros below and nothing indexes by number. The PE
 * loader in src/hybrid runs the ORIGINAL binary over this layer, and the
 * original indexes by number: its InitDirectDraw calls slot 21 of
 * IDirectDraw2 with six dwords, which in the private order was
 * WaitForVerticalBlank taking three, and it returned twelve bytes off into
 * its own HWND argument. tools/checkvtables.py compares every vtable here
 * with the SDK's declaration, slot by slot. */
typedef struct IDirectDrawVtbl {
    AM2_DDRAW_METHODS(IDirectDraw);
    HRESULT (STDMETHODCALLTYPE *SetDisplayMode)(IDirectDraw *, DWORD, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *WaitForVerticalBlank)(IDirectDraw *, DWORD, HANDLE);
} IDirectDrawVtbl;
struct IDirectDraw { const IDirectDrawVtbl *lpVtbl; };

typedef struct IDirectDraw2Vtbl {
    AM2_DDRAW_METHODS(IDirectDraw2);
    HRESULT (STDMETHODCALLTYPE *SetDisplayMode)(IDirectDraw2 *, DWORD, DWORD, DWORD, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *WaitForVerticalBlank)(IDirectDraw2 *, DWORD, HANDLE);
    HRESULT (STDMETHODCALLTYPE *GetAvailableVidMem)(IDirectDraw2 *, LPDDSCAPS, LPDWORD, LPDWORD);
} IDirectDraw2Vtbl;
struct IDirectDraw2 { const IDirectDraw2Vtbl *lpVtbl; };

#define IDirectDraw_QueryInterface(p, a, b)       ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectDraw_AddRef(p)                     ((p)->lpVtbl->AddRef(p))
#define IDirectDraw_Release(p)                    ((p)->lpVtbl->Release(p))
#define IDirectDraw_CreateClipper(p, a, b, c)     ((p)->lpVtbl->CreateClipper(p, a, b, c))
#define IDirectDraw_CreatePalette(p, a, b, c, d)  ((p)->lpVtbl->CreatePalette(p, a, b, c, d))
#define IDirectDraw_CreateSurface(p, a, b, c)     ((p)->lpVtbl->CreateSurface(p, a, b, c))
#define IDirectDraw_FlipToGDISurface(p)           ((p)->lpVtbl->FlipToGDISurface(p))
#define IDirectDraw_GetGDISurface(p, a)           ((p)->lpVtbl->GetGDISurface(p, a))
#define IDirectDraw_RestoreDisplayMode(p)         ((p)->lpVtbl->RestoreDisplayMode(p))
#define IDirectDraw_SetCooperativeLevel(p, a, b)  ((p)->lpVtbl->SetCooperativeLevel(p, a, b))
#define IDirectDraw_SetDisplayMode(p, a, b, c)    ((p)->lpVtbl->SetDisplayMode(p, a, b, c))

#define IDirectDraw2_QueryInterface(p, a, b)      ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectDraw2_AddRef(p)                    ((p)->lpVtbl->AddRef(p))
#define IDirectDraw2_Release(p)                   ((p)->lpVtbl->Release(p))
#define IDirectDraw2_CreateClipper(p, a, b, c)    ((p)->lpVtbl->CreateClipper(p, a, b, c))
#define IDirectDraw2_CreatePalette(p, a, b, c, d) ((p)->lpVtbl->CreatePalette(p, a, b, c, d))
#define IDirectDraw2_CreateSurface(p, a, b, c)    ((p)->lpVtbl->CreateSurface(p, a, b, c))
#define IDirectDraw2_FlipToGDISurface(p)          ((p)->lpVtbl->FlipToGDISurface(p))
#define IDirectDraw2_GetGDISurface(p, a)          ((p)->lpVtbl->GetGDISurface(p, a))
#define IDirectDraw2_RestoreDisplayMode(p)        ((p)->lpVtbl->RestoreDisplayMode(p))
#define IDirectDraw2_SetCooperativeLevel(p, a, b) ((p)->lpVtbl->SetCooperativeLevel(p, a, b))
#define IDirectDraw2_SetDisplayMode(p, a, b, c, d, e) ((p)->lpVtbl->SetDisplayMode(p, a, b, c, d, e))
#define IDirectDraw2_GetAvailableVidMem(p, a, b, c) ((p)->lpVtbl->GetAvailableVidMem(p, a, b, c))

/* ---- IDirectDrawSurface (v1) ------------------------------------------------- */

typedef struct IDirectDrawSurfaceVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectDrawSurface *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectDrawSurface *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectDrawSurface *);
    HRESULT (STDMETHODCALLTYPE *AddAttachedSurface)(IDirectDrawSurface *, LPDIRECTDRAWSURFACE);
    HRESULT (STDMETHODCALLTYPE *AddOverlayDirtyRect)(IDirectDrawSurface *, LPRECT);
    HRESULT (STDMETHODCALLTYPE *Blt)(IDirectDrawSurface *, LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDBLTFX);
    HRESULT (STDMETHODCALLTYPE *BltBatch)(IDirectDrawSurface *, LPVOID, DWORD, DWORD);
    HRESULT (STDMETHODCALLTYPE *BltFast)(IDirectDrawSurface *, DWORD, DWORD, LPDIRECTDRAWSURFACE, LPRECT, DWORD);
    HRESULT (STDMETHODCALLTYPE *DeleteAttachedSurface)(IDirectDrawSurface *, DWORD, LPDIRECTDRAWSURFACE);
    HRESULT (STDMETHODCALLTYPE *EnumAttachedSurfaces)(IDirectDrawSurface *, LPVOID, LPDDENUMSURFACESCALLBACK);
    HRESULT (STDMETHODCALLTYPE *EnumOverlayZOrders)(IDirectDrawSurface *, DWORD, LPVOID, LPDDENUMSURFACESCALLBACK);
    HRESULT (STDMETHODCALLTYPE *Flip)(IDirectDrawSurface *, LPDIRECTDRAWSURFACE, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetAttachedSurface)(IDirectDrawSurface *, LPDDSCAPS, LPDIRECTDRAWSURFACE *);
    HRESULT (STDMETHODCALLTYPE *GetBltStatus)(IDirectDrawSurface *, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetCaps)(IDirectDrawSurface *, LPDDSCAPS);
    HRESULT (STDMETHODCALLTYPE *GetClipper)(IDirectDrawSurface *, LPDIRECTDRAWCLIPPER *);
    HRESULT (STDMETHODCALLTYPE *GetColorKey)(IDirectDrawSurface *, DWORD, LPDDCOLORKEY);
    HRESULT (STDMETHODCALLTYPE *GetDC)(IDirectDrawSurface *, HDC *);
    HRESULT (STDMETHODCALLTYPE *GetFlipStatus)(IDirectDrawSurface *, DWORD);
    HRESULT (STDMETHODCALLTYPE *GetOverlayPosition)(IDirectDrawSurface *, LPLONG, LPLONG);
    HRESULT (STDMETHODCALLTYPE *GetPalette)(IDirectDrawSurface *, LPDIRECTDRAWPALETTE *);
    HRESULT (STDMETHODCALLTYPE *GetPixelFormat)(IDirectDrawSurface *, LPDDPIXELFORMAT);
    HRESULT (STDMETHODCALLTYPE *GetSurfaceDesc)(IDirectDrawSurface *, LPDDSURFACEDESC);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IDirectDrawSurface *, LPDIRECTDRAW, LPDDSURFACEDESC);
    HRESULT (STDMETHODCALLTYPE *IsLost)(IDirectDrawSurface *);
    HRESULT (STDMETHODCALLTYPE *Lock)(IDirectDrawSurface *, LPRECT, LPDDSURFACEDESC, DWORD, HANDLE);
    HRESULT (STDMETHODCALLTYPE *ReleaseDC)(IDirectDrawSurface *, HDC);
    HRESULT (STDMETHODCALLTYPE *Restore)(IDirectDrawSurface *);
    HRESULT (STDMETHODCALLTYPE *SetClipper)(IDirectDrawSurface *, LPDIRECTDRAWCLIPPER);
    HRESULT (STDMETHODCALLTYPE *SetColorKey)(IDirectDrawSurface *, DWORD, LPDDCOLORKEY);
    HRESULT (STDMETHODCALLTYPE *SetOverlayPosition)(IDirectDrawSurface *, LONG, LONG);
    HRESULT (STDMETHODCALLTYPE *SetPalette)(IDirectDrawSurface *, LPDIRECTDRAWPALETTE);
    HRESULT (STDMETHODCALLTYPE *Unlock)(IDirectDrawSurface *, LPVOID);
    HRESULT (STDMETHODCALLTYPE *UpdateOverlay)(IDirectDrawSurface *, LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPVOID);
    HRESULT (STDMETHODCALLTYPE *UpdateOverlayDisplay)(IDirectDrawSurface *, DWORD);
    HRESULT (STDMETHODCALLTYPE *UpdateOverlayZOrder)(IDirectDrawSurface *, DWORD, LPDIRECTDRAWSURFACE);
} IDirectDrawSurfaceVtbl;
struct IDirectDrawSurface { const IDirectDrawSurfaceVtbl *lpVtbl; };

#define IDirectDrawSurface_QueryInterface(p, a, b)     ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectDrawSurface_AddRef(p)                   ((p)->lpVtbl->AddRef(p))
#define IDirectDrawSurface_Release(p)                  ((p)->lpVtbl->Release(p))
#define IDirectDrawSurface_Blt(p, a, b, c, d, e)       ((p)->lpVtbl->Blt(p, a, b, c, d, e))
#define IDirectDrawSurface_BltFast(p, a, b, c, d, e)   ((p)->lpVtbl->BltFast(p, a, b, c, d, e))
#define IDirectDrawSurface_Flip(p, a, b)               ((p)->lpVtbl->Flip(p, a, b))
#define IDirectDrawSurface_GetAttachedSurface(p, a, b) ((p)->lpVtbl->GetAttachedSurface(p, a, b))
#define IDirectDrawSurface_GetColorKey(p, a, b)        ((p)->lpVtbl->GetColorKey(p, a, b))
#define IDirectDrawSurface_GetDC(p, a)                 ((p)->lpVtbl->GetDC(p, a))
#define IDirectDrawSurface_GetPalette(p, a)            ((p)->lpVtbl->GetPalette(p, a))
#define IDirectDrawSurface_GetSurfaceDesc(p, a)        ((p)->lpVtbl->GetSurfaceDesc(p, a))
#define IDirectDrawSurface_IsLost(p)                   ((p)->lpVtbl->IsLost(p))
#define IDirectDrawSurface_Lock(p, a, b, c, d)         ((p)->lpVtbl->Lock(p, a, b, c, d))
#define IDirectDrawSurface_ReleaseDC(p, a)             ((p)->lpVtbl->ReleaseDC(p, a))
#define IDirectDrawSurface_Restore(p)                  ((p)->lpVtbl->Restore(p))
#define IDirectDrawSurface_SetClipper(p, a)            ((p)->lpVtbl->SetClipper(p, a))
#define IDirectDrawSurface_SetColorKey(p, a, b)        ((p)->lpVtbl->SetColorKey(p, a, b))
#define IDirectDrawSurface_SetPalette(p, a)            ((p)->lpVtbl->SetPalette(p, a))
#define IDirectDrawSurface_Unlock(p, a)                ((p)->lpVtbl->Unlock(p, a))

/* ---- IDirectDrawPalette ------------------------------------------------------- */

typedef struct IDirectDrawPaletteVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectDrawPalette *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectDrawPalette *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectDrawPalette *);
    HRESULT (STDMETHODCALLTYPE *GetCaps)(IDirectDrawPalette *, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetEntries)(IDirectDrawPalette *, DWORD, DWORD, DWORD, LPPALETTEENTRY);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IDirectDrawPalette *, LPDIRECTDRAW, DWORD, LPPALETTEENTRY);
    HRESULT (STDMETHODCALLTYPE *SetEntries)(IDirectDrawPalette *, DWORD, DWORD, DWORD, LPPALETTEENTRY);
} IDirectDrawPaletteVtbl;
struct IDirectDrawPalette { const IDirectDrawPaletteVtbl *lpVtbl; };

#define IDirectDrawPalette_QueryInterface(p, a, b) ((p)->lpVtbl->QueryInterface(p, a, b))
#define IDirectDrawPalette_AddRef(p)               ((p)->lpVtbl->AddRef(p))
#define IDirectDrawPalette_Release(p)              ((p)->lpVtbl->Release(p))
#define IDirectDrawPalette_GetEntries(p, a, b, c, d) ((p)->lpVtbl->GetEntries(p, a, b, c, d))
#define IDirectDrawPalette_SetEntries(p, a, b, c, d) ((p)->lpVtbl->SetEntries(p, a, b, c, d))

/* ---- IDirectDrawClipper ------------------------------------------------------- */

typedef struct IDirectDrawClipperVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDirectDrawClipper *, REFIID, LPVOID *);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IDirectDrawClipper *);
    ULONG   (STDMETHODCALLTYPE *Release)(IDirectDrawClipper *);
    HRESULT (STDMETHODCALLTYPE *GetClipList)(IDirectDrawClipper *, LPRECT, LPVOID, LPDWORD);
    HRESULT (STDMETHODCALLTYPE *GetHWnd)(IDirectDrawClipper *, HWND *);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IDirectDrawClipper *, LPDIRECTDRAW, DWORD);
    HRESULT (STDMETHODCALLTYPE *IsClipListChanged)(IDirectDrawClipper *, BOOL *);
    HRESULT (STDMETHODCALLTYPE *SetClipList)(IDirectDrawClipper *, LPVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE *SetHWnd)(IDirectDrawClipper *, DWORD, HWND);
} IDirectDrawClipperVtbl;
struct IDirectDrawClipper { const IDirectDrawClipperVtbl *lpVtbl; };

#define IDirectDrawClipper_Release(p)   ((p)->lpVtbl->Release(p))
#define IDirectDrawClipper_SetHWnd(p, a, b) ((p)->lpVtbl->SetHWnd(p, a, b))

HRESULT WINAPI DirectDrawCreate(GUID *guid, LPDIRECTDRAW *out, IUnknown *outer);

AM2_EXTERN_C_END

#endif /* AM2_PLATFORM_DDRAW_H */
