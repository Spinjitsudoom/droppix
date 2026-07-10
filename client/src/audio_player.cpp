#include "audio_player.h"
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QIODevice>

namespace droppix {
namespace {
// ~200ms at 48000Hz/stereo/s16le = 48000 * 2ch * 2bytes * 0.2s
constexpr qsizetype kMaxPendingBytes = 48000 * 2 * 2 * 200 / 1000;
}  // namespace

AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent) {
  QAudioFormat fmt;
  fmt.setSampleRate(48000);
  fmt.setChannelCount(2);
  fmt.setSampleFormat(QAudioFormat::Int16);
  sink_ = std::make_unique<QAudioSink>(QMediaDevices::defaultAudioOutput(), fmt);
  io_ = sink_->start();
  connect(&pumpTimer_, &QTimer::timeout, this, &AudioPlayer::pump);
  pumpTimer_.start(10);  // ~100Hz: keep the sink fed as it drains at playback rate
}

AudioPlayer::~AudioPlayer() {
  pumpTimer_.stop();
  if (sink_) sink_->stop();
}

void AudioPlayer::submit(const QByteArray& pcm) {
  pending_.append(pcm);
  if (pending_.size() > kMaxPendingBytes) {
    // Drop the OLDEST buffered bytes, not the newly-arrived chunk — keeps latency
    // bounded while always playing the most recent audio, same policy as the Android
    // AudioPlayer's bounded queue.
    pending_.remove(0, pending_.size() - kMaxPendingBytes);
  }
  pump();
}

void AudioPlayer::pump() {
  if (!io_ || pending_.isEmpty()) return;
  qint64 writable = sink_->bytesFree();
  if (writable <= 0) return;
  qint64 n = std::min<qint64>(writable, pending_.size());
  qint64 written = io_->write(pending_.constData(), n);
  if (written > 0) pending_.remove(0, static_cast<int>(written));
}

}  // namespace droppix
