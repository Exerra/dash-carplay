#ifdef DASH_CARPLAY
#include "carplay/surface.hpp"
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTouchEvent>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix3x3>
#include <QVector3D>
#include <QApplication>
#include <QDebug>
#include <functional>

class CarPlayVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
    CarPlayVideoWidget(CarPlayClient &client, QWidget *parent, std::function<void()> failed)
        : QOpenGLWidget(parent), client_(client), failed_(std::move(failed)) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
    }
    ~CarPlayVideoWidget() override { QObject::disconnect(contextConnection_); cleanup(); }
 protected:
    void showEvent(QShowEvent *event) override {
        QOpenGLWidget::showEvent(event);
        QTimer::singleShot(0, this, [this] { if (!isValid()) fail(); });
    }
    void initializeGL() override {
        initializeOpenGLFunctions();
        QObject::disconnect(contextConnection_);
        contextConnection_ = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { cleanup(); });
        program_.reset(new QOpenGLShaderProgram);
        const char *vertex =
            "attribute vec2 position; attribute vec2 coordinate; uniform vec2 scale; varying vec2 uv;"
            "void main() { gl_Position=vec4(position*scale,0.0,1.0); uv=coordinate; }";
        const QByteArray fragment = QByteArray(context()->isOpenGLES() ? "precision mediump float;\n" : "") +
            "varying vec2 uv; uniform sampler2D yPlane; uniform sampler2D uPlane; uniform sampler2D vPlane;"
            "uniform vec3 planeScale; uniform mat3 colorMatrix; uniform vec3 colorOffset;"
            "void main() { vec3 yuv=vec3(texture2D(yPlane,vec2(uv.x*planeScale.x,uv.y)).r,"
            "texture2D(uPlane,vec2(uv.x*planeScale.y,uv.y)).r,texture2D(vPlane,vec2(uv.x*planeScale.z,uv.y)).r);"
            "gl_FragColor=vec4(colorMatrix*(yuv+colorOffset),1.0); }";
        if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
            !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)) { fail(); return; }
        program_->bindAttributeLocation("position", 0); program_->bindAttributeLocation("coordinate", 1);
        if (!program_->link()) { fail(); return; }
        vao_.create(); if (vao_.isCreated()) vao_.bind();
        vertices_.create(); vertices_.bind();
        const GLfloat quad[] = {-1,1,0,0, -1,-1,0,1, 1,1,1,0, 1,-1,1,1};
        vertices_.allocate(quad, sizeof(quad)); vertices_.release();
        if (vao_.isCreated()) vao_.release();
        glGenTextures(3, textures_);
        for (int i = 0; i < 3; ++i) {
            glBindTexture(GL_TEXTURE_2D, textures_[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        qInfo() << "CarPlay renderer: OpenGL YUV" << reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    }

    void paintGL() override {
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        const auto frame = client_.videoFrame();
        if (!frame || !program_ || !program_->isLinked()) { uploaded_.reset(); return; }
        const auto size = frame->size();
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (int plane = 0; plane < 3; ++plane) {
            const int rows = plane ? (size.height()+1)/2 : size.height();
            const int stride = frame->stride(plane);
            if (stride <= 0) { fail(); return; }
            glActiveTexture(GL_TEXTURE0 + plane); glBindTexture(GL_TEXTURE_2D, textures_[plane]);
            if (frame != uploaded_) {
                if (textureSizes_[plane] != QSize(stride, rows)) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, stride, rows, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr);
                    textureSizes_[plane] = QSize(stride, rows);
                }
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, stride, rows, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->plane(plane));
            }
        }
        uploaded_ = frame;
        program_->bind();
        const auto fitted = size.scaled(this->size(), Qt::KeepAspectRatio);
        program_->setUniformValue("scale", QVector2D(float(fitted.width())/width(), float(fitted.height())/height()));
        program_->setUniformValue("yPlane", 0); program_->setUniformValue("uPlane", 1); program_->setUniformValue("vPlane", 2);
        program_->setUniformValue("planeScale", QVector3D(float(size.width())/frame->stride(0),
            float((size.width()+1)/2)/frame->stride(1), float((size.width()+1)/2)/frame->stride(2)));
        double kr = 0.299, kb = 0.114;
        gst_video_color_matrix_get_Kr_Kb(frame->info().colorimetry.matrix, &kr, &kb);
        const bool full = frame->info().colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;
        const float ys = full ? 1.0f : 255.0f/219.0f, cs = full ? 1.0f : 255.0f/224.0f;
        const double kg = 1.0 - kr - kb;
        QMatrix3x3 matrix;
        matrix(0,0)=ys; matrix(0,1)=0; matrix(0,2)=float(2*(1-kr))*cs;
        matrix(1,0)=ys; matrix(1,1)=float(-2*kb*(1-kb)/kg)*cs; matrix(1,2)=float(-2*kr*(1-kr)/kg)*cs;
        matrix(2,0)=ys; matrix(2,1)=float(2*(1-kb))*cs; matrix(2,2)=0;
        program_->setUniformValue("colorMatrix", matrix);
        program_->setUniformValue("colorOffset", QVector3D(full ? 0 : -16.0f/255, -128.0f/255, -128.0f/255));
        if (vao_.isCreated()) vao_.bind();
        vertices_.bind();
        program_->enableAttributeArray(0); program_->enableAttributeArray(1);
        program_->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4*sizeof(GLfloat));
        program_->setAttributeBuffer(1, GL_FLOAT, 2*sizeof(GLfloat), 2, 4*sizeof(GLfloat));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        program_->disableAttributeArray(0); program_->disableAttributeArray(1);
        vertices_.release(); if (vao_.isCreated()) vao_.release(); program_->release();
        glActiveTexture(GL_TEXTURE0);
    }
 private:
    CarPlayClient &client_;
    std::function<void()> failed_;
    QMetaObject::Connection contextConnection_;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    QOpenGLBuffer vertices_;
    QOpenGLVertexArrayObject vao_;
    GLuint textures_[3] = {};
    QSize textureSizes_[3];
    std::shared_ptr<const CarPlayVideoFrame> uploaded_;
    void fail() {
        qWarning() << "CarPlay OpenGL unavailable; using software renderer";
        QTimer::singleShot(0, parentWidget(), failed_);
    }
    void cleanup() {
        if (!context()) return;
        makeCurrent();
        program_.reset();
        vertices_.destroy(); vao_.destroy();
        if (textures_[0]) glDeleteTextures(3, textures_);
        for (int i=0;i<3;++i) { textures_[i]=0; textureSizes_[i]={}; }
        uploaded_.reset(); doneCurrent();
    }
};

