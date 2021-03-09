// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/android/android_external_texture_gl.h"

#include <GLES/glext.h>

#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/GrDirectContext.h"

namespace flutter {

AndroidExternalTextureGL::AndroidExternalTextureGL(
    int64_t id,
    const fml::jni::JavaObjectWeakGlobalRef& surface_texture,
    std::shared_ptr<PlatformViewAndroidJNI> jni_facade)
    : Texture(id),
      jni_facade_(jni_facade),
      surface_texture_(surface_texture),
      transform(SkMatrix::I()) {}

AndroidExternalTextureGL::~AndroidExternalTextureGL() {
  if (state_ == AttachmentState::attached) {
    glDeleteTextures(1, &texture_name_);
  }
}

void AndroidExternalTextureGL::OnGrContextCreated() {
  state_ = AttachmentState::uninitialized;
}

void AndroidExternalTextureGL::MarkNewFrameAvailable() {
  new_frame_ready_ = true;
}

void AndroidExternalTextureGL::Paint(SkCanvas& canvas,
                                     const SkRect& bounds,
                                     bool freeze,
                                     GrDirectContext* context,
                                     const SkSamplingOptions& sampling) {
  if (state_ == AttachmentState::detached) {
    return;
  }
  if (state_ == AttachmentState::uninitialized) {
    glGenTextures(1, &texture_name_);
    Attach(static_cast<jint>(texture_name_));
    state_ = AttachmentState::attached;
  }
  if (!freeze && new_frame_ready_) {
    Update();
    new_frame_ready_ = false;
  }
  GrGLTextureInfo textureInfo = {GL_TEXTURE_EXTERNAL_OES, texture_name_,
                                 GL_RGBA8_OES};
  GrBackendTexture backendTexture(1, 1, GrMipMapped::kNo, textureInfo);
  sk_sp<SkImage> image = SkImage::MakeFromTexture(
      context, backendTexture, kTopLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType,
      kPremul_SkAlphaType, nullptr);
  if (image) {
    SkAutoCanvasRestore autoRestore(&canvas, true);
    canvas.translate(bounds.x(), bounds.y());
    canvas.scale(bounds.width(), bounds.height());
    if (!transform.isIdentity()) {
      sk_sp<SkShader> shader = image->makeShader(
          SkTileMode::kRepeat, SkTileMode::kRepeat, sampling, transform);

      SkPaint paint;
      paint.setShader(shader);
      canvas.drawRect(SkRect::MakeWH(1, 1), paint);
    } else {
      canvas.drawImage(image, 0, 0, sampling, nullptr);
    }
  }
}

void AndroidExternalTextureGL::UpdateTransform() {
  jni_facade_->SurfaceTextureGetTransformMatrix(surface_texture_, transform);

  SkMatrix inverted;
  if (!transform.invert(&inverted)) {
    FML_LOG(FATAL) << "Invalid SurfaceTexture transformation matrix";
  }
  transform = inverted;

  transform.preScale(1.0, -1.0);
  transform.postTranslate(0.0, -1.0);
}

void AndroidExternalTextureGL::OnGrContextDestroyed() {
  if (state_ == AttachmentState::attached) {
    Detach();
    glDeleteTextures(1, &texture_name_);
  }
  state_ = AttachmentState::detached;
}

void AndroidExternalTextureGL::Attach(jint textureName) {
  jni_facade_->SurfaceTextureAttachToGLContext(surface_texture_, textureName);
}

void AndroidExternalTextureGL::Update() {
  jni_facade_->SurfaceTextureUpdateTexImage(surface_texture_);
  UpdateTransform();
}

void AndroidExternalTextureGL::Detach() {
  jni_facade_->SurfaceTextureDetachFromGLContext(surface_texture_);
}

void AndroidExternalTextureGL::OnTextureUnregistered() {}

}  // namespace flutter
