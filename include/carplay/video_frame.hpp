#pragma once
#ifdef DASH_CARPLAY
#include <QImage>
#include <QMutex>
#include <gst/video/video.h>
#include <memory>

// The mapped decoder buffer stays referenced until its last reader releases it.
// Rendering never holds the client's frame mutex or copies decoded pixels.
class CarPlayVideoFrame {
 public:
    static std::shared_ptr<const CarPlayVideoFrame> map(GstSample *sample);
    ~CarPlayVideoFrame();
    QSize size() const { return QSize(GST_VIDEO_FRAME_WIDTH(&frame_), GST_VIDEO_FRAME_HEIGHT(&frame_)); }
    const GstVideoInfo &info() const { return frame_.info; }
    const uchar *plane(int index) const { return static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame_, index)); }
    int stride(int index) const { return GST_VIDEO_FRAME_PLANE_STRIDE(&frame_, index); }
    QImage image() const; // Lazy fallback for headless/software rendering and snapshots.
 private:
    CarPlayVideoFrame() = default;
    GstVideoFrame frame_ = {};
    bool mapped_ = false;
    mutable QMutex imageMutex_;
    mutable QImage image_;
};
#endif
