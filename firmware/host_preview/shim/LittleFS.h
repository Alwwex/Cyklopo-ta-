// LittleFS pro PC - soubory lezi v adresari ./preview_fs
#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
namespace fs_ = std::filesystem;
extern std::string g_fsRoot;
class File : public Print {
public:
  File() {}
  File(const std::string& vpath, const char* mode) : vpath_(vpath) {
    std::string real = g_fsRoot + vpath;
    if (fs_::is_directory(real)) { dir_ = true; for (auto& e : fs_::directory_iterator(real)) entries_.push_back(e.path().filename().string()); ok_ = true; return; }
    std::ios::openmode m = std::ios::binary | (mode[0] == 'w' ? (std::ios::out | std::ios::trunc) : std::ios::in);
    if (mode[0] != 'w' && !fs_::exists(real)) return;
    f_ = std::make_shared<std::fstream>(real, m);
    ok_ = f_->good();
  }
  explicit operator bool() const { return ok_; }
  size_t write(uint8_t c) override { f_->put((char)c); return 1; }
  size_t write(const uint8_t* b, size_t n) override { f_->write((const char*)b, n); return n; }
  using Print::write;
  int read(uint8_t* b, size_t n) { f_->read((char*)b, n); return (int)f_->gcount(); }
  int readBytes(char* b, size_t n) { return read((uint8_t*)b, n); }
  size_t size() { auto p = f_->tellg(); f_->seekg(0, std::ios::end); size_t s = f_->tellg(); f_->seekg(p); return s; }
  bool seek(size_t pos) { f_->clear(); f_->seekg(pos); return true; }
  void close() { if (f_) f_->close(); }
  bool isDirectory() const { return dir_; }
  const char* name() const { return name_.c_str(); }
  File openNextFile() {
    if (idx_ >= entries_.size()) return File();
    File f(vpath_ + "/" + entries_[idx_], "r");
    f.name_ = entries_[idx_++];
    return f;
  }
private:
  std::string vpath_, name_;
  std::shared_ptr<std::fstream> f_;
  std::vector<std::string> entries_;
  size_t idx_ = 0;
  bool ok_ = false, dir_ = false;
};
class LittleFSClass {
public:
  bool begin(bool) { fs_::create_directories(g_fsRoot); return true; }
  bool mkdir(const char* p) { fs_::create_directories(g_fsRoot + p); return true; }
  bool remove(const char* p) { return fs_::remove(g_fsRoot + p); }
  File open(const char* p, const char* mode = "r") { return File(p, mode); }
};
extern LittleFSClass LittleFS;
