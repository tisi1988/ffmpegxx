#include "avformat/MediaInfoFactory.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace libffmpegxx {
namespace avformat {
void buildMediaInfo(MediaInfo &info, AVFormatContext *ctx) {
  info.duration = time::Seconds{ctx->duration * av_q2d(AV_TIME_BASE_Q)};
  info.format = ctx->iformat->name;
  info.formatLongName = ctx->iformat->long_name;
  // Duration value is represented in AV_TIME_BASE units
  info.startTime = time::Seconds{ctx->start_time * av_q2d(AV_TIME_BASE_Q)};
  info.uri = ctx->url;
}

StreamType getTypeFromCodecId(AVMediaType codecType) {
  // Deduce type by codec intervals, check codec_id.h
  switch (codecType) {
  case AVMEDIA_TYPE_UNKNOWN:
    return StreamType::NONE;
  case AVMEDIA_TYPE_VIDEO:
    return StreamType::VIDEO;
  case AVMEDIA_TYPE_AUDIO:
    return StreamType::AUDIO;
  case AVMEDIA_TYPE_SUBTITLE:
    return StreamType::SUBTITLE;
  case AVMEDIA_TYPE_ATTACHMENT:
  case AVMEDIA_TYPE_DATA:
  case AVMEDIA_TYPE_NB:
  default:
    return StreamType::DATA;
  }
}

void buildGenericProperties(StreamBaseInfo &info, AVStream *stream) {
  info.codecId = stream->codecpar->codec_id;
  info.codecName = avcodec_get_name(info.codecId);
  if (stream->duration != 0) {
    info.duration = time::Seconds{stream->duration * av_q2d(stream->time_base)};
  } else {
    info.duration = time::Seconds{0.};
  }
  info.startTime = time::Seconds{
      av_rescale_q(stream->start_time, stream->time_base, {1, 1})};
  info.timebase = time::Timebase(stream->time_base.num, stream->time_base.den);
  info.bitrate = stream->codecpar->bit_rate;
  info.level = stream->codecpar->level;
  info.profile = stream->codecpar->profile;
}

VideoInfo buildVideoStreamProperties(AVStream *stream) {
  VideoInfo info;

  buildGenericProperties(info, stream);
  info.averageFramerate = av_q2d(stream->avg_frame_rate);
  info.frameCount = stream->nb_frames;
  info.format = static_cast<AVPixelFormat>(stream->codecpar->format);
  info.height = stream->codecpar->height;
  info.width = stream->codecpar->width;

  return info;
}

AudioInfo buildAudioStreamProperties(AVStream *stream) {
  AudioInfo info;

  buildGenericProperties(info, stream);
  info.format = static_cast<AVSampleFormat>(stream->codecpar->format);
  info.channelCount = stream->codecpar->channels;
  info.channelLayout = stream->codecpar->channel_layout;
  info.frameSize = stream->codecpar->frame_size;
  info.sampleRate = stream->codecpar->sample_rate;

  return info;
}

SubtitleInfo buildSubtitleStreamProperties(AVStream *stream) {
  SubtitleInfo info;

  buildGenericProperties(info, stream);

  return info;
}

DataInfo buildDataStreamProperties(AVStream *stream) {
  DataInfo info;

  buildGenericProperties(info, stream);

  return info;
}

StreamInfo buildStreamInfo(AVStream *stream) {
  StreamInfo info;

  info.index = stream->index;
  info.type = getTypeFromCodecId(stream->codecpar->codec_type);
  switch (info.type) {
  case StreamType::VIDEO:
    info.properties = buildVideoStreamProperties(stream);
    break;
  case StreamType::AUDIO:
    info.properties = buildAudioStreamProperties(stream);
    break;
  case StreamType::SUBTITLE:
    info.properties = buildSubtitleStreamProperties(stream);
    break;
  case StreamType::DATA:
  default:
    info.properties = buildDataStreamProperties(stream);
    break;
  }

  return info;
}

void buildStreamsInfo(std::map<int, StreamInfo> &streamsInfo,
                      AVStream **streams, int nbStreams) {
  for (int streamIdx = 0; streamIdx < nbStreams; ++streamIdx) {
    auto const streamInfo = buildStreamInfo(streams[streamIdx]);
    streamsInfo.insert({streamIdx, streamInfo});
  }
}

MediaInfo MediaInfoFactory::build(AVFormatContext *ctx) {
  MediaInfo info;

  buildMediaInfo(info, ctx);
  buildStreamsInfo(info.streamsInfo, ctx->streams, ctx->nb_streams);

  return info;
}
} // namespace avformat
} // namespace libffmpegxx
