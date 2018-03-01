// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/assets/directory_asset_bundle.h"
#include "lib/fxl/build_config.h"

#include <fcntl.h>

#if defined(OS_WIN)
#include <cstdio>
#endif

#include <utility>

#include "lib/fxl/files/eintr_wrapper.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/portable_unistd.h"

namespace blink {

bool DirectoryAssetBundle::GetAsBuffer(const std::string& asset_name,
                                       std::vector<uint8_t>* data) {
#if defined(OS_WIN)
  {
    // On Windows, files::ReadFileToVector doesn't open the file with the O_BINARY flag
    // Without this flag, binary data gets clobbered because '\n' gets translated to '\r\n'
    // To avoid this, we use cstdio to read the file instead

    struct AutoFile {
      FILE* file_handle;
      explicit AutoFile(FILE* handle) : file_handle(handle) {}
      ~AutoFile() {
        std::fclose(file_handle);
      }
      operator FILE*() {
        return file_handle;
      }
    };

    auto path = GetPathForAsset(asset_name);
    AutoFile fh(std::fopen(path.c_str(), "rb"));

    if (!fh)
      return false;

    constexpr size_t kBufferSize = 1 << 16;
    size_t offset = 0;
    ssize_t bytes_read = 0;
    do {
      offset += bytes_read;
      data->resize(offset + kBufferSize);
      bytes_read = std::fread(&(*data)[offset], 1, kBufferSize, fh);
    } while (bytes_read > 0);

    if (bytes_read < 0) {
      FXL_LOG(ERROR) << "Reading " << asset_name << " failed";
      data->clear();
      return false;
    }

    data->resize(offset + bytes_read);
    return true;
  }
#else
  if (fd_.is_valid()) {
    fxl::UniqueFD asset_file(openat(fd_.get(), asset_name.c_str(), O_RDONLY));
    if (!asset_file.is_valid())
      return false;

    constexpr size_t kBufferSize = 1 << 16;
    size_t offset = 0;
    ssize_t bytes_read = 0;
    do {
      offset += bytes_read;
      data->resize(offset + kBufferSize);
      bytes_read = read(asset_file.get(), &(*data)[offset], kBufferSize);
    } while (bytes_read > 0);

    if (bytes_read < 0) {
      FXL_LOG(ERROR) << "Reading " << asset_name << " failed";
      data->clear();
      return false;
    }

    data->resize(offset + bytes_read);
    return true;
  }
#endif
  std::string asset_path = GetPathForAsset(asset_name);
  if (asset_path.empty())
    return false;
  return files::ReadFileToVector(asset_path, data);
}

DirectoryAssetBundle::~DirectoryAssetBundle() {}

DirectoryAssetBundle::DirectoryAssetBundle(std::string directory)
    : directory_(std::move(directory)), fd_() {}

DirectoryAssetBundle::DirectoryAssetBundle(fxl::UniqueFD fd)
    : fd_(std::move(fd)) {}

std::string DirectoryAssetBundle::GetPathForAsset(
    const std::string& asset_name) {
  std::string asset_path = files::SimplifyPath(directory_ + "/" + asset_name);
  if (asset_path.find(directory_) != 0u) {
    FXL_LOG(ERROR) << "Asset name '" << asset_name
                   << "' attempted to traverse outside asset bundle.";
    return std::string();
  }
  return asset_path;
}

}  // namespace blink
