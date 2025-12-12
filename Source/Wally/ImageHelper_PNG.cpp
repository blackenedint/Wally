/////////////////////////////////////////////////////////////////////////////
//                           Wally the WAL Editor
//---------------------------------------------------------------------------
//                             © Copyright 1998,
//                      Ty Matthews and Neal White III,
//                           All rights reserved.
//---------------------------------------------------------------------------
//  ImageHelper.cpp : implementation of the CImageHelper class
//
//  Created by Ty Matthews, 5-8-2001
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "ImageHelper.h"
#include "MiscFunctions.h"
#include "Wally.h"
#include "TextureFlags.h"
#include "WallyPal.h"
#include "ColorOpt.h"
#include "WADList.h"
#include "RegistryHelper.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

////////////////////////////////////////////////////////////////////////////////////
//   PNG Support
////////////////////////////////////////////////////////////////////////////////////
BOOL CImageHelper::DecodePNG(int iFlags /* = 0 */)
{
	// Decode PNG from the in-memory buffer using stb_image
	// m_pbyEncodedData + GetDataSize() already contains the file data. :contentReference[oaicite:1]{index=1}
	int width = 0;
	int height = 0;
	int channelsInFile = 0;

	if (!stbi_info_from_memory(m_pbyEncodedData, (int)GetDataSize(), &width, &height, &channelsInFile))
	{
		SetErrorCode(IH_PNG_MALFORMED);
		SetErrorText(_T("stb_image: failed to read PNG header"));
		return FALSE;
	}

	SetImageWidth(width);
	SetImageHeight(height);

	// Map stb channels to our color depth
	int colorDepth = IH_24BIT;
	if (channelsInFile == 1)
		colorDepth = IH_8BIT;       // grayscale
	else if (channelsInFile == 2)
		colorDepth = IH_32BIT;      // gray+alpha -> RGBA
	else if (channelsInFile == 3)
		colorDepth = IH_24BIT;      // RGB
	else
		colorDepth = IH_32BIT;      // RGBA or anything >=4

	SetColorDepth(colorDepth);

	// If the caller only wanted dimensions, we’re done.
	if (iFlags & IH_LOAD_DIMENSIONS)
		return TRUE;

	// Decide the number of channels we actually want in memory
	int desiredChannels = 0;
	if (colorDepth == IH_8BIT)
		desiredChannels = 1;
	else if (colorDepth == IH_24BIT)
		desiredChannels = 3;
	else
		desiredChannels = 4;

	int w = 0, h = 0, actualChannels = 0;
	unsigned char* data = stbi_load_from_memory(
		m_pbyEncodedData,
		(int)GetDataSize(),
		&w,
		&h,
		&actualChannels,
		desiredChannels
	);

	if (!data)
	{
		SetErrorCode(IH_PNG_MALFORMED);
		CString msg;
		msg.Format(_T("stb_image: %hs"), stbi_failure_reason());
		SetErrorText(msg);
		return FALSE;
	}

	// Update dimensions from stb (just in case)
	SetImageWidth(w);
	SetImageHeight(h);

	const size_t pixelCount = (size_t)w * (size_t)h;

	if (colorDepth == IH_8BIT)
	{
		// 8-bit grayscale: keep indices as-is and build a grayscale palette.
		m_pbyDecodedData = new BYTE[pixelCount];
		if (!m_pbyDecodedData)
		{
			stbi_image_free(data);
			SetErrorCode(IH_OUT_OF_MEMORY);
			return FALSE;
		}

		memcpy(m_pbyDecodedData, data, pixelCount);

		// Build grayscale palette
		for (int i = 0; i < 256; ++i)
		{
			m_byPalette[i * 3 + 0] = (BYTE)i;
			m_byPalette[i * 3 + 1] = (BYTE)i;
			m_byPalette[i * 3 + 2] = (BYTE)i;
		}
	}
	else if (colorDepth == IH_24BIT)
	{
		const size_t bytes = pixelCount * 3;
		m_pbyDecodedData = new BYTE[bytes];
		if (!m_pbyDecodedData)
		{
			stbi_image_free(data);
			SetErrorCode(IH_OUT_OF_MEMORY);
			return FALSE;
		}
		memcpy(m_pbyDecodedData, data, bytes);
	}
	else // IH_32BIT
	{
		const size_t bytes = pixelCount * 4;
		m_pbyDecodedData = new BYTE[bytes];
		if (!m_pbyDecodedData)
		{
			stbi_image_free(data);
			SetErrorCode(IH_OUT_OF_MEMORY);
			return FALSE;
		}
		memcpy(m_pbyDecodedData, data, bytes);
	}

	stbi_image_free(data);
	return TRUE;
}

