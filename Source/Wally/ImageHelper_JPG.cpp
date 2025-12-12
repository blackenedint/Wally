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
//   JPG Support
////////////////////////////////////////////////////////////////////////////////////

BOOL CImageHelper::DecodeJPG(int iFlags /* = 0 */)
{
	int width = 0;
	int height = 0;
	int channelsInFile = 0;

	// Read header only first, for dimensions.
	if (!stbi_info_from_memory(m_pbyEncodedData, (int)GetDataSize(), &width, &height, &channelsInFile))
	{
		SetErrorCode(IH_JPG_MALFORMED);
		SetErrorText(_T("stb_image: failed to read JPG header"));
		return FALSE;
	}

	SetImageWidth(width);
	SetImageHeight(height);

	// Wally always treated JPG as 24-bit RGB. :contentReference[oaicite:7]{index=7}
	SetColorDepth(IH_24BIT);

	if (iFlags & IH_LOAD_DIMENSIONS)
		return TRUE;

	// Decode as 3-channel RGB (stb will upconvert grayscale for us).
	int w = 0, h = 0, actualChannels = 0;
	unsigned char* data = stbi_load_from_memory(
		m_pbyEncodedData,
		(int)GetDataSize(),
		&w,
		&h,
		&actualChannels,
		3
	);

	if (!data)
	{
		SetErrorCode(IH_JPG_MALFORMED);
		CString msg;
		msg.Format(_T("stb_image: %hs"), stbi_failure_reason());
		SetErrorText(msg);
		return FALSE;
	}

	SetImageWidth(w);
	SetImageHeight(h);

	const size_t bytes = (size_t)w * (size_t)h * 3;
	m_pbyDecodedData = new BYTE[bytes];
	if (!m_pbyDecodedData)
	{
		stbi_image_free(data);
		SetErrorCode(IH_OUT_OF_MEMORY);
		return FALSE;
	}

	memcpy(m_pbyDecodedData, data, bytes);
	stbi_image_free(data);

	return TRUE;
}

//////////////////////////////////////////////////////////////////////////////////////////
// This stuff is for encoding JPGs
//////////////////////////////////////////////////////////////////////////////////////////
struct StbJpegWriteContext
{
	CImageHelper* pThis;
	FILE* pFile;   // null if writing to archive
};

static void StbJpegWriteCallback(void* context, void* data, int size)
{
	StbJpegWriteContext* ctx = static_cast<StbJpegWriteContext*>(context);
	if (!ctx || !ctx->pThis || size <= 0)
		return;

	if (ctx->pThis->UseArchive())
	{
		CArchive* pArchive = ctx->pThis->GetArchive();
		if (pArchive)
			pArchive->Write(data, (UINT)size);
	}
	else if (ctx->pFile)
	{
		fwrite(data, 1, (size_t)size, ctx->pFile);
	}
}
BOOL CImageHelper::EncodeJPG()
{
	const int iWidth = GetImageWidth();
	const int iHeight = GetImageHeight();
	const int iColorDepth = GetColorDepth();  // should be IH_8BIT or IH_24BIT as before.

	if (iWidth <= 0 || iHeight <= 0 || !m_pbyDecodedData)
	{
		SetErrorCode(IH_ERROR_WRITING_FILE);
		return FALSE;
	}

	BYTE* pSourceData = nullptr;
	CMemBuffer mbSourceData;

	switch (iColorDepth)
	{
	case IH_8BIT:
	{
		// Expand to 24-bit through m_byPalette, as the old code did. :contentReference[oaicite:9]{index=9}
		const size_t pixelCount = (size_t)iWidth * (size_t)iHeight;
		BYTE* pDest = mbSourceData.GetBuffer((UINT)(pixelCount * 3), 0);
		if (!pDest)
		{
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

		pSourceData = pDest;
	}
	break;

	case IH_24BIT:
		pSourceData = m_pbyDecodedData;
		break;

	default:
		ASSERT(FALSE);
		SetErrorCode(IH_ERROR_WRITING_FILE);
		return FALSE;
	}

	StbJpegWriteContext ctx;
	ctx.pThis = this;
	ctx.pFile = nullptr;

	if (!UseArchive())
	{
		FILE* fp = nullptr;
		errno_t err = fopen_s(&fp, m_strFileName, "wb");
		if (err != 0 || !fp)
		{
			SetErrorCode(IH_ERROR_WRITING_FILE);
			return FALSE;
		}
		ctx.pFile = fp;
	}

	// Quality 100, like the old jpeg code. :contentReference[oaicite:10]{index=10}
	const int quality = 100;

	int ok = stbi_write_jpg_to_func(
		StbJpegWriteCallback,
		&ctx,
		iWidth,
		iHeight,
		3,              // RGB
		pSourceData,
		quality
	);

	if (!UseArchive() && ctx.pFile)
	{
		fclose(ctx.pFile);
		ctx.pFile = nullptr;
	}

	if (!ok)
	{
		SetErrorCode(IH_ERROR_WRITING_FILE);
		return FALSE;
	}

	return TRUE;
}
