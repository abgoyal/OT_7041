/*
 * Copyright 2007, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ImageBuffer.h"

#include "Base64.h"
#include "BitmapImage.h"
#include "ColorSpace.h"
#include "GraphicsContext.h"
#include "NotImplemented.h"
#include "PlatformBridge.h"
#include "PlatformGraphicsContext.h"
#include "PlatformGraphicsContextSkia.h"
#include "SkBitmapRef.h"
#include "SkCanvas.h"
#include "SkColorPriv.h"
#include "SkData.h"
#include "SkDevice.h"
#include "SkImageEncoder.h"
#include "SkStream.h"
#include "SkUnPreMultiply.h"

#include "AndroidLog.h"
/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
#include "ImageData.h"
#endif
/// @}

// for debug
#define DEBUG_CANVAS 0
#include "AndroidLog.h"

#if DEBUG_CANVAS
#define XLOG(...) android_printLog(ANDROID_LOG_DEBUG, "HTML5Canvas", __VA_ARGS__)
#else
#define XLOG(...)
#endif

using namespace std;

namespace WebCore {

SkCanvas* imageBufferCanvas(const ImageBuffer* buffer)
{
    // We know that our PlatformGraphicsContext is a PlatformGraphicsContextSkia
    // because that is what we create in GraphicsContext::createOffscreenContext
    if (!buffer || !buffer->context())
        return 0;
    PlatformGraphicsContext* pc = buffer->context()->platformContext();
    return static_cast<PlatformGraphicsContextSkia*>(pc)->canvas();
}

ImageBufferData::ImageBufferData(const IntSize&)
{
}

ImageBuffer::ImageBuffer(const IntSize& size, ColorSpace, RenderingMode, bool& success)
    : m_data(size)
    , m_size(size)
{
    // GraphicsContext creates a 32bpp SkBitmap, so 4 bytes per pixel.
    if (!PlatformBridge::canSatisfyMemoryAllocation(size.width() * size.height() * 4))
        success = false;
    else {
        m_context.set(GraphicsContext::createOffscreenContext(size.width(), size.height()));
        success = true;
    }
}

ImageBuffer::~ImageBuffer()
{
}

GraphicsContext* ImageBuffer::context() const
{
    return m_context.get();
}

bool ImageBuffer::drawsUsingCopy() const
{
    return true;
}

PassRefPtr<Image> ImageBuffer::copyImage() const
{
    ASSERT(context());

    SkCanvas* canvas = imageBufferCanvas(this);
    if (!canvas)
      return 0;

    SkDevice* device = canvas->getDevice();
    const SkBitmap& orig = device->accessBitmap(false);

    SkBitmap copy;
    if (PlatformBridge::canSatisfyMemoryAllocation(orig.getSize()))
        orig.copyTo(&copy, orig.config());

    SkBitmapRef* ref = new SkBitmapRef(copy);
    RefPtr<Image> image = BitmapImage::create(ref, 0);
    ref->unref();
    return image;
}

/// M: added for HTML5-benchmark performance @{
PassRefPtr<Image> ImageBuffer::getContextImageRef() const
{
    SkCanvas* canvas = imageBufferCanvas(this);
    if (!canvas)
        return 0;
    SkDevice* device = canvas->getDevice();
    const SkBitmap& orig = device->accessBitmap(false);

    SkBitmapRef* ref = new SkBitmapRef(orig);
    RefPtr<Image> image = BitmapImage::create(ref, 0);
    ref->unref();

    return image;
}
/// M: @}

void ImageBuffer::clip(GraphicsContext* context, const FloatRect& rect) const
{
    SkDebugf("xxxxxxxxxxxxxxxxxx clip not implemented\n");
}

void ImageBuffer::draw(GraphicsContext* context, ColorSpace styleColorSpace, const FloatRect& destRect, const FloatRect& srcRect, CompositeOperator op, bool useLowQualityScale)
{
    /// M: modified for HTML5-benchmark performance
    /// just get image ref from cavnas
    /// skia drawImage will copy again.
    RefPtr<Image> imageCopy = getContextImageRef();
    context->drawImage(imageCopy.get(), styleColorSpace, destRect, srcRect, op, useLowQualityScale);
}

void ImageBuffer::drawPattern(GraphicsContext* context, const FloatRect& srcRect, const AffineTransform& patternTransform, const FloatPoint& phase, ColorSpace styleColorSpace, CompositeOperator op, const FloatRect& destRect)
{
    RefPtr<Image> imageCopy = copyImage();
    imageCopy->drawPattern(context, srcRect, patternTransform, phase, styleColorSpace, op, destRect);
}

PassRefPtr<ByteArray> ImageBuffer::getUnmultipliedImageData(const IntRect& rect) const
{
    GraphicsContext* gc = this->context();
    if (!gc) {
        return 0;
    }

    const SkBitmap& src = imageBufferCanvas(this)->getDevice()->accessBitmap(false);
    SkAutoLockPixels alp(src);
    if (!src.getPixels()) {
        return 0;
    }

    RefPtr<ByteArray> result = ByteArray::create(rect.width() * rect.height() * 4);
    unsigned char* data = result->data();

    if (rect.x() < 0 || rect.y() < 0 || rect.maxX() > m_size.width() || rect.maxY() > m_size.height())
        memset(data, 0, result->length());

    int originx = rect.x();
    int destx = 0;
    if (originx < 0) {
        destx = -originx;
        originx = 0;
    }
    int endx = rect.x() + rect.width();
    if (endx > m_size.width())
        endx = m_size.width();
    int numColumns = endx - originx;

    int originy = rect.y();
    int desty = 0;
    if (originy < 0) {
        desty = -originy;
        originy = 0;
    }
    int endy = rect.y() + rect.height();
    if (endy > m_size.height())
        endy = m_size.height();
    int numRows = endy - originy;

    unsigned srcPixelsPerRow = src.rowBytesAsPixels();
    unsigned destBytesPerRow = 4 * rect.width();

    /// M: change process to read pixel @{
    /* fix w3c pixel manipulation relative test case */
    SkBitmap destBitmap;
    destBitmap.setConfig(SkBitmap::kARGB_8888_Config, rect.width(), rect.height(), destBytesPerRow);
    destBitmap.setPixels(data);

    SkCanvas* canvas = imageBufferCanvas(this);
    canvas->readPixels(&destBitmap, rect.x(), rect.y(), SkCanvas::kRGBA_Unpremul_Config8888);
    return result.release();
    /// @}
}