namespace {
bool commandForKey(int key, PhoneControls::Command &command) {
    using Command = PhoneControls::Command;
    switch (key) {
        case Qt::Key_Left: command = Command::Left; break;
        case Qt::Key_Right: command = Command::Right; break;
        case Qt::Key_Up: command = Command::Up; break;
        case Qt::Key_Down: command = Command::Down; break;
        case Qt::Key_Return: case Qt::Key_Enter: command = Command::Enter; break;
        case Qt::Key_Escape: command = Command::Back; break;
        case Qt::Key_Home: command = Command::Home; break;
        case Qt::Key_MediaPlay: command = Command::Play; break;
        case Qt::Key_MediaPause: command = Command::Pause; break;
        case Qt::Key_MediaTogglePlayPause: command = Command::TogglePlay; break;
        case Qt::Key_MediaPrevious: command = Command::Previous; break;
        case Qt::Key_MediaNext: command = Command::Next; break;
        default: return false;
    }
    return true;
}
}

CarPlaySurface::CarPlaySurface(CarPlayClient &client, QWidget *parent) : QWidget(parent), client_(client) {
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(160, 96);
    const auto platform = QGuiApplication::platformName();
    if (qEnvironmentVariable("CARPLAY_RENDERER") != "software" && platform != "offscreen" && platform != "minimal") {
        video_ = new CarPlayVideoWidget(client, this, [this] { video_->hide(); update(); });
        video_->setGeometry(rect());
        connect(&client_, &CarPlayClient::frameChanged, video_, QOverload<>::of(&QWidget::update), Qt::QueuedConnection);
    }
    // Qt coalesces updates; an idle phone no longer repaints the whole display at 30 Hz.
    connect(&client_, &CarPlayClient::frameChanged, this, QOverload<>::of(&QWidget::update), Qt::QueuedConnection);
    connect(&client_, &CarPlayClient::connectedChanged, this, QOverload<>::of(&QWidget::update));
}