// stb write callback for PNG – uses CImageHelper::UseArchive / GetArchive / GetPNGWriteFile
static void StbPngWriteCallback(void* context, void* data, int size)
{
	CImageHelper* pThis = static_cast<CImageHelper*>(context);
	if (!pThis || size <= 0)
		return;

	if (pThis->UseArchive())
	{
		CArchive* pArchive = pThis->GetArchive();
		if (pArchive)
			pArchive->Write(data, (UINT)size);
	}
	else
	{
		FILE* fp = pThis->GetPNGWriteFile();
		if (fp)
		{
			fwrite(data, 1, (size_t)size, fp);
		}
	}
}

BOOL CImageHelper::EncodePNG()
{
	const int iWidth = GetImageWidth();
	const int iHeight = GetImageHeight();
	const int iBitDepth = GetColorDepth();   // 8, 24, or 32. :contentReference[oaicite:3]{index=3}

	if (iWidth <= 0 || iHeight <= 0 || !m_pbyDecodedData)
	{
		SetErrorCode(IH_ERROR_WRITING_FILE);
		return FALSE;
	}

	// Open output file if not using an archive (PNG used to do this too). :contentReference[oaicite:4]{index=4}
	if (!UseArchive())
	{
		if (m_fpPNGOutput)
		{
			// Already open? treat as error like the old code.
			SetErrorCode(IH_ERROR_WRITING_FILE);
			return FALSE;
		}

		errno_t err = fopen_s(&m_fpPNGOutput, m_strFileName, "wb");
		if (err != 0 || !m_fpPNGOutput)
		{
			SetErrorCode(IH_ERROR_WRITING_FILE);
			return FALSE;
		}
	}

	BYTE* pPixels = nullptr;
	int components = 0;
	CMemBuffer tempBuffer; // used when we need to expand 8-bit -> 24-bit like EncodeJPG did. :contentReference[oaicite:5]{index=5}

	switch (iBitDepth)
	{
	case IH_8BIT:
	{
		// Expand indexed 8-bit through the palette into 24-bit RGB
		const size_t pixelCount = (size_t)iWidth * (size_t)iHeight;
		BYTE* pDest = tempBuffer.GetBuffer((UINT)(pixelCount * 3), 0);
		if (!pDest)
		{
			if (!UseArchive() && m_fpPNGOutput)
			{
				fclose(m_fpPNGOutput);
				m_fpPNGOutput = NULL;
			}
			SetErrorCode(IH_OUT_OF_MEMORY);
			return FALSE;
		}

		BYTE* pSrc = m_pbyDecodedData;
		for (size_t i = 0; i < pixelCount; ++i)
		{
			BYTE idx = pSrc[i];
			BYTE* pPal = &m_byPalette[idx * 3];
			pDest[i * 3 + 0] = pPal[0];
			pDest[i * 3 + 1] = pPal[1];
			pDest[i * 3 + 2] = pPal[2];
		}

		pPixels = pDest;
		components = 3;
	}
	break;

	case IH_24BIT:
		pPixels = m_pbyDecodedData;
		components = 3;
		break;

	case IH_32BIT:
		pPixels = m_pbyDecodedData;
		components = 4;
		break;

	default:
		if (!UseArchive() && m_fpPNGOutput)
		{
			fclose(m_fpPNGOutput);
			m_fpPNGOutput = NULL;
		}
		ASSERT(FALSE);
		SetErrorCode(IH_ERROR_WRITING_FILE);
		return FALSE;
	}

	const int stride = iWidth * components;

	int ok = stbi_write_png_to_func(
		StbPngWriteCallback,
		this,
		iWidth,
		iHeight,
		components,
		pPixels,
		stride
	);

	if (!UseArchive())
	{
		if (m_fpPNGOutput)
		{
			fclose(m_fpPNGOutput);
			m_fpPNGOutput = NULL;
		}
	}

	if (!ok)
	{
		SetErrorCode(IH_ERROR_WRITING_FILE);
		return FALSE;
	}

	return TRUE;
}

