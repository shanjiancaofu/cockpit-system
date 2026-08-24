#include <gst/gst.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <utility>

#include "cockpit/modules/media/media_manifest.h"
#include "cockpit/modules/media/media_player.h"

namespace cockpit {
namespace media {
namespace {

void AssignError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

class GstreamerMediaPlayer final : public MediaPlayer {
 public:
  GstreamerMediaPlayer(std::vector<MediaManifestTrack> tracks, std::string sink_element)
      : tracks_(std::move(tracks)), sink_element_(std::move(sink_element)) {
    std::call_once(gstreamer_once_, [] {
      gst_init(nullptr, nullptr);
    });
    pipeline_ = gst_element_factory_make("playbin3", "cockpit_media_playbin");
    sink_ = gst_element_factory_make(sink_element_.c_str(), "cockpit_media_sink");
    if (pipeline_ != nullptr && sink_ != nullptr) {
      if (sink_element_ == "fakesink") {
        g_object_set(sink_, "sync", TRUE, nullptr);
      }
      g_object_set(pipeline_, "audio-sink", sink_, nullptr);
      // playbin owns the sink after the property assignment.
      sink_ = nullptr;
      backend_ready_ = true;
      status_.state = MediaPlaybackState::kStopped;
    } else {
      status_.state = MediaPlaybackState::kFaulted;
      status_.last_error = "GStreamer playbin3 or configured sink is unavailable";
    }
  }

  bool ready() const {
    return backend_ready_;
  }

  ~GstreamerMediaPlayer() override {
    Stop(nullptr);
    if (pipeline_ != nullptr) {
      gst_object_unref(pipeline_);
    }
  }

  std::vector<MediaTrack> ListTracks() const override {
    std::vector<MediaTrack> result;
    result.reserve(tracks_.size());
    for (const auto& track : tracks_) {
      result.push_back(track.metadata);
    }
    return result;
  }

  MediaPlaybackStatus status() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    DrainBusLocked();
    MediaPlaybackStatus result = status_;
    if (pipeline_ != nullptr && (status_.state == MediaPlaybackState::kPlaying ||
                                 status_.state == MediaPlaybackState::kPaused)) {
      gint64 position = 0;
      if (gst_element_query_position(pipeline_, GST_FORMAT_TIME, &position) && position >= 0) {
        result.position_ms = static_cast<std::uint64_t>(position / GST_MSECOND);
      }
    }
    return result;
  }

  bool Play(const std::string& track_id, std::string* error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!backend_ready_ || pipeline_ == nullptr) {
      AssignError(error, "GStreamer media backend is unavailable");
      return false;
    }
    const std::string resolved =
        track_id == "default_track" && !tracks_.empty() ? tracks_.front().metadata.id : track_id;
    const auto found = Find(resolved);
    if (found == tracks_.end()) {
      AssignError(error, "media track id is not allowlisted");
      return false;
    }
    return PlayUnlocked(found->metadata.id, MediaPlaybackState::kPlaying, error);
  }

  bool Pause(std::string* error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != MediaPlaybackState::kPlaying) {
      AssignError(error, "media is not playing");
      return false;
    }
    gint64 position = 0;
    if (gst_element_query_position(pipeline_, GST_FORMAT_TIME, &position) && position >= 0) {
      paused_position_ms_ = static_cast<std::uint64_t>(position / GST_MSECOND);
    } else {
      paused_position_ms_ = status_.position_ms;
    }
    if (!SetStateAndWait(GST_STATE_NULL)) {
      AssignError(error, "GStreamer failed to pause media playback");
      return false;
    }
    status_.state = MediaPlaybackState::kPaused;
    status_.position_ms = paused_position_ms_;
    AssignError(error, {});
    return true;
  }

