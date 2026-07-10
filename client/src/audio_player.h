#pragma once
#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <memory>

class QAudioSink;
class QIODevice;

namespace droppix {

// Raw PCM playback: 48000 Hz, stereo, signed 16-bit little-endian — a fixed
// out-of-band format contract (never sent on the wire), matching the host's
// `parec --format=s16le --rate=48000 --channels=2` and the Android AudioPlayer.kt.
//
// Must be constructed on a thread with a running Qt event loop (QAudioSink requires
// one); submit() is a slot so audio arriving on the net thread can be queued via a
// Qt::QueuedConnection without touching QAudioSink from the wrong thread.
//
// Backpressure: pending PCM is capped at ~200ms; new bytes push out the OLDEST
// buffered bytes once the cap is hit (never grows unbounded, never blocks the
// caller) — same tradeoff as Android's bounded drop-oldest queue.
class AudioPlayer : public QObject {
  Q_OBJECT
 public:
  explicit AudioPlayer(QObject* parent = nullptr);
  ~AudioPlayer() override;

 public slots:
  void submit(const QByteArray& pcm);

 private slots:
  void pump();  // flush as much of pending_ into the sink as it will currently accept

 private:
  std::unique_ptr<QAudioSink> sink_;
  QIODevice* io_ = nullptr;
  QByteArray pending_;
  QTimer pumpTimer_;
};

}  // namespace droppix
