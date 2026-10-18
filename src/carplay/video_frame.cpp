#ifdef DASH_CARPLAY
#include "carplay/video_frame.hpp"
#include <QMutexLocker>
#include <gst/video/video-converter.h>

std::shared_ptr<const CarPlayVideoFrame> CarPlayVideoFrame::map(GstSample *sample) {
    GstVideoInfo info;
    if (!sample || !gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) ||
        GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_I420) return {};
    auto frame = std::shared_ptr<CarPlayVideoFrame>(new CarPlayVideoFrame);
    frame->mapped_ = gst_video_frame_map(&frame->frame_, &info, gst_sample_get_buffer(sample), GST_MAP_READ);
    if (!frame->mapped_) return {};
    return frame;
}

CarPlayVideoFrame::~CarPlayVideoFrame() {
    if (mapped_) gst_video_frame_unmap(&frame_);
}

QImage CarPlayVideoFrame::image() const {
    QMutexLocker guard(&imageMutex_);
    if (!image_.isNull()) return image_;
    GstVideoInfo output;
    gst_video_info_set_format(&output, GST_VIDEO_FORMAT_BGRx, size().width(), size().height());
    output.fps_n = frame_.info.fps_n; output.fps_d = frame_.info.fps_d;
    output.par_n = frame_.info.par_n; output.par_d = frame_.info.par_d;
    output.interlace_mode = frame_.info.interlace_mode;
    output.colorimetry = frame_.info.colorimetry;
    output.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
    output.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    auto *converter = gst_video_converter_new(&frame_.info, &output, nullptr);
    if (!converter) return {};
    auto *buffer = gst_buffer_new_allocate(nullptr, output.size, nullptr);
    GstVideoFrame destination;
    if (buffer && gst_video_frame_map(&destination, &output, buffer, GST_MAP_WRITE)) {
        gst_video_converter_frame(converter, &frame_, &destination);
        image_ = QImage(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&destination, 0)),
            size().width(), size().height(), GST_VIDEO_FRAME_PLANE_STRIDE(&destination, 0), QImage::Format_RGB32).copy();
        gst_video_frame_unmap(&destination);
    }
    if (buffer) gst_buffer_unref(buffer);
    gst_video_converter_free(converter);
    return image_;
}
#endif
