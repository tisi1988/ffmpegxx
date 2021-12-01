#include "avformat/DemuxerImpl.h"

#include "avcodec/AVPacketImpl.h"
#include "avformat/MediaInfoFactory.h"
#include "public/avcodec/IAVPacket.h"
#include "public/time/Timebase.h"
#include "public/time/Timestamp.h"
#include "utils/LoggerApi.h"
#include "utils/exception.h"

#include <sstream>
#include <vector>

libffmpegxx::avformat::StreamInfo buildStreamInfo(AVStream *stream) {
  return libffmpegxx::avformat::StreamInfo{};
}

namespace libffmpegxx {
namespace avformat {
IDemuxer *DemuxerFactory::create(std::string const &uri) {
  return new DemuxerImpl(uri);
}

DemuxerImpl::DemuxerImpl(std::string const &uri) : m_uri(uri) {
  m_readingPacket = dynamic_cast<avcodec::AVPacketImpl *>(
      libffmpegxx::avcodec::AVPacketFactory::create());

  if (!m_readingPacket) {
    throw std::runtime_error("Unable to allocate packet for reading media " +
                             m_uri);
  }
}

DemuxerImpl::~DemuxerImpl() {
  this->DemuxerImpl::close();
  delete m_readingPacket;
}

AVDictionary *parseOptions(DemuxingOptions const &options) {
  AVDictionary *opts = nullptr;

  for (auto &&[key, value] : options) {
    std::string valueStr{""};

    if (std::holds_alternative<int>(value)) {
      valueStr = std::to_string(std::get<int>(value));
    } else {
      valueStr = std::get<std::string>(value);
    }

    int const err = av_dict_set(&opts, key.c_str(), valueStr.c_str(),
                                AV_DICT_DONT_OVERWRITE);
    if (err < 0) {
      THROW_FFMPEG_ERR_DESCRIPTION("Error while parsing option " + key, err)
    }
  }

  return opts;
}

MediaInfo DemuxerImpl::open(const DemuxingOptions &options) {
  if (m_formatContext) {
    throw std::runtime_error("Trying to open but it is already opened");
  }

  m_formatContext = avformat_alloc_context();
  if (m_formatContext == nullptr) {
    throw std::runtime_error("Error allocating format context.");
  }

  // TODO Implement option handling
  AVDictionary *opts = parseOptions(options);

  int error = avformat_open_input(&m_formatContext, m_uri.c_str(), nullptr,
                                  opts ? (&opts) : nullptr);

  if (error < 0) {
    close();
    THROW_FFMPEG_ERR_DESCRIPTION("Error opening media: " + m_uri, error)
  }

  // Get streams info
  error = avformat_find_stream_info(m_formatContext, opts ? (&opts) : nullptr);
  if (error < 0) {
    close();
    THROW_FFMPEG_ERR_DESCRIPTION(
        "Could not open find stream info for media: " + m_uri, error)
  }

  // Dump media info to the log
  av_dump_format(m_formatContext, 0, m_formatContext->url, false);

  return getMediaInfo();
}

void DemuxerImpl::close() { avformat_close_input(&m_formatContext); }

int DemuxerImpl::read(avcodec::IAVPacket *packet) {
  packet->clear();

  auto avpacket = m_readingPacket->getWrappedPacket();

  int const error = av_read_frame(m_formatContext, avpacket);

  if (error < 0) {
    auto const msgError = "Error while reading " + m_uri + ": " +
                          utils::Logger::avErrorToStr(error);
    LOG_WARN(msgError);
    return error;
  }

  auto const tb = m_formatContext->streams[avpacket->stream_index]->time_base;
  auto const streamTb = time::Timebase(tb.num, tb.den);
  m_readingPacket->setContentType(getStreamType(avpacket->stream_index));
  m_readingPacket->setTimebase(streamTb);
  m_readingPacket->setTimestamp(time::Timestamp(avpacket->pts, streamTb),
                                time::Timestamp(avpacket->dts, streamTb));

  packet->refToPacket(m_readingPacket);

  return error;
}

MediaInfo DemuxerImpl::getMediaInfo() {
  return libffmpegxx::avformat::MediaInfoFactory::build(m_formatContext);
}

StreamType DemuxerImpl::getStreamType(int streamIdx) const {
  static std::vector<AVMediaType> const EXPECTED_TYPES = {
      AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, AVMEDIA_TYPE_DATA,
      AVMEDIA_TYPE_SUBTITLE, AVMEDIA_TYPE_ATTACHMENT};

  AVMediaType foundType = AVMEDIA_TYPE_UNKNOWN;
  for (AVMediaType mediaType : EXPECTED_TYPES) {
    int foundIndex = av_find_best_stream(m_formatContext, mediaType, streamIdx,
                                         -1, nullptr, 0);
    if (foundIndex == streamIdx) {
      foundType = mediaType;
    }
  }

  switch (foundType) {
  case AVMEDIA_TYPE_VIDEO:
    return StreamType::VIDEO;
  case AVMEDIA_TYPE_AUDIO:
    return StreamType::AUDIO;
  case AVMEDIA_TYPE_DATA:
    return StreamType::DATA;
  case AVMEDIA_TYPE_SUBTITLE:
    return StreamType::SUBTITLE;
  default:
    LOG_WARN("Could not find stream type for stream " +
             std::to_string(streamIdx) + " in media " + m_uri);
    return StreamType::NONE;
  }
}
}; // namespace avformat
}; // namespace libffmpegxx