  bool Resume(std::string* error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != MediaPlaybackState::kPaused) {
      AssignError(error, "media is not paused");
      return false;
    }
    const std::uint64_t resume_position_ms = paused_position_ms_;
    if (!PlayUnlocked(tracks_[current_index_].metadata.id, MediaPlaybackState::kPlaying, error)) {
      return false;
    }
    if (resume_position_ms > 0U &&
        !gst_element_seek_simple(
            pipeline_, GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            static_cast<gint64>(resume_position_ms) * GST_MSECOND)) {
      const std::string message = "GStreamer failed to seek while resuming media playback";
      AssignError(error, message);
      status_.state = MediaPlaybackState::kFaulted;
      status_.last_error = message;
      static_cast<void>(SetStateAndWait(GST_STATE_NULL));
      return false;
    }
    status_.state = MediaPlaybackState::kPlaying;
    status_.position_ms = resume_position_ms;
    paused_position_ms_ = 0;
    AssignError(error, {});
    return true;
  }

  bool Stop(std::string* error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pipeline_ != nullptr && !SetStateAndWait(GST_STATE_NULL)) {
      AssignError(error, "GStreamer failed to stop media playback");
      return false;
    }
    status_.state = MediaPlaybackState::kStopped;
    status_.position_ms = 0;
    paused_position_ms_ = 0;
    status_.last_error.clear();
    AssignError(error, {});
    return true;
  }

  bool Next(std::string* error) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state != MediaPlaybackState::kPlaying &&
        status_.state != MediaPlaybackState::kPaused) {
      AssignError(error, "media must be playing or paused to select next track");
      return false;
    }
    if (tracks_.empty()) {
      AssignError(error, "media manifest has no tracks");
      return false;
    }
    current_index_ = (current_index_ + 1U) % tracks_.size();
    const std::string id = tracks_[current_index_].metadata.id;
    const MediaPlaybackState previous = status_.state;
    if (!PlayUnlocked(id, previous, error)) {
      return false;
    }
    return true;
  }

 private:
  using TrackIterator = std::vector<MediaManifestTrack>::const_iterator;

  TrackIterator Find(const std::string& id) const {
    return std::find_if(tracks_.begin(), tracks_.end(), [&id](const MediaManifestTrack& track) {
      return track.metadata.id == id;
    });
  }

  bool PlayUnlocked(const std::string& id, MediaPlaybackState requested_state, std::string* error) {
    const auto found = Find(id);
    if (found == tracks_.end()) {
      AssignError(error, "media track id is not allowlisted");
      return false;
    }
    gchar* uri = gst_filename_to_uri(found->file.c_str(), nullptr);
    if (uri == nullptr) {
      AssignError(error, "failed to convert media file to URI");
      return false;
    }
    if (!SetStateAndWait(GST_STATE_NULL)) {
      g_free(uri);
      AssignError(error, "GStreamer failed to reset media playback");
      return false;
    }
    g_object_set(pipeline_, "uri", uri, nullptr);
    g_free(uri);
    const GstState target_state =
        requested_state == MediaPlaybackState::kPaused ? GST_STATE_NULL : GST_STATE_PLAYING;
    if (!SetStateAndWait(target_state)) {
      const std::string message = "GStreamer failed to start media playback";
      AssignError(error, message);
      status_.state = MediaPlaybackState::kFaulted;
      status_.last_error = message;
      return false;
    }
    current_index_ = static_cast<std::size_t>(std::distance(tracks_.begin(), found));
    status_.state = requested_state;
    status_.position_ms = 0;
    paused_position_ms_ = 0;
    status_.last_error.clear();
    ApplyTrack(found->metadata);
    AssignError(error, {});
    return true;
  }

  void ApplyTrack(const MediaTrack& track) {
    status_.current_track_id = track.id;
    status_.title = track.title;
    status_.artist = track.artist;
    status_.duration_ms = track.duration_ms;
  }

  bool SetStateAndWait(GstState target) const {
    const GstStateChangeReturn result = gst_element_set_state(pipeline_, target);
    if (result == GST_STATE_CHANGE_FAILURE) {
      return false;
    }
    if (result == GST_STATE_CHANGE_ASYNC) {
      GstState current = GST_STATE_NULL;
      GstState pending = GST_STATE_VOID_PENDING;
      const GstStateChangeReturn wait_result =
          gst_element_get_state(pipeline_, &current, &pending, GST_SECOND);
      return wait_result != GST_STATE_CHANGE_FAILURE && current == target;
    }
    return true;
  }

  void DrainBusLocked() const {
    if (pipeline_ == nullptr) {
      return;
    }
    GstBus* bus = gst_element_get_bus(pipeline_);
    if (bus == nullptr) {
      return;
    }
    while (GstMessage* message = gst_bus_pop_filtered(
               bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
      if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        status_.state = MediaPlaybackState::kStopped;
        status_.position_ms = status_.duration_ms;
        paused_position_ms_ = 0;
        status_.last_error.clear();
        static_cast<void>(SetStateAndWait(GST_STATE_NULL));
      } else {
        GError* gerror = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &gerror, &debug);
        status_.state = MediaPlaybackState::kFaulted;
        status_.last_error =
            gerror == nullptr ? "GStreamer media playback failed" : gerror->message;
        static_cast<void>(SetStateAndWait(GST_STATE_NULL));
        if (gerror != nullptr) {
          g_error_free(gerror);
        }
        g_free(debug);
      }
      gst_message_unref(message);
    }
    gst_object_unref(bus);
  }

  static std::once_flag gstreamer_once_;
  mutable std::mutex mutex_;
  const std::vector<MediaManifestTrack> tracks_;
  const std::string sink_element_;
  GstElement* pipeline_ = nullptr;
  GstElement* sink_ = nullptr;
  bool backend_ready_ = false;
  std::size_t current_index_ = 0;
  mutable std::uint64_t paused_position_ms_ = 0;
  mutable MediaPlaybackStatus status_{};
};

std::once_flag GstreamerMediaPlayer::gstreamer_once_;

}  // namespace

std::unique_ptr<MediaPlayer> CreateGstreamerMediaPlayer(const std::string& manifest_path,
                                                        const std::string& sink_element) {
  MediaManifest manifest;
  std::string error;
  if (!MediaManifest::Load(manifest_path, &manifest, &error)) {
    return nullptr;
  }
  if (sink_element != "fakesink" && sink_element != "alsasink") {
    return nullptr;
  }
  auto player = std::make_unique<GstreamerMediaPlayer>(manifest.tracks(), sink_element);
  if (!player->ready()) {
    return nullptr;
  }
  return player;
}

}  // namespace media
}  // namespace cockpit