void CarPlaySurface::paintEvent(QPaintEvent *) {
    const auto frame = client_.videoFrame();
    const auto size = frame ? frame->size().scaled(this->size(), Qt::KeepAspectRatio) : QSize();
    imageRect_ = QRectF(QPointF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0), size);
    if (!frame) { imageRect_ = {}; contacts_.clear(); mouseDown_ = false; }
    if (video_ && !video_->isHidden()) return;
    QPainter painter(this); painter.fillRect(rect(), Qt::black);
    if (frame) painter.drawImage(imageRect_, frame->image());
}

void CarPlaySurface::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (video_) video_->setGeometry(rect());
}

QJsonObject CarPlaySurface::contact(int id, const QPointF &position, bool down) const {
    return {{"id", id}, {"x", qBound(0.0, (position.x() - imageRect_.left()) / imageRect_.width(), 1.0)},
        {"y", qBound(0.0, (position.y() - imageRect_.top()) / imageRect_.height(), 1.0)}, {"down", down}};
}

bool CarPlaySurface::event(QEvent *event) {
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        if (imageRect_.isEmpty()) { event->accept(); return true; }
        QJsonArray values;
        if (event->type() == QEvent::TouchCancel) {
            for (const int id : contacts_) values.append(contact(id, imageRect_.center(), false));
            contacts_.clear();
        } else {
            auto *touch = static_cast<QTouchEvent *>(event);
            for (const auto &point : touch->touchPoints()) {
                if (!contacts_.contains(point.id())) {
                    if (contacts_.size() >= 2 || !imageRect_.contains(point.pos()) ||
                        point.state() == Qt::TouchPointReleased) continue;
                    contacts_.insert(point.id(), contacts_.values().contains(0) ? 1 : 0);
                }
                const bool down = point.state() != Qt::TouchPointReleased;
                values.append(contact(contacts_[point.id()], point.pos(), down));
                if (!down) contacts_.remove(point.id());
            }
        }
        client_.sendCommand({{"type", "touch"}, {"contacts", values}});
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void CarPlaySurface::mouseContact(QMouseEvent *event, bool down) {
    if (!imageRect_.isEmpty())
        client_.sendCommand({{"type", "touch"}, {"contacts", QJsonArray{contact(0, event->localPos(), down)}}});
    event->accept();
}
void CarPlaySurface::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !imageRect_.contains(event->localPos())) return;
    mouseDown_ = true;
    setFocus();
    mouseContact(event, true);
}
void CarPlaySurface::mouseMoveEvent(QMouseEvent *event) { if (mouseDown_) mouseContact(event, true); }
void CarPlaySurface::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && mouseDown_) { mouseDown_ = false; mouseContact(event, false); }
}
void CarPlaySurface::keyPressEvent(QKeyEvent *event) {
    PhoneControls::Command command;
    if (!commandForKey(event->key(), command)) { QWidget::keyPressEvent(event); return; }
    if (!event->isAutoRepeat() && !heldKeys_.contains(event->key())) {
        heldKeys_.insert(event->key(), command);
        client_.sendControl(command, PhoneControls::State::Pressed);
    }
    event->accept();
}

void CarPlaySurface::keyReleaseEvent(QKeyEvent *event) {
    const auto found = heldKeys_.find(event->key());
    if (found == heldKeys_.end()) { QWidget::keyReleaseEvent(event); return; }
    if (!event->isAutoRepeat()) {
        client_.sendControl(found.value(), PhoneControls::State::Released);
        heldKeys_.erase(found);
    }
    event->accept();
}

void CarPlaySurface::focusOutEvent(QFocusEvent *event) {
    for (auto command : heldKeys_) client_.sendControl(command, PhoneControls::State::Released);
    heldKeys_.clear();
    QWidget::focusOutEvent(event);
}

#endif