void ImageBuffer::putUnmultipliedImageData(ByteArray* source, const IntSize& sourceSize, const IntRect& sourceRect, const IntPoint& destPoint)
{
    GraphicsContext* gc = this->context();
    if (!gc) {
        return;
    }

    ASSERT(sourceRect.width() > 0);
    ASSERT(sourceRect.height() > 0);

    int originx = sourceRect.x();
    int destx = destPoint.x() + sourceRect.x();
    ASSERT(destx >= 0);
    ASSERT(destx < m_size.width());
    ASSERT(originx >= 0);
    ASSERT(originx <= sourceRect.maxX());

    int endx = destPoint.x() + sourceRect.maxX();
    ASSERT(endx <= m_size.width());

    int numColumns = endx - destx;

    int originy = sourceRect.y();
    int desty = destPoint.y() + sourceRect.y();
    ASSERT(desty >= 0);
    ASSERT(desty < m_size.height());
    ASSERT(originy >= 0);
    ASSERT(originy <= sourceRect.maxY());

    int endy = destPoint.y() + sourceRect.maxY();
    ASSERT(endy <= m_size.height());
    int numRows = endy - desty;

    unsigned srcBytesPerRow = 4 * sourceSize.width();

    SkBitmap srcBitmap;
    /// M: change process to write pixel @{
    /* fix w3c pixel manipulation relative test case */
    srcBitmap.setConfig(SkBitmap::kARGB_8888_Config, numColumns, numRows, srcBytesPerRow);
    srcBitmap.setPixels(source->data() + originy * srcBytesPerRow + originx * 4);

    SkCanvas* canvas = imageBufferCanvas(this);
    canvas->writePixels(srcBitmap, destx, desty, SkCanvas::kRGBA_Unpremul_Config8888);
    /// @}
}


String ImageBuffer::toDataURL(const String&, const double*) const
{
    // Encode the image into a vector.
    SkDynamicMemoryWStream pngStream;
    const SkBitmap& dst = imageBufferCanvas(this)->getDevice()->accessBitmap(true);
    SkImageEncoder::EncodeStream(&pngStream, dst, SkImageEncoder::kPNG_Type, 100);

    // Convert it into base64.
    Vector<char> pngEncodedData;
    SkData* streamData = pngStream.copyToData();
    pngEncodedData.append((char*)streamData->data(), streamData->size());
    streamData->unref();
    Vector<char> base64EncodedData;
    base64Encode(pngEncodedData, base64EncodedData);
    // Append with a \0 so that it's a valid string.
    base64EncodedData.append('\0');

    // And the resulting string.
    return String::format("data:image/png;base64,%s", base64EncodedData.data());
}

void ImageBuffer::platformTransformColorSpace(const Vector<int>& lookupTable)
{
    notImplemented();
}

size_t ImageBuffer::dataSize() const
{
    return m_size.width() * m_size.height() * 4;
}

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
String ByteArrayToDataURL(const PassRefPtr<ByteArray> byteArray, const IntSize& size, const String& mimeType, const double* quality)
{
    // Encode the image into a vector.
    SkDynamicMemoryWStream pngStream;
    SkBitmap srcBitmap;
    srcBitmap.setConfig(SkBitmap::kARGB_8888_Config, size.width(), size.height());
    srcBitmap.setPixels(byteArray->data());
    SkImageEncoder::EncodeStream(&pngStream, srcBitmap, SkImageEncoder::kPNG_Type, 100);
    // Convert it into base64.
    Vector<char> pngEncodedData;
    SkData* streamData = pngStream.copyToData();
    pngEncodedData.append((char*)streamData->data(), streamData->size());
    XLOG("ImageDataToDataURL streamData->size()=%d", streamData->size());
    streamData->unref();
    Vector<char> base64EncodedData;
    base64Encode(pngEncodedData, base64EncodedData);
    // Append with a \0 so that it's a valid string.
    base64EncodedData.append('\0');

    // And the resulting string.
    return String::format("data:image/png;base64,%s", base64EncodedData.data());
}
#endif
/// @}

}
